#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "shoppanel.h"
#include "notafiscal.h"

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
	struct Data { int seq = 0; int cicon = 99; std::string title, sub; std::vector<Prod> prods; };

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
	uint16_t seq = 0; uint8_t n = 0, cicon = 99;
	if (!bs.Read(seq) || !bs.Read(cicon) || !RdStr(bs, d.title) || !RdStr(bs, d.sub) || !bs.Read(n) || n > 20) return false;
	for (int i = 0; i < n; i++)
	{
		Prod p; uint16_t stock = 0; uint8_t icon = 8, color = 0, q0 = 0; uint32_t price = 0;
		if (!RdStr(bs, p.name) || !RdStr(bs, p.desc) || !bs.Read(price) || !bs.Read(stock) || !bs.Read(icon) || !bs.Read(color) || !bs.Read(q0)) return false;
		p.price = price; p.stock = stock; p.icon = icon; p.color = color % 6; p.qty0 = q0;
		d.prods.push_back(std::move(p));
	}
	if (seq == 0) return false;
	d.seq = seq; d.cicon = cicon;
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


// ----------------------------------------------------------------------------------------------------- icones dos produtos (por codigo)
// g = 0 cola | 1 lima | 2 agua | 3 chips | 4 cereal | 5 molho | 6 pao | 7 camisetas | 8 jeans | 9 tenis | 10 camiseta no cabide | 11 pizza | 12 caixa de pizza | 13 caixa de hamburguer
//     14 hamburguer | 15 batata frita | 16 kit de reparo | 17 pneu | 18 oleo | 19 galao | 20 pomada | 21 navalha | 22 shampoo | outro = sacola.  (>= 100: icone do inventario, g - 100)
void ShopPanel::DrawGlyph(ImGuiRenderer* r, int g, const float a[2], const float b[2], int alpha)
{
	const float s = std::min(b[0] - a[0], b[1] - a[1]) / 16.0f;
	const float ox = (a[0] + b[0]) * 0.5f - 8.0f * s, oy = (a[1] + b[1]) * 0.5f - 8.0f * s;
	auto P = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
	auto C = [&](int rr, int gg, int bb, int aa = 255) { return ImColor(rr, gg, bb, aa * alpha / 255); };
	auto RR = [&](float x0, float y0, float x1, float y1, const ImColor& c, float rad) { r->drawRoundedRect(P(x0, y0), P(x1, y1), c, rad * s, true); };
	auto Ci = [&](float cx, float cy, float rad, const ImColor& c) { r->drawRoundedRect(P(cx - rad, cy - rad), P(cx + rad, cy + rad), c, rad * s, true); };
	auto Ln = [&](float x0, float y0, float x1, float y1, float th, const ImColor& c) { r->drawLine(P(x0, y0), P(x1, y1), c, std::max(1.0f, th * s)); };
	auto Pg = [&](std::vector<ImVec2> pts, const ImColor& c)
	{
		for (ImVec2& q : pts) q = P(q.x, q.y);
		float a2 = 0.0f;
		for (size_t i = 0; i < pts.size(); i++) { const ImVec2& u0 = pts[i]; const ImVec2& u1 = pts[(i + 1) % pts.size()]; a2 += u0.x * u1.y - u1.x * u0.y; }
		if (a2 < 0.0f) std::reverse(pts.begin(), pts.end());
		r->drawConvexPolyFilled(pts.data(), (int)pts.size(), c);
	};
	auto Shirt = [&](const ImColor& body, const ImColor& neck)
	{
		Pg({ ImVec2(5, 3), ImVec2(11, 3), ImVec2(11, 14), ImVec2(5, 14) }, body);
		Pg({ ImVec2(5.2f, 3.2f), ImVec2(5.2f, 7.6f), ImVec2(2.2f, 8.8f), ImVec2(0.6f, 5.4f) }, body);
		Pg({ ImVec2(10.8f, 3.2f), ImVec2(15.4f, 5.4f), ImVec2(13.8f, 8.8f), ImVec2(10.8f, 7.6f) }, body);
		Pg({ ImVec2(6.5f, 2.8f), ImVec2(9.5f, 2.8f), ImVec2(8, 5) }, neck);
	};
	switch (g)
	{
	case 0: case 1:   // lata (cola vermelha / lima verde)
	{
		const bool lima = g == 1;
		RR(4.6f, 2.0f, 11.4f, 14.0f, lima ? C(70, 190, 90) : C(222, 52, 56), 1.4f);
		RR(4.6f, 2.0f, 11.4f, 3.6f, C(200, 206, 214), 0.8f); RR(4.6f, 12.4f, 11.4f, 14.0f, C(200, 206, 214), 0.8f);
		RR(4.6f, 6.4f, 11.4f, 9.6f, lima ? C(230, 250, 200) : C(255, 255, 255), 0.2f);
		Ci(8.0f, 8.0f, 1.1f, lima ? C(70, 190, 90) : C(222, 52, 56));
		break;
	}
	case 2:   // garrafa de agua
		RR(6.8f, 1.0f, 9.2f, 2.8f, C(40, 90, 200), 0.5f); RR(7.1f, 2.6f, 8.9f, 5.0f, C(150, 210, 255), 0.4f);
		RR(5.6f, 4.6f, 10.4f, 15.0f, C(130, 205, 255), 1.6f); RR(5.6f, 8.2f, 10.4f, 11.4f, C(255, 255, 255), 0.2f);
		Ci(8.0f, 9.8f, 0.9f, C(40, 130, 220));
		break;
	case 3:   // pacote de chips
		RR(3.8f, 2.0f, 12.2f, 14.2f, C(255, 160, 40), 1.2f);
		for (int i = 0; i < 5; i++) Ln(4.2f + i * 1.8f, 2.0f, 4.2f + i * 1.8f, 3.6f, 0.9f, C(200, 110, 20));
		Ci(8.0f, 8.4f, 2.6f, C(255, 226, 90)); Ci(8.0f, 8.4f, 1.2f, C(230, 70, 50));
		break;
	case 4:   // caixa de cereal
		RR(4.0f, 1.8f, 12.0f, 14.2f, C(255, 214, 80), 0.9f); RR(4.0f, 1.8f, 12.0f, 4.4f, C(230, 70, 60), 0.9f);
		Ci(8.0f, 9.4f, 2.8f, C(250, 250, 250)); Ci(8.0f, 9.4f, 1.9f, C(190, 120, 60));
		break;
	case 5:   // lata de molho de tomate
		RR(4.6f, 3.0f, 11.4f, 14.0f, C(214, 60, 52), 1.2f);
		RR(4.6f, 3.0f, 11.4f, 4.4f, C(200, 206, 214), 0.6f); RR(4.6f, 12.6f, 11.4f, 14.0f, C(200, 206, 214), 0.6f);
		Ci(8.0f, 8.6f, 2.5f, C(255, 245, 235)); Ci(8.0f, 8.6f, 1.5f, C(220, 50, 40)); Ln(8.0f, 6.4f, 8.8f, 5.2f, 1.0f, C(60, 160, 70));
		break;
	case 6:   // pao de forma
		RR(2.0f, 6.0f, 14.0f, 13.6f, C(206, 142, 70), 1.6f); RR(3.0f, 3.6f, 13.0f, 8.6f, C(232, 172, 92), 2.6f);
		Ln(6.0f, 4.8f, 6.0f, 7.6f, 0.9f, C(190, 120, 50)); Ln(8.0f, 4.6f, 8.0f, 7.6f, 0.9f, C(190, 120, 50)); Ln(10.0f, 4.8f, 10.0f, 7.6f, 0.9f, C(190, 120, 50));
		break;
	case 7:   Shirt(C(120, 170, 255), C(70, 110, 210)); break;                       // camisetas
	case 8:   // calca jeans
		RR(4.0f, 1.8f, 12.0f, 4.6f, C(52, 96, 190), 0.8f);
		Pg({ ImVec2(4.0f, 4.4f), ImVec2(7.8f, 4.4f), ImVec2(7.4f, 14.6f), ImVec2(4.6f, 14.6f) }, C(70, 118, 214));
		Pg({ ImVec2(8.2f, 4.4f), ImVec2(12.0f, 4.4f), ImVec2(11.4f, 14.6f), ImVec2(8.6f, 14.6f) }, C(70, 118, 214));
		Ln(8.0f, 4.4f, 8.0f, 8.0f, 0.7f, C(40, 76, 160)); Ci(6.0f, 3.2f, 0.45f, C(255, 214, 90));
		break;
	case 9:   // tenis
		Pg({ ImVec2(2.4f, 11.0f), ImVec2(2.4f, 5.2f), ImVec2(6.6f, 5.2f), ImVec2(8.6f, 8.2f), ImVec2(13.8f, 9.6f), ImVec2(13.8f, 11.0f) }, C(230, 70, 70));
		RR(1.8f, 10.8f, 14.4f, 13.4f, C(250, 250, 250), 1.2f); Ln(5.0f, 7.4f, 7.2f, 9.2f, 0.8f, C(255, 255, 255));
		break;
	case 10:  // camiseta no cabide
		Shirt(C(255, 140, 170), C(210, 80, 110));
		Ln(8.0f, 2.8f, 8.0f, 1.6f, 0.9f, C(200, 206, 214)); Ln(8.0f, 1.6f, 9.4f, 1.0f, 0.9f, C(200, 206, 214)); Ln(8.0f, 2.6f, 4.2f, 4.4f, 0.8f, C(200, 206, 214)); Ln(8.0f, 2.6f, 11.8f, 4.4f, 0.8f, C(200, 206, 214));
		break;
	case 11:  // fatia de pizza
		Pg({ ImVec2(8.0f, 14.6f), ImVec2(1.8f, 4.0f), ImVec2(14.2f, 4.0f) }, C(255, 200, 70));
		RR(1.6f, 2.8f, 14.4f, 5.0f, C(200, 130, 60), 1.0f);
		Ci(6.4f, 7.2f, 1.2f, C(214, 56, 56)); Ci(9.8f, 8.0f, 1.2f, C(214, 56, 56)); Ci(8.0f, 10.8f, 1.1f, C(214, 56, 56));
		break;
	case 12:  // caixa de pizza
		RR(2.0f, 5.0f, 14.0f, 13.4f, C(196, 146, 96), 0.8f); RR(2.0f, 4.0f, 14.0f, 7.0f, C(222, 178, 126), 0.8f);
		Ci(8.0f, 10.0f, 1.7f, C(230, 70, 60)); Ln(5.0f, 8.0f, 11.0f, 8.0f, 0.6f, C(160, 110, 66));
		break;
	case 13:  // caixa de hamburguer
		RR(2.6f, 6.0f, 13.4f, 13.6f, C(206, 150, 96), 1.0f); RR(2.6f, 4.6f, 13.4f, 7.4f, C(232, 184, 126), 1.0f);
		Ci(8.0f, 10.2f, 2.0f, C(255, 214, 90)); Ci(8.0f, 10.2f, 1.0f, C(206, 80, 50));
		break;
	case 14:  // hamburguer
	{
		std::vector<ImVec2> dome;
		for (int k = 0; k <= 10; k++) { const float th = 3.1415927f * (1.0f + (float)k / 10.0f); dome.push_back(ImVec2(8.0f + 6.8f * std::cos(th), 7.0f + 6.0f * std::sin(th))); }
		Pg(dome, C(232, 160, 70));
		Ci(5.8f, 4.4f, 0.5f, C(255, 240, 200)); Ci(8.4f, 3.4f, 0.5f, C(255, 240, 200)); Ci(10.6f, 4.8f, 0.5f, C(255, 240, 200));
		RR(1.4f, 7.4f, 14.6f, 9.0f, C(90, 190, 80), 0.8f); RR(1.8f, 9.0f, 14.2f, 11.2f, C(120, 62, 40), 0.8f); RR(2.6f, 11.6f, 13.4f, 14.2f, C(232, 160, 70), 1.4f);
		break;
	}
	case 15:  // batata frita
		for (int i = 0; i < 5; i++) RR(4.8f + i * 1.5f, 2.2f + (i % 2) * 1.0f, 6.2f + i * 1.5f, 9.0f, C(255, 214, 80), 0.4f);
		Pg({ ImVec2(3.6f, 8.0f), ImVec2(12.4f, 8.0f), ImVec2(11.0f, 14.6f), ImVec2(5.0f, 14.6f) }, C(222, 54, 54));
		Ci(8.0f, 11.2f, 1.4f, C(255, 226, 90));
		break;
	case 16:  // kit de reparo (caixa de ferramentas)
		r->drawRoundedRect(P(5.2f, 3.4f), P(10.8f, 7.4f), C(200, 206, 214), 1.4f * s, false, std::max(1.0f, 1.2f * s));
		RR(1.8f, 6.0f, 14.2f, 13.8f, C(220, 60, 52), 1.2f); RR(1.8f, 8.6f, 14.2f, 9.6f, C(150, 36, 32), 0.1f); RR(6.8f, 7.6f, 9.2f, 10.8f, C(255, 214, 80), 0.6f);
		break;
	case 17:  // pneu
		Ci(8.0f, 8.0f, 6.4f, C(40, 44, 52)); Ci(8.0f, 8.0f, 3.6f, C(170, 176, 186)); Ci(8.0f, 8.0f, 1.3f, C(70, 76, 86));
		for (int i = 0; i < 4; i++) { const float t = 0.785f + i * 1.5708f; Ln(8.0f + 1.4f * std::cos(t), 8.0f + 1.4f * std::sin(t), 8.0f + 3.4f * std::cos(t), 8.0f + 3.4f * std::sin(t), 0.8f, C(70, 76, 86)); }
		break;
	case 18:  // oleo de motor
		RR(4.6f, 5.0f, 11.4f, 14.4f, C(242, 168, 40), 1.4f); Pg({ ImVec2(6.0f, 5.0f), ImVec2(10.0f, 5.0f), ImVec2(9.0f, 2.4f), ImVec2(7.0f, 2.4f) }, C(60, 60, 66));
		RR(4.6f, 8.2f, 11.4f, 11.8f, C(255, 245, 220), 0.2f); Ci(8.0f, 10.0f, 1.1f, C(20, 20, 24));
		break;
	case 19:  // galao de combustivel
		RR(3.0f, 4.0f, 13.0f, 14.4f, C(222, 52, 52), 1.4f); Pg({ ImVec2(8.6f, 4.0f), ImVec2(12.0f, 4.0f), ImVec2(11.4f, 1.6f), ImVec2(9.2f, 1.6f) }, C(60, 60, 66));
		RR(4.6f, 5.4f, 8.0f, 7.8f, C(60, 20, 20), 0.8f); Ci(8.6f, 10.8f, 1.8f, C(255, 214, 80));
		break;
	case 20:  // pomada
		RR(4.0f, 7.0f, 12.0f, 14.2f, C(56, 70, 120), 1.2f); RR(3.8f, 4.8f, 12.2f, 7.6f, C(200, 206, 214), 0.8f); RR(5.4f, 9.2f, 10.6f, 12.0f, C(240, 240, 250), 0.4f);
		break;
	case 21:  // navalha
		RR(1.8f, 8.4f, 9.4f, 11.2f, C(120, 74, 44), 1.2f);
		Pg({ ImVec2(8.6f, 7.8f), ImVec2(14.6f, 5.6f), ImVec2(14.6f, 7.4f), ImVec2(9.0f, 10.6f) }, C(214, 222, 232));
		Ci(5.6f, 9.8f, 0.6f, C(214, 222, 232));
		break;
	case 22:  // shampoo
		RR(5.0f, 5.0f, 11.0f, 14.6f, C(170, 112, 232), 1.6f); RR(6.4f, 1.6f, 9.6f, 5.2f, C(245, 245, 250), 0.8f); RR(5.0f, 8.0f, 11.0f, 11.6f, C(250, 240, 255), 0.2f); Ci(8.0f, 9.8f, 1.0f, C(170, 112, 232));
		break;
	default:  // sacola
		RR(3.0f, 5.6f, 13.0f, 14.8f, C(190, 150, 255), 1.4f); r->drawRoundedRect(P(5.4f, 1.8f), P(10.6f, 7.8f), C(190, 150, 255), 2.6f * s, false, std::max(1.0f, 1.4f * s));
		break;
	}
}

