#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../game/Entity/CPedGTA.h"
#include "../../game/Entity/CVehicleGTA.h"
#include "../../game/ScooterRider.h"
#include "../../vendor/raknet/BitStream.h"
#include "vehhud.h"
#include "hudstate.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;

namespace
{
	constexpr float kPi = 3.14159265f;

	inline int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ------------------------------------------------------------------ dados vindos do servidor
	struct Note { int type; std::string title, text; int ms; int64_t at; };
	std::mutex g_mx;
	std::vector<Note> g_pending;             // escrito na thread de rede, consumido no draw
	std::vector<Note> g_active;              // so o draw mexe
	std::atomic<int>  g_fuel{100};
	std::atomic<int>  g_flags{0};
	std::atomic<int64_t> g_stateAt{0};       // 0 = o servidor nunca mandou o estado do veiculo (botoes escondidos)
	constexpr int64_t kStateFresh = 6000;    // sem atualizacao por 6 s = servidor sem veiculo.pwn / saiu do carro

	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], (unsigned)n)) return false;
		return true;
	}

	// ------------------------------------------------------------------ desenho
	void Arc(ImGuiRenderer* r, const ImVec2& c, float rad, float a0, float a1, const ImColor& col, float thick)
	{
		if (a1 - a0 < 0.01f) return;
		constexpr int MAXP = 80;
		ImVec2 p[MAXP];
		const int n = std::clamp((int)((a1 - a0) * 18.0f) + 2, 3, MAXP);
		for (int i = 0; i < n; i++)
		{
			const float a = a0 + (a1 - a0) * (float)i / (float)(n - 1);
			p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
		}
		r->drawPolyline(p, n, col, false, thick);
	}

	void Disc(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col)
	{
		constexpr int N = 36;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = 2.0f * kPi * (float)i / (float)N; p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad); }
		r->drawConvexPolyFilled(p, N, col);
	}

	void Hex(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col)
	{
		ImVec2 p[6];
		for (int i = 0; i < 6; i++) { const float a = kPi / 6.0f + kPi * (float)i / 3.0f; p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad); }
		r->drawConvexPolyFilled(p, 6, col);
	}

	void TextC(ImGuiRenderer* r, const ImVec2& c, const ImColor& col, const std::string& s, float fs)
	{
		const ImVec2 ts = r->calculateTextSize(s, fs);
		r->drawText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, s, false, fs);
	}

	// quebra em ate 2 linhas pela largura maxima
	std::vector<std::string> Wrap(ImGuiRenderer* r, const std::string& s, float fs, float maxW)
	{
		std::vector<std::string> out;
		std::string cur;
		size_t i = 0;
		while (i < s.size())
		{
			size_t j = s.find(' ', i);
			if (j == std::string::npos) j = s.size();
			const std::string w = s.substr(i, j - i);
			const std::string t = cur.empty() ? w : cur + " " + w;
			if (!cur.empty() && r->calculateTextSize(t, fs).x > maxW) { out.push_back(cur); cur = w; }
			else cur = t;
			i = j + 1;
		}
		if (!cur.empty()) out.push_back(cur);
		if (out.size() > 2) { out.resize(2); out[1] += "..."; }
		return out;
	}

	ImColor Accent(int type)
	{
		if (type == 0) return ImColor(52, 199, 89, 255);     // sucesso
		if (type == 1) return ImColor(240, 62, 110, 255);    // erro
		return ImColor(247, 184, 28, 255);                    // aviso
	}

	void DrawIcon(ImGuiRenderer* r, const ImVec2& c, float k, int type)
	{
		const ImColor ac = Accent(type);
		Hex(r, c, 15.0f * k, ImColor(ac.Value.x, ac.Value.y, ac.Value.z, 0.28f));
		Hex(r, c, 11.0f * k, ac);
		const ImColor w(255, 255, 255, 255);
		const float t = 1.8f * k;
		if (type == 0)
		{
			const ImVec2 p[3] = { ImVec2(c.x - 5.0f * k, c.y), ImVec2(c.x - 1.5f * k, c.y + 4.0f * k), ImVec2(c.x + 5.5f * k, c.y - 4.0f * k) };
			r->drawPolyline(p, 3, w, false, t);
		}
		else if (type == 1)
		{
			r->drawLine(ImVec2(c.x - 4.5f * k, c.y - 4.5f * k), ImVec2(c.x + 4.5f * k, c.y + 4.5f * k), w, t);
			r->drawLine(ImVec2(c.x + 4.5f * k, c.y - 4.5f * k), ImVec2(c.x - 4.5f * k, c.y + 4.5f * k), w, t);
		}
		else
		{
			r->drawLine(ImVec2(c.x, c.y - 5.0f * k), ImVec2(c.x, c.y + 1.5f * k), w, t);
			Disc(r, ImVec2(c.x, c.y + 5.0f * k), 1.3f * k, w);
		}
	}

	void DrawNotes(ImGuiRenderer* r, const ImVec2& scr)
	{
		const int64_t now = NowMs();
		{
			std::lock_guard<std::mutex> l(g_mx);
			for (Note& n : g_pending) { n.at = now; g_active.push_back(n); }
			g_pending.clear();
		}
		while (g_active.size() > 4) g_active.erase(g_active.begin());
		g_active.erase(std::remove_if(g_active.begin(), g_active.end(), [now](const Note& n) { return now - n.at >= n.ms; }), g_active.end());
		if (g_active.empty()) return;

		const float k = scr.y / 720.0f;
		const float W = 236.0f * k;
		const float x1 = scr.x - 16.0f * k;
		float y = 132.0f * k;                      // abaixo da HUD (circulos + pilulas)
		const float fsT = 10.5f * k, fsB = 9.0f * k;

		for (const Note& n : g_active)
		{
			const int64_t age = now - n.at;
			const float slide = age < 260 ? 1.0f - (float)age / 260.0f : 0.0f;       // entra deslizando da direita
			const float fade = (n.ms - age) < 260 ? (float)(n.ms - age) / 260.0f : 1.0f;
			const int A = (int)(255.0f * std::clamp(fade, 0.0f, 1.0f));
			const float ox = slide * (W + 24.0f * k);

			const std::vector<std::string> lines = Wrap(r, n.text, fsB, W - 62.0f * k);
			const float H = 38.0f * k + (float)lines.size() * (fsB + 3.0f * k) + 6.0f * k;
			const ImVec2 a(x1 - W + ox, y), b(x1 + ox, y + H);

			r->drawRoundedRect(a, b, ImColor(22, 22, 26, A * 215 / 255), 8.0f * k, true);
			DrawIcon(r, ImVec2(a.x + 27.0f * k, y + 27.0f * k), k, n.type);
			r->drawText(ImVec2(a.x + 52.0f * k, y + 11.0f * k), ImColor(255, 255, 255, A), n.title, false, fsT);
			float ty = y + 11.0f * k + fsT + 4.0f * k;
			for (const std::string& ln : lines) { r->drawText(ImVec2(a.x + 52.0f * k, ty), ImColor(200, 200, 206, A), ln, false, fsB); ty += fsB + 3.0f * k; }

			// barra de tempo
			const float frac = std::clamp(1.0f - (float)age / (float)n.ms, 0.0f, 1.0f);
			const ImColor ac = Accent(n.type);
			const float by = b.y + 5.0f * k;
			r->drawRoundedRect(ImVec2(a.x + 6.0f * k, by), ImVec2(b.x - 6.0f * k, by + 2.5f * k), ImColor(70, 70, 76, A * 150 / 255), 1.2f * k, true);
			r->drawRoundedRect(ImVec2(a.x + 6.0f * k, by), ImVec2(a.x + 6.0f * k + (W - 12.0f * k) * frac, by + 2.5f * k), ImColor(ac.Value.x, ac.Value.y, ac.Value.z, (float)A / 255.0f), 1.2f * k, true);
			y = by + 2.5f * k + 8.0f * k;
		}
	}

	// ------------------------------------------------------------------ veiculo local
	struct Veh { bool ok = false; float kmh = 0.0f; bool reverse = false; };

	Veh LocalVeh()
	{
		Veh v;
		if (!pNetGame || !pNetGame->GetPlayerPool()) return v;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp) return v;
		CPlayerPed* pp = lp->GetPlayerPed();
		if (!pp || !pp->m_pPed || !pp->IsInVehicle() || pp->IsAPassenger()) return v;
		CVehicleGTA* gv = pp->m_pPed->pVehicle;
		if (!gv) return v;
		if (ScooterRider::LocalRiding()) return v;                 // patinete tem HUD proprio
		const float sx = gv->m_vecMoveSpeed.x, sy = gv->m_vecMoveSpeed.y, sz = gv->m_vecMoveSpeed.z;
		v.kmh = std::sqrt(sx * sx + sy * sy + sz * sz) * 180.0f;    // 1.0 do jogo ~ 180 km/h
		if (gv->m_matrix)
		{
			const CVector f = gv->m_matrix->GetForward();
			v.reverse = (sx * f.x + sy * f.y) < -0.008f;
		}
		v.ok = true;
		return v;
	}

	bool UiBlocked()
	{
		if (!pUI) return false;
		return (pUI->keyboard()      && pUI->keyboard()->visible())      ||
		       (pUI->dialog()        && pUI->dialog()->visible())        ||
		       (pUI->playertablist() && pUI->playertablist()->visible()) ||
		       (pUI->spawn()         && pUI->spawn()->visible())         ||
		       (pUI->splashscreen()  && pUI->splashscreen()->visible());
	}

	void DrawPower(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col, float t)
	{
		Arc(r, c, rad, -kPi * 0.5f + 0.7f, -kPi * 0.5f + 2.0f * kPi - 0.7f, col, t);
		r->drawLine(ImVec2(c.x, c.y - rad * 1.25f), ImVec2(c.x, c.y - rad * 0.15f), col, t);
	}

	void DrawLock(ImGuiRenderer* r, const ImVec2& c, float s, const ImColor& col, float t, bool locked)
	{
		r->drawRoundedRect(ImVec2(c.x - s, c.y - s * 0.15f), ImVec2(c.x + s, c.y + s * 1.05f), col, s * 0.2f, true);
		const float sh = s * 0.62f;
		if (locked) Arc(r, ImVec2(c.x, c.y - s * 0.15f), sh, kPi, 2.0f * kPi, col, t);
		else        Arc(r, ImVec2(c.x + sh * 1.1f, c.y - s * 0.15f), sh, kPi, 1.75f * kPi, col, t);
	}
}

