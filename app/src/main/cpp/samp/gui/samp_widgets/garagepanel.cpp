#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../game/IconTexDb.h"
#include "../../game/RW/RenderWare.h"
#include "garagepanel.h"
#include "garagicons.h"
#include <chrono>
#include <random>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	struct Car { int reg = 0, model = 0, cat = 1, state = 0, fuel = 0, engine = 0; unsigned km10 = 0; std::string name, desc; };
	std::mutex       g_mtx;
	std::vector<Car> g_net;
	unsigned         g_ver = 0;
	std::vector<Car> g_cars;               // copia da thread de desenho
	unsigned         g_seen = ~0u;
	int g_tab = 1, g_sel = 0, g_page = 0;  // aba (0 motos, 1 carros, 2 vips), registro selecionado, pagina de cartoes
	struct Hit { ImVec2 a, b; int kind; int idx; };
	enum { H_NONE = 0, H_CLOSE, H_TAB, H_CARD, H_PULL, H_PREV, H_NEXT };
	int g_finger = -1; Hit g_down{ImVec2(), ImVec2(), H_NONE, 0};

	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		return !n || bs.Read(&out[0], (unsigned)n);
	}
}


// ---- icone do topo: silhuetas que mudam ao acaso com a tela aberta (garagicons.h) ----
// A cada 1,8 a 3,4 s sorteia outra silhueta (nunca a mesma); transicao de 0,6 s: a antiga encolhe girando, a nova cresce com "pulinho".
// Sem alfa de proposito (triangulos vizinhos com alfa deixariam emendas visiveis): so escala + rotacao + flutuacao.
namespace
{
	int64_t HdrNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	struct HdrAnim { int cur = 0, prev = -1; int64_t t0 = 0, next = 0, lastDraw = 0; std::mt19937 rng{ std::random_device{}() }; } g_hdrs[2];   // [0] = topo da garagem, [1] = botao P1 do HUD

	template <class L>
	void DrawIconLayers(ImGuiRenderer* r, const L* layers, int nLayers, ImVec2 c, float half, float ang, const ImColor& fg, const ImColor& bg)
	{
		const float k = half / 1000.0f, ca = std::cos(ang), sa = std::sin(ang);
		for (int li = 0; li < nLayers; li++)
		{
			const ImColor& col = (layers[li].depth & 1) ? bg : fg;
			const int16_t* a = layers[li].t;
			for (int i = 0; i + 5 < layers[li].n; i += 6)
			{
				ImVec2 p[3];
				for (int j = 0; j < 3; j++)
				{
					const float x = a[i + j * 2] * k, y = a[i + j * 2 + 1] * k;
					p[j] = ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca);
				}
				r->drawConvexPolyFilled(p, 3, col);
			}
		}
	}

	void DrawHeaderIconSlot(ImGuiRenderer* r, ImVec2 c, float half, const ImColor& fg, const ImColor& bg, int slot)
	{
		using namespace GarageIcons;
		HdrAnim& g_hdr = g_hdrs[slot ? 1 : 0];
		const int64_t now = HdrNowMs();
		if (g_hdr.lastDraw == 0 || now - g_hdr.lastDraw > 700)          // a tela acabou de abrir: comeca numa silhueta ao acaso, sem transicao
		{
			g_hdr.cur = (int)(g_hdr.rng() % (unsigned)kCount); g_hdr.prev = -1;
			g_hdr.t0 = now; g_hdr.next = now + 1800 + (int)(g_hdr.rng() % 1600u);
		}
		g_hdr.lastDraw = now;
		if (now >= g_hdr.next)
		{
			int n = (int)(g_hdr.rng() % (unsigned)(kCount - 1)); if (n >= g_hdr.cur) n++;   // nunca repete a atual
			g_hdr.prev = g_hdr.cur; g_hdr.cur = n; g_hdr.t0 = now;
			g_hdr.next = now + 1800 + (int)(g_hdr.rng() % 1600u);
		}
		const float T = std::min(1.0f, (float)(now - g_hdr.t0) / 600.0f);
		float scale = 1.0f, ang = 0.0f; int icon = g_hdr.cur;
		if (g_hdr.prev >= 0 && T < 0.40f)            // saindo: antiga encolhe e gira
		{
			const float p = T / 0.40f; icon = g_hdr.prev; scale = 1.0f - p * p; ang = p * 0.9f;
		}
		else if (g_hdr.prev >= 0)                    // entrando: nova cresce com overshoot (easeOutBack)
		{
			const float p = (T - 0.40f) / 0.60f, c1 = 1.70158f, c3 = c1 + 1.0f, q = p - 1.0f;
			scale = 1.0f + c3 * q * q * q + c1 * q * q; ang = (1.0f - p) * -0.9f;
		}
		const float bob = std::sin((float)now * 0.004f) * 0.06f * half;       // flutua de leve
		const float sc = std::max(0.0f, scale) * half;
		if (sc < 0.5f) return;
		const ImVec2 cc(c.x, c.y + bob);
		switch (icon)
		{
			case 0: DrawIconLayers(r, k_moto_layers, (int)(sizeof(k_moto_layers) / sizeof(k_moto_layers[0])), cc, sc, ang, fg, bg); break;
			case 1: DrawIconLayers(r, k_scooter_layers, (int)(sizeof(k_scooter_layers) / sizeof(k_scooter_layers[0])), cc, sc, ang, fg, bg); break;
			case 2: DrawIconLayers(r, k_carro_layers, (int)(sizeof(k_carro_layers) / sizeof(k_carro_layers[0])), cc, sc, ang, fg, bg); break;
			case 3: DrawIconLayers(r, k_patinete_layers, (int)(sizeof(k_patinete_layers) / sizeof(k_patinete_layers[0])), cc, sc, ang, fg, bg); break;
			case 4: DrawIconLayers(r, k_bike_layers, (int)(sizeof(k_bike_layers) / sizeof(k_bike_layers[0])), cc, sc, ang, fg, bg); break;
			case 5: DrawIconLayers(r, k_van_layers, (int)(sizeof(k_van_layers) / sizeof(k_van_layers[0])), cc, sc, ang, fg, bg); break;
			default: DrawIconLayers(r, k_hoverboard_layers, (int)(sizeof(k_hoverboard_layers) / sizeof(k_hoverboard_layers[0])), cc, sc, ang, fg, bg); break;
		}
	}
	void DrawHeaderIcon(ImGuiRenderer* r, ImVec2 c, float half, const ImColor& fg, const ImColor& bg) { DrawHeaderIconSlot(r, c, half, fg, bg, 0); }
}

