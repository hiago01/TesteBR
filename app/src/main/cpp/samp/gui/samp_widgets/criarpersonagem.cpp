#include "criarpersonagem.h"
#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../game/game.h"
#include "../../game/CorpTex.h"
#include "../../game/SkinTexDb.h"
#include "../../net/netgame.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/raknet/BitStream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

extern CNetGame* pNetGame;

namespace
{
	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// categorias: 0 tom | 1 rosto | 2 camisa | 3 short | 4 tenis | 5 chinelo
	const int kCats = 6;
	std::mutex g_m;
	bool g_active = false;
	int  g_sex = 0, g_part = 0;
	int  g_sel[kCats] = { 1, 1, 1, 1, 1, 0 };
	float g_scrollPx = 0.0f;               // rolagem vertical da lista (px)
	float g_hx[kCats] = { 0, 0, 0, 0, 0, 0 };   // rolagem horizontal de cada fileira de variantes (px)

	// variantes de cada categoria (valores enviados ao servidor); refeito ao trocar de sexo
	std::vector<int> g_vals[kCats];
	int  g_valsSex = -1;
	int64_t g_valsAt = 0;

	struct Hit { float x0, y0, x1, y1; int id; };   // id: 1/2 sexo | 1000*(cat+1)+valor chip | 30+p camera | 40 finalizar
	std::vector<Hit> g_hits;
	struct Strip { float x0, y0, x1, y1; };
	Strip g_strip[kCats];
	bool  g_stripOn[kCats] = { false, false, false, false, false, false };
	float g_listX0 = 0, g_listX1 = 0, g_listY0 = 0, g_listY1 = 0;   // area rolavel (px)
	bool  g_down = false, g_moved = false;
	int   g_axis = 0;                      // 0 indefinido | 1 horizontal (fileira) | 2 vertical (lista)
	int   g_dragStrip = -1;
	bool  g_dragList = false;
	float g_dx = 0, g_dy = 0, g_lastX = 0, g_lastY = 0;
	int   g_tap = 0;
	int64_t g_lastSend = 0;
	bool  g_acked = false;

	void Send(int act, int a, int b)
	{
		if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED || !pNetGame->GetRakClient()) return;
		RakNet::BitStream bs;
		bs.Write((uint8_t)70);
		bs.Write((uint8_t)act);
		if (act == 1 || act == 3) bs.Write((uint8_t)a);
		if (act == 2) { bs.Write((uint8_t)a); bs.Write((uint8_t)b); }
		pNetGame->GetRakClient()->RPC(&RPC_JobsRequest, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}

	const char* const kCatTitle[kCats] = { "TOM DE PELE", "ROSTO", "CAMISA", "SHORT", "TÊNIS", "CHINELO" };

	// monta as listas de variantes. Mulher: contagens da base feminina (camisa 2 Blusa cropped, calca 6 Shorts, calcado 4 Tenis, 1 Chinelo).
	// Homem: variantes que existem no banco (pack) de torso1 / short1 / boot1 / slipper1 (a ultima de cada pasta e a transparente: fora).
	void BuildVals(int sex)
	{
		static const int kFaceM[7] = { 0, 6, 8, 8, 7, 5, 12 };
		static const int kFaceF[7] = { 0, 6, 8, 8, 7, 6, 12 };
		for (int c = 0; c < kCats; ++c) g_vals[c].clear();
		for (int t = 1; t <= 6; ++t) g_vals[0].push_back(t);
		const int tone = std::max(1, std::min(g_sel[0], 6));
		const int fm = sex ? kFaceF[tone] : kFaceM[tone];
		for (int f = 1; f <= fm; ++f) g_vals[1].push_back(f);
		if (sex)
		{
			const int n[4] = { CorpTex::FemCount(4, 2), CorpTex::FemCount(3, 6), CorpTex::FemCount(2, 4), CorpTex::FemCount(2, 1) };
			for (int k = 0; k < 4; ++k) for (int v = 1; v <= std::min(n[k], 99); ++v) g_vals[2 + k].push_back(v);
		}
		else
		{
			static const char* const kFolder[4] = { "tshirts/1", "pants/5", "boot/1", "slipper/1" };
			for (int k = 0; k < 4; ++k)
			{
				std::vector<int> v = SkinTexDb::Variants(kFolder[k]);
				std::sort(v.begin(), v.end());
				if (v.size() > 1) v.pop_back();                       // a ultima e o PNG transparente ("tirar a peca")
				if (v.empty()) for (int i = 1; i <= 8; ++i) v.push_back(i);   // banco ainda nao carregado: ate 8 (o servidor valida; refeito a cada 2 s)
				for (int x : v) if (x >= 1 && x <= 99) g_vals[2 + k].push_back(x);
			}
		}
		g_valsSex = sex; g_valsAt = NowMs();
	}
}