namespace VehHud
{
	void OnServerPacket(const unsigned char* data, int bytes)
	{
		if (!data || bytes <= 0 || bytes > 1024) return;
		RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
		uint8_t op = 0;
		if (!bs.Read(op)) return;
		if (op == 1)
		{
			uint8_t type = 1; uint16_t ms = 4000; std::string title, text;
			if (!bs.Read(type) || !RdStr(bs, title) || !RdStr(bs, text) || !bs.Read(ms)) return;
			if (text.empty() && title.empty()) return;
			Note n{ std::min<int>(type, 2), title, text, std::clamp<int>(ms, 1500, 15000), 0 };
			std::lock_guard<std::mutex> l(g_mx);
			if (g_pending.size() < 8) g_pending.push_back(n);
		}
		else if (op == 2)
		{
			uint8_t fuel = 100, flags = 0;
			if (!bs.Read(fuel) || !bs.Read(flags)) return;
			g_fuel.store(std::min<int>(fuel, 100));
			g_flags.store(flags);
			g_stateAt.store(NowMs());
		}
	}

	void Reset()
	{
		{ std::lock_guard<std::mutex> l(g_mx); g_pending.clear(); }
		g_active.clear();
		g_stateAt.store(0);
	}
}

VehHudWidget::VehHudWidget()
{
	m_btnC[0] = m_btnC[1] = ImVec2(0.0f, 0.0f);
}

