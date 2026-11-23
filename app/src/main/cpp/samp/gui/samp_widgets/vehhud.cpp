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
#include "../vehfonts.h"

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
	constexpr float kHandlingToKmh = 1.0f;   // ajuste fino: handling.cfg (fMaxVelocity) -> km/h do velocimetro

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
	std::atomic<int64_t> g_km10{-1};         // hodometro do carro em decimos de km (garagem); -1 = servidor nao mandou
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

	// ------------------------------------------------------------------ novo velocimetro (layout do banner)
	ImColor Mix(const ImColor& a, const ImColor& b, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return ImColor(a.Value.x + (b.Value.x - a.Value.x) * t, a.Value.y + (b.Value.y - a.Value.y) * t,
		               a.Value.z + (b.Value.z - a.Value.z) * t, a.Value.w + (b.Value.w - a.Value.w) * t);
	}

	// cor do segmento pela posicao (0..1) na barra: cinza 0-20% | verde 21-45% | amarelo 46-70% | laranja 71-90% | vermelho 91-100%, transicoes suaves
	ImColor SegColor(float t)
	{
		struct Stop { float p; float r, g, b; };
		static const Stop st[] = {
			{ 0.00f, 191, 196, 202 }, { 0.14f, 191, 196, 202 },    // #BFC4CA
			{ 0.33f,  54, 209, 124 },                               // #36D17C
			{ 0.58f, 242, 201,  76 },                               // #F2C94C
			{ 0.805f,255, 135,  61 },                               // #FF873D
			{ 0.95f, 255,  61,  70 }, { 1.00f, 255,  61,  70 } };   // #FF3D46
		constexpr int N = (int)(sizeof(st) / sizeof(st[0]));
		t = std::clamp(t, 0.0f, 1.0f);
		for (int i = 1; i < N; i++)
		{
			if (t <= st[i].p)
			{
				const float u = (t - st[i - 1].p) / std::max(0.0001f, st[i].p - st[i - 1].p);
				return ImColor((st[i - 1].r + (st[i].r - st[i - 1].r) * u) / 255.0f, (st[i - 1].g + (st[i].g - st[i - 1].g) * u) / 255.0f,
				               (st[i - 1].b + (st[i].b - st[i - 1].b) * u) / 255.0f, 1.0f);
			}
		}
		return ImColor(255, 61, 70, 255);
	}

	// texto de UMA linha com a fonte do papel, centrado na vertical em cy pela altura do algarismo; shear > 0 inclina para a direita (italico)
	void RoleText(ImGuiRenderer* r, int role, const ImVec2& pos, bool centerX, float cy, const char* txt, float fs, const ImColor& col, float shear)
	{
		ImFont* old = r->swapFont(VehFonts::get(role));
		const ImVec2 ts = r->calculateTextSize(txt, fs);
		const float y0 = cy - (VehFonts::ascent(role) - VehFonts::digitHeight(role) * 0.5f) * fs;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		const int v0 = dl->VtxBuffer.Size;
		r->drawText(ImVec2(centerX ? pos.x - ts.x * 0.5f : pos.x, y0), col, txt, false, fs);
		if (shear != 0.0f)
			for (int i = v0; i < dl->VtxBuffer.Size; i++) dl->VtxBuffer[i].pos.x += (cy - dl->VtxBuffer[i].pos.y) * shear;
		r->swapFont(old);
	}

	float RoleTextW(ImGuiRenderer* r, int role, const char* txt, float fs)
	{
		ImFont* old = r->swapFont(VehFonts::get(role));
		const float w = r->calculateTextSize(txt, fs).x;
		r->swapFont(old);
		return w;
	}

	// bomba de combustivel (vetor; desenhada numa grade de 280x280 e escalada para s px). hole = cor do furo (fundo)
	void FuelIcon(ImGuiRenderer* r, const ImVec2& tl, float s, const ImColor& col, const ImColor& hole)
	{
		const float sc = s / 280.0f;
		auto P = [&](float X, float Y) { return ImVec2(tl.x + (X - 120.0f) * sc, tl.y + (Y - 70.0f) * sc); };
		ImVec2 body[6] = { P(205, 150), P(250, 128), P(285, 185), P(255, 345), P(125, 320), P(170, 185) };
		r->drawConvexPolyFilled(body, 6, col);
		ImVec2 slot[4] = { P(190, 245), P(232, 228), P(236, 298), P(205, 310) };
		r->drawConvexPolyFilled(slot, 4, hole);
		const ImVec2 sp[4] = { P(270, 150), P(282, 120), P(320, 100), P(385, 80) };       // bico
		r->drawPolyline(sp, 4, col, false, 26.0f * sc);
		Disc(r, P(372, 196), 21.0f * sc, col);                                              // gota
		r->drawTriangle(P(372, 150), P(352, 192), P(392, 192), col, true);
	}

	float g_dispSpd = 0.0f, g_dispFuel = 0.0f;      // valores suavizados (so o draw mexe): evitam piscar/saltos
	const void* g_dispVeh = nullptr;

	// ------------------------------------------------------------------ veiculo local
	struct Veh { bool ok = false; float kmh = 0.0f; bool reverse = false; float vmax = 200.0f; const void* id = nullptr; };

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
		// velocidade de referencia do indicador = a maxima do handling DESTE veiculo (so vale numa faixa sensata; senao 200)
		v.id = gv;
		if (gv->m_pHandlingData)
		{
			const float h = gv->m_pHandlingData->m_transmissionData.m_fMaxGearVelocity * kHandlingToKmh;
			if (h >= 60.0f && h <= 400.0f) v.vmax = h;
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
			uint32_t km10 = 0;                                                  // extensao opcional no FIM do pacote (servidor antigo nao manda)
			g_km10.store(bs.Read(km10) ? (int64_t)km10 : -1);
			g_stateAt.store(NowMs());
		}
	}

	void Reset()
	{
		{ std::lock_guard<std::mutex> l(g_mx); g_pending.clear(); }
		g_active.clear();
		g_stateAt.store(0);
		g_km10.store(-1);
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
	if (!v.ok) { g_dispVeh = nullptr; return; }

	const float k = scr.y / 720.0f;
	const int64_t now = NowMs();
	const bool haveState = g_stateAt.load() != 0 && now - g_stateAt.load() < kStateFresh;
	const int flags = haveState ? g_flags.load() : 0;
	const float fuelPct = std::clamp((float)g_fuel.load(), 0.0f, 100.0f);          // fuelPercent = clamp(atual / max * 100, 0, 100) (o servidor ja manda 0..100)
	const int64_t km10 = g_km10.load();

	// suavizacao (sem piscar): ao trocar de veiculo salta direto para o valor dele
	const float spdTarget = std::clamp(v.kmh / std::max(60.0f, v.vmax), 0.0f, 1.0f);
	if (g_dispVeh != v.id) { g_dispVeh = v.id; g_dispSpd = spdTarget; g_dispFuel = fuelPct; }
	{
		const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
		g_dispSpd  += (spdTarget - g_dispSpd)  * (1.0f - std::exp(-dt * 10.0f));
		g_dispFuel += (fuelPct   - g_dispFuel) * (1.0f - std::exp(-dt * 6.0f));
	}

	// painel (layout do banner): 300 x 84, a esquerda dos pedais nativos
	const float W = 300.0f * k, H = 84.0f * k;
	const float px = scr.x - 300.0f * k - W, py = scr.y - H - 26.0f * k;
	r->drawRoundedRect(ImVec2(px, py), ImVec2(px + W, py + H), ImColor(16, 16, 20, 205), 9.0f * k, true);

	const ImColor kGrey(106, 106, 112, 255), kWhite(255, 255, 255, 255), kBox(6, 6, 9, 190);

	// velocidade "000": Barlow Condensed Black, zeros a esquerda apagados, inclinada como no banner
	{
		const int spd = std::clamp((int)(v.kmh + 0.5f), 0, 999);
		const char dg[3] = { (char)('0' + spd / 100), (char)('0' + (spd / 10) % 10), (char)('0' + spd % 10) };
		const int first = spd >= 100 ? 0 : (spd >= 10 ? 1 : 2);
		const float fs = 74.0f * k;
		const float cell = RoleTextW(r, VehFonts::Speed, "0", fs) * 1.10f;      // celula fixa (algarismos nao saltam) com folga para a inclinacao
		const float x0 = px + 20.0f * k;
		for (int i = 0; i < 3; i++)
		{
			const char t[2] = { dg[i], 0 };
			RoleText(r, VehFonts::Speed, ImVec2(x0 + cell * ((float)i + 0.5f), 0.0f), true, py + H * 0.5f, t, fs, i < first ? kGrey : kWhite, 0.2f);
		}
	}

	const float rx0 = px + 128.0f * k, rx1 = px + W - 14.0f * k;

	// linha 1: "KM/H" (Barlow Condensed Bold Italic) + hodometro do carro em caixinhas (Rajdhani Bold), vindo da garagem
	{
		const float cy = py + 21.0f * k;
		RoleText(r, VehFonts::Unit, ImVec2(rx0, 0.0f), false, cy, "KM/H", 19.0f * k, ImColor(200, 203, 208, 255), 0.0f);
		if (km10 >= 0)
		{
			const int km = (int)std::min<int64_t>(km10 / 10, 999999);
			char od[16]; snprintf(od, sizeof(od), "%06d", km);
			int first = 5; for (int i = 0; i < 6; i++) if (od[i] != '0') { first = i; break; }
			const float bw = 17.0f * k, bh = 22.0f * k, gap = 3.4f * k;
			const float bx0 = rx1 - (6.0f * bw + 5.0f * gap);
			for (int i = 0; i < 6; i++)
			{
				const float bx = bx0 + (float)i * (bw + gap);
				r->drawRoundedRect(ImVec2(bx, cy - bh * 0.5f), ImVec2(bx + bw, cy + bh * 0.5f), kBox, 3.2f * k, true);
				const char t[2] = { od[i], 0 };
				RoleText(r, VehFonts::Aux, ImVec2(bx + bw * 0.5f, 0.0f), true, cy, t, 25.0f * k, i < first ? kGrey : kWhite, 0.0f);
			}
		}
	}

	// linha 2: 30 segmentos acendendo com a velocidade (cinza -> verde -> amarelo -> laranja -> vermelho), proporcional ao limite do veiculo
	{
		const float cy = py + 46.0f * k, th = 14.0f * k;
		r->drawRoundedRect(ImVec2(rx0, cy - th * 0.5f), ImVec2(rx1, cy + th * 0.5f), kBox, 4.0f * k, true);
		constexpr int N = 30;
		const float pad = 3.0f * k, gap = 1.7f * k, sh = 8.0f * k;
		const float sw = ((rx1 - rx0) - 2.0f * pad - gap * (float)(N - 1)) / (float)N;
		const float level = g_dispSpd * (float)N;
		const ImColor off(74, 74, 79, 255);
		for (int i = 0; i < N; i++)
		{
			const float a = std::clamp(level - (float)i, 0.0f, 1.0f);
			const ImColor col = Mix(off, SegColor((float)i / (float)(N - 1)), a);
			const float sx = rx0 + pad + (float)i * (sw + gap);
			r->drawRoundedRect(ImVec2(sx, cy - sh * 0.5f), ImVec2(sx + sw, cy + sh * 0.5f), col, 1.1f * k, true);
		}
	}

	// linha 3: bomba de combustivel + barra com o nivel REAL (g_fuel = o mesmo valor que o servidor ja alimenta pelo consumo do veiculo.pwn)
	{
		const float cy = py + 67.0f * k;
		const bool low = fuelPct <= 15.0f;
		FuelIcon(r, ImVec2(rx0 - 2.0f * k, cy - 10.0f * k), 20.0f * k, low ? ImColor(255, 78, 78, 255) : ImColor(238, 238, 242, 255), ImColor(16, 16, 20, 255));
		const float bx0 = rx0 + 30.0f * k, bh = 7.0f * k;
		r->drawRoundedRect(ImVec2(bx0, cy - bh * 0.5f), ImVec2(rx1, cy + bh * 0.5f), kBox, bh * 0.5f, true);
		const float full = rx1 - bx0;
		float fw = full * (std::clamp(g_dispFuel, 0.0f, 100.0f) / 100.0f);       // fuelBarWidth = total * (percent / 100)
		if (g_dispFuel > 0.4f) fw = std::max(fw, bh);                            // minimo visivel (ponta arredondada inteira)
		if (fw > 0.5f)
			r->drawRoundedRect(ImVec2(bx0, cy - bh * 0.5f), ImVec2(bx0 + std::min(fw, full), cy + bh * 0.5f),
			                   low ? ImColor(255, 78, 78, 255) : ImColor(240, 128, 60, 255), bh * 0.5f, true);
	}

	// botoes de motor e trava (so se o servidor mandou o estado do veiculo), acima do painel
	if (haveState)
	{
		const float br = 21.0f * k;
		m_btnR = br;
		const float bcx = px + W * 0.5f;
		m_btnC[0] = ImVec2(bcx - 30.0f * k, py - 26.0f * k);
		m_btnC[1] = ImVec2(bcx + 30.0f * k, py - 26.0f * k);
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
