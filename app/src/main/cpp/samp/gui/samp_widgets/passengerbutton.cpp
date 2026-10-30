#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/Widgets/TouchInterface.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../util/CUtil.h"
#include "passengerbutton_logic.h"

#include <chrono>
#include <algorithm>
#include <cmath>

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;
extern volatile bool g_bTouchCancel;       // game/multitouch.cpp
extern bool      bNeedEnterVehicle;           // buttonpanel.cpp: consumido por CLocalPlayer::HandlePassengerEntry

namespace
{
	inline int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// distancia (m) ao veiculo SA-MP mais proximo do ped local; -1 = nenhum
	float NearestVehicleDist(CPedGTA*& pedOut, bool& aliveOut, bool& inVehOut, bool& busyOut)
	{
		pedOut = nullptr; aliveOut = false; inVehOut = false; busyOut = false;
		if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetVehiclePool()) return -1.0f;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp || !lp->GetPlayerPed() || !lp->GetPlayerPed()->m_pPed) return -1.0f;
		CPedGTA* ped = lp->GetPlayerPed()->m_pPed;
		pedOut = ped; aliveOut = ped->m_fHealth > 0.0f; inVehOut = ped->IsInVehicle();
		busyOut = ped->IsEnteringCar() || ped->IsExitingVehicle();
		CVehiclePool* vp = pNetGame->GetVehiclePool();
		const VEHICLEID id = vp->FindNearestToLocalPlayerPed();
		CVehicle* v = vp->GetAt(id);
		if (!v || !v->m_pVehicle) return -1.0f;
		return v->m_pVehicle->GetDistanceFromLocalPlayerPed();
	}

	bool UiBlocked()
	{
		if (pGame && pGame->IsGamePaused()) return true;
		if (!pUI) return false;
		return (pUI->keyboard()      && pUI->keyboard()->visible())      ||
		       (pUI->dialog()        && pUI->dialog()->visible())        ||
		       (pUI->playertablist() && pUI->playertablist()->visible()) ||
		       (pUI->spawn()         && pUI->spawn()->visible())         ||
		       (pUI->splashscreen()  && pUI->splashscreen()->visible());
	}

	// retangulo do botao NATIVO do carro, em px da tela do ImGui. false = sem referencia confiavel (usa o ponto medido).
	bool NativeCarRect(float screenW, float screenH, float& l, float& t, float& r, float& b)
	{
		if (!CTouchInterface::m_pWidgets) return false;
		CWidgetGta* w = CTouchInterface::m_pWidgets[WIDGET_ENTER_CAR];
		if (!w) return false;
		float gw = (float)RsGlobal->maximumWidth, gh = (float)RsGlobal->maximumHeight;
		if (!(gw > 1.0f) || !(gh > 1.0f)) return false;
		const float kx = screenW / gw, ky = screenH / gh;
		l = w->m_RectScreen.left * kx;  r = w->m_RectScreen.right * kx;
		const float ya = w->m_RectScreen.top * ky, yb = w->m_RectScreen.bottom * ky;   // CRect do jogo: top/bottom podem vir invertidos
		t = std::min(ya, yb); b = std::max(ya, yb);
		return PassengerLogic::NativeRectOk(l, t, r, b, screenW, screenH);
	}
}

PassengerButton::PassengerButton() : Widget()
{
	m_available.store(false);
	m_finger.store(-1);
	m_cx.store(0.0f); m_cy.store(0.0f); m_r.store(0.0f);
	m_lastUseMs.store(0);
}

