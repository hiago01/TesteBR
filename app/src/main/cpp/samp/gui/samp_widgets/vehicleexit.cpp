#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/Widgets/TouchInterface.h"
#include "../../game/RW/RenderWare.h"
#include "../../game/Enums/eVehicleType.h"
#include "../../game/ScooterRider.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "passengerbutton_logic.h"
#include "garagepanel.h"
#include "vehicleexit.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

extern CNetGame* pNetGame;
extern CGame*    pGame;

namespace
{
	std::atomic<bool> g_hide{ false };

	int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	enum { I_MOTO = 0, I_SCOOTER = 1, I_CARRO = 2, I_PATINETE = 3, I_BIKE = 4, I_VAN = 5, I_BARCO = 7, I_HELI = 8, I_AVIAO = 9 };

	bool IsVan(int m)
	{
		switch (m) {   // furgoes, utilitarios, onibus e caminhoes do SA
			case 403: case 408: case 413: case 414: case 418: case 423: case 428: case 431: case 435: case 437: case 440: case 450: case 455:
			case 456: case 459: case 482: case 483: case 498: case 499: case 508: case 514: case 515: case 524: case 578: case 582: case 584:
			case 588: case 591: case 609:
				return true;
		}
		return false;
	}

	int Classify(CVehicleGTA* v)
	{
		if (ScooterRider::LocalRiding()) return I_PATINETE;
		if (!v) return I_CARRO;
		const int model = (int)v->m_nModelIndex;
		switch ((int)v->m_nVehicleType)
		{
			case VEHICLE_TYPE_BMX:   return I_BIKE;
			case VEHICLE_TYPE_BIKE:  return (model == 462 || model == 448) ? I_SCOOTER : I_MOTO;   // Faggio / Pizzaboy
			case VEHICLE_TYPE_QUAD:  return I_MOTO;
			case VEHICLE_TYPE_BOAT:  return I_BARCO;
			case VEHICLE_TYPE_HELI: case VEHICLE_TYPE_FHELI: return I_HELI;
			case VEHICLE_TYPE_PLANE: case VEHICLE_TYPE_FPLANE: return I_AVIAO;
			case VEHICLE_TYPE_MTRUCK: case VEHICLE_TYPE_TRAILER: return I_VAN;
			default: return IsVan(model) ? I_VAN : I_CARRO;
		}
	}

