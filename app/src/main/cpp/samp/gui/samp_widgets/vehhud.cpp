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
	std::atomic<int>   g_mkOn{0};             // circulo no chao atras do porta-malas aberto (0 = apagado)
	std::atomic<int>   g_mkBau{0};            // 1 = o ponto e o BAU de uma moto (escreve "Acessar baú")
	std::atomic<int>   g_mkIn{0};             // 1 = o jogador esta dentro do circulo
	std::atomic<float> g_mkX{0.f}, g_mkY{0.f}, g_mkZ{0.f};
	std::atomic<int>  g_enterVeh{0};         // veiculo com porta-malas aberto ao alcance (0 = nenhum)
	std::atomic<int>  g_inside{0};           // 0 fora | 1 dentro do porta-malas (aberto) | 2 dentro (fechado: tela escura)
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

	// ---- painel de teclas 2x2 (motor, trava, farol, pisca alerta)
	void KeyIconPower(ImGuiRenderer* r, const ImVec2& c, float s, bool on)
	{
		const ImColor col = on ? ImColor(52, 230, 96, 255) : ImColor(226, 226, 232, 255);
		if (on) { DrawPower(r, c, s, ImColor(52, 230, 96, 60), s * 0.55f); }
		DrawPower(r, c, s, col, s * 0.24f);
	}
	void KeyIconLock(ImGuiRenderer* r, const ImVec2& c, float s, bool locked)
	{
		DrawLock(r, ImVec2(c.x, c.y + s * 0.1f), s, locked ? ImColor(244, 244, 248, 255) : ImColor(150, 150, 158, 255), s * 0.22f, locked);
	}
	void KeyIconBeam(ImGuiRenderer* r, const ImVec2& c, float s, bool on)
	{
		const ImColor col = on ? ImColor(132, 200, 255, 255) : ImColor(226, 226, 232, 255);
		const float t = s * 0.2f;
		ImVec2 p[16]; int n = 0;                                          // "D": haste vertical + meia-lua para a direita
		const ImVec2 o(c.x + s * 0.2f, c.y);
		for (int i = 0; i <= 12; i++) { const float a = -kPi * 0.5f + kPi * (float)i / 12.0f; p[n++] = ImVec2(o.x + std::cos(a) * s * 0.62f * 0.8f, o.y + std::sin(a) * s * 0.62f); }
		p[n++] = ImVec2(o.x, o.y - s * 0.62f);
		if (on) r->drawPolyline(p, n, ImColor(132, 200, 255, 60), true, t * 2.4f);
		r->drawPolyline(p, n, col, true, t);
		for (int i = 0; i < 3; i++)                                       // raios para a esquerda
		{
			const float y = c.y + ((float)i - 1.0f) * s * 0.42f;
			r->drawLine(ImVec2(o.x - s * 0.3f, y + s * 0.06f), ImVec2(o.x - s * 0.95f, y - s * 0.06f), col, t);
		}
	}
	void KeyIconHazard(ImGuiRenderer* r, const ImVec2& c, float s, bool on)
	{
		const ImColor col = on ? ImColor(255, 56, 56, 255) : ImColor(150, 150, 158, 255);
		const float t = s * 0.2f;
		const ImVec2 p[3] = { ImVec2(c.x, c.y - s * 0.78f), ImVec2(c.x + s * 0.92f, c.y + s * 0.62f), ImVec2(c.x - s * 0.92f, c.y + s * 0.62f) };
		if (on) r->drawPolyline(p, 3, ImColor(255, 56, 56, 60), true, t * 2.4f);
		r->drawPolyline(p, 3, col, true, t);
		r->drawLine(ImVec2(c.x, c.y - s * 0.22f), ImVec2(c.x, c.y + s * 0.18f), col, t * 0.9f);
		Disc(r, ImVec2(c.x, c.y + s * 0.4f), t * 0.55f, col);
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
		else if (op == 3)
		{
			uint8_t on = 0; uint16_t veh = 0;
			if (!bs.Read(on) || !bs.Read(veh)) return;
			g_enterVeh.store(on ? (int)veh : 0);
		}
		else if (op == 5)
		{
			uint8_t on = 0, in = 0; float x = 0, y = 0, z = 0;
			if (!bs.Read(on) || !bs.Read(x) || !bs.Read(y) || !bs.Read(z) || !bs.Read(in)) return;
			g_mkX.store(x); g_mkY.store(y); g_mkZ.store(z); g_mkIn.store((in & 1) ? 1 : 0); g_mkBau.store((in & 2) ? 1 : 0); g_mkOn.store(on ? 1 : 0);   // in: bit0 dentro do circulo | bit1 bau de moto
		}
		else if (op == 6)
		{
			uint32_t id = 0;
			if (!bs.Read(id)) return;
			Hud::g.accountId.store((int)(id > 99999999u ? 0u : id));
		}
		else if (op == 4)
		{
			uint8_t st = 0;
			if (!bs.Read(st)) return;
			g_inside.store(std::min<int>(st, 2));
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
		g_enterVeh.store(0);
		g_mkOn.store(0);
		g_inside.store(0);
	}
}

