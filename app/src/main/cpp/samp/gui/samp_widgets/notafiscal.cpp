#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "notafiscal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

// Nota fiscal das lojas (ver notafiscal.h). Layout de referencia: cupom de 300 unidades de largura; tudo e calculado em
// "unidades de referencia" e multiplicado por u (escala da tela), como a agencia/garagem do jobs.cpp.

namespace
{
	struct Item { std::string name; int qty = 0; };
	struct Data { int seq = 0, icon = 0, pct = 0; uint32_t total = 0, tax = 0; std::string co; std::vector<Item> items; };

	std::mutex g_mtx;
	Data       g_net;
	unsigned   g_ver = 0;
	Data       g_d;                        // copia da thread de desenho
	unsigned   g_seen = ~0u;
	int64_t    g_t0 = 0;                   // instante em que a nota apareceu (animacao de entrada)
	int        g_finger = -1;              // dedo que esta apertando um botao
	int        g_down = 0;                 // botao apertado: 0 nenhum | 1 Finalizar | 2 Cancelar

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
		return "R$" + o;
	}

	// ---- paleta azul ----
	struct Pal
	{
		ImColor accent, accentD, ink, inkSoft, dash, paperT, paperB, bar;
	};

	// poligono convexo preenchido, sempre no sentido horario da tela (o contorno suave do imgui 1.71 so funciona assim)
	void Poly(ImGuiRenderer* r, std::vector<ImVec2> p, const ImColor& col)
	{
		if (p.size() < 3) return;
		float a2 = 0.0f;
		for (size_t i = 0; i < p.size(); i++) { const ImVec2& a = p[i]; const ImVec2& b = p[(i + 1) % p.size()]; a2 += a.x * b.y - b.x * a.y; }
		if (a2 < 0.0f) std::reverse(p.begin(), p.end());
		r->drawConvexPolyFilled(p.data(), (int)p.size(), col);
	}
}

bool InvoicePanel::OnShow(RakNet::BitStream& bs)
{
	Data d;
	uint16_t seq = 0, qty = 0; uint8_t icon = 0, pct = 0, n = 0; uint32_t total = 0, tax = 0;
	if (!bs.Read(seq) || !bs.Read(icon) || !bs.Read(pct) || !bs.Read(total) || !bs.Read(tax) || !RdStr(bs, d.co) || !bs.Read(n) || n > 8) return false;
	for (int i = 0; i < n; i++)
	{
		Item it;
		if (!RdStr(bs, it.name) || !bs.Read(qty)) return false;
		it.qty = qty;
		d.items.push_back(std::move(it));
	}
	if (seq == 0) return false;
	d.seq = seq; d.icon = icon; d.pct = pct > 100 ? 100 : pct; d.total = total; d.tax = tax > total ? total : tax;
	std::lock_guard<std::mutex> l(g_mtx);
	g_net = std::move(d); g_ver++;
	return true;
}

void InvoicePanel::Reset()
{
	{ std::lock_guard<std::mutex> l(g_mtx); g_net = Data(); g_ver++; }
	g_finger = -1; g_down = 0;
}

int InvoicePanel::Seq()
{
	std::lock_guard<std::mutex> l(g_mtx);
	return g_net.seq;
}