void GaragePanel::DrawCycleIcon(ImGuiRenderer* r, ImVec2 c, float half, const ImColor& fg, const ImColor& bg, int slot) { DrawHeaderIconSlot(r, c, half, fg, bg, slot); }

void GaragePanel::OnList(RakNet::BitStream& bs)
{
	uint8_t n = 0;
	if (!bs.Read(n) || n > 24) return;
	std::vector<Car> v;
	for (int i = 0; i < n; i++)
	{
		Car c; uint16_t reg = 0, model = 0; uint8_t cat = 1, st = 0, fuel = 0, eng = 0; uint32_t km = 0;
		if (!bs.Read(reg) || !bs.Read(model) || !bs.Read(cat) || !bs.Read(st) || !bs.Read(fuel) || !bs.Read(eng) || !bs.Read(km) || !RdStr(bs, c.name) || !RdStr(bs, c.desc)) return;
		c.reg = reg; c.model = model; c.cat = cat > 2 ? 1 : cat; c.state = st > 2 ? 2 : st; c.fuel = std::min<int>(fuel, 100); c.engine = std::min<int>(eng, 100); c.km10 = km;
		v.push_back(std::move(c));
	}
	std::lock_guard<std::mutex> l(g_mtx);
	g_net = std::move(v); g_ver++;
}

void GaragePanel::Reset()
{
	{ std::lock_guard<std::mutex> l(g_mtx); g_net.clear(); g_ver++; }
	g_tab = 1; g_sel = 0; g_page = 0; g_finger = -1; g_down = Hit{ImVec2(), ImVec2(), H_NONE, 0};
}