VehHudWidget::VehHudWidget()
{
	for (int i = 0; i < 4; i++) m_key[i] = ImVec4(0, 0, 0, 0);
}

void VehHudWidget::draw(ImGuiRenderer* r)
{
	m_btnR = 0.0f;
	const ImVec2 scr = ImGui::GetIO().DisplaySize;
	if (pUI && pUI->splashscreen() && pUI->splashscreen()->visible()) return;
	DrawNotes(r, scr);

	// ---- circulo no chao atras do porta-malas aberto: entre nele para aparecer o botao "Entrar no porta-malas" ----
	if (g_mkOn.load() && g_inside.load() == 0 && Hud::g.visible.load() && !UiBlocked())
	{
		const float cx = g_mkX.load(), cy = g_mkY.load(), cz = g_mkZ.load();
		const bool in = g_mkIn.load() != 0;
		auto proj = [](float x, float y, float z, ImVec2& o) -> bool
		{
			CVector w(x, y, z), sc;
			((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&w, &sc, 0, 0, 0, 0);   // CSprite::CalcScreenCoors
			if (sc.z < 1.0f) return false;
			o = ImVec2(sc.x, sc.y);
			return true;
		};
		const ImColor fill = in ? ImColor(60, 220, 110, 70) : ImColor(255, 200, 60, 55);
		const ImColor line = in ? ImColor(90, 255, 140, 235) : ImColor(255, 215, 90, 225);
		constexpr int N = 36;
		for (int ring = 0; ring < 2; ring++)
		{
			const float R = ring == 0 ? 1.0f : 0.55f;
			ImVec2 p[N]; bool ok = true;
			for (int i = 0; i < N && ok; i++)
			{
				const float a = (float)i * 6.2831853f / (float)N;
				ok = proj(cx + std::cos(a) * R, cy + std::sin(a) * R, cz + 0.05f, p[i]);
			}
			if (!ok) continue;
			if (ring == 0) r->drawConvexPolyFilled(p, N, fill);
			r->drawPolyline(p, N, line, true, ring == 0 ? 3.0f : 1.6f);
		}
		ImVec2 c;
		if (proj(cx, cy, cz + 0.35f, c))
		{
			const char* t = g_mkBau.load() ? "Acessar baú" : "Porta-malas";
			const float fs = 11.0f * (scr.y / 720.0f);
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), line, t, false, fs);
		}
	}

	// ---- porta-malas: dentro dele = tela escura se fechado + botao Sair; fora, com um aberto ao alcance = botao Entrar ----
	m_xOn = false;
	{
		const float kk = scr.y / 720.0f;
		const int ins = g_inside.load();
		{   // dentro do porta-malas o proprio corpo nao pode aparecer na camera: esconde o ped local (e devolve ao sair)
			static bool s_hid = false;
			CPedGTA* lped = nullptr;
			if (pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer() && pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed())
				lped = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed()->m_pPed;
			if (ins > 0 && lped) { lped->m_bIsVisible = false; s_hid = true; }
			else if (ins == 0 && s_hid) { if (lped) lped->m_bIsVisible = true; s_hid = false; }
		}
		if (ins > 0)
		{
			if (ins == 2)      // fechado: nao enxerga nada
			{
				r->drawRect(ImVec2(0, 0), scr, ImColor(0, 0, 0, 255), true);
				const char* t = "Porta-malas fechado";
				const ImVec2 ts = r->calculateTextSize(t, 13.0f * kk);
				r->drawText(ImVec2((scr.x - ts.x) * 0.5f, scr.y * 0.5f - ts.y * 0.5f), ImColor(150, 150, 158, 255), t, false, 13.0f * kk);
			}
			if (ins == 1) { m_xOn = true; m_xCmd = 1; m_xVeh = 0; }   // "Sair do porta-malas" SO com o porta-malas ABERTO (fechado: tela escura, sem botao)
		}
		else if (g_enterVeh.load() > 0 && Hud::g.visible.load() && !UiBlocked() && !LocalVeh().ok)
		{
			m_xOn = true; m_xCmd = 0; m_xVeh = g_enterVeh.load();
		}
		if (m_xOn)
		{
			const char* lab = m_xCmd == 1 ? "Sair do porta-malas" : "Entrar no porta-malas";
			const float fs = 12.0f * kk, w = 190.0f * kk, h = 38.0f * kk;
			const float x0 = (scr.x - w) * 0.5f, y0 = scr.y - 150.0f * kk;
			m_xb = ImVec4(x0, y0, x0 + w, y0 + h);
			const bool dn = (m_down == 10);
			r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), dn ? ImColor(26, 80, 46, 245) : ImColor(36, 120, 64, 235), 10.0f * kk, true);
			r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), ImColor(255, 255, 255, 60), 10.0f * kk, false, 1.2f * kk);
			const ImVec2 ts = r->calculateTextSize(lab, fs);
			r->drawText(ImVec2(x0 + (w - ts.x) * 0.5f, y0 + (h - ts.y) * 0.5f), ImColor(255, 255, 255, 255), lab, false, fs);
		}
	}

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
	const float px = (scr.x - W) * 0.5f, py = scr.y - H - 22.0f * k;          // centro da tela, na base
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

	// painel de teclas 2x2 ao lado direito do velocimetro (so se o servidor mandou o estado do veiculo): motor | trava / farol | pisca alerta
	if (haveState)
	{
		m_btnR = 1.0f;
		const float bs = 36.0f * k, gp = 5.0f * k, pad = 6.0f * k;
		const float S = pad * 2.0f + bs * 2.0f + gp;
		const float x0 = px + W + 12.0f * k, y0 = py + H - S;
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + S, y0 + S), ImColor(20, 20, 24, 235), 14.0f * k, true);
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + S, y0 + S), ImColor(255, 255, 255, 26), 14.0f * k, false, 1.0f * k);
		const bool st[4] = { (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0 };
		for (int i = 0; i < 4; i++)
		{
			const float bx = x0 + pad + (float)(i & 1) * (bs + gp), by = y0 + pad + (float)(i >> 1) * (bs + gp);
			m_key[i] = ImVec4(bx, by, bx + bs, by + bs);
			const bool dn = (m_down == i);
			r->drawRoundedRect(ImVec2(bx, by), ImVec2(bx + bs, by + bs), dn ? ImColor(26, 26, 30, 255) : ImColor(40, 40, 45, 255), 10.0f * k, true);
			r->drawRoundedRect(ImVec2(bx + 1.0f * k, by + 1.0f * k), ImVec2(bx + bs - 1.0f * k, by + bs * 0.5f), ImColor(255, 255, 255, dn ? 4 : 14), 9.0f * k, true);   // brilho de cima
			const ImVec2 c(bx + bs * 0.5f, by + bs * 0.5f + (dn ? 0.8f * k : 0.0f));
			const float is = 9.5f * k;
			if (i == 0)      KeyIconPower(r, c, is, st[0]);
			else if (i == 1) KeyIconLock(r, c, is * 0.95f, st[1]);
			else if (i == 2) KeyIconBeam(r, c, is, st[2]);
			else             KeyIconHazard(r, c, is, st[3]);
		}
	}
}

