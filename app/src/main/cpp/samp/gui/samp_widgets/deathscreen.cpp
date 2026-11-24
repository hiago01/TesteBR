#include "deathscreen.h"
#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../net/netgame.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
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
	bool    g_enabled = false, g_active = false, g_called = false, g_hasPos = false, g_cleanup = false;
	int     g_medics = 0;
	int64_t g_endMs = 0, g_lastPkt = 0, g_pendingUntil = 0, g_graceUntil = 0; std::atomic<int64_t> g_callLock{0};
	float   g_px = 0, g_py = 0, g_pz = 0;
	// botao (preenchido no desenho, lido no toque)
	float   g_bx0 = 0, g_by0 = 0, g_bx1 = 0, g_by1 = 0; bool g_btnOn = false; std::atomic<bool> g_down{false};
	std::atomic<bool> g_fire{false};
}

namespace DeathScreen
{
	void SetEnabled(bool on) { std::lock_guard<std::mutex> l(g_m); g_enabled = on; if (!on) { g_active = false; g_pendingUntil = 0; g_graceUntil = 0; } }
	bool Enabled() { std::lock_guard<std::mutex> l(g_m); return g_enabled; }

	void OnState(int, int secs, int medics, int called, bool hasPos, float x, float y, float z)
	{
		std::lock_guard<std::mutex> l(g_m);
		const int64_t now = NowMs();
		g_active = true; g_pendingUntil = 0; g_graceUntil = 0;
		g_endMs = now + (int64_t)std::max(0, secs) * 1000;
		g_medics = medics; g_called = called != 0; g_lastPkt = now;
		if (called) g_callLock = 0;
		if (hasPos) { g_px = x; g_py = y; g_pz = z; g_hasPos = true; }
	}

	void Hide(int)
	{
		std::lock_guard<std::mutex> l(g_m);
		if (g_active || g_pendingUntil) { g_cleanup = true; g_graceUntil = NowMs() + 3000; }   // 3 s: o servidor ainda vai nascer o jogador (evita a tela de classes piscar)
		g_active = false; g_pendingUntil = 0; g_called = false; g_medics = 0; g_hasPos = false;
	}

	void Reset()
	{
		std::lock_guard<std::mutex> l(g_m);
		g_enabled = false; g_active = false; g_pendingUntil = 0; g_graceUntil = 0; g_called = false; g_hasPos = false; g_cleanup = false; g_medics = 0; g_down = false;
	}

	void OnLocalDeath(float x, float y, float z)
	{
		std::lock_guard<std::mutex> l(g_m);
		if (!g_enabled) return;
		if (!g_active) { g_px = x; g_py = y; g_pz = z; g_hasPos = true; }     // se o servidor ja esta contando, vale o local dele
		g_pendingUntil = NowMs() + 5000;     // espera o servidor confirmar (op 50); sem confirmacao em 5 s o jogo segue o fluxo normal
	}

	bool Active() { std::lock_guard<std::mutex> l(g_m); return g_active; }

	bool Holding()
	{
		std::lock_guard<std::mutex> l(g_m);
		if (g_active)
		{
			// servidor sumiu (queda/travada) = nao prende o jogador para sempre: 40 s sem nenhum pacote alem do tempo restante
			if (NowMs() > g_endMs + 40000 && NowMs() - g_lastPkt > 40000) { g_active = false; g_cleanup = true; return false; }
			return true;
		}
		if (g_pendingUntil && NowMs() < g_pendingUntil) return true;
		g_pendingUntil = 0;
		return g_graceUntil && NowMs() < g_graceUntil;
	}

	bool Releasing() { std::lock_guard<std::mutex> l(g_m); return !g_active && g_graceUntil && NowMs() < g_graceUntil; }

	bool HoldPos(float& x, float& y, float& z)
	{
		std::lock_guard<std::mutex> l(g_m);
		if (!g_hasPos) return false;
		x = g_px; y = g_py; z = g_pz; return true;
	}

	bool TakeCleanup() { std::lock_guard<std::mutex> l(g_m); const bool c = g_cleanup; g_cleanup = false; return c; }

	void OnTouch(int type, int x, int y)
	{
		std::lock_guard<std::mutex> l(g_m);
		const bool in = g_btnOn && x >= g_bx0 && x <= g_bx1 && y >= g_by0 && y <= g_by1;
		if (type == 2) g_down = in;
		else if (type == 1) { if (g_down && in) g_fire.store(true); g_down = false; }
		else if (type == 3 && !in) g_down = false;
	}