void VehHudWidget::draw(ImGuiRenderer* r)
{
	m_btnR = 0.0f;
	const ImVec2 scr = ImGui::GetIO().DisplaySize;
	if (pUI && pUI->splashscreen() && pUI->splashscreen()->visible()) return;
	DrawNotes(r, scr);

	if (!Hud::g.visible.load() || UiBlocked()) return;
	const Veh v = LocalVeh();
	if (!v.ok) return;

	const float k = scr.y / 720.0f;
	const float R = 58.0f * k;
	const ImVec2 c(scr.x * 0.742f, scr.y - R - 30.0f * k);        // a esquerda dos pedais nativos, em cima do minimapa nao
	const int64_t now = NowMs();
	const bool haveState = g_stateAt.load() != 0 && now - g_stateAt.load() < kStateFresh;
	const int flags = haveState ? g_flags.load() : 0;
	const int fuel = g_fuel.load();

	// fundo e arco de velocidade (270 graus, de baixo-esquerda a baixo-direita)
	Disc(r, c, R + 6.0f * k, ImColor(14, 14, 18, 150));
	const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
	Arc(r, c, R, a0, a1, ImColor(70, 70, 78, 200), 4.0f * k);
	const float frac = std::clamp(v.kmh / 280.0f, 0.0f, 1.0f);
	const ImColor spd = frac > 0.82f ? ImColor(232, 64, 64, 255) : ImColor(238, 238, 242, 255);
	Arc(r, c, R, a0, a0 + (a1 - a0) * frac, spd, 4.0f * k);

	// marcha (caixinha no topo), velocidade e unidade
	char g[4];
	if (v.kmh < 1.5f) snprintf(g, sizeof(g), "N");
	else if (v.reverse) snprintf(g, sizeof(g), "R");
	else snprintf(g, sizeof(g), "%d", std::clamp(1 + (int)(v.kmh / 42.0f), 1, 6));
	r->drawRoundedRect(ImVec2(c.x - 8.0f * k, c.y - R * 0.74f), ImVec2(c.x + 8.0f * k, c.y - R * 0.74f + 15.0f * k), ImColor(255, 255, 255, 40), 3.0f * k, true);
	TextC(r, ImVec2(c.x, c.y - R * 0.74f + 7.5f * k), ImColor(255, 255, 255, 255), g, 10.0f * k);
	char sp[8]; snprintf(sp, sizeof(sp), "%03d", (int)(v.kmh + 0.5f));
	TextC(r, ImVec2(c.x, c.y - 2.0f * k), ImColor(255, 255, 255, 255), sp, 23.0f * k);
	TextC(r, ImVec2(c.x, c.y + 14.0f * k), ImColor(190, 190, 198, 255), "KM/H", 7.5f * k);

	// combustivel: anel pequeno + porcentagem
	{
		const ImVec2 fc(c.x, c.y + R * 0.52f);
		Arc(r, fc, 8.5f * k, -kPi * 0.5f, 1.5f * kPi, ImColor(70, 70, 78, 220), 2.2f * k);
		const ImColor fcol = fuel <= 15 ? ImColor(232, 64, 64, 255) : (fuel <= 35 ? ImColor(247, 184, 28, 255) : ImColor(52, 199, 89, 255));
		Arc(r, fc, 8.5f * k, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * (float)fuel / 100.0f, fcol, 2.2f * k);
		char fs[8]; snprintf(fs, sizeof(fs), "%d%%", fuel);
		TextC(r, ImVec2(fc.x, fc.y + 15.0f * k), ImColor(230, 230, 236, 255), fs, 6.5f * k);
	}

	// indicadores de motor e trava no pe do arco
	{
		const ImVec2 ec(c.x - R * 0.64f, c.y + R * 0.72f), lc(c.x + R * 0.64f, c.y + R * 0.72f);
		DrawPower(r, ec, 4.2f * k, (flags & 1) ? ImColor(52, 199, 89, 255) : ImColor(120, 120, 128, 255), 1.4f * k);
		DrawLock(r, lc, 4.0f * k, (flags & 2) ? ImColor(240, 62, 110, 255) : ImColor(120, 120, 128, 255), 1.3f * k, (flags & 2) != 0);
	}

	// botoes de motor e trava (so se o servidor mandou o estado do veiculo)
	if (haveState)
	{
		const float br = 21.0f * k;
		m_btnR = br;
		m_btnC[0] = ImVec2(c.x - 30.0f * k, c.y - R - 22.0f * k);
		m_btnC[1] = ImVec2(c.x + 30.0f * k, c.y - R - 22.0f * k);
		for (int i = 0; i < 2; i++)
		{
			const bool on = (i == 0) ? (flags & 1) != 0 : (flags & 2) != 0;
			const ImColor ac = (i == 0) ? ImColor(52, 199, 89, 255) : ImColor(240, 62, 110, 255);
			Disc(r, m_btnC[i], br, ImColor(20, 20, 24, m_down == i ? 235 : 190));
			Arc(r, m_btnC[i], br, 0.0f, 2.0f * kPi, on ? ac : ImColor(110, 110, 118, 255), 2.0f * k);
			if (i == 0) DrawPower(r, m_btnC[i], 7.5f * k, on ? ac : ImColor(235, 235, 240, 255), 2.0f * k);
			else        DrawLock(r, m_btnC[i], 7.0f * k, on ? ac : ImColor(235, 235, 240, 255), 2.0f * k, on);
		}
	}
}

void VehHudWidget::touchEvent(const ImVec2& pos, TouchType type)
{
	if (m_btnR <= 0.0f) { m_down = -1; return; }
	auto hit = [&](int i) { const float dx = pos.x - m_btnC[i].x, dy = pos.y - m_btnC[i].y; return dx * dx + dy * dy <= m_btnR * m_btnR * 1.44f; };
	if (type == TouchType::push)
	{
		m_down = hit(0) ? 0 : (hit(1) ? 1 : -1);
	}
	else if (type == TouchType::pop)
	{
		const int d = m_down; m_down = -1;
		if (d < 0 || !hit(d) || !pNetGame) return;
		const int64_t now = NowMs();
		if (now - m_lastTap < 700) return;                           // anti-repeticao
		m_lastTap = now;
		pNetGame->SendChatCommand(d == 0 ? "/motor" : "/trancar");
	}
}
