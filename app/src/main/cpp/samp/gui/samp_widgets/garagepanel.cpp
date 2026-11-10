#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "garagepanel.h"

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

	const ImColor cPink(250, 44, 92, 255), cText(246, 246, 248, 255), cSub(150, 150, 158, 255), cTrack(52, 44, 50, 255), cLine(120, 116, 124, 110), cAmber(247, 184, 28, 255), cRed(236, 70, 76, 255), cOff(40, 34, 40, 150);
	std::vector<Hit> hits;
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), ImColor(40, 8, 18, 168), true);

	// ---- fechar (canto superior direito) ----
	txtR(1212, 41, cSub, "Para fechar", 10.0f);
	r->drawRoundedRect(V(1220, 30), V(1262, 58), ImColor(30, 24, 30, 200), 4.0f * u, true);
	r->drawRoundedRect(V(1220, 30), V(1262, 58), cLine, 4.0f * u, false, 1.0f);
	r->drawLine(V(1233, 38), V(1249, 50), cText, 2.0f);
	r->drawLine(V(1249, 38), V(1233, 50), cText, 2.0f);
	hits.push_back({V(1212, 24), V(1270, 64), H_CLOSE, 0});

	// ---- cabecalho ----
	r->drawRoundedRect(V(71, 45), V(113, 87), cPink, 7.0f * u, true);
	{   // icone de carro
		r->drawRoundedRect(V(78, 68), V(106, 76), cText, 2.0f * u, true);
		ImVec2 cab[4] = {V(83, 68), V(88, 58), V(98, 58), V(103, 68)};
		r->drawConvexPolyFilled(cab, 4, cText);
		r->drawRoundedRect(V(81, 73), V(87, 79), cPink, 3.0f * u, true); r->drawRoundedRect(V(97, 73), V(103, 79), cPink, 3.0f * u, true);
		r->drawRoundedRect(V(82, 74), V(86, 78), cText, 2.0f * u, true); r->drawRoundedRect(V(98, 74), V(102, 78), cText, 2.0f * u, true);
	}
	txt(124, 56, cText, "Garagem", 17.0f);

	// ---- abas ----
	const char* tabName[3] = {"Motos", "Carros", "Vips"};
	for (int i = 0; i < 3; i++)
	{
		const float x0 = 315.0f + i * 108.0f;
		r->drawRoundedRect(V(x0, 45), V(x0 + 100, 78), g_tab == i ? cPink : cOff, 5.0f * u, true);
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
		r->drawRoundedRect(V(cx, cy), V(cx + 143, cy + 176), ImColor(28, 22, 28, 150), 4.0f * u, true);
		r->drawRoundedRect(V(cx, cy), V(cx + 143, cy + 176), sel ? ImColor(250, 44, 92, 170) : cLine, 4.0f * u, false, 1.0f);
		std::string nm = c.name;
		while (nm.size() > 3 && r->calculateTextSize(nm, 10.5f * u).x > 123.0f * u) nm.pop_back();
		txt(cx + 10, cy + 10, cText, nm, 10.5f);
		if (c.state == 1) txt(cx + 10, cy + 26, cAmber, "Na rua", 8.5f);
		else if (c.state == 2) txt(cx + 10, cy + 26, cRed, "Desmanchado", 8.5f);
		const bool moto = c.cat == 0;
		const ImColor sil(14, 12, 16, 235);
		const float mx = cx + 71.0f, my = cy + 84.0f;
		if (!moto)
		{
			r->drawRoundedRect(V(mx - 36, my + 2), V(mx + 36, my + 18), sil, 6.0f * u, true);
			ImVec2 cab[4] = {V(mx - 24, my + 3), V(mx - 15, my - 13), V(mx + 13, my - 13), V(mx + 26, my + 3)};
			r->drawConvexPolyFilled(cab, 4, sil);
			r->drawRoundedRect(V(mx - 27, my + 11), V(mx - 13, my + 25), sil, 7.0f * u, true);
			r->drawRoundedRect(V(mx + 13, my + 11), V(mx + 27, my + 25), sil, 7.0f * u, true);
		}
		else
		{
			r->drawRoundedRect(V(mx - 36, my - 2), V(mx - 12, my + 22), sil, 12.0f * u, false, 3.0f * u);
			r->drawRoundedRect(V(mx + 12, my - 2), V(mx + 36, my + 22), sil, 12.0f * u, false, 3.0f * u);
			ImVec2 fr[5] = {V(mx - 24, my + 10), V(mx - 6, my - 6), V(mx + 8, my - 6), V(mx + 18, my - 12), V(mx + 24, my + 10)};
			r->drawPolyline(fr, 5, sil, false, 3.0f * u);
			r->drawRoundedRect(V(mx - 10, my - 10), V(mx + 8, my - 5), sil, 2.0f * u, true);
		}
		r->drawRoundedRect(V(cx + 10, cy + 137), V(cx + 133, cy + 166), sel ? cPink : ImColor(46, 38, 46, 220), 4.0f * u, true);
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
	r->drawLine(V(184, 214), V(303, 214), ImColor(120, 116, 124, 90), 1.0f);
	auto bar = [&](float y, const char* label, const std::string& val, int pct)
	{
		txt(74, y, cSub, label, 9.5f);
		txtR(303, y, cText, val, 9.5f);
		r->drawRoundedRect(V(71, y + 19), V(303, y + 23), cTrack, 2.0f * u, true);
		const float w = 232.0f * std::clamp(pct, 0, 100) / 100.0f;
		if (w > 0.5f) r->drawRoundedRect(V(71, y + 19), V(71 + w, y + 23), cPink, 2.0f * u, true);
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
	r->drawLine(V(71, 330), V(303, 330), ImColor(120, 116, 124, 60), 1.0f);

	{   // aviso
		ImVec2 tri[3] = {V(75, 343), V(80, 352), V(70, 352)};
		r->drawTriangle(tri[0], tri[1], tri[2], cText, false, 1.3f);
		txt(90, 343, cText, "Aviso", 10.0f);
		r->drawLine(V(130, 350), V(303, 350), ImColor(120, 116, 124, 90), 1.0f);
		wrap(71, 366, "Para realizar as ações selecione o veículo desejado e clique em uma das opções abaixo para realizar a ação desejada.", cSub, 9.0f, 232.0f, 4, 12.0f);
	}

	const bool canPull = selI >= 0 && g_cars[(size_t)selI].state == 0;
	const char* pullTxt = selI < 0 ? "Retirar veículo" : (g_cars[(size_t)selI].state == 0 ? "Retirar veículo" : (g_cars[(size_t)selI].state == 1 ? "Veículo na rua" : "Veículo desmanchado"));
	r->drawRoundedRect(V(71, 415), V(303, 448), canPull ? cPink : ImColor(36, 30, 36, 120), 4.0f * u, true);
	if (!canPull) r->drawRoundedRect(V(71, 415), V(303, 448), cLine, 4.0f * u, false, 1.0f);
	txtC(71, 303, 425, canPull ? cText : ImColor(90, 88, 94, 255), pullTxt, 10.5f);
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