namespace CharCreate
{
	bool OnPacket(RakNet::BitStream& bs)
	{
		uint8_t mode = 0, sex = 0, part = 0, sel[kCats] = { 1, 1, 1, 1, 1, 0 };
		if (!bs.Read(mode) || !bs.Read(sex) || !bs.Read(part)) return false;
		for (int i = 0; i < kCats; ++i) if (!bs.Read(sel[i])) return false;
		FLog("[CRIAR] pacote recebido: modo=%d sexo=%d parte=%d", (int)mode, (int)sex, (int)part);
		std::lock_guard<std::mutex> l(g_m);
		if (mode == 0) { g_active = false; g_hits.clear(); return true; }
		if (!g_active) { g_scrollPx = 0.0f; g_acked = false; for (int i = 0; i < kCats; ++i) g_hx[i] = 0.0f; g_valsSex = -1; }
		g_active = true;
		if ((sex ? 1 : 0) != g_sex) g_valsSex = -1;
		g_sex = sex ? 1 : 0; g_part = std::min<int>(part, 4);
		const int oldTone = g_sel[0];
		for (int i = 0; i < kCats; ++i) g_sel[i] = sel[i];
		if (oldTone != g_sel[0]) g_valsSex = -1;      // o tom muda a quantidade de rostos
		return true;
	}

	void Reset() { std::lock_guard<std::mutex> l(g_m); g_active = false; g_hits.clear(); g_down = false; g_tap = 0; g_scrollPx = 0.0f; g_acked = false; g_valsSex = -1; }
	bool Active() { std::lock_guard<std::mutex> l(g_m); return g_active; }