void GaragePanel::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<GarageTouch>& events, bool& close, int& pullReg)
{
	close = false; pullReg = 0;
	{
		std::lock_guard<std::mutex> l(g_mtx);
		if (g_ver != g_seen) { g_cars = g_net; g_seen = g_ver; }
	}
	// layout de 1280 x 540 (so 2 fileiras de cartoes): ocupa a tela toda em escala maior e fica um pouco mais abaixo do topo
	const float u = std::min(scrW / 1280.0f, scrH / 540.0f);
	const float ox = (scrW - 1280.0f * u) * 0.5f, oy = (scrH - 540.0f * u) * 0.5f + 12.0f * u;
	auto V = [&](float x, float y) { return ImVec2(ox + x * u, oy + y * u); };
	auto txt = [&](float x, float y, const ImColor& c, const std::string& s, float fs) { r->drawText(V(x, y), c, s, false, fs * u); };
	auto txtR = [&](float xr, float y, const ImColor& c, const std::string& s, float fs)
	{ const ImVec2 ts = r->calculateTextSize(s, fs * u); r->drawText(ImVec2(ox + xr * u - ts.x, oy + y * u), c, s, false, fs * u); };
	auto txtC = [&](float x0, float x1, float y, const ImColor& c, const std::string& s, float fs)
	{ const ImVec2 ts = r->calculateTextSize(s, fs * u); r->drawText(ImVec2(ox + (x0 + x1) * 0.5f * u - ts.x * 0.5f, oy + y * u), c, s, false, fs * u); };
	auto wrap = [&](float x, float y, const std::string& text, const ImColor& col, float fs, float maxW, int maxLines, float lh) -> int
	{
		std::string line, word; int lines = 0; const std::string t = text + " ";
		for (char ch : t)
		{
			if (ch != ' ') { word += ch; continue; }
			const std::string t2 = line.empty() ? word : line + " " + word;
			if (r->calculateTextSize(t2, fs * u).x > maxW * u && !line.empty()) { if (lines < maxLines) txt(x, y + lines * lh, col, line, fs); lines++; line = word; }
			else line = t2;
			word.clear();
		}
		if (!line.empty() && lines < maxLines) { txt(x, y + lines * lh, col, line, fs); lines++; }
		return lines < maxLines ? lines : maxLines;
	};

	const ImColor cAcc(38, 150, 255, 255), cText(246, 248, 252, 255), cSub(150, 166, 194, 255), cTrack(34, 48, 74, 255), cLine(110, 142, 196, 110), cAmber(247, 184, 28, 255), cRed(236, 70, 76, 255), cOff(26, 40, 66, 150);   // paleta AZUL (igual ao Menu Jogador)
	std::vector<Hit> hits;
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), ImColor(6, 14, 36, 172), true);

	// ---- fechar (canto superior direito) ----
	txtR(1212, 41, cSub, "Para fechar", 10.0f);
	r->drawRoundedRect(V(1220, 30), V(1262, 58), ImColor(14, 26, 50, 200), 4.0f * u, true);
	r->drawRoundedRect(V(1220, 30), V(1262, 58), cLine, 4.0f * u, false, 1.0f);
	r->drawLine(V(1233, 38), V(1249, 50), cText, 2.0f);
	r->drawLine(V(1249, 38), V(1233, 50), cText, 2.0f);
	hits.push_back({V(1212, 24), V(1270, 64), H_CLOSE, 0});

	// ---- cabecalho ----
	r->drawRoundedRect(V(71, 45), V(113, 87), cAcc, 7.0f * u, true);
	DrawHeaderIcon(r, V(92, 66), 15.5f * u, cText, cAcc);   // silhuetas (moto, patinete, carro, bike, van, hoverboard) trocando ao acaso com a tela aberta
	txt(124, 56, cText, "Garagem", 17.0f);

	// ---- abas ----
	const char* tabName[3] = {"Motos", "Carros", "Vips"};
	for (int i = 0; i < 3; i++)
	{
		const float x0 = 315.0f + i * 108.0f;
		r->drawRoundedRect(V(x0, 45), V(x0 + 100, 78), g_tab == i ? cAcc : cOff, 5.0f * u, true);
		txtC(x0, x0 + 100, 53, g_tab == i ? cText : cSub, tabName[i], 11.0f);
		hits.push_back({V(x0, 45), V(x0 + 100, 78), H_TAB, i});
	}

	// ---- veiculos da aba ----
	std::vector<int> idx;
	for (int i = 0; i < (int)g_cars.size(); i++) if (g_cars[(size_t)i].cat == g_tab) idx.push_back(i);
	const int perPage = 8, pages = std::max(1, ((int)idx.size() + perPage - 1) / perPage);
	g_page = std::clamp(g_page, 0, pages - 1);
	int selI = -1;
	for (int i : idx) if (g_cars[(size_t)i].reg == g_sel) selI = i;
	if (selI < 0 && !idx.empty()) { selI = idx[0]; g_sel = g_cars[(size_t)selI].reg; }
	if (idx.empty()) txt(315, 100, cSub, "Nenhum veículo nesta categoria.", 11.0f);

	for (int n = g_page * perPage; n < (int)idx.size() && n < (g_page + 1) * perPage; n++)
	{
		const Car& c = g_cars[(size_t)idx[(size_t)n]];
		const int slot = n - g_page * perPage;
		const float cx = 315.0f + (slot % 4) * 153.0f, cy = 92.0f + (slot / 4) * 186.0f;
		const bool sel = c.reg == g_sel;
		r->drawRoundedRect(V(cx, cy), V(cx + 143, cy + 176), ImColor(14, 26, 50, 160), 4.0f * u, true);
		r->drawRoundedRect(V(cx, cy), V(cx + 143, cy + 176), sel ? ImColor(38, 150, 255, 190) : cLine, 4.0f * u, false, 1.0f);
		std::string nm = c.name;
		while (nm.size() > 3 && r->calculateTextSize(nm, 10.5f * u).x > 123.0f * u) nm.pop_back();
		txt(cx + 10, cy + 10, cText, nm, 10.5f);
		if (c.state == 1) txt(cx + 10, cy + 26, cAmber, "Na rua", 8.5f);
		else if (c.state == 2) txt(cx + 10, cy + 26, cRed, "Desmanchado", 8.5f);
		// imagem do veiculo: carro/moto coberto de preto; VIP coberto de vermelho (texturas car_preto / car_vip do banco texdb/icones)
		if (RwTexture* tex = IconTexDb::Texture(c.cat == 2 ? "car_vip" : "car_preto"); tex && tex->raster)
			r->drawImage(V(cx + 8, cy + 56), V(cx + 135, cy + 120), (ImTextureID)tex->raster, ImColor(255, 255, 255, 255));
		r->drawRoundedRect(V(cx + 10, cy + 137), V(cx + 133, cy + 166), sel ? cAcc : ImColor(28, 44, 76, 220), 4.0f * u, true);
		txtC(cx + 10, cx + 133, cy + 144, sel ? cText : cSub, sel ? "Selecionado" : "Selecionar", 10.0f);
		hits.push_back({V(cx, cy), V(cx + 143, cy + 176), H_CARD, c.reg});
	}
	if (pages > 1)
	{
		char pg[24]; snprintf(pg, sizeof pg, "%d / %d", g_page + 1, pages);
		txtC(315, 925, 478, cSub, pg, 10.0f);
		r->drawRoundedRect(V(560, 470), V(600, 500), cOff, 5.0f * u, true); txtC(560, 600, 476, cText, "<", 12.0f);
		r->drawRoundedRect(V(640, 470), V(680, 500), cOff, 5.0f * u, true); txtC(640, 680, 476, cText, ">", 12.0f);
		hits.push_back({V(560, 470), V(600, 500), H_PREV, 0});
		hits.push_back({V(640, 470), V(680, 500), H_NEXT, 0});
	}

	// ---- coluna esquerda: veiculo selecionado ----
	txt(71, 104, cSub, "Você selecionou", 10.0f);
	if (selI >= 0)
	{
		const Car& c = g_cars[(size_t)selI];
		float fs = 30.0f;
		while (fs > 16.0f && r->calculateTextSize(c.name, fs * u).x > 232.0f * u) fs -= 1.0f;
		txt(71, 120, cText, c.name, fs);
		const char* st = c.state == 0 ? "Esse carro está guardado na garagem" : (c.state == 1 ? "Esse carro já está na rua" : "Esse carro foi desmanchado");
		const int sl = wrap(71, 160, st, cText, 9.5f, 232.0f, 2, 12.5f);
		if (!c.desc.empty()) wrap(71, 160 + sl * 12.5f, c.desc, cText, 9.5f, 232.0f, 3, 12.5f);
	}
	else txt(71, 120, cSub, "-", 30.0f);

	r->drawRoundedRect(V(71, 208), V(81, 218), cSub, 5.0f * u, false, 1.3f);
	txt(90, 207, cText, "Especificações", 10.0f);
	r->drawLine(V(184, 214), V(303, 214), ImColor(110, 142, 196, 90), 1.0f);
	auto bar = [&](float y, const char* label, const std::string& val, int pct)
	{
		txt(74, y, cSub, label, 9.5f);
		txtR(303, y, cText, val, 9.5f);
		r->drawRoundedRect(V(71, y + 19), V(303, y + 23), cTrack, 2.0f * u, true);
		const float w = 232.0f * std::clamp(pct, 0, 100) / 100.0f;
		if (w > 0.5f) r->drawRoundedRect(V(71, y + 19), V(71 + w, y + 23), cAcc, 2.0f * u, true);
	};
	char b1[24], b2[24], b3[32] = "-";
	int fuel = 0, eng = 0;
	if (selI >= 0)
	{
		const Car& c = g_cars[(size_t)selI]; fuel = c.fuel; eng = c.engine;
		snprintf(b3, sizeof b3, "%.0f km", c.km10 / 10.0f);
	}
	snprintf(b1, sizeof b1, "%d L", fuel); snprintf(b2, sizeof b2, "%d %%", eng);
	bar(233, "Gasolina", b1, fuel);
	bar(271, "Motor", b2, eng);
	txt(74, 309, cSub, "Milhas", 9.5f);
	txtR(303, 309, cText, b3, 9.5f);
	r->drawLine(V(71, 330), V(303, 330), ImColor(110, 142, 196, 60), 1.0f);

	{   // aviso
		ImVec2 tri[3] = {V(75, 343), V(80, 352), V(70, 352)};
		r->drawTriangle(tri[0], tri[1], tri[2], cText, false, 1.3f);
		txt(90, 343, cText, "Aviso", 10.0f);
		r->drawLine(V(130, 350), V(303, 350), ImColor(110, 142, 196, 90), 1.0f);
		wrap(71, 366, "Para realizar as ações selecione o veículo desejado e clique em uma das opções abaixo para realizar a ação desejada.", cSub, 9.0f, 232.0f, 4, 12.0f);
	}

	const bool canPull = selI >= 0 && g_cars[(size_t)selI].state == 0;
	const char* pullTxt = selI < 0 ? "Retirar veículo" : (g_cars[(size_t)selI].state == 0 ? "Retirar veículo" : (g_cars[(size_t)selI].state == 1 ? "Veículo na rua" : "Veículo desmanchado"));
	r->drawRoundedRect(V(71, 415), V(303, 448), canPull ? cAcc : ImColor(22, 36, 64, 120), 4.0f * u, true);
	if (!canPull) r->drawRoundedRect(V(71, 415), V(303, 448), cLine, 4.0f * u, false, 1.0f);
	txtC(71, 303, 425, canPull ? cText : ImColor(96, 110, 140, 255), pullTxt, 10.5f);
	if (canPull) hits.push_back({V(71, 415), V(303, 448), H_PULL, 0});

	// ---- toques ----
	auto hitTest = [&](float x, float y) -> Hit
	{
		for (int i = (int)hits.size() - 1; i >= 0; i--)
			if (x >= hits[(size_t)i].a.x && x <= hits[(size_t)i].b.x && y >= hits[(size_t)i].a.y && y <= hits[(size_t)i].b.y) return hits[(size_t)i];
		return Hit{ImVec2(), ImVec2(), H_NONE, 0};
	};
	for (const GarageTouch& e : events)
	{
		if (e.type == 2 && g_finger < 0) { g_finger = e.num; g_down = hitTest(e.x, e.y); }
		else if (e.num == g_finger && e.type == 1)
		{
			const Hit d = g_down; g_finger = -1; g_down = Hit{ImVec2(), ImVec2(), H_NONE, 0};
			if (e.cancel || d.kind == H_NONE) continue;
			const Hit h1 = hitTest(e.x, e.y);
			if (h1.kind != d.kind || h1.idx != d.idx) continue;
			switch (d.kind)
			{
			case H_CLOSE: close = true; break;
			case H_TAB: g_tab = d.idx; g_page = 0; g_sel = 0; break;
			case H_CARD: g_sel = d.idx; break;
			case H_PREV: if (g_page > 0) g_page--; break;
			case H_NEXT: g_page++; break;
			case H_PULL: if (canPull) pullReg = g_sel; break;
			default: break;
			}
		}
	}
}