	// barco / helicoptero / aviao: silhuetas simples (poligonos convexos), sem tipo de veiculo equivalente nas silhuetas da garagem
	void Poly(ImGuiRenderer* r, const float* p, int n, ImVec2 c, float k, float ca, float sa, const ImColor& col)
	{
		ImVec2 q[8];
		for (int i = 0; i < n && i < 8; i++) { const float x = p[i * 2] * k, y = p[i * 2 + 1] * k; q[i] = ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca); }
		r->drawConvexPolyFilled(q, n, col);
	}
	void DrawExtra(ImGuiRenderer* r, int icon, ImVec2 c, float half, float ang, const ImColor& fg, const ImColor& bg)
	{
		const float ca = std::cos(ang), sa = std::sin(ang);
		if (icon == I_BARCO)
		{
			static const float hull[] = { -1.00f, 0.05f, 1.00f, 0.05f, 0.72f, 0.55f, -0.78f, 0.55f };
			static const float cab[]  = { -0.28f, -0.42f, 0.34f, -0.42f, 0.52f, 0.05f, -0.46f, 0.05f };
			static const float mast[] = { -0.04f, -0.95f, 0.04f, -0.95f, 0.04f, -0.42f, -0.04f, -0.42f };
			static const float win[]  = { -0.18f, -0.30f, 0.22f, -0.30f, 0.30f, -0.08f, -0.26f, -0.08f };
			Poly(r, hull, 4, c, half, ca, sa, fg); Poly(r, cab, 4, c, half, ca, sa, fg); Poly(r, mast, 4, c, half, ca, sa, fg); Poly(r, win, 4, c, half, ca, sa, bg);
		}
		else if (icon == I_HELI)
		{
			static const float body[]  = { -0.55f, -0.10f, -0.20f, -0.38f, 0.30f, -0.38f, 0.58f, -0.05f, 0.40f, 0.28f, -0.45f, 0.28f };
			static const float tail[]  = { 0.50f, -0.12f, 1.05f, -0.22f, 1.05f, -0.04f, 0.50f, 0.06f };
			static const float rotor[] = { -1.00f, -0.62f, 1.00f, -0.62f, 1.00f, -0.52f, -1.00f, -0.52f };
			static const float mastp[] = { -0.05f, -0.55f, 0.05f, -0.55f, 0.05f, -0.36f, -0.05f, -0.36f };
			static const float skid[]  = { -0.60f, 0.52f, 0.60f, 0.52f, 0.60f, 0.60f, -0.60f, 0.60f };
			static const float win[]   = { -0.40f, -0.20f, -0.10f, -0.30f, 0.20f, -0.30f, 0.34f, -0.04f, -0.40f, -0.04f };
			Poly(r, body, 6, c, half, ca, sa, fg); Poly(r, tail, 4, c, half, ca, sa, fg); Poly(r, rotor, 4, c, half, ca, sa, fg);
			Poly(r, mastp, 4, c, half, ca, sa, fg); Poly(r, skid, 4, c, half, ca, sa, fg); Poly(r, win, 5, c, half, ca, sa, bg);
		}
		else
		{
			static const float fus[]  = { -1.00f, -0.06f, 0.70f, -0.14f, 1.00f, 0.00f, 0.70f, 0.14f, -1.00f, 0.06f };
			static const float wing[] = { -0.20f, -0.05f, 0.25f, -0.05f, -0.45f, -0.80f, -0.65f, -0.80f };
			static const float wing2[]= { -0.20f, 0.05f, 0.25f, 0.05f, -0.45f, 0.80f, -0.65f, 0.80f };
			static const float tail[] = { -0.95f, -0.02f, -0.72f, -0.02f, -0.88f, -0.42f, -1.00f, -0.42f };
			static const float tail2[]= { -0.95f, 0.02f, -0.72f, 0.02f, -0.88f, 0.42f, -1.00f, 0.42f };
			Poly(r, fus, 5, c, half, ca, sa, fg); Poly(r, wing, 4, c, half, ca, sa, fg); Poly(r, wing2, 4, c, half, ca, sa, fg);
			Poly(r, tail, 4, c, half, ca, sa, fg); Poly(r, tail2, 4, c, half, ca, sa, fg);
		}
	}

	// retangulo do botao nativo (px da tela do ImGui), igual ao do passengerbutton.cpp
	bool NativeRect(float sw, float sh, float& l, float& t, float& r, float& b)
	{
		if (!CTouchInterface::m_pWidgets) return false;
		CWidgetGta* w = CTouchInterface::m_pWidgets[WIDGET_ENTER_CAR];
		if (!w) return false;
		const float gw = (float)RsGlobal->maximumWidth, gh = (float)RsGlobal->maximumHeight;
		if (!(gw > 1.0f) || !(gh > 1.0f)) return false;
		const float kx = sw / gw, ky = sh / gh;
		l = w->m_RectScreen.left * kx; r = w->m_RectScreen.right * kx;
		const float ya = w->m_RectScreen.top * ky, yb = w->m_RectScreen.bottom * ky;
		t = std::min(ya, yb); b = std::max(ya, yb);
		return PassengerLogic::NativeRectOk(l, t, r, b, sw, sh);
	}

	constexpr float kInMs = 600.0f, kOutMs = 420.0f;
}

bool VehicleExit::NativeHidden() { return g_hide.load(); }
namespace { std::atomic<unsigned> g_padMask{ 0 }, g_press{ 0 }; std::atomic<int> g_ringNat{ 0 }; std::atomic<bool> g_ringOn{ false }; }
bool VehicleExit::PadDrawn(int id) { return (g_padMask.load() >> id) & 1u; }
void VehicleExit::SetPressed(unsigned m) { g_press.store(m); }
void VehicleExit::SetRing(int a) { g_ringNat.store(a); }
bool VehicleExit::RingDrawn() { return g_ringOn.load(); }