	void OnTouch(int type, int x, int y)
	{
		std::lock_guard<std::mutex> l(g_m);
		auto inRect = [&](float px, float py, float a, float b, float c, float d) { return px >= a && px <= c && py >= b && py <= d; };
		auto inList = [&](float px, float py) { return inRect(px, py, g_listX0, g_listY0, g_listX1, g_listY1); };
		if (type == 2)
		{
			g_down = true; g_moved = false; g_axis = 0; g_dx = g_lastX = (float)x; g_dy = g_lastY = (float)y;
			g_dragList = inList((float)x, (float)y);
			g_dragStrip = -1;
			for (int c = 0; c < kCats; ++c)
				if (g_stripOn[c] && inRect((float)x, (float)y, g_strip[c].x0, g_strip[c].y0, g_strip[c].x1, g_strip[c].y1)) { g_dragStrip = c; break; }
		}
		else if (type == 3 && g_down)
		{
			const float ax = std::fabs((float)x - g_dx), ay = std::fabs((float)y - g_dy);
			if (!g_moved && (ax > 12.0f || ay > 12.0f))
			{
				g_moved = true;
				g_axis = (g_dragStrip >= 0 && ax > ay) ? 1 : 2;        // fileira de variantes: arrasta para o lado | senao rola a lista
			}
			if (g_moved)
			{
				if (g_axis == 1 && g_dragStrip >= 0) g_hx[g_dragStrip] -= ((float)x - g_lastX);
				else if (g_axis == 2 && g_dragList) g_scrollPx -= ((float)y - g_lastY);
			}
			g_lastX = (float)x; g_lastY = (float)y;
		}
		else if (type == 1)
		{
			if (g_down && !g_moved)
			{
				for (const Hit& h : g_hits)
					if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) { g_tap = h.id; break; }
			}
			g_down = false; g_dragList = false; g_dragStrip = -1; g_axis = 0;
		}
	}

	void Draw(ImGuiRenderer* r, const ImVec2& scr)
	{
		int sex, part, sel[kCats]; float scrollPx;
		{
			std::lock_guard<std::mutex> l(g_m);
			if (!g_active) return;
			sex = g_sex; part = g_part; for (int i = 0; i < kCats; ++i) sel[i] = g_sel[i]; scrollPx = g_scrollPx;
			const int64_t nowv = NowMs();
			if (g_valsSex != sex || (sex == 0 && nowv - g_valsAt > 2000 && g_vals[2].size() <= 8)) BuildVals(sex);
		}
		std::vector<int> vals[kCats];
		{ std::lock_guard<std::mutex> l(g_m); for (int c = 0; c < kCats; ++c) vals[c] = g_vals[c]; }

		const float k = scr.y / 540.0f;
		auto P = [&](float x, float y) { return ImVec2(x * k, y * k); };

		const float panelW = 300.0f;
		r->drawRectFilledMulticolor(ImVec2(0, 0), P(panelW + 60, 540), ImColor(6, 12, 24, 235), ImColor(6, 12, 24, 0), ImColor(6, 12, 24, 0), ImColor(6, 12, 24, 235));
		r->drawRect(P(0, 0), P(panelW, 540), ImColor(8, 14, 28, 150), true);
		r->drawRect(P(panelW, 24), P(panelW + 1.2f, 516), ImColor(90, 150, 255, 90), true);
		r->drawText(P(28, 20), ImColor(150, 190, 255, 255), "CRIE SEU", false, 12.0f * k);
		r->drawText(P(28, 36), ImColor(255, 255, 255, 255), "PERSONAGEM", false, 24.0f * k);

		std::vector<Hit> hits;
		Strip strips[kCats]; bool stripOn[kCats] = { false, false, false, false, false, false };
		float hxNew[kCats];
		const float vy0 = 84.0f, vy1 = 540.0f - 76.0f, lx0 = 20.0f, lx1 = panelW - 16.0f;
		const float viewH = vy1 - vy0;
		const float secH = 84.0f, contentH = 74.0f + kCats * secH;
		const float maxScroll = std::max(0.0f, (contentH - viewH) * k);
		const float sp = std::max(0.0f, std::min(scrollPx, maxScroll));
		{ std::lock_guard<std::mutex> l(g_m); g_scrollPx = sp; g_listX0 = lx0 * k; g_listX1 = lx1 * k; g_listY0 = vy0 * k; g_listY1 = vy1 * k; for (int c = 0; c < kCats; ++c) hxNew[c] = g_hx[c]; }
		const float off = sp / k - vy0;     // o conteudo comeca no topo da area visivel

		r->pushClipRect(P(lx0 - 2, vy0), P(lx1 + 2, vy1), false);
		auto addHit = [&](float x0, float y0, float x1, float y1, int id)     // coordenadas virtuais ja na tela; recorta pela area visivel
		{
			const float a = std::max(y0, vy0), b = std::min(y1, vy1);
			if (b <= a) return;
			hits.push_back({ x0 * k, a * k, x1 * k, b * k, id });
		};
		auto card = [&](float x, float y, float w, float h, const std::string& label, bool on, int id)
		{
			const float yy = y - off;
			r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(28, 96, 230, 255) : ImColor(24, 34, 54, 245), 9.0f * k, true);
			r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(140, 190, 255, 255) : ImColor(70, 100, 160, 130), 9.0f * k, false, std::max(1.0f, 1.3f * k));
			const float fs = 13.0f * k; const ImVec2 ts = r->calculateTextSize(label, fs);
			r->drawText(ImVec2((x + w * 0.5f) * k - ts.x * 0.5f, (yy + h * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), label, false, fs);
			addHit(x, yy, x + w, yy + h, id);
		};

		float y = 0.0f;
		r->drawText(P(lx0, y - off + 2), ImColor(150, 190, 255, 255), "SEXO", false, 11.0f * k);
		const float hw = (lx1 - lx0 - 8) * 0.5f;
		card(lx0, y + 22, hw, 40, "Masculino", sex == 0, 1);
		card(lx0 + hw + 8, y + 22, hw, 40, "Feminino", sex == 1, 2);
		y += 74.0f;

		for (int c = 0; c < kCats; ++c)
		{
			const float ty = y - off;
			r->drawText(P(lx0, ty + 2), ImColor(150, 190, 255, 255), kCatTitle[c], false, 11.0f * k);
			if (c == 4) r->drawText(P(lx0 + 40, ty + 3), ImColor(120, 140, 180, 255), "(calçado: tênis OU chinelo)", false, 9.0f * k);
			const std::vector<int>& v = vals[c];
			const float cw = 40.0f, gap = 6.0f, chH = 42.0f;
			const float sx0 = lx0, sx1 = lx1, sy0 = ty + 20.0f, sy1 = sy0 + chH;
			const float totalW = v.size() * (cw + gap) - gap;
			const float maxH = std::max(0.0f, totalW - (sx1 - sx0)) * k;
			const float hxv = std::max(0.0f, std::min(hxNew[c], maxH));
			hxNew[c] = hxv;
			const float ox = hxv / k;
			// recorte da fileira (so desenha/toca dentro dela)
			const float cy0 = std::max(sy0, vy0), cy1 = std::min(sy1, vy1);
			if (cy1 > cy0)
			{
				strips[c] = { sx0 * k, cy0 * k, sx1 * k, cy1 * k }; stripOn[c] = true;
				r->pushClipRect(P(sx0, cy0), P(sx1, cy1), true);
				for (size_t i = 0; i < v.size(); ++i)
				{
					const float bx = sx0 + i * (cw + gap) - ox;
					if (bx + cw < sx0 || bx > sx1) continue;
					const bool on = sel[c] == v[i];
					r->drawRoundedRect(P(bx, sy0), P(bx + cw, sy1), on ? ImColor(28, 96, 230, 255) : ImColor(24, 34, 54, 245), 8.0f * k, true);
					r->drawRoundedRect(P(bx, sy0), P(bx + cw, sy1), on ? ImColor(140, 190, 255, 255) : ImColor(70, 100, 160, 130), 8.0f * k, false, std::max(1.0f, 1.3f * k));
					const std::string lab = std::to_string(v[i]);
					const float fs = 12.5f * k; const ImVec2 ts = r->calculateTextSize(lab, fs);
					r->drawText(ImVec2((bx + cw * 0.5f) * k - ts.x * 0.5f, ((sy0 + sy1) * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), lab, false, fs);
					// toque: so a parte visivel da fileira
					const float hx0 = std::max(bx, sx0), hx1 = std::min(bx + cw, sx1);
					if (hx1 > hx0) hits.push_back({ hx0 * k, cy0 * k, hx1 * k, cy1 * k, 1000 * (c + 1) + v[i] });
				}
				r->popClipRect();
				// setas indicando que da para arrastar para o lado
				if (maxH > 0.0f)
				{
					if (hxv > 1.0f) r->drawText(P(sx0 - 1, (sy0 + sy1) * 0.5f - 7), ImColor(255, 255, 255, 170), "<", false, 15.0f * k);
					if (hxv < maxH - 1.0f) r->drawText(P(sx1 - 7, (sy0 + sy1) * 0.5f - 7), ImColor(255, 255, 255, 170), ">", false, 15.0f * k);
				}
			}
			y += secH;
		}
		r->popClipRect();

		// camera: o que esta sendo visto
		{
			static const char* const kPart[5] = { "Corpo", "Rosto", "Camisa", "Short", "Calçado" };
			static const int kPartId[5] = { 0, 4, 1, 2, 3 };
			r->drawText(P(panelW + 24, 540 - 74), ImColor(190, 205, 240, 255), "Câmera", false, 10.0f * k);
			for (int p = 0; p < 5; ++p)
			{
				const float x = panelW + 24 + p * 80, yy = 540 - 56, w = 74, h = 34;
				const bool on = part == kPartId[p];
				r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(28, 96, 230, 255) : ImColor(14, 22, 40, 220), 8.0f * k, true);
				r->drawRoundedRect(P(x, yy), P(x + w, yy + h), ImColor(90, 140, 230, on ? 255 : 120), 8.0f * k, false, std::max(1.0f, 1.2f * k));
				const float fs = 11.0f * k; const ImVec2 ts = r->calculateTextSize(kPart[p], fs);
				r->drawText(ImVec2((x + w * 0.5f) * k - ts.x * 0.5f, (yy + h * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), kPart[p], false, fs);
				hits.push_back({ x * k, yy * k, (x + w) * k, (yy + h) * k, 30 + kPartId[p] });
			}
		}
		// finalizar
		{
			const float x = lx0, yy = 540 - 62, w = lx1 - lx0, h = 44;
			r->drawRoundedRect(P(x, yy), P(x + w, yy + h), ImColor(36, 180, 96, 255), 10.0f * k, true);
			const float fs = 14.0f * k; const ImVec2 ts = r->calculateTextSize("FINALIZAR", fs);
			r->drawText(ImVec2((x + w * 0.5f) * k - ts.x * 0.5f, (yy + h * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), "FINALIZAR", false, fs);
			hits.push_back({ x * k, yy * k, (x + w) * k, (yy + h) * k, 40 });
		}
		{
			std::lock_guard<std::mutex> l(g_m);
			g_hits = hits;
			for (int c = 0; c < kCats; ++c) { g_strip[c] = strips[c]; g_stripOn[c] = stripOn[c]; g_hx[c] = hxNew[c]; }
		}

		bool needAck; { std::lock_guard<std::mutex> l(g_m); needAck = !g_acked; g_acked = true; }
		if (needAck) Send(5, 0, 0);       // avisa o servidor: o painel abriu (sem isso ele finaliza sozinho em 9 s)
		int tap; { std::lock_guard<std::mutex> l(g_m); tap = g_tap; g_tap = 0; }
		const int64_t now = NowMs();
		if (tap && now - g_lastSend > 120)
		{
			g_lastSend = now;
			if (tap == 1 || tap == 2) Send(1, tap - 1, 0);
			else if (tap >= 1000) Send(2, tap / 1000 - 1, tap % 1000);
			else if (tap >= 30 && tap < 35) Send(3, tap - 30, 0);
			else if (tap == 40) Send(4, 0, 0);
		}
	}
}