void PassengerButton::draw(ImGuiRenderer* r)
{
	CPedGTA* ped = nullptr; bool alive = false, inVeh = false, busy = false;
	const float dist = NearestVehicleDist(ped, alive, inVeh, busy);
	const bool show = PassengerLogic::ShouldShow(ped != nullptr, UiBlocked(), inVeh, alive, dist, busy);
	m_available.store(show);
	if (!show) { m_finger.store(-1); return; }

	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	float l = 0, t = 0, rr = 0, b = 0;
	const bool nat = NativeCarRect(screen.x, screen.y, l, t, rr, b);
	const PassengerLogic::Circle c = PassengerLogic::Place(nat, l, t, rr, b, screen.x, screen.y);
	m_cx.store(c.cx); m_cy.store(c.cy); m_r.store(c.r);

	const bool pressed = m_finger.load() >= 0;
	const bool cooling = (NowMs() - m_lastUseMs.load()) < PassengerLogic::kCooldownMs;

	// Icone original "enterandexit" (texdb/mobile, o mesmo banco do icone nativo do carro). Carrega sob demanda; se nao achar,
	// tenta de novo a cada ~2 s e enquanto isso desenha o botao simples "P2".
	static RwTexture* s_icon = nullptr;
	static int64_t    s_nextTry = 0;
	if (!(s_icon && s_icon->raster) && NowMs() >= s_nextTry)
	{
		s_nextTry = NowMs() + 2000;
		s_icon = CUtil::LoadTextureFromDB("mobile", "enterandexit");
	}

	if (s_icon && s_icon->raster)
	{
		float iw = (float)s_icon->raster->width, ih = (float)s_icon->raster->height;
		if (iw < 1.0f || ih < 1.0f) iw = ih = 1.0f;
		const float box = c.r * 2.0f, k = std::min(box / iw, box / ih);
		const float dw = iw * k, dh = ih * k;
		const int a = cooling ? 110 : 255;
		const ImColor tint = pressed ? ImColor(255, 220, 90, a) : ImColor(255, 255, 255, a);
		r->drawImage(ImVec2(c.cx - dw * 0.5f, c.cy - dh * 0.5f), ImVec2(c.cx + dw * 0.5f, c.cy + dh * 0.5f), (ImTextureID)s_icon->raster, tint);
		return;
	}

	constexpr int N = 28;
	ImVec2 p[N];
	for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(c.cx + std::cos(a) * c.r, c.cy + std::sin(a) * c.r); }
	r->drawConvexPolyFilled(p, N, pressed ? ImColor(255, 255, 255, 200) : ImColor(15, 15, 15, cooling ? 90 : 170));
	r->drawPolyline(p, N, ImColor(255, 255, 255, cooling ? 120 : 255), true, std::max(2.0f, c.r * 0.08f));
	const float fs = c.r * 0.95f;
	const ImVec2 ts = r->calculateTextSize("P2", fs);
	r->drawText(ImVec2(c.cx - ts.x * 0.5f, c.cy - ts.y * 0.5f), pressed ? ImColor(0, 0, 0, 255) : ImColor(255, 255, 255, 255), "P2", true, fs);
}

bool PassengerButton::onRawTouch(int type, int num, int x, int y)
{
	const float fx = (float)x, fy = (float)y;
	const PassengerLogic::Circle c{ m_cx.load(), m_cy.load(), m_r.load() };

	if (type == 2)                                                   // push
	{
		if (!m_available.load() || m_finger.load() >= 0 || !(c.r > 0.0f)) return false;
		if (bNeedEnterVehicle) return PassengerLogic::Hit(c, fx, fy);        // pedido anterior ainda nao consumido: engole o toque, nao empilha
		if (!PassengerLogic::Hit(c, fx, fy)) return false;
		m_finger.store(num);
		return true;
	}

	if (m_finger.load() != num) return false;                        // move/pop: so do dedo dono
	if (type == 3) return true;
	if (type == 1)                                                   // pop
	{
		m_finger.store(-1);
		const bool cancel = g_bTouchCancel;
		if (!cancel && m_available.load() && PassengerLogic::Hit(c, fx, fy) && (NowMs() - m_lastUseMs.load()) >= PassengerLogic::kCooldownMs)
		{
			m_lastUseMs.store(NowMs());
			bNeedEnterVehicle = true;                                // CLocalPlayer::HandlePassengerEntry (thread do jogo) faz o EnterVehicle(passageiro)
		}
		return true;
	}
	return false;
}
