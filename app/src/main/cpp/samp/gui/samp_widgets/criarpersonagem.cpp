#include "criarpersonagem.h"
#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../game/game.h"
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

	std::mutex g_m;
	bool g_active = false;
	int  g_sex = 0, g_part = 0;
	int  g_sel[4] = { 0, 0, 0, -1 };
	float g_scrollPx = 0.0f;         // rolagem da lista em pixels (limitada no desenho)

	// toque
	struct Hit { float x0, y0, x1, y1; int id; };   // id: 1 masculino | 2 feminino | 10+c*2+i escolher | 30+p camera | 40 finalizar
	std::vector<Hit> g_hits;
	float g_listX0 = 0, g_listX1 = 0, g_listY0 = 0, g_listY1 = 0;   // area rolavel (px)
	bool  g_down = false, g_moved = false, g_dragList = false;
	float g_dx = 0, g_dy = 0, g_lastY = 0;
	int   g_tap = 0;                  // acao pendente (tratada no desenho)
	int64_t g_lastSend = 0;
	bool g_acked = false;             // ja avisou o servidor que o painel abriu (act 5)

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

	const char* const kCatTitle[4] = { "Camisas", "Shorts", "Tênis", "Chinelos" };
	const char* const kCatItem[4]  = { "Camisa", "Short", "Tênis", "Chinelo" };
}

namespace CharCreate
{
	bool OnPacket(RakNet::BitStream& bs)
	{
		uint8_t mode = 0, sex = 0, part = 0, sel[4] = { 255, 255, 255, 255 };
		if (!bs.Read(mode) || !bs.Read(sex) || !bs.Read(part)) return false;
		for (int i = 0; i < 4; ++i) if (!bs.Read(sel[i])) return false;
		FLog("[CRIAR] pacote recebido: modo=%d sexo=%d parte=%d", (int)mode, (int)sex, (int)part);
		std::lock_guard<std::mutex> l(g_m);
		if (mode == 0) { g_active = false; g_hits.clear(); return true; }
		if (!g_active) { g_scrollPx = 0.0f; g_acked = false; }
		g_active = true;
		g_sex = sex ? 1 : 0; g_part = std::min<int>(part, 3);
		for (int i = 0; i < 4; ++i) g_sel[i] = (sel[i] == 255) ? -1 : std::min<int>(sel[i], 1);
		return true;
	}

	void Reset() { std::lock_guard<std::mutex> l(g_m); g_active = false; g_hits.clear(); g_down = false; g_tap = 0; g_scrollPx = 0.0f; g_acked = false; }
	bool Active() { std::lock_guard<std::mutex> l(g_m); return g_active; }