// icone de produto: glifo desenhado por codigo (0..99) ou icone do inventario (100 + icone)
static void DrawItem(ImGuiRenderer* r, int icon, const float a[2], const float b[2], int alpha)
{
	if (icon >= 100) ShopDrawItemIcon(r, icon - 100, a, b, alpha);
	else ShopPanel::DrawGlyph(r, icon, a, b, alpha);
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
	const float LW = 416.0f, CWID = 290.0f, H = 416.0f;
	const float cartA = g_cartAnim;
	const float W = LW + CWID * cartA;
	const float u = std::min(0.84f * scrH / H, 0.95f * scrW / (LW + CWID));
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
		InvoicePanel::DrawCompanyIcon(r, g_d.cicon, V(14, 10).x, V(14, 10).y, 32.0f * u, (int)(255 * f));
		txt(56.0f, 14.0f, A(240, 246, 255, 255), g_d.title.empty() ? std::string("Loja") : fit(g_d.title, 16.0f, W - 56.0f - 120.0f), 16.0f, true);
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
		// ordem de exibicao: com estoque primeiro, sem estoque no fim (mesma ordem do servidor dentro de cada grupo); i continua sendo o indice real do produto
		std::vector<int> ord; ord.reserve((size_t)np);
		for (int k = 0; k < np; k++) if (g_d.prods[(size_t)k].stock > 0) ord.push_back(k);
		for (int k = 0; k < np; k++) if (g_d.prods[(size_t)k].stock <= 0) ord.push_back(k);
		for (int slot = 0; slot < np; slot++)
		{
			const int i = ord[(size_t)slot];
			const Prod& p = g_d.prods[(size_t)i];
			const float cx = (slot % 2 == 0) ? GX0 : GX1, cy = GY0 + (float)(slot / 2) * (CARDH + GAP) - g_scrollGrid;
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
				const ImVec2 a = V(cx + 12, cy + 16), b = V(cx + 54, cy + 58);
				const float aa[2] = { a.x, a.y }, bb[2] = { b.x, b.y };
				DrawItem(r, p.icon, aa, bb, out ? 110 : A255);
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
				// sem estoque: pilula e valor em vermelho apagado (o jogador ve de cara que acabou)
				r->drawRoundedRect(V(cx + 8, cy + 69), V(cx + 8 + pw, cy + 83), out ? A(104, 34, 40, 170) : A(18, 92, 112, 190), 7.0f * u, true);
				r->drawRoundedRect(V(cx + 8, cy + 69), V(cx + 8 + pw, cy + 83), out ? A(176, 78, 84, 170) : A(64, 200, 222, 190), 7.0f * u, false, std::max(1.0f, 1.0f * u));
				txt(cx + 8 + (pw - txtW(pr, 7.4f)) * 0.5f, cy + 69 + (14.0f - txtH(pr, 7.4f)) * 0.5f, out ? A(214, 112, 116, 230) : A(100, 232, 248, 255), pr, 7.4f, true);
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
				DrawItem(r, p.icon, aa, bb, (int)(255.0f * f * cartA));
			}
			const float nx = CX0 + 52.0f, nw = (CX1 - 66.0f) - nx;
			r->drawText(V(nx, ry + 1.0f), CA(244, 248, 255, 255), fit(p.name, 10.6f, nw), false, 10.6f * u);
			r->drawText(V(nx + 0.4f, ry + 1.0f), CA(244, 248, 255, 255), fit(p.name, 10.6f, nw), false, 10.6f * u);
			const std::string pr = Money(p.price);
			const float pw = std::max(46.0f, txtW(pr, 8.0f) + 10.0f);
			r->drawRoundedRect(V(nx, ry + 15), V(nx + pw, ry + 27), CA(18, 92, 112, 190), 6.0f * u, true);
			r->drawRoundedRect(V(nx, ry + 15), V(nx + pw, ry + 27), CA(64, 200, 222, 190), 6.0f * u, false, std::max(1.0f, 1.0f * u));
			r->drawText(V(nx + (pw - txtW(pr, 8.0f)) * 0.5f, ry + 15 + (12.0f - txtH(pr, 8.0f)) * 0.5f), CA(100, 232, 248, 255), pr, false, 8.0f * u);
			// - x1 +
			const std::string qs = "x" + std::to_string(ln.qty);
			const float qw = txtW(qs, 9.4f), qy = ry + 12.0f;
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
			r->drawText(V(tx0, qy - 0.5f), CA(236, 242, 252, 255), qs, false, 9.4f * u);
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
		r->drawText(V(CX0 + 11, 328), CA(150, 172, 204, 255), "Total:", false, 8.6f * u);
		r->drawText(V(CX0 + 11, 339), CA(120, 224, 226, 255), Money(CartTotal()), false, 13.0f * u);
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