static void DrawExit(ImGuiRenderer* r)
{
	static bool wasIn = false, haveRect = false;
	static int64_t tIn = 0, tOut = 0;
	static float cx = 0, cy = 0, rad = 0;
	static int lastIcon = I_CARRO;

	const int64_t now = NowMs();
	CPedGTA* ped = nullptr;
	if (pNetGame && pNetGame->GetPlayerPool())
	{
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (lp && lp->GetPlayerPed() && lp->GetPlayerPed()->m_pPed) ped = lp->GetPlayerPed()->m_pPed;
	}
	const bool inVeh = ped && ped->IsInVehicle() && ped->m_fHealth > 0.0f && !(pGame && pGame->IsGamePaused());
	if (inVeh && !wasIn) tIn = now;
	if (!inVeh && wasIn) tOut = now;
	wasIn = inVeh;

	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	float scale = 1.0f, ang = 0.0f;
	if (inVeh)
	{
		float l, t, rr, b;
		if (NativeRect(screen.x, screen.y, l, t, rr, b))
		{
			cx = (l + rr) * 0.5f; cy = (t + b) * 0.5f; rad = std::min(rr - l, b - t) * 0.5f; haveRect = true;
		}
		lastIcon = Classify(ped->pVehicle);
		const float T = std::min(1.0f, (float)(now - tIn) / kInMs);
		if (T < 1.0f)   // entrando: cresce com overshoot (easeOutBack), girando, igual ao topo da garagem
		{
			const float c1 = 1.70158f, c3 = c1 + 1.0f, q = T - 1.0f;
			scale = 1.0f + c3 * q * q * q + c1 * q * q; ang = (1.0f - T) * -0.9f;
		}
	}
	else
	{
		const float T = (float)(now - tOut) / kOutMs;
		if (!haveRect || T >= 1.0f || T < 0.0f) { g_hide.store(false); return; }
		scale = 1.0f - T * T; ang = T * 0.9f;   // saindo: encolhe girando
	}
	if (!haveRect || !(rad > 2.0f)) { g_hide.store(false); return; }   // sem retangulo confiavel: fica o botao nativo
	g_hide.store(true);

	const float R = rad * 0.96f * std::max(0.0f, scale);
	if (R < 1.0f) return;
	constexpr int N = 32;
	ImVec2 p[N];
	for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(cx + std::cos(a) * R, cy + std::sin(a) * R); }
	r->drawConvexPolyFilled(p, N, ImColor(20, 20, 20, 170));
	r->drawPolyline(p, N, ImColor(255, 255, 255, 235), true, std::max(2.0f, R * 0.07f));

	const ImColor fg(255, 255, 255, 255), bg(20, 20, 20, 190);
	const float half = R * 0.62f;
	if (lastIcon >= I_BARCO) DrawExtra(r, lastIcon, ImVec2(cx, cy), half, ang, fg, bg);
	else GaragePanel::DrawIcon(r, lastIcon, ImVec2(cx, cy), half, ang, fg, bg);
}

// =====================================================================================================================
//  ACELERAR / FREIO / DERRAPAGEM (widgets nativos 2,3,4) e ANEL hud_analognub (0xA4): desenho do cliente por cima do nativo
// =====================================================================================================================
namespace
{
	bool WidgetRect(int id, float sw, float sh, float& l, float& t, float& r, float& b)
	{
		if (!CTouchInterface::m_pWidgets) return false;
		CWidgetGta* w = CTouchInterface::m_pWidgets[id];
		if (!w || !w->m_bEnabled) return false;
		const float gw = (float)RsGlobal->maximumWidth, gh = (float)RsGlobal->maximumHeight;
		if (!(gw > 1.0f) || !(gh > 1.0f)) return false;
		const float kx = sw / gw, ky = sh / gh;
		l = w->m_RectScreen.left * kx; r = w->m_RectScreen.right * kx;
		const float ya = w->m_RectScreen.top * ky, yb = w->m_RectScreen.bottom * ky;
		t = std::min(ya, yb); b = std::max(ya, yb);
		return PassengerLogic::NativeRectOk(l, t, r, b, sw, sh);
	}