	void OnTouch(int type, int x, int y)
	{
		std::lock_guard<std::mutex> l(g_m);
		auto inList = [&](float px, float py) { return px >= g_listX0 && px <= g_listX1 && py >= g_listY0 && py <= g_listY1; };
		if (type == 2) { g_down = true; g_moved = false; g_dx = (float)x; g_dy = (float)y; g_lastY = (float)y; g_dragList = inList((float)x, (float)y); }
		else if (type == 3 && g_down)
		{
			if (std::fabs((float)x - g_dx) > 14.0f || std::fabs((float)y - g_dy) > 14.0f) g_moved = true;
			if (g_moved && g_dragList) g_scrollPx -= ((float)y - g_lastY);
			g_lastY = (float)y;
		}
		else if (type == 1)
		{
			if (g_down && !g_moved)
			{
				for (const Hit& h : g_hits)
					if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1)
					{
						if (h.id >= 1 && h.id < 30 && !inList((float)x, (float)y)) continue;   // sexo e linhas so valem dentro da area rolavel
						g_tap = h.id; break;
					}
			}
			g_down = false; g_dragList = false;
		}
	}

	void Draw(ImGuiRenderer* r, const ImVec2& scr)
	{
		int sex, part, sel[4]; float scrollPx;
		{ std::lock_guard<std::mutex> l(g_m); if (!g_active) return; sex = g_sex; part = g_part; for (int i = 0; i < 4; ++i) sel[i] = g_sel[i]; scrollPx = g_scrollPx; }
		const float k = scr.y / 540.0f;
		auto P = [&](float x, float y) { return ImVec2(x * k, y * k); };

		// painel da esquerda (o mundo, com o personagem, fica visivel a direita)
		const float panelW = 300.0f;
		r->drawRectFilledMulticolor(ImVec2(0, 0), P(panelW + 60, 540), ImColor(6, 12, 24, 235), ImColor(6, 12, 24, 0), ImColor(6, 12, 24, 0), ImColor(6, 12, 24, 235));
		r->drawRect(P(0, 0), P(panelW, 540), ImColor(8, 14, 28, 150), true);
		r->drawRect(P(panelW, 24), P(panelW + 1.2f, 516), ImColor(90, 150, 255, 90), true);
		r->drawText(P(28, 20), ImColor(150, 190, 255, 255), "CRIE SEU", false, 12.0f * k);
		r->drawText(P(28, 36), ImColor(255, 255, 255, 255), "PERSONAGEM", false, 24.0f * k);

		std::vector<Hit> hits;
		const float vy0 = 84.0f, vy1 = 540.0f - 76.0f, lx0 = 20.0f, lx1 = panelW - 16.0f;
		const float viewH = vy1 - vy0;
		// conteudo: sexo (74) + 4 secoes de 140
		const float contentH = 74.0f + 4 * 140.0f;
		const float maxScroll = std::max(0.0f, (contentH - viewH) * k);
		float sp = std::max(0.0f, std::min(scrollPx, maxScroll));
		{ std::lock_guard<std::mutex> l(g_m); g_scrollPx = sp; g_listX0 = lx0 * k; g_listX1 = lx1 * k; g_listY0 = vy0 * k; g_listY1 = vy1 * k; }
		const float off = sp / k - vy0;     // o conteudo comeca no topo da area visivel (y = vy0)

		r->pushClipRect(P(lx0 - 2, vy0), P(lx1 + 2, vy1), false);
		auto addHit = [&](float x, float y, float w, float h, int id)
		{
			const float yy = y - off;
			// so toca no que esta dentro da area visivel
			if (yy + h < vy0 || yy > vy1) return;
			hits.push_back({ x * k, std::max(yy, vy0) * k, (x + w) * k, std::min(yy + h, vy1) * k, id });
		};
		auto card = [&](float x, float y, float w, float h, const std::string& label, bool on, int id)
		{
			const float yy = y - off;
			r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(28, 96, 230, 255) : ImColor(24, 34, 54, 245), 9.0f * k, true);
			r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(140, 190, 255, 255) : ImColor(70, 100, 160, 130), 9.0f * k, false, std::max(1.0f, 1.3f * k));
			const float fs = 13.0f * k; const ImVec2 ts = r->calculateTextSize(label, fs);
			r->drawText(ImVec2((x + 14) * k, (yy + h * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), label, false, fs);
			// bolinha de escolha
			const float cx = x + w - 22, cy = yy + h * 0.5f;
			r->drawRoundedRect(P(cx - 8, cy - 8), P(cx + 8, cy + 8), on ? ImColor(255, 255, 255, 255) : ImColor(110, 130, 175, 255), 8.0f * k, false, 1.6f * k);
			if (on) r->drawRoundedRect(P(cx - 4.5f, cy - 4.5f), P(cx + 4.5f, cy + 4.5f), ImColor(255, 255, 255, 255), 4.5f * k, true);
			addHit(x, y, w, h, id);
		};

		float y = 0.0f;
		r->drawText(P(lx0, y - off + 2), ImColor(150, 190, 255, 255), "SEXO", false, 11.0f * k);
		card(lx0, y + 22, (lx1 - lx0 - 8) * 0.5f, 40, "Masculino", sex == 0, 1);
		card(lx0 + (lx1 - lx0 - 8) * 0.5f + 8, y + 22, (lx1 - lx0 - 8) * 0.5f, 40, "Feminino", sex == 1, 2);
		y += 74.0f;
		for (int c = 0; c < 4; ++c)
		{
			r->drawText(P(lx0, y - off + 2), ImColor(150, 190, 255, 255), kCatTitle[c], false, 11.0f * k);
			if (c == 2) r->drawText(P(lx0 + 56, y - off + 3), ImColor(120, 140, 180, 255), "(calçado: escolha 1)", false, 9.0f * k);
			if (c == 3) r->drawText(P(lx0 + 70, y - off + 3), ImColor(120, 140, 180, 255), "(ou chinelo)", false, 9.0f * k);
			for (int i = 0; i < 2; ++i)
				card(lx0, y + 22 + i * 52, lx1 - lx0, 44, std::string(kCatItem[c]) + " " + std::to_string(i + 1), sel[c] == i, 10 + c * 2 + i);
			y += 140.0f;
		}
		r->popClipRect();

		// camera: o que esta sendo visto
		{
			static const char* const kPart[4] = { "Corpo", "Camisa", "Short", "Calçado" };
			r->drawText(P(panelW + 24, 540 - 74), ImColor(190, 205, 240, 255), "Câmera", false, 10.0f * k);
			for (int p = 0; p < 4; ++p)
			{
				const float x = panelW + 24 + p * 92, yy = 540 - 56, w = 84, h = 34;
				const bool on = part == p;
				r->drawRoundedRect(P(x, yy), P(x + w, yy + h), on ? ImColor(28, 96, 230, 255) : ImColor(14, 22, 40, 220), 8.0f * k, true);
				r->drawRoundedRect(P(x, yy), P(x + w, yy + h), ImColor(90, 140, 230, on ? 255 : 120), 8.0f * k, false, std::max(1.0f, 1.2f * k));
				const float fs = 11.5f * k; const ImVec2 ts = r->calculateTextSize(kPart[p], fs);
				r->drawText(ImVec2((x + w * 0.5f) * k - ts.x * 0.5f, (yy + h * 0.5f) * k - ts.y * 0.5f), ImColor(255, 255, 255, 255), kPart[p], false, fs);
				hits.push_back({ x * k, yy * k, (x + w) * k, (yy + h) * k, 30 + p });
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
		{ std::lock_guard<std::mutex> l(g_m); g_hits = hits; }

		bool needAck; { std::lock_guard<std::mutex> l(g_m); needAck = !g_acked; g_acked = true; }
		if (needAck) Send(5, 0, 0);       // avisa o servidor: o painel abriu (sem isso ele finaliza sozinho em 9 s)
		int tap; { std::lock_guard<std::mutex> l(g_m); tap = g_tap; g_tap = 0; }
		const int64_t now = NowMs();
		if (tap && now - g_lastSend > 120)
		{
			g_lastSend = now;
			if (tap == 1 || tap == 2) Send(1, tap - 1, 0);
			else if (tap >= 10 && tap < 18) Send(2, (tap - 10) / 2, (tap - 10) % 2);
			else if (tap >= 30 && tap < 34) Send(3, tap - 30, 0);
			else if (tap == 40) Send(4, 0, 0);
		}
	}
}