void InvoicePanel::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<InvoiceTouch>& events, int& answer)
{
	answer = 0;
	const int64_t now = NowMs();
	{
		std::lock_guard<std::mutex> l(g_mtx);
		if (g_ver != g_seen) { g_d = g_net; g_seen = g_ver; g_t0 = now; g_finger = -1; g_down = 0; }
	}
	if (g_d.seq == 0) return;

	const Pal c = {
		ImColor(37, 109, 255, 255),     // accent  (azul principal)
		ImColor(20, 58, 168, 255),      // accentD (azul escuro)
		ImColor(28, 40, 70, 255),       // ink     (texto forte)
		ImColor(100, 114, 146, 255),    // inkSoft (texto fraco)
		ImColor(160, 178, 220, 255),    // dash    (tracejado)
		ImColor(228, 238, 255, 255),    // paperT  (topo do papel)
		ImColor(250, 252, 255, 255),    // paperB  (base do papel)
		ImColor(70, 84, 120, 255),      // bar     (codigo de barras)
	};

	// entrada: 220 ms (aparece e sobe 12 unidades)
	float f = std::clamp((float)(now - g_t0) / 220.0f, 0.0f, 1.0f);
	f = 1.0f - (1.0f - f) * (1.0f - f);
	auto A = [&](const ImColor& col, float mul = 1.0f) { return ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * f * mul); };

	// ---- medidas (unidades de referencia) ----
	const int n = (int)std::min<size_t>(g_d.items.size(), 8);
	const float W = 300.0f, PADX = 22.0f, HOLE = 4.6f, ROW = 24.0f;
	const float yDash1 = 68.0f;
	const float yItems = yDash1 + 15.0f;
	const float yDash2 = yItems + (float)(n > 0 ? n : 1) * ROW + 4.0f;
	const float yTot = yDash2 + 14.0f, TROW = 27.0f;
	const float yDash3 = yTot + 3.0f * TROW + 2.0f;
	const float yBar = yDash3 + 13.0f, BARH = 24.0f;
	const float yBtn = yBar + BARH + 20.0f;
	const float H = yBtn + 38.0f;

	const float u = std::min(0.80f * scrH / H, 0.62f * scrW / W);
	const float slide = (1.0f - f) * 12.0f;
	const ImVec2 P0((scrW - W * u) * 0.5f, (scrH - H * u) * 0.5f + slide * u);
	auto V = [&](float x, float y) { return ImVec2(P0.x + x * u, P0.y + y * u); };
	auto txtW = [&](const std::string& s, float fs) { return r->calculateTextSize(s, fs * u).x / u; };
	auto txt = [&](float x, float y, const ImColor& col, const std::string& s, float fs, bool bold = false)
	{
		r->drawText(V(x, y), col, s, false, fs * u);
		if (bold) r->drawText(V(x + 0.45f, y), col, s, false, fs * u);
	};
	auto txtR = [&](float xr, float y, const ImColor& col, const std::string& s, float fs, bool bold = false) { txt(xr - txtW(s, fs), y, col, s, fs, bold); };
	auto fitName = [&](const std::string& s, float fs, float maxW)
	{
		if (txtW(s, fs) <= maxW) return s;
		std::string t = s;
		while (t.size() > 1 && txtW(t + "...", fs) > maxW) t.pop_back();
		return t + "...";
	};

	// ---- fundo: tela levemente azulada + brilho azul atras do cupom (aparece pelos furos) ----
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), A(ImColor(4, 14, 46, 105)), true);
	for (int g = 6; g >= 1; g--)
		r->drawRoundedRect(V(-g * 3.0f, -g * 3.0f), V(W + g * 3.0f, H + g * 3.0f), A(c.accent, 0.075f), (8.0f + g * 3.0f) * u, true);

	// ---- papel: corpo com degrade + dentes entre os furos ----
	r->drawRectFilledMulticolor(V(0, HOLE), V(W, H - HOLE), A(c.paperT), A(c.paperT), A(c.paperB), A(c.paperB));
	{
		const int N = 16;
		const float sp = W / (float)N;
		for (int side = 0; side < 2; side++)              // 0 = borda de cima, 1 = borda de baixo (espelhada)
		{
			const ImColor col = A(side == 0 ? c.paperT : c.paperB);
			for (int i = -1; i < N; i++)                  // i = -1 e N-1: pedacos das pontas (o furo vizinho fica fora do cupom)
			{
				const float x0 = (i + 0.5f) * sp, x1 = (i + 1.5f) * sp;       // centros dos furos vizinhos
				std::vector<ImVec2> b;                                         // contorno do dente: arco do furo da esquerda (de baixo para cima), borda, arco do da direita
				const int K = 4;
				for (int k = 0; k <= K; k++) { const float th = 1.5707963f * (1.0f - (float)k / K); b.push_back(ImVec2(x0 + HOLE * std::cos(th), HOLE * std::sin(th))); }
				for (int k = 0; k <= K; k++) { const float th = 3.1415927f - 1.5707963f * (float)k / K; b.push_back(ImVec2(x1 + HOLE * std::cos(th), HOLE * std::sin(th))); }
				const float sx = (x0 + x1) * 0.5f;                              // o dente e "estrela" vista do meio da base: leque de triangulos
				for (size_t k = 0; k + 1 < b.size(); k++)
				{
					std::vector<ImVec2> q = { ImVec2(sx, HOLE), b[k], b[k + 1] };
					for (ImVec2& p : q)
					{
						p.x = std::clamp(p.x, 0.0f, W);
						if (side == 1) p.y = H - p.y;
						p = V(p.x, p.y);
					}
					Poly(r, std::move(q), col);
				}
			}
		}
	}
	r->drawLine(V(0.0f, HOLE), V(0.0f, H - HOLE), A(c.accentD, 0.20f), 1.0f);
	r->drawLine(V(W, HOLE), V(W, H - HOLE), A(c.accentD, 0.20f), 1.0f);

	// ---- cabecalho: icone da empresa + titulo ----
	{
		const float tx = PADX, ty = 20.0f, ts = 30.0f;
		for (int g = 3; g >= 1; g--)
			r->drawRoundedRect(V(tx - g * 1.8f, ty - g * 1.8f), V(tx + ts + g * 1.8f, ty + ts + g * 1.8f), A(c.accent, 0.16f - g * 0.04f), (7.0f + g * 1.8f) * u, false, 1.6f);
		r->drawRoundedRect(V(tx, ty), V(tx + ts, ty + ts), A(c.accent), 7.0f * u, true);
		r->drawRoundedRect(V(tx, ty), V(tx + ts, ty + ts * 0.5f), A(ImColor(255, 255, 255, 38)), 7.0f * u, true);
		const ImColor wh = A(ImColor(255, 255, 255, 255)), tile = A(c.accent);
		auto G = [&](float gx, float gy) { return V(tx + 5.0f + gx * 1.25f, ty + 5.0f + gy * 1.25f); };    // grade 16 x 16 dentro do icone
		auto GD = [&](float cx, float cy, float rad, const ImColor& col) { r->drawRoundedRect(G(cx - rad, cy - rad), G(cx + rad, cy + rad), col, rad * 1.25f * u, true); };
		auto GL = [&](float x0, float y0, float x1, float y1, float th) { r->drawLine(G(x0, y0), G(x1, y1), wh, th * u); };
		auto GP = [&](std::vector<ImVec2> pts, const ImColor& col) { for (ImVec2& p : pts) p = G(p.x, p.y); Poly(r, std::move(pts), col); };
		switch (g_d.icon)
		{
		case 0:   // carrinho (mercado)
		{
			const ImVec2 h[4] = { G(0.4f, 2.4f), G(3.0f, 2.4f), G(5.0f, 10.6f), G(13.6f, 10.6f) };
			r->drawPolyline(h, 4, wh, false, 1.7f * u);
			GP({ ImVec2(3.7f, 4.3f), ImVec2(15.2f, 4.3f), ImVec2(13.5f, 9.3f), ImVec2(5.3f, 9.3f) }, wh);
			GD(6.4f, 13.4f, 1.35f, wh); GD(12.6f, 13.4f, 1.35f, wh);
			break;
		}
		case 1:   // chave inglesa (auto pecas)
		{
			GL(3.0f, 13.0f, 9.4f, 6.6f, 2.6f);
			GD(3.0f, 13.0f, 1.5f, wh);
			GD(11.0f, 5.0f, 3.9f, wh);
			GD(11.2f, 4.8f, 1.6f, tile);
			GP({ ImVec2(11.2f, 4.8f), ImVec2(13.6f, 0.4f), ImVec2(16.0f, 2.4f) }, tile);
			break;
		}
		case 2:   // camiseta (roupas)
		{
			GP({ ImVec2(5.0f, 2.8f), ImVec2(11.0f, 2.8f), ImVec2(11.0f, 14.4f), ImVec2(5.0f, 14.4f) }, wh);
			GP({ ImVec2(5.2f, 3.0f), ImVec2(5.2f, 7.6f), ImVec2(2.2f, 8.8f), ImVec2(0.4f, 5.4f) }, wh);
			GP({ ImVec2(10.8f, 3.0f), ImVec2(15.6f, 5.4f), ImVec2(13.8f, 8.8f), ImVec2(10.8f, 7.6f) }, wh);
			GP({ ImVec2(6.5f, 2.6f), ImVec2(9.5f, 2.6f), ImVec2(8.0f, 4.8f) }, tile);
			break;
		}
		case 3:   // tesoura (barbearia)
		{
			GL(5.2f, 1.0f, 11.0f, 10.6f, 1.8f);
			GL(10.8f, 1.0f, 5.0f, 10.6f, 1.8f);
			r->drawRoundedRect(G(2.0f, 10.4f), G(6.8f, 15.2f), wh, 2.4f * 1.25f * u, false, 1.6f * u);
			r->drawRoundedRect(G(9.2f, 10.4f), G(14.0f, 15.2f), wh, 2.4f * 1.25f * u, false, 1.6f * u);
			GD(8.0f, 5.8f, 0.9f, tile);
			break;
		}
		case 4:   // hamburguer (lanchonete)
		{
			std::vector<ImVec2> dome;
			for (int k = 0; k <= 10; k++) { const float th = 3.1415927f * (1.0f + (float)k / 10.0f); dome.push_back(ImVec2(8.0f + 6.8f * std::cos(th), 7.0f + 6.0f * std::sin(th))); }
			GP(dome, wh);
			GD(5.8f, 4.4f, 0.55f, tile); GD(8.4f, 3.4f, 0.55f, tile); GD(10.6f, 4.8f, 0.55f, tile);
			const ImVec2 z[9] = { G(1.6f, 8.8f), G(3.2f, 9.9f), G(4.8f, 8.8f), G(6.4f, 9.9f), G(8.0f, 8.8f), G(9.6f, 9.9f), G(11.2f, 8.8f), G(12.8f, 9.9f), G(14.4f, 8.8f) };
			r->drawPolyline(z, 9, wh, false, 1.5f * u);
			r->drawRoundedRect(G(1.6f, 10.5f), G(14.4f, 12.3f), wh, 0.9f * 1.25f * u, true);
			r->drawRoundedRect(G(2.8f, 13.0f), G(13.2f, 15.2f), wh, 1.6f * 1.25f * u, true);
			break;
		}
		default:  // sacola
		{
			r->drawRoundedRect(G(3.0f, 5.6f), G(13.0f, 15.0f), wh, 1.4f * 1.25f * u, true);
			r->drawRoundedRect(G(5.4f, 1.6f), G(10.6f, 7.8f), wh, 2.6f * 1.25f * u, false, 1.5f * u);
			break;
		}
		}
		const std::string title = "Nota Fiscal";
		const float tfs = 14.0f, x0 = tx + ts + 12.0f;
		const float th = r->calculateTextSize(title, tfs * u).y / u;
		if (g_d.co.empty()) txt(x0, ty + (ts - th) * 0.5f, A(c.ink), title, tfs, true);
		else
		{
			txt(x0, ty + 1.0f, A(c.ink), title, tfs, true);
			txt(x0, ty + 1.0f + th + 0.5f, A(c.accent), fitName(g_d.co, 9.0f, W - x0 - PADX), 9.0f, true);
		}
	}

	// ---- tracejado ----
	auto dashed = [&](float y)
	{
		for (float x = PADX; x < W - PADX; x += 6.5f) r->drawLine(V(x, y), V(std::min(x + 3.5f, W - PADX), y), A(c.dash), std::max(1.0f, 1.0f * u));
	};
	dashed(yDash1);

	// ---- itens ----
	{
		const float fs = 10.5f;
		if (n == 0) txt(PADX, yItems, A(c.inkSoft), "Sem itens", fs);
		for (int i = 0; i < n; i++)
		{
			const Item& it = g_d.items[(size_t)i];
			const std::string q = "(" + std::to_string(it.qty) + "x)";
			const float qw = txtW(q, fs);
			txt(PADX, yItems + i * ROW, A(c.inkSoft), fitName(it.name, fs, W - 2.0f * PADX - qw - 10.0f), fs);
			txtR(W - PADX, yItems + i * ROW, A(c.accent), q, fs, true);
		}
	}
	dashed(yDash2);

	// ---- totais: Sub-total + Imposto = Valor total (o imposto ja esta incluso no preco; vai para a Prefeitura) ----
	{
		const uint32_t sub = g_d.total - g_d.tax;
		char lb[40]; snprintf(lb, sizeof lb, "Imposto (%d%%):", g_d.pct);
		const float fs = 11.5f;
		txt(PADX, yTot, A(c.ink), "Sub-total:", fs, true);                  txtR(W - PADX, yTot, A(c.ink), Money(sub), fs, true);
		txt(PADX, yTot + TROW, A(c.ink), lb, fs, true);                      txtR(W - PADX, yTot + TROW, A(c.accent), Money(g_d.tax), fs, true);
		txt(PADX, yTot + 2 * TROW, A(c.ink), "Valor total:", fs + 1.0f, true); txtR(W - PADX, yTot + 2 * TROW, A(c.accentD), Money(g_d.total), fs + 1.0f, true);
	}
	dashed(yDash3);

	// ---- codigo de barras (derivado da nota: cada nota tem o seu) ----
	{
		uint32_t s = (uint32_t)g_d.seq * 2654435761u ^ g_d.total * 40503u ^ g_d.tax * 9973u;
		auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 16) & 0xFF; };
		float x = PADX;
		while (x < W - PADX - 1.0f)
		{
			const float w = 0.9f + (float)(rnd() % 4) * 0.55f, gap = 0.9f + (float)(rnd() % 3) * 0.55f;
			const float x1 = std::min(x + w, W - PADX);
			r->drawRect(V(x, yBar), V(x1, yBar + BARH), A(c.bar), true);
			x = x1 + gap;
		}
	}

	// ---- botoes ----
	const ImVec2 cb0 = V(PADX, yBtn - 9.0f), cb1 = V(W * 0.5f - 4.0f, yBtn + 27.0f);          // Cancelar
	const ImVec2 fb0 = V(W * 0.5f + 4.0f, yBtn - 9.0f), fb1 = V(W - PADX, yBtn + 27.0f);      // Finalizar pedido
	if (g_down == 2) r->drawRoundedRect(cb0, cb1, A(c.inkSoft, 0.16f), 7.0f * u, true);
	if (g_down == 1) r->drawRoundedRect(fb0, fb1, A(c.accent, 0.16f), 7.0f * u, true);
	{
		const float fs = 11.5f;
		const std::string a = "Cancelar", b = "Finalizar pedido";
		txt((PADX + W * 0.5f - 4.0f) * 0.5f - txtW(a, fs) * 0.5f, yBtn + 3.0f, A(c.inkSoft), a, fs, true);
		txt((W * 0.5f + 4.0f + W - PADX) * 0.5f - txtW(b, fs) * 0.5f, yBtn + 3.0f, A(c.accent), b, fs, true);
	}

	// ---- toques ----
	auto hit = [&](float x, float y) -> int
	{
		if (x >= fb0.x && x <= fb1.x && y >= fb0.y && y <= fb1.y) return 1;
		if (x >= cb0.x && x <= cb1.x && y >= cb0.y && y <= cb1.y) return 2;
		return 0;
	};
	for (const InvoiceTouch& e : events)
	{
		if (f < 1.0f) continue;                                                // durante a animacao de entrada ignora toques
		if (e.type == 2 && g_finger < 0) { g_finger = e.num; g_down = hit(e.x, e.y); }
		else if (e.num == g_finger && e.type == 3) { if (g_down && hit(e.x, e.y) != g_down) g_down = 0; }
		else if (e.num == g_finger && e.type == 1)
		{
			const int was = g_down;
			g_finger = -1; g_down = 0;
			if (!e.cancel && was && hit(e.x, e.y) == was) { answer = was; return; }
		}
	}
}