	// coracao pulsando + linha de batimento
	static void Heart(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col)
	{
		ImVec2 p[64];
		for (int i = 0; i < 64; ++i)
		{
			const float t = (float)i / 64.0f * 6.2831853f;
			const float hx = 16.0f * std::pow(std::sin(t), 3.0f);
			const float hy = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) - std::cos(4 * t);
			p[i] = ImVec2(cx + hx * s, cy - hy * s);
		}
		r->drawConvexPolyFilled(p, 64, col);
	}

	void Draw(ImGuiRenderer* r, const ImVec2& scr)
	{
		int medics; bool called; int64_t endMs, now = NowMs();
		{ std::lock_guard<std::mutex> l(g_m); if (!g_active) { g_btnOn = false; return; } medics = g_medics; called = g_called; endMs = g_endMs; }
		const float k = scr.y / 540.0f;
		const float cx = scr.x * 0.5f;
		r->drawRect(ImVec2(0, 0), scr, ImColor(6, 10, 18, 150), true);
		r->drawRectFilledMulticolor(ImVec2(0, 0), ImVec2(scr.x, scr.y * 0.35f), ImColor(0, 0, 0, 120), ImColor(0, 0, 0, 120), ImColor(0, 0, 0, 0), ImColor(0, 0, 0, 0));

		// coracao (batimento) + ECG branca
		const float beat = 1.0f + 0.07f * std::sin((float)(now % 900) / 900.0f * 6.2831853f * 1.0f);
		const float hy = scr.y * 0.34f;
		for (int i = 3; i >= 1; --i) Heart(r, cx, hy, 1.85f * k * beat * (1.0f + 0.12f * i), ImColor(40, 110, 255, 18));
		Heart(r, cx, hy, 1.85f * k * beat, ImColor(38, 112, 255, 245));
		{
			ImVec2 e[9] = { {cx - 17 * k, hy - 2 * k}, {cx - 7 * k, hy - 2 * k}, {cx - 4 * k, hy - 11 * k}, {cx - 1 * k, hy + 9 * k}, {cx + 2 * k, hy - 5 * k}, {cx + 5 * k, hy - 2 * k}, {cx + 17 * k, hy - 2 * k}, {cx + 17 * k, hy - 2 * k}, {cx + 17 * k, hy - 2 * k} };
			r->drawPolyline(e, 7, ImColor(255, 255, 255, 235), false, std::max(1.2f, 1.6f * k));
		}

		auto centered = [&](float y, const std::string& t, float fs, const ImColor& c)
		{
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(cx - ts.x * 0.5f, y), c, t, false, fs);
		};
		centered(scr.y * 0.53f, "VOCÊ ESTÁ FERIDO E VAI PERDER A CONSCIÊNCIA EM", 15.0f * k, ImColor(235, 240, 250, 245));
		const int secs = (int)std::max<int64_t>(0, (endMs - now + 999) / 1000);
		centered(scr.y * 0.58f, std::to_string(secs), 46.0f * k, ImColor(40, 112, 255, 255));
		centered(scr.y * 0.70f, "segundos", 10.0f * k, ImColor(210, 218, 232, 235));
		centered(scr.y * 0.78f, "VOCÊ SÓ PODE SER SALVO POR UM MÉDICO PRÓXIMO, CASO CONTRÁRIO VOCÊ IRÁ PARA O HOSPITAL", 11.0f * k, ImColor(235, 240, 250, 240));
		std::string st = called ? "OS MÉDICOS FORAM AVISADOS" : medics > 0 ? std::to_string(medics) + (medics == 1 ? " MÉDICO EM SERVIÇO" : " MÉDICOS EM SERVIÇO") : "NENHUM MÉDICO EM SERVIÇO NO MOMENTO";
		centered(scr.y * 0.82f, st, 9.0f * k, medics > 0 || called ? ImColor(120, 224, 150, 235) : ImColor(150, 170, 215, 235));

		// botao: so funciona com medico em servico e se ainda nao acionou (o servidor confere de novo)
		const bool can = medics > 0 && !called && now >= g_callLock;
		const char* lab = called ? "Médicos acionados" : medics > 0 ? "Acionar os médicos" : "Sem médicos em serviço";
		const float bw = 250.0f * k, bh = 40.0f * k;
		const ImVec2 a(cx - bw * 0.5f, scr.y * 0.87f), b(cx + bw * 0.5f, scr.y * 0.87f + bh);
		{ std::lock_guard<std::mutex> l(g_m); g_bx0 = a.x; g_by0 = a.y; g_bx1 = b.x; g_by1 = b.y; g_btnOn = can; }
		const ImColor bc = can ? (g_down ? ImColor(30, 90, 210, 255) : ImColor(40, 112, 255, 255)) : ImColor(70, 78, 96, 235);
		r->drawRoundedRect(a, b, bc, 5.0f * k, true);
		const float fs = 12.0f * k; const ImVec2 ts = r->calculateTextSize(lab, fs);
		r->drawText(ImVec2(cx - ts.x * 0.5f, a.y + (bh - ts.y) * 0.5f), can ? ImColor(255, 255, 255, 255) : ImColor(170, 178, 196, 255), lab, false, fs);

		if (g_fire.exchange(false) && can)
		{
			g_callLock = now + 2500;       // trava local contra clique repetido ate o servidor responder
			if (pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED) pNetGame->SendChatCommand("/acionarmedicos");
		}
	}
}
