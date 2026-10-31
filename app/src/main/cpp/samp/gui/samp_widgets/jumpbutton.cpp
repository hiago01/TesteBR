#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/Widgets/TouchInterface.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../util/CUtil.h"
#include "jumpbutton_logic.h"
#include "maximomenu.h"

#include <chrono>
#include <algorithm>
#include <cmath>

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;
extern volatile bool g_bTouchCancel;       // game/multitouch.cpp

namespace
{
	JumpLogic::Request g_req;
	int64_t            g_lastJumpMs = -1000000;

	inline int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	bool UiBlocked()
	{
		if (pGame && pGame->IsGamePaused()) return true;
		if (MaximoMenu::IsOpen()) return true;
		if (!pUI) return false;
		return (pUI->keyboard()      && pUI->keyboard()->visible())      ||
		       (pUI->dialog()        && pUI->dialog()->visible())        ||
		       (pUI->playertablist() && pUI->playertablist()->visible()) ||
		       (pUI->spawn()         && pUI->spawn()->visible())         ||
		       (pUI->splashscreen()  && pUI->splashscreen()->visible());
	}

	bool LocalPed(CPedGTA*& ped)
	{
		ped = nullptr;
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp || !lp->GetPlayerPed() || !lp->GetPlayerPed()->m_pPed) return false;
		ped = lp->GetPlayerPed()->m_pPed;
		return true;
	}

	// retangulo do botao NATIVO de correr (WIDGET_SPRINT), em px da tela do ImGui
	bool NativeSprintRect(float sw, float sh, float& l, float& t, float& r, float& b)
	{
		if (!CTouchInterface::m_pWidgets) return false;
		CWidgetGta* w = CTouchInterface::m_pWidgets[WIDGET_SPRINT];
		if (!w) return false;
		float gw = (float)RsGlobal->maximumWidth, gh = (float)RsGlobal->maximumHeight;
		if (!(gw > 1.0f) || !(gh > 1.0f)) return false;
		const float kx = sw / gw, ky = sh / gh;
		l = w->m_RectScreen.left * kx;  r = w->m_RectScreen.right * kx;
		const float ya = w->m_RectScreen.top * ky, yb = w->m_RectScreen.bottom * ky;
		t = std::min(ya, yb); b = std::max(ya, yb);
		return JumpLogic::NativeRectOk(l, t, r, b, sw, sh);
	}
}

bool JumpButtonState::ConsumeJustDown() { return g_req.ConsumeJustDown(); }
bool JumpButtonState::Held()            { return g_req.Held(NowMs()); }

JumpButton::JumpButton() : Widget()
{
	m_available.store(false);
	m_finger.store(-1);
	m_cx.store(0.0f); m_cy.store(0.0f); m_r.store(0.0f);
}

void JumpButton::draw(ImGuiRenderer* r)
{
	CPedGTA* ped = nullptr;
	const bool pedOk = LocalPed(ped);
	const bool show = JumpLogic::ShouldShow(pedOk, UiBlocked(), pedOk && ped->IsInVehicle(), pedOk && ped->m_fHealth > 0.0f);
	m_available.store(show);
	if (!show) { m_finger.store(-1); return; }

	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	float l = 0, t = 0, rr = 0, b = 0;
	const bool nat = NativeSprintRect(screen.x, screen.y, l, t, rr, b);
	const JumpLogic::Circle c = JumpLogic::Place(nat, l, t, rr, b, screen.x, screen.y);
	m_cx.store(c.cx); m_cy.store(c.cy); m_r.store(c.r);

	const bool pressed = m_finger.load() >= 0;

	// Icone "hud_crouch" (texdb/mobile). Carrega sob demanda; se nao achar, tenta no MAXIMO 3 vezes (espacadas) e depois
	// desiste: fica o botao simples "PULO" e nao ha mais nenhuma busca em disco durante o jogo.
	static RwTexture* s_icon = nullptr;
	static int64_t    s_nextTry = 0;
	static int        s_tries = 0;
	if (!(s_icon && s_icon->raster) && s_tries < 3 && NowMs() >= s_nextTry)
	{
		s_tries++;
		s_nextTry = NowMs() + 3000;
		s_icon = CUtil::LoadTextureFromDB("mobile", "hud_crouch");
	}

	if (s_icon && s_icon->raster)
	{
		float iw = (float)s_icon->raster->width, ih = (float)s_icon->raster->height;
		if (iw < 1.0f || ih < 1.0f) iw = ih = 1.0f;
		const float box = c.r * 2.0f, k = std::min(box / iw, box / ih);
		const float dw = iw * k, dh = ih * k;
		const ImColor tint = pressed ? ImColor(255, 220, 90, 255) : ImColor(255, 255, 255, 255);
		r->drawImage(ImVec2(c.cx - dw * 0.5f, c.cy - dh * 0.5f), ImVec2(c.cx + dw * 0.5f, c.cy + dh * 0.5f), (ImTextureID)s_icon->raster, tint);
		return;
	}

	constexpr int N = 28;
	ImVec2 p[N];
	for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(c.cx + std::cos(a) * c.r, c.cy + std::sin(a) * c.r); }
	r->drawConvexPolyFilled(p, N, pressed ? ImColor(255, 255, 255, 200) : ImColor(15, 15, 15, 170));
	r->drawPolyline(p, N, ImColor(255, 255, 255, 255), true, std::max(2.0f, c.r * 0.08f));
	const float fs = c.r * 0.6f;
	const ImVec2 ts = r->calculateTextSize("PULO", fs);
	r->drawText(ImVec2(c.cx - ts.x * 0.5f, c.cy - ts.y * 0.5f), pressed ? ImColor(0, 0, 0, 255) : ImColor(255, 255, 255, 255), "PULO", true, fs);
}

bool JumpButton::onRawTouch(int type, int num, int x, int y)
{
	const float fx = (float)x, fy = (float)y;
	const JumpLogic::Circle c{ m_cx.load(), m_cy.load(), m_r.load() };

	if (type == 2)                                                   // push
	{
		if (!m_available.load() || m_finger.load() >= 0 || !(c.r > 0.0f)) return false;
		if (!JumpLogic::Hit(c, fx, fy)) return false;
		m_finger.store(num);
		const int64_t now = NowMs();
		if (!g_bTouchCancel && now - g_lastJumpMs >= JumpLogic::kCooldownMs) { g_lastJumpMs = now; g_req.Press(now); }   // pula ao TOCAR (resposta imediata)
		return true;
	}

	if (m_finger.load() != num) return false;                        // move/pop: so do dedo dono
	if (type == 3) return true;
	if (type == 1) { m_finger.store(-1); return true; }              // pop
	return false;
}
