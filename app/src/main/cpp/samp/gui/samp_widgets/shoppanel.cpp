#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "shoppanel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

// Loja das empresas (ver shoppanel.h). Medidas em "unidades de referencia" = pixels da captura de referencia (tela de 720 de altura);
// tudo e multiplicado por u (escala da tela), como a nota fiscal / garagem.

namespace
{
	struct Prod { std::string name, desc; uint32_t price = 0; int stock = 0, icon = 8, color = 0, qty0 = 0; };
	struct Data { int seq = 0; std::string title, sub; std::vector<Prod> prods; };

	std::mutex g_mtx;
	Data       g_net;
	unsigned   g_ver = 0;
	Data       g_d;                         // copia da thread de desenho
	unsigned   g_seen = ~0u;
	int64_t    g_t0 = 0;                    // instante em que a loja apareceu
	std::vector<ShopLine> g_cart;           // carrinho local (indice do produto + quantidade)
	float      g_scrollGrid = 0.0f, g_scrollCart = 0.0f;
	float      g_cartAnim = 0.0f;           // 0 = painel estreito (carrinho vazio) | 1 = painel largo (com carrinho)
	int64_t    g_lastDraw = 0;

	// toque
	enum { T_NONE = 0, T_ADD, T_MINUS, T_PLUS, T_CLOSE, T_FIN };
	struct Target { int kind = T_NONE; int idx = 0; };
	struct HitR { float x0, y0, x1, y1; Target t; };
	std::vector<HitR> g_hits;
	float g_gridR[4] = {0, 0, 0, 0}, g_cartR[4] = {0, 0, 0, 0};     // viewports (px) para rolar com o dedo
	int    g_finger = -1;
	Target g_down;
	float  g_sx = 0, g_sy = 0, g_ly = 0;
	bool   g_moved = false;
	int    g_area = 0;                      // 1 = rolando a grade | 2 = rolando o carrinho

	// aviso "Informacao"
	std::mutex g_tmx;
	std::string g_toastText; int g_toastKind = 0; int64_t g_toastT0 = 0; unsigned g_toastVer = 0, g_toastSeen = 0;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], (unsigned)n)) return false;
		for (char& c : out) if ((unsigned char)c < 32) c = ' ';      // so texto imprimivel
		return true;
	}

	std::string Money(uint32_t v)
	{
		const std::string s = std::to_string(v);
		std::string o;
		for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return "R$ " + o;
	}

	const int kAccent[6][3] = { {56, 231, 248}, {255, 143, 176}, {255, 240, 126}, {255, 196, 136}, {208, 159, 255}, {120, 240, 170} };

	int CartQty(int idx) { for (const ShopLine& l : g_cart) if (l.idx == idx) return l.qty; return 0; }
	int Limit(const Prod& p) { return std::min(p.stock, 99); }
	int CartKinds() { return (int)g_cart.size(); }

	void CartSet(int idx, int qty)
	{
		for (size_t i = 0; i < g_cart.size(); i++)
			if (g_cart[i].idx == idx) { if (qty <= 0) g_cart.erase(g_cart.begin() + (long)i); else g_cart[i].qty = qty; return; }
		if (qty > 0) g_cart.push_back({idx, qty});
	}

	uint32_t CartTotal()
	{
		uint64_t t = 0;
		for (const ShopLine& l : g_cart) if (l.idx >= 0 && l.idx < (int)g_d.prods.size()) t += (uint64_t)g_d.prods[(size_t)l.idx].price * (uint64_t)l.qty;
		return t > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)t;
	}
}

bool ShopPanel::OnShow(RakNet::BitStream& bs)
{
	Data d;
	uint16_t seq = 0; uint8_t n = 0;
	if (!bs.Read(seq) || !RdStr(bs, d.title) || !RdStr(bs, d.sub) || !bs.Read(n) || n > 20) return false;
	for (int i = 0; i < n; i++)
	{
		Prod p; uint16_t stock = 0; uint8_t icon = 8, color = 0, q0 = 0; uint32_t price = 0;
		if (!RdStr(bs, p.name) || !RdStr(bs, p.desc) || !bs.Read(price) || !bs.Read(stock) || !bs.Read(icon) || !bs.Read(color) || !bs.Read(q0)) return false;
		p.price = price; p.stock = stock; p.icon = icon; p.color = color % 6; p.qty0 = q0;
		d.prods.push_back(std::move(p));
	}
	if (seq == 0) return false;
	d.seq = seq;
	std::lock_guard<std::mutex> l(g_mtx);
	g_net = std::move(d); g_ver++;
	return true;
}