	void Pedal(ImGuiRenderer* r, ImVec2 c, float u, bool tall, const ImColor& fg, const ImColor& bg)
	{   // pedal com ranhuras; acelerar = estreito e alto (+ seta para cima), freio = largo e baixo (+ duas ranhuras grandes)
		const float w = tall ? 0.52f * u : 1.00f * u, h = tall ? 0.95f * u : 0.60f * u;
		r->drawRoundedRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), fg, u * 0.22f, true);
		const float lw = std::max(1.5f, u * 0.10f);
		if (tall) {
			for (int i = 0; i < 4; i++) { const float y = c.y - h * 0.15f + (float)i * h * 0.26f; r->drawLine(ImVec2(c.x - w * 0.62f, y), ImVec2(c.x + w * 0.62f, y), bg, lw); }
			ImVec2 a[3] = { ImVec2(c.x, c.y - h * 0.82f), ImVec2(c.x + w * 0.55f, c.y - h * 0.40f), ImVec2(c.x - w * 0.55f, c.y - h * 0.40f) };
			r->drawConvexPolyFilled(a, 3, bg);
		} else {
			for (int i = 0; i < 3; i++) { const float y = c.y - h * 0.50f + (float)i * h * 0.50f; r->drawLine(ImVec2(c.x - w * 0.80f, y), ImVec2(c.x + w * 0.80f, y), bg, lw); }
		}
	}

	void Skid(ImGuiRenderer* r, ImVec2 c, float u, const ImColor& fg, const ImColor& bg)
	{   // derrapagem: pneu (retangulo arredondado) + duas marcas de freada em S saindo dele
		const float lw = std::max(2.0f, u * 0.17f);
		for (int k = 0; k < 2; k++) {
			ImVec2 pts[14];
			for (int i = 0; i < 14; i++) {
				const float t = (float)i / 13.0f;
				pts[i] = ImVec2(c.x - u * 0.55f + (k ? u * 0.42f : 0.0f) + std::sin(t * 3.4f) * u * 0.42f, c.y + u * 0.95f - t * u * 1.85f);
			}
			r->drawPolyline(pts, 14, fg, false, lw);
		}
		r->drawRoundedRect(ImVec2(c.x + u * 0.05f, c.y - u * 1.0f), ImVec2(c.x + u * 0.95f, c.y - u * 0.15f), fg, u * 0.16f, true);
		r->drawLine(ImVec2(c.x + u * 0.25f, c.y - u * 0.88f), ImVec2(c.x + u * 0.25f, c.y - u * 0.27f), bg, std::max(1.5f, u * 0.09f));
		r->drawLine(ImVec2(c.x + u * 0.50f, c.y - u * 0.88f), ImVec2(c.x + u * 0.50f, c.y - u * 0.27f), bg, std::max(1.5f, u * 0.09f));
		r->drawLine(ImVec2(c.x + u * 0.75f, c.y - u * 0.88f), ImVec2(c.x + u * 0.75f, c.y - u * 0.27f), bg, std::max(1.5f, u * 0.09f));
	}

	void DrawPads(ImGuiRenderer* r, bool inVeh, const ImVec2& screen)
	{
		unsigned mask = 0;
		if (inVeh)
		{
			const unsigned press = g_press.load();
			for (int id = 2; id <= 4; id++)
			{
				float l, t, rr, b;
				if (!WidgetRect(id, screen.x, screen.y, l, t, rr, b)) continue;
				mask |= 1u << id;
				const bool dn = (press >> id) & 1u;
				const float w = rr - l, h = b - t, m = std::min(w, h) * 0.04f;
				const ImVec2 a(l + m, t + m), e(rr - m, b - m), c((l + rr) * 0.5f, (t + b) * 0.5f);
				const float rnd = std::min(w, h) * (id == 4 ? 0.5f : 0.30f);
				r->drawRoundedRect(a, e, dn ? ImColor(255, 255, 255, 235) : ImColor(20, 20, 20, 170), rnd, true);
				r->drawRoundedRect(a, e, ImColor(255, 255, 255, 235), rnd, false, std::max(2.0f, std::min(w, h) * 0.05f));
				const ImColor fg = dn ? ImColor(20, 20, 20, 255) : ImColor(255, 255, 255, 255), bg = dn ? ImColor(255, 255, 255, 255) : ImColor(20, 20, 20, 230);
				const float u = std::min(w, h) * 0.34f;
				if (id == 2) Pedal(r, c, u, true, fg, bg);
				else if (id == 3) Pedal(r, c, u, false, fg, bg);
				else Skid(r, c, u, fg, bg);
			}
		}
		g_padMask.store(mask);
	}

	void DrawRing(ImGuiRenderer* r, const ImVec2& screen)
	{   // anel hud_analognub (indicador da mira no controle moderno): anel preto grosso com fio branco, aparece/some suave
		static float a = 0.0f; static int64_t last = 0;
		const int64_t now = NowMs(); const float dt = last ? std::min(0.1f, (float)(now - last) / 1000.0f) : 0.016f; last = now;
		const bool want = g_ringNat.load() > 0;
		a += ((want ? 1.0f : 0.0f) - a) * std::min(1.0f, dt * 14.0f);
		float l, t, rr, b;
		bool ok = false;
		if (CTouchInterface::m_pWidgets && CTouchInterface::m_pWidgets[WIDGET_THUMB_CIRCLE])
		{
			CWidgetGta* w = CTouchInterface::m_pWidgets[WIDGET_THUMB_CIRCLE];
			const float gw = (float)RsGlobal->maximumWidth, gh = (float)RsGlobal->maximumHeight;
			if (gw > 1.0f && gh > 1.0f) {
				const float kx = screen.x / gw, ky = screen.y / gh;
				l = w->m_RectScreen.left * kx; rr = w->m_RectScreen.right * kx;
				const float ya = w->m_RectScreen.top * ky, yb = w->m_RectScreen.bottom * ky;
				t = std::min(ya, yb); b = std::max(ya, yb);
				ok = PassengerLogic::NativeRectOk(l, t, rr, b, screen.x, screen.y);
			}
		}
		g_ringOn.store(ok);
		if (!ok || a < 0.02f) return;
		const float cx = (l + rr) * 0.5f, cy = (t + b) * 0.5f, R = std::min(rr - l, b - t) * 0.5f;
		const int A = (int)(255.0f * a);
		constexpr int N = 40;
		ImVec2 p[N], q[N], s[N];
		for (int i = 0; i < N; i++) {
			const float ang = (float)i * 6.2831853f / (float)N, co = std::cos(ang), si = std::sin(ang);
			p[i] = ImVec2(cx + co * R * 0.86f, cy + si * R * 0.86f);
			q[i] = ImVec2(cx + co * R * 0.98f, cy + si * R * 0.98f);
			s[i] = ImVec2(cx + co * R * 0.74f, cy + si * R * 0.74f);
		}
		r->drawPolyline(q, N, ImColor(255, 255, 255, A / 2), true, std::max(1.5f, R * 0.04f));
		r->drawPolyline(s, N, ImColor(255, 255, 255, A / 2), true, std::max(1.5f, R * 0.04f));
		r->drawPolyline(p, N, ImColor(0, 0, 0, A), true, std::max(3.0f, R * 0.14f));
	}
}

void VehicleExit::Draw(ImGuiRenderer* r)
{
	DrawExit(r);
	CPedGTA* ped = nullptr;
	if (pNetGame && pNetGame->GetPlayerPool())
	{
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (lp && lp->GetPlayerPed() && lp->GetPlayerPed()->m_pPed) ped = lp->GetPlayerPed()->m_pPed;
	}
	const bool inVeh = ped && ped->IsInVehicle() && ped->m_fHealth > 0.0f && !(pGame && pGame->IsGamePaused());
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	DrawPads(r, inVeh, screen);
	DrawRing(r, screen);
}