void VehHudWidget::touchEvent(const ImVec2& pos, TouchType type)
{
	if (m_xOn)       // botao de tela do porta-malas (tem prioridade)
	{
		const bool in = pos.x >= m_xb.x && pos.x <= m_xb.z && pos.y >= m_xb.y && pos.y <= m_xb.w;
		if (type == TouchType::push && in) { m_down = 10; return; }
		if (type == TouchType::pop && m_down == 10)
		{
			m_down = -1;
			const int64_t now = NowMs();
			if (in && pNetGame && now - m_lastTap >= 700)
			{
				m_lastTap = now;
				if (m_xCmd == 1) pNetGame->SendChatCommand("/malasair");
				else { char b[32]; snprintf(b, sizeof b, "/entrarmalas %d", m_xVeh); pNetGame->SendChatCommand(b); }
			}
			return;
		}
		if (m_down == 10) return;
	}
	if (m_btnR <= 0.0f) { m_down = -1; return; }
	auto hit = [&](int i) { const ImVec4& q = m_key[i]; const float m = 4.0f; return pos.x >= q.x - m && pos.x <= q.z + m && pos.y >= q.y - m && pos.y <= q.w + m; };
	if (type == TouchType::push)
	{
		m_down = -1;
		for (int i = 0; i < 4; i++) if (hit(i)) { m_down = i; break; }
	}
	else if (type == TouchType::pop)
	{
		const int d = m_down; m_down = -1;
		if (d < 0 || !hit(d) || !pNetGame) return;
		const int64_t now = NowMs();
		if (now - m_lastTap < 500) return;                           // anti-repeticao
		m_lastTap = now;
		static const char* kCmd[4] = { "/motor", "/trancar", "/farol", "/pisca" };
		pNetGame->SendChatCommand(kCmd[d]);
	}
}