void ShopPanel::Reset()
{
	{ std::lock_guard<std::mutex> l(g_mtx); g_net = Data(); g_ver++; }
	g_finger = -1; g_down = Target(); g_moved = false; g_area = 0;
}

int ShopPanel::Seq()
{
	std::lock_guard<std::mutex> l(g_mtx);
	return g_net.seq;
}

// ----------------------------------------------------------------------------------------------------- aviso "Informacao"
bool ShopPanel::OnToast(RakNet::BitStream& bs)
{
	uint8_t kind = 0; std::string t;
	if (!bs.Read(kind) || !RdStr(bs, t) || t.empty()) return false;
	std::lock_guard<std::mutex> l(g_tmx);
	g_toastKind = kind ? 1 : 0;
	g_toastText = kind ? t : ("Você comprou " + t + " com sucesso!");
	g_toastVer++;
	return true;
}

void ShopPanel::LocalToast(int kind, const char* text)
{
	if (!text || !*text) return;
	std::lock_guard<std::mutex> l(g_tmx);
	g_toastKind = kind ? 1 : 0; g_toastText = text; g_toastVer++;
}

void ShopPanel::DrawToast(ImGuiRenderer* r, float scrW, float scrH)
{
	const int64_t now = NowMs();
	std::string text; int kind = 0;
	{
		std::lock_guard<std::mutex> l(g_tmx);
		if (g_toastVer != g_toastSeen) { g_toastSeen = g_toastVer; g_toastT0 = now; }
		text = g_toastText; kind = g_toastKind;
	}
	if (g_toastT0 == 0 || text.empty()) return;
	const float dt = (float)(now - g_toastT0);
	const float IN = 220.0f, HOLD = 3800.0f, OUT = 450.0f;
	if (dt > IN + HOLD + OUT) return;
	float a = dt < IN ? dt / IN : (dt > IN + HOLD ? 1.0f - (dt - IN - HOLD) / OUT : 1.0f);
	a = std::clamp(a, 0.0f, 1.0f);
	const float u = std::min(0.62f * scrH / 416.0f, 0.80f * scrW / 646.0f);
	const int A = (int)(255.0f * a);
	const float cx = scrW * 0.5f, cy = scrH * 0.715f - (1.0f - a) * 8.0f * u, R = 19.0f * u;
	const ImColor ring = kind ? ImColor(255, 96, 96, A) : ImColor(46, 222, 150, A);
	for (int g = 3; g >= 1; g--) r->drawRoundedRect(ImVec2(cx - R - g * 2.2f * u, cy - R - g * 2.2f * u), ImVec2(cx + R + g * 2.2f * u, cy + R + g * 2.2f * u), ImColor(ring.Value.x, ring.Value.y, ring.Value.z, 0.10f * a), (R + g * 2.2f * u), false, 1.6f * u);
	r->drawRoundedRect(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R), ImColor(10, 34, 30, (int)(215.0f * a)), R, true);
	r->drawRoundedRect(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R), ring, R, false, 2.0f * u);
	const std::string glyph = kind ? "!" : "$";
	const float gfs = 17.0f * u;
	const ImVec2 gs = r->calculateTextSize(glyph, gfs);
	r->drawText(ImVec2(cx - gs.x * 0.5f, cy - gs.y * 0.5f), ring, glyph, false, gfs);
	const std::string title = kind ? "Aviso" : "Informação";
	const float tfs = 12.5f * u, mfs = 10.5f * u;
	const ImVec2 ts = r->calculateTextSize(title, tfs);
	const float ty = cy + R + 7.0f * u;
	r->drawText(ImVec2(cx - ts.x * 0.5f, ty), ImColor(255, 255, 255, A), title, true, tfs);
	r->drawText(ImVec2(cx - ts.x * 0.5f + 0.5f * u, ty), ImColor(255, 255, 255, A), title, false, tfs);
	std::string m = text;
	ImVec2 ms = r->calculateTextSize(m, mfs);
	const float maxW = 0.80f * scrW;
	while (ms.x > maxW && m.size() > 4) { m.pop_back(); ms = r->calculateTextSize(m + "...", mfs); }
	if (m.size() != text.size()) m += "...";
	ms = r->calculateTextSize(m, mfs);
	r->drawText(ImVec2(cx - ms.x * 0.5f, ty + ts.y + 3.0f * u), ImColor(255, 255, 255, A), m, true, mfs);
}

