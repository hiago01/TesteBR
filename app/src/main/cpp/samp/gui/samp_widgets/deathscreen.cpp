#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "deathscreen.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Tela de morte (ver deathscreen.h). Medidas em "unidades de referencia" (1280 x 720) multiplicadas por u.

namespace
{
	std::atomic<bool>    g_en{false};        // servidor ligou o sistema (op 52)
	std::atomic<bool>    g_on{false};
	std::atomic<int64_t> g_pend{0};          // instante (ms) da morte local ainda sem resposta do servidor (0 = nada pendente)
	std::atomic<int64_t> g_t0{0};            // instante (ms) em que chegou o ultimo pacote
	std::atomic<int>     g_secs{0};          // segundos restantes naquele instante
	std::atomic<int>     g_medics{0};
	std::atomic<bool>    g_called{false};
	std::atomic<int64_t> g_shownAt{0};

	int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	int Remaining() { return std::max(0, g_secs.load() - (int)((NowMs() - g_t0.load()) / 1000)); }

	int     g_finger = -1;
	bool    g_down = false;
	ImVec2  g_btnA, g_btnB;
	bool Inside(float x, float y) { return x >= g_btnA.x && x <= g_btnB.x && y >= g_btnA.y && y <= g_btnB.y; }
}

bool DeathScreen::OnState(RakNet::BitStream& bs)
{
	uint8_t mode = 0, med = 0, called = 0; uint16_t secs = 0;
	if (!bs.Read(mode) || !bs.Read(secs) || !bs.Read(med) || !bs.Read(called)) return false;
	if (mode != 1 && mode != 2) return false;
	g_secs.store((int)std::min<uint16_t>(secs, 900));
	g_t0.store(NowMs());
	g_medics.store(med);
	g_called.store(called != 0);
	if (mode == 1 || !g_on.load()) g_shownAt.store(NowMs());
	g_pend.store(0);
	g_en.store(true);
	g_on.store(true);
	return true;
}

void DeathScreen::SetEnabled(bool on) { g_en.store(on); if (!on) { g_pend.store(0); g_on.store(false); } }

void DeathScreen::Hide()
{
	g_on.store(false); g_pend.store(0); g_called.store(false); g_finger = -1; g_down = false;
}

void DeathScreen::Reset() { Hide(); g_en.store(false); }

void DeathScreen::OnLocalDeath()
{
	if (g_en.load() && !g_on.load()) g_pend.store(NowMs());
}

bool DeathScreen::Active() { return g_on.load(); }

bool DeathScreen::Holding()
{
	if (g_on.load())
	{
		if (NowMs() - g_t0.load() > ((int64_t)g_secs.load() + 25) * 1000) { g_on.store(false); return false; }   // trava de seguranca
		return true;
	}
	const int64_t p = g_pend.load();
	if (p != 0)
	{
		if (NowMs() - p < 4000) return true;       // morreu e o servidor ainda nao respondeu: segura o respawn
		g_pend.store(0);                           // o servidor nao reconheceu a morte: segue o fluxo normal
	}
	return false;
}

void DeathScreen::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer)
{
	answer = 0;
	if (!g_on.load()) return;
	const float u = std::min(scrW / 1280.0f, scrH / 720.0f);
	const int64_t now = NowMs();
	const float cx = scrW * 0.5f;
	const float fade = std::min(1.0f, (float)(now - g_shownAt.load()) / 900.0f);        // entra suave
	auto A = [&](float a) { return (int)std::lround(a * fade); };

	// fundo: escurece e tinge de azul (vinheta mais forte nas bordas)
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), ImColor(4, 12, 30, A(165)), true);
	r->drawRectFilledMulticolor(ImVec2(0, 0), ImVec2(scrW, scrH * 0.35f), ImColor(0, 0, 0, A(150)), ImColor(0, 0, 0, A(150)), ImColor(0, 0, 0, 0), ImColor(0, 0, 0, 0));
	r->drawRectFilledMulticolor(ImVec2(0, scrH * 0.65f), ImVec2(scrW, scrH), ImColor(0, 0, 0, 0), ImColor(0, 0, 0, 0), ImColor(0, 0, 0, A(150)), ImColor(0, 0, 0, A(150)));

	// coracao azul com pulso (curva parametrica) e linha de batimento
	const float beat = 1.0f + 0.07f * std::max(0.0f, std::sin((float)(now % 1100) * 6.2831853f / 1100.0f * 2.0f));
	const float hy = scrH * 0.285f, hs = 2.15f * u * beat;
	{
		constexpr int N = 48; ImVec2 p[N];
		for (int i = 0; i < N; i++)
		{
			const float t = (float)i * 6.2831853f / (float)N;
			const float x = 16.0f * std::pow(std::sin(t), 3.0f);
			const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) - std::cos(4 * t);
			p[i] = ImVec2(cx + x * hs, hy - y * hs);
		}
		for (int g = 3; g >= 1; g--)   // brilho
		{
			ImVec2 q[N]; for (int i = 0; i < N; i++) q[i] = ImVec2(cx + (p[i].x - cx) * (1.0f + 0.12f * g), hy + (p[i].y - hy) * (1.0f + 0.12f * g));
			r->drawConvexPolyFilled(q, N, ImColor(37, 99, 219, A(26)));
		}
		r->drawConvexPolyFilled(p, N, ImColor(37, 120, 240, A(235)));
		const float w = 15.0f * hs;
		const ImVec2 e[9] = { ImVec2(cx - w, hy - 1.0f * hs), ImVec2(cx - w * 0.45f, hy - 1.0f * hs), ImVec2(cx - w * 0.30f, hy - 5.5f * hs), ImVec2(cx - w * 0.10f, hy + 5.5f * hs),
		                      ImVec2(cx + w * 0.05f, hy - 3.0f * hs), ImVec2(cx + w * 0.18f, hy - 1.0f * hs), ImVec2(cx + w * 0.42f, hy - 1.0f * hs), ImVec2(cx + w * 0.55f, hy - 1.0f * hs), ImVec2(cx + w, hy - 1.0f * hs) };
		r->drawPolyline(e, 9, ImColor(255, 255, 255, A(235)), false, std::max(2.0f, 1.6f * u));
	}

	auto centered = [&](const std::string& s, float y, float fs, const ImColor& c) {
		const ImVec2 ts = r->calculateTextSize(s, fs * u);
		r->drawText(ImVec2(cx - ts.x * 0.5f, y), c, s, false, fs * u);
	};
	const int rem = Remaining();
	centered("VOCÊ ESTÁ FERIDO E VAI PERDER A CONSCIÊNCIA EM", scrH * 0.43f, 17.0f, ImColor(240, 244, 255, A(245)));
	{
		char b[16]; std::snprintf(b, sizeof b, "%d", rem);
		const float fs = 54.0f;
		const ImVec2 ts = r->calculateTextSize(b, fs * u);
		r->drawText(ImVec2(cx - ts.x * 0.5f, scrH * 0.475f), ImColor(48, 132, 255, A(255)), b, false, fs * u);
	}
	centered(rem == 1 ? "segundo" : "segundos", scrH * 0.595f, 13.0f, ImColor(210, 220, 240, A(220)));
	centered("VOCÊ SÓ PODE SER SALVO POR UM MÉDICO PRÓXIMO, CASO CONTRÁRIO VOCÊ IRÁ PARA O HOSPITAL", scrH * 0.665f, 13.5f, ImColor(235, 238, 248, A(235)));
	{
		const int med = g_medics.load();
		std::string s = g_called.load() ? "OS MÉDICOS FORAM AVISADOS E ESTÃO A CAMINHO" : (med > 0 ? "MÉDICOS EM SERVIÇO: " + std::to_string(med) : "NENHUM MÉDICO EM SERVIÇO NO MOMENTO");
		centered(s, scrH * 0.705f, 12.0f, ImColor(170, 190, 225, A(215)));
	}

	// botao "Acionar os medicos"
	const bool called = g_called.load();
	const float bw = 250.0f * u, bh = 46.0f * u, by = scrH * 0.765f;
	g_btnA = ImVec2(cx - bw * 0.5f, by); g_btnB = ImVec2(cx + bw * 0.5f, by + bh);
	{
		const ImColor fill = called ? ImColor(60, 70, 92, A(230)) : (g_down ? ImColor(28, 78, 190, A(255)) : ImColor(37, 110, 245, A(255)));
		r->drawRoundedRect(g_btnA, g_btnB, fill, 10.0f * u, true);
		const std::string t = called ? "Médicos acionados" : "Acionar os médicos";
		const float fs = 15.0f * u; const ImVec2 ts = r->calculateTextSize(t, fs);
		r->drawText(ImVec2(cx - ts.x * 0.5f, by + (bh - ts.y) * 0.5f), ImColor(255, 255, 255, A(called ? 170 : 255)), t, false, fs);
	}

	for (const ShopTouch& e : events)
	{
		if (fade < 0.6f) continue;
		if (e.type == 2 && g_finger < 0) { g_finger = e.num; g_down = !called && Inside(e.x, e.y); }
		else if (e.num == g_finger && e.type == 3) { if (g_down && !Inside(e.x, e.y)) g_down = false; }
		else if (e.num == g_finger && e.type == 1)
		{
			const bool was = g_down; g_finger = -1; g_down = false;
			if (was && !e.cancel && Inside(e.x, e.y) && !g_called.load()) { answer = 1; g_called.store(true); }   // marca na hora (o servidor confirma com o op 50)
		}
	}
}