// ----------------------------------------------------------------------------------------------------- a tela da loja
void ShopPanel::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, std::vector<ShopLine>& order)
{
	answer = 0; order.clear();
	const int64_t now = NowMs();
	{
		std::lock_guard<std::mutex> l(g_mtx);
		if (g_ver != g_seen)
		{
			const bool same = (g_net.seq == g_d.seq && g_net.seq != 0);
			g_d = g_net; g_seen = g_ver; g_finger = -1; g_down = Target(); g_moved = false; g_area = 0;
			if (!same) { g_t0 = now; g_scrollGrid = 0.0f; g_scrollCart = 0.0f; }
			g_cart.clear();
			for (size_t i = 0; i < g_d.prods.size(); i++) if (g_d.prods[i].qty0 > 0) g_cart.push_back({(int)i, std::min(g_d.prods[i].qty0, Limit(g_d.prods[i]))});
			g_cartAnim = g_cart.empty() ? 0.0f : 1.0f;
			g_lastDraw = now;
		}
	}
	if (g_d.seq == 0) return;

	// entrada: 220 ms (aparece e sobe 10 unidades)
	float f = std::clamp((float)(now - g_t0) / 220.0f, 0.0f, 1.0f);
	f = 1.0f - (1.0f - f) * (1.0f - f);
	const int A255 = (int)(255.0f * f);
	auto A = [&](int rr, int gg, int bb, int aa) { return ImColor(rr, gg, bb, (int)((float)aa * f)); };

	// painel estreito <-> largo (anima ao entrar/sair o primeiro produto do carrinho)
	{
		const float dt = std::clamp((float)(now - g_lastDraw) / 180.0f, 0.0f, 1.0f);
		const float want = g_cart.empty() ? 0.0f : 1.0f;
		g_cartAnim += (want - g_cartAnim) * dt;
		if (std::fabs(want - g_cartAnim) < 0.01f) g_cartAnim = want;
		g_lastDraw = now;
	}
	const float LW = 416.0f, CWID = 230.0f, H = 416.0f;
	const float cartA = g_cartAnim;
	const float W = LW + CWID * cartA;
	const float u = std::min(0.62f * scrH / H, 0.86f * scrW / (LW + CWID));
	const float slide = (1.0f - f) * 10.0f;
	const ImVec2 P0((scrW - W * u) * 0.5f, (scrH - H * u) * 0.5f + slide * u);
	auto V = [&](float x, float y) { return ImVec2(P0.x + x * u, P0.y + y * u); };
	auto txtW = [&](const std::string& s, float fs) { return r->calculateTextSize(s, fs * u).x / u; };
	auto txtH = [&](const std::string& s, float fs) { return r->calculateTextSize(s, fs * u).y / u; };
	auto txt = [&](float x, float y, const ImColor& col, const std::string& s, float fs, bool bold = false)
	{
		r->drawText(V(x, y), col, s, false, fs * u);
		if (bold) r->drawText(V(x + 0.4f, y), col, s, false, fs * u);
	};
	auto fit = [&](const std::string& s, float fs, float maxW)
	{
		if (txtW(s, fs) <= maxW) return s;
		std::string t = s;
		while (t.size() > 1 && txtW(t + "...", fs) > maxW) t.pop_back();
		return t + "...";
	};
	auto wrap2 = [&](const std::string& s, float fs, float maxW, std::string& l1, std::string& l2)
	{
		l1 = s; l2.clear();
		if (txtW(s, fs) <= maxW) return;
		size_t cut = std::string::npos;
		for (size_t i = 0; i < s.size(); i++) { if (s[i] == ' ' && txtW(s.substr(0, i), fs) <= maxW) cut = i; else if (txtW(s.substr(0, i), fs) > maxW) break; }
		if (cut == std::string::npos) { l1 = fit(s, fs, maxW); return; }
		l1 = s.substr(0, cut); l2 = fit(s.substr(cut + 1), fs, maxW);
	};

	g_hits.clear();
	const float PX = 15.0f;                                   // margem esquerda do painel

	// ---- fundo: escurece a tela de leve + painel de vidro escuro ----
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), A(4, 10, 24, 70), true);
	for (int g = 4; g >= 1; g--)
		r->drawRoundedRect(V(-g * 2.0f, -g * 2.0f), V(W + g * 2.0f, H + g * 2.0f), A(10, 18, 36, 18), (16.0f + g * 2.0f) * u, true);
	r->drawRoundedRect(V(0, 0), V(W, H), A(26, 38, 60, 236), 16.0f * u, true);
	r->drawRectFilledMulticolor(V(6, 6), V(W - 6, H * 0.5f), A(70, 92, 130, 40), A(70, 92, 130, 40), A(70, 92, 130, 0), A(70, 92, 130, 0));
	r->drawRoundedRect(V(0, 0), V(W, H), A(120, 150, 196, 78), 16.0f * u, false, std::max(1.0f, 1.0f * u));

	// ---- cabecalho: icone de loja (roxo) + titulo + subtitulo ----
	{
		const ImColor pur = A(176, 112, 255, 255);
		r->drawRoundedRect(V(20, 20), V(33, 32), pur, 2.0f * u, true);                 // corpo da lojinha
		r->drawRoundedRect(V(18, 16), V(35, 22), pur, 2.0f * u, true);                 // toldo
		r->drawRoundedRect(V(24, 25), V(29, 32), A(26, 38, 60, 255), 1.0f * u, true);  // porta
		txt(40.0f, 16.0f, A(240, 246, 255, 255), g_d.title.empty() ? std::string("Loja") : fit(g_d.title, 14.5f, W - 40.0f - 120.0f), 14.5f, true);
		if (!g_d.sub.empty()) txt(PX, 46.0f, A(130, 168, 210, 255), fit(g_d.sub, 7.8f, W - 2.0f * PX - 110.0f), 7.8f);
	}

	// ---- botao Fechar (canto direito): tecla "ESC" + texto ----
	{
		const float x1 = W - 16.0f, x0 = x1 - 78.0f, y0 = 13.0f, y1 = 37.0f;
		const bool dn = g_down.kind == T_CLOSE;
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(70, 94, 130, dn ? 200 : 150), 12.0f * u, true);
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(150, 180, 220, 70), 12.0f * u, false, std::max(1.0f, 1.0f * u));
		r->drawRoundedRect(V(x0 + 7.0f, y0 + 6.0f), V(x0 + 26.0f, y1 - 6.0f), A(226, 240, 250, 235), 3.0f * u, true);
		const std::string esc = "ESC";
		txt(x0 + 16.5f - txtW(esc, 5.6f) * 0.5f, y0 + 6.0f + (12.0f - txtH(esc, 5.6f)) * 0.5f, A(38, 52, 78, 255), esc, 5.6f, true);
		const std::string fc = "Fechar";
		txt(x0 + 32.0f, y0 + (24.0f - txtH(fc, 8.6f)) * 0.5f, A(226, 238, 250, 255), fc, 8.6f);
		g_hits.push_back({V(x0 - 6, y0 - 6).x, V(x0 - 6, y0 - 6).y, V(x1 + 6, y1 + 6).x, V(x1 + 6, y1 + 6).y, {T_CLOSE, 0}});
	}

	// ---- aba "Todos" ----
	{
		const std::string tb = "Todos";
		r->drawRoundedRect(V(PX, 84), V(PX + 61, 106), A(233, 251, 254, 255), 11.0f * u, true);
		txt(PX + 30.5f - txtW(tb, 9.0f) * 0.5f, 84.0f + (22.0f - txtH(tb, 9.0f)) * 0.5f, A(34, 48, 72, 255), tb, 9.0f, true);
	}

	// ---- grade de produtos (2 colunas, rola com o dedo) ----
	const float GY0 = 124.0f, GY1 = 389.0f, CARDW = 178.0f, CARDH = 93.0f, GAP = 8.0f, GX0 = PX, GX1 = PX + CARDW + 15.0f;
	const int np = (int)g_d.prods.size();
	const int rows = (np + 1) / 2;
	const float content = rows > 0 ? rows * (CARDH + GAP) - GAP : 0.0f;
	const float view = GY1 - GY0;
	const float maxScroll = std::max(0.0f, content - view);
	g_scrollGrid = std::clamp(g_scrollGrid, 0.0f, maxScroll);
	{
		const ImVec2 c0 = V(0, GY0), c1 = V(LW, GY1);
		g_gridR[0] = c0.x; g_gridR[1] = c0.y; g_gridR[2] = c1.x; g_gridR[3] = c1.y;
		r->pushClipRect(c0, c1, false);
		for (int i = 0; i < np; i++)
		{
			const Prod& p = g_d.prods[(size_t)i];
			const float cx = (i % 2 == 0) ? GX0 : GX1, cy = GY0 + (float)(i / 2) * (CARDH + GAP) - g_scrollGrid;
			if (cy + CARDH < GY0 - 1.0f || cy > GY1 + 1.0f) continue;
			const int* ac = kAccent[p.color];
			const bool out = p.stock <= 0;
			const int inCart = CartQty(i);
			r->drawRoundedRect(V(cx, cy), V(cx + CARDW, cy + CARDH), A(84, 112, 152, out ? 52 : 86), 8.0f * u, true);
			r->drawRoundedRect(V(cx, cy), V(cx + CARDW, cy + CARDH), A(150, 180, 220, 46), 8.0f * u, false, std::max(1.0f, 1.0f * u));
			r->drawRoundedRect(V(cx + 8, cy + 3), V(cx + 16, cy + 6), A(ac[0], ac[1], ac[2], out ? 110 : 255), 1.5f * u, true);    // pontinho colorido
			// icone do produto
			r->drawRoundedRect(V(cx + 8, cy + 12), V(cx + 58, cy + 62), A(20, 30, 48, 190), 6.0f * u, true);
			{
				const ImVec2 a = V(cx + 11, cy + 15), b = V(cx + 55, cy + 59);
				const float aa[2] = { a.x, a.y }, bb[2] = { b.x, b.y };
				ShopDrawItemIcon(r, p.icon, aa, bb, out ? 110 : A255);
			}
			const float tx = cx + 67.0f, tw = CARDW - 67.0f - 6.0f;
			float nfs = 10.5f; while (nfs > 7.6f && txtW(p.name, nfs) > tw) nfs -= 0.4f;
			txt(tx, cy + 10.0f, A(244, 248, 255, out ? 150 : 255), fit(p.name, nfs, tw), nfs, true);
			std::string l1, l2; wrap2(p.desc, 7.4f, tw, l1, l2);
			const ImColor dc = A(ac[0], ac[1], ac[2], out ? 130 : 235);
			txt(tx, cy + 26.0f, dc, l1, 7.4f); if (!l2.empty()) txt(tx, cy + 36.0f, dc, l2, 7.4f);
			// preco (pilula azul-petroleo)
			{
				const std::string pr = Money(p.price);
				const float pw = std::max(50.0f, txtW(pr, 7.4f) + 12.0f);
				r->drawRoundedRect(V(cx + 8, cy + 69), V(cx + 8 + pw, cy + 83), A(18, 92, 112, 190), 7.0f * u, true);
				r->drawRoundedRect(V(cx + 8, cy + 69), V(cx + 8 + pw, cy + 83), A(64, 200, 222, 190), 7.0f * u, false, std::max(1.0f, 1.0f * u));
				txt(cx + 8 + (pw - txtW(pr, 7.4f)) * 0.5f, cy + 69 + (14.0f - txtH(pr, 7.4f)) * 0.5f, A(100, 232, 248, 255), pr, 7.4f, true);
				// botao Adicionar ao carrinho
				const float bx0 = cx + 8 + pw + 9.0f, bx1 = cx + CARDW - 8.0f;
				const bool dn = g_down.kind == T_ADD && g_down.idx == i;
				const bool full = inCart >= Limit(p);
				r->drawRoundedRect(V(bx0, cy + 69), V(bx1, cy + 84), A(120, 152, 200, (out || full) ? 60 : (dn ? 190 : 120)), 7.0f * u, true);
				r->drawRoundedRect(V(bx0, cy + 69), V(bx1, cy + 84), A(170, 200, 240, (out || full) ? 40 : 90), 7.0f * u, false, std::max(1.0f, 1.0f * u));
				const std::string bt = out ? "Esgotado" : (full ? "No carrinho (máx.)" : "Adicionar ao carrinho");
				txt((bx0 + bx1) * 0.5f - txtW(bt, 7.4f) * 0.5f, cy + 69 + (15.0f - txtH(bt, 7.4f)) * 0.5f, A(230, 240, 255, (out || full) ? 130 : 255), fit(bt, 7.4f, bx1 - bx0 - 4.0f), 7.4f);
				if (!out)
				{
					// area de toque do botao (maior que o desenho: dedo de verdade) cortada pela janela da lista
					const ImVec2 h0 = V(bx0 - 6.0f, std::max(cy + 60.0f, GY0)), h1 = V(bx1 + 6.0f, std::min(cy + CARDH, GY1));
					if (h1.y > h0.y) g_hits.push_back({h0.x, h0.y, h1.x, h1.y, {T_ADD, i}});
				}
			}
		}
		r->popClipRect();
		// barra de rolagem
		if (maxScroll > 0.0f)
		{
			const float tl = std::max(26.0f, view * view / content), ty = GY0 + (view - tl) * (g_scrollGrid / maxScroll);
			r->drawRoundedRect(V(LW - 21.0f, GY0), V(LW - 18.0f, GY1), A(120, 150, 196, 36), 1.5f * u, true);
			r->drawRoundedRect(V(LW - 21.0f, ty), V(LW - 18.0f, ty + tl), A(178, 232, 254, 235), 1.5f * u, true);
		}
	}

	// ---- coluna Carrinho (so quando ha produto) ----
	const float CX0 = LW + 12.0f, CX1 = LW + CWID - 18.0f + 0.0f;      // 428 .. 628 quando o painel esta largo
	if (cartA > 0.02f)
	{
		auto CA = [&](int rr, int gg, int bb, int aa) { return ImColor(rr, gg, bb, (int)((float)aa * f * cartA)); };
		// icone do carrinho (desenhado) + titulo
		{
			const ImColor wh = CA(232, 240, 252, 255);
			const ImVec2 h[4] = { V(CX0 + 11, 41), V(CX0 + 14, 41), V(CX0 + 16, 49), V(CX0 + 24, 49) };
			r->drawPolyline(h, 4, wh, false, 1.4f * u);
			r->drawRoundedRect(V(CX0 + 14.5f, 43), V(CX0 + 25, 47.5f), wh, 1.0f * u, true);
			r->drawRoundedRect(V(CX0 + 16, 51), V(CX0 + 18, 53), wh, 1.0f * u, true);
			r->drawRoundedRect(V(CX0 + 22, 51), V(CX0 + 24, 53), wh, 1.0f * u, true);
			const std::string ct = "Carrinho";
			r->drawText(V(CX0 + 31, 37), CA(244, 248, 255, 255), ct, false, 13.5f * u);
			r->drawText(V(CX0 + 31.4f, 37), CA(244, 248, 255, 255), ct, false, 13.5f * u);
		}
		r->drawLine(V(CX0, 69), V(CX1, 69), CA(120, 150, 196, 90), std::max(1.0f, 1.0f * u));

		const float RY0 = 80.0f, RY1 = 322.0f, ROWH = 40.0f;
		const int nc = (int)g_cart.size();
		const float cview = RY1 - RY0, ccontent = nc * ROWH;
		const float cmax = std::max(0.0f, ccontent - cview);
		g_scrollCart = std::clamp(g_scrollCart, 0.0f, cmax);
		const ImVec2 c0 = V(CX0 - 4, RY0), c1 = V(CX1 + 4, RY1);
		g_cartR[0] = c0.x; g_cartR[1] = c0.y; g_cartR[2] = c1.x; g_cartR[3] = c1.y;
		r->pushClipRect(c0, c1, false);
		for (int k = 0; k < nc; k++)
		{
			const ShopLine& ln = g_cart[(size_t)k];
			if (ln.idx < 0 || ln.idx >= np) continue;
			const Prod& p = g_d.prods[(size_t)ln.idx];
			const float ry = RY0 + 5.0f + (float)k * ROWH - g_scrollCart;
			if (ry + 34.0f < RY0 || ry > RY1) continue;
			r->drawRoundedRect(V(CX0 + 11, ry), V(CX0 + 43, ry + 32), CA(20, 30, 48, 190), 6.0f * u, true);
			{
				const ImVec2 a = V(CX0 + 13, ry + 2), b = V(CX0 + 41, ry + 30);
				const float aa[2] = { a.x, a.y }, bb[2] = { b.x, b.y };
				ShopDrawItemIcon(r, p.icon, aa, bb, (int)(255.0f * f * cartA));
			}
			const float nx = CX0 + 52.0f, nw = (CX1 - 66.0f) - nx;
			r->drawText(V(nx, ry + 1.0f), CA(244, 248, 255, 255), fit(p.name, 9.2f, nw), false, 9.2f * u);
			r->drawText(V(nx + 0.4f, ry + 1.0f), CA(244, 248, 255, 255), fit(p.name, 9.2f, nw), false, 9.2f * u);
			const std::string pr = Money(p.price);
			const float pw = std::max(46.0f, txtW(pr, 7.0f) + 10.0f);
			r->drawRoundedRect(V(nx, ry + 15), V(nx + pw, ry + 27), CA(18, 92, 112, 190), 6.0f * u, true);
			r->drawRoundedRect(V(nx, ry + 15), V(nx + pw, ry + 27), CA(64, 200, 222, 190), 6.0f * u, false, std::max(1.0f, 1.0f * u));
			r->drawText(V(nx + (pw - txtW(pr, 7.0f)) * 0.5f, ry + 15 + (12.0f - txtH(pr, 7.0f)) * 0.5f), CA(100, 232, 248, 255), pr, false, 7.0f * u);
			// - x1 +
			const std::string qs = "x" + std::to_string(ln.qty);
			const float qw = txtW(qs, 8.0f), qy = ry + 12.0f;
			const float pcx = CX1 - 6.0f, tx0 = pcx - 7.0f - qw, mcx = tx0 - 7.0f;
			const bool dnM = g_down.kind == T_MINUS && g_down.idx == ln.idx, dnP = g_down.kind == T_PLUS && g_down.idx == ln.idx;
			const bool atMax = ln.qty >= Limit(p);
			auto circ = [&](float cx, float cy, bool plus, bool dim, bool dn)
			{
				const ImVec2 a = V(cx - 4.5f, cy - 4.5f), b = V(cx + 4.5f, cy + 4.5f);
				const ImColor col = CA(dn ? 250 : 170, dn ? 250 : 176, dn ? 255 : 200, dim ? 90 : 235);
				r->drawRoundedRect(a, b, col, 4.5f * u, false, std::max(1.0f, 1.0f * u));
				r->drawLine(V(cx - 2.2f, cy), V(cx + 2.2f, cy), col, std::max(1.0f, 1.0f * u));
				if (plus) r->drawLine(V(cx, cy - 2.2f), V(cx, cy + 2.2f), col, std::max(1.0f, 1.0f * u));
			};
			circ(mcx, qy + 4.5f, false, false, dnM);
			r->drawText(V(tx0, qy - 0.5f), CA(236, 242, 252, 255), qs, false, 8.0f * u);
			circ(pcx, qy + 4.5f, true, atMax, dnP);
			{
				const ImVec2 m0 = V(mcx - 9.0f, std::max(ry, RY0)), m1 = V(mcx + 8.0f, std::min(ry + 34.0f, RY1));
				if (m1.y > m0.y) g_hits.push_back({m0.x, m0.y, m1.x, m1.y, {T_MINUS, ln.idx}});
				const ImVec2 q0 = V(pcx - 9.0f, std::max(ry, RY0)), q1 = V(pcx + 9.0f, std::min(ry + 34.0f, RY1));
				if (q1.y > q0.y && !atMax) g_hits.push_back({q0.x, q0.y, q1.x, q1.y, {T_PLUS, ln.idx}});
			}
		}
		r->popClipRect();

		// total + Finalizar Compra
		r->drawText(V(CX0 + 11, 328), CA(150, 172, 204, 255), "Total:", false, 7.6f * u);
		r->drawText(V(CX0 + 11, 339), CA(120, 224, 226, 255), Money(CartTotal()), false, 11.5f * u);
		const bool dn = g_down.kind == T_FIN;
		const float bx0 = CX0 + 11, bx1 = CX1, by0 = 367.0f, by1 = 397.0f;
		r->drawRoundedRect(V(bx0, by0), V(bx1, by1), CA(66, 166, 196, dn ? 255 : 220), 9.0f * u, true);
		r->drawRoundedRect(V(bx0, by0), V(bx1, by0 + 14.0f), CA(255, 255, 255, 22), 9.0f * u, true);
		const std::string fb = "Finalizar Compra";
		r->drawText(V((bx0 + bx1) * 0.5f - txtW(fb, 10.2f) * 0.5f, by0 + (30.0f - txtH(fb, 10.2f)) * 0.5f), CA(236, 248, 252, 255), fb, false, 10.2f * u);
		r->drawText(V((bx0 + bx1) * 0.5f - txtW(fb, 10.2f) * 0.5f + 0.4f, by0 + (30.0f - txtH(fb, 10.2f)) * 0.5f), CA(236, 248, 252, 255), fb, false, 10.2f * u);
		if (cartA > 0.9f) g_hits.push_back({V(bx0, by0).x, V(bx0, by0).y, V(bx1, by1).x, V(bx1, by1).y, {T_FIN, 0}});
	}
	else { g_cartR[0] = g_cartR[1] = g_cartR[2] = g_cartR[3] = 0; }

	// ---- toques ----
	auto hitAt = [&](float x, float y) -> Target
	{
		for (size_t i = g_hits.size(); i-- > 0;)
			if (x >= g_hits[i].x0 && x <= g_hits[i].x1 && y >= g_hits[i].y0 && y <= g_hits[i].y1) return g_hits[i].t;
		return Target();
	};
	auto inR = [](const float rc[4], float x, float y) { return rc[2] > rc[0] && x >= rc[0] && x <= rc[2] && y >= rc[1] && y <= rc[3]; };
	for (const ShopTouch& e : events)
	{
		if (f < 1.0f) continue;                                                // durante a animacao de entrada ignora toques
		if (e.type == 2 && g_finger < 0)
		{
			g_finger = e.num; g_sx = e.x; g_sy = e.y; g_ly = e.y; g_moved = false;
			g_down = hitAt(e.x, e.y);
			g_area = inR(g_gridR, e.x, e.y) ? 1 : (inR(g_cartR, e.x, e.y) ? 2 : 0);
		}
		else if (e.num == g_finger && e.type == 3)
		{
			if (!g_moved && std::fabs(e.y - g_sy) > 7.0f * u) { g_moved = true; g_down = Target(); }
			if (g_moved && g_area == 1) g_scrollGrid = std::clamp(g_scrollGrid - (e.y - g_ly) / u, 0.0f, maxScroll);
			if (g_moved && g_area == 2) g_scrollCart = g_scrollCart - (e.y - g_ly) / u;
			g_ly = e.y;
			if (!g_moved && g_down.kind != T_NONE) { const Target t = hitAt(e.x, e.y); if (t.kind != g_down.kind || t.idx != g_down.idx) g_down = Target(); }
		}
		else if (e.num == g_finger && e.type == 1)
		{
			const Target was = g_down; const bool mv = g_moved;
			g_finger = -1; g_down = Target(); g_moved = false; g_area = 0;
			if (e.cancel || mv || was.kind == T_NONE) continue;
			const Target now2 = hitAt(e.x, e.y);
			if (now2.kind != was.kind || now2.idx != was.idx) continue;
			switch (was.kind)
			{
			case T_CLOSE: answer = 2; return;
			case T_FIN: if (!g_cart.empty()) { order = std::vector<ShopLine>(g_cart.begin(), g_cart.end()); answer = 1; return; } break;
			case T_ADD:
			{
				if (was.idx < 0 || was.idx >= np) break;
				const Prod& p = g_d.prods[(size_t)was.idx];
				const int have = CartQty(was.idx);
				if (have >= Limit(p)) { ShopPanel::LocalToast(1, p.stock > 99 || have >= 99 ? "O máximo é 99 unidades de cada produto por compra." : "Estoque insuficiente para este produto."); break; }
				if (have == 0 && CartKinds() >= 8) { ShopPanel::LocalToast(1, "O carrinho aceita até 8 produtos diferentes."); break; }
				CartSet(was.idx, have + 1);
				break;
			}
			case T_PLUS:
			{
				if (was.idx < 0 || was.idx >= np) break;
				const Prod& p = g_d.prods[(size_t)was.idx];
				const int have = CartQty(was.idx);
				if (have < Limit(p)) CartSet(was.idx, have + 1);
				break;
			}
			case T_MINUS: CartSet(was.idx, CartQty(was.idx) - 1); break;
			default: break;
			}
		}
	}
}
