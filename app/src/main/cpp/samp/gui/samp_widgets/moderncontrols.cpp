#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "controlmode.h"
#include "maximomenu.h"
#include "controlsselector.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

using namespace CtlLogic;

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;
extern CSettings* pSettings;
extern volatile bool g_bTouchCancel;       // game/multitouch.cpp

namespace
{
	std::mutex            g_mu;
	ModernController      g_ctl;
	uint32_t              g_seenVer = 0;
	float                 g_sw = 0, g_sh = 0;

	// saida lida pelos hooks de CPad (thread do jogo)
	std::atomic<bool>     g_active{ false };
	std::atomic<bool>     g_editing{ false };
	std::atomic<int>      g_lr{ 0 }, g_ud{ 0 };
	std::atomic<uint32_t> g_held{ 0 };
	std::atomic<uint32_t> g_edges{ 0 };
	std::atomic<int64_t>  g_jumpMs{ -1000000 };
	int                   g_okFinger = -1;
	CtlLogic::AimDrag     g_aim;
	int                   g_aimFinger = -1;
	std::vector<ModernControls::AimTouch> g_aimQ;

	void QueueAim(const AimEv* ev, int n, int num)       // g_mu travado
	{
		for (int i = 0; i < n && g_aimQ.size() < 64; i++)
			g_aimQ.push_back({ ev[i].type, num, (int)std::lround(ev[i].x), (int)std::lround(ev[i].y) });
	}
	void EndAimLocked()
	{
		if (g_aimFinger >= 0) { AimEv ev[2]; QueueAim(ev, g_aim.End(ev), g_aimFinger); }
		g_aimFinger = -1;
	}

	constexpr int kJumpHoldMs = 120;

	int64_t NowMs()
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

	CtlRect OkRect(float sw, float sh)
	{
		const float w = sh * 0.34f, h = sh * 0.11f, cx = sw * 0.5f, t = sh * 0.04f;
		return CtlRect{ cx - w * 0.5f, t, cx + w * 0.5f, t + h };
	}
	CtlRect ResetRect(float sw, float sh)
	{
		const CtlRect o = OkRect(sw, sh);
		const float w = o.r - o.l, gap = sh * 0.02f;
		return CtlRect{ o.l - gap - w, o.t, o.l - gap, o.b };
	}

	void Publish()   // chamar com g_mu travado, depois de mudar o controlador
	{
		const Output o = g_ctl.Read(true);
		g_lr.store(o.lr); g_ud.store(o.ud); g_held.store(o.held);
		if (o.pressedEdge) {
			g_edges.fetch_or(o.pressedEdge);
			if (o.pressedEdge & (1u << B_JUMP)) g_jumpMs.store(NowMs());
		}
		// microfone: mesmo caminho do botao original (VoiceButton), so que acionado por este botao
		if (pUI && pUI->voicebutton())
		{
			if (o.pressedEdge  & (1u << B_MIC)) pUI->voicebutton()->touchPushEvent();
			if (o.releasedEdge & (1u << B_MIC)) pUI->voicebutton()->touchPopEvent();
		}
	}

	void ReleaseAllLocked()
	{
		EndAimLocked();
		g_ctl.CancelAll();
		Publish();
		g_edges.store(0);
		g_lr.store(0); g_ud.store(0); g_held.store(0);
		g_okFinger = -1;
	}

	bool ConsumeEdge(int b) { const uint32_t bit = 1u << b; return (g_edges.fetch_and(~bit) & bit) != 0; }
}

// ----------------------------------------------------------------------------- ModernInput
bool ModernInput::Active() { return g_active.load(); }

bool ModernInput::Stick(int16_t& lr, int16_t& ud)
{
	if (!g_active.load()) return false;
	lr = (int16_t)g_lr.load(); ud = (int16_t)g_ud.load();
	return true;
}
bool ModernInput::SprintHeld()    { return g_active.load() && (g_held.load() & (1u << B_RUN)) && (g_lr.load() != 0 || g_ud.load() != 0); }
bool ModernInput::JumpJustDown()  { return g_active.load() && ConsumeEdge(B_JUMP); }
bool ModernInput::JumpHeld()      { return g_active.load() && (NowMs() - g_jumpMs.load()) < kJumpHoldMs; }
bool ModernInput::CrouchJustDown(){ return g_active.load() && ConsumeEdge(B_CROUCH); }
bool ModernInput::AttackHeld()    { return g_active.load() && (g_held.load() & (1u << B_ATTACK)); }
bool ModernInput::AimWanted()      { return g_active.load() && (g_held.load() & (1u << B_ATTACK)); }
bool ModernInput::AttackJustDown(){ return g_active.load() && ConsumeEdge(B_ATTACK); }

int ModernControls::DrainAimTouches(AimTouch* out, int max)
{
	std::lock_guard<std::mutex> l(g_mu);
	int n = 0;
	while (n < max && n < (int)g_aimQ.size()) { out[n] = g_aimQ[n]; n++; }
	g_aimQ.erase(g_aimQ.begin(), g_aimQ.begin() + n);
	return n;
}

// ----------------------------------------------------------------------------- widget
ModernControls::ModernControls() : Widget() {}

void ModernControls::StartEdit() { if (ControlMode::IsModern()) g_editing.store(true); }
bool ModernControls::IsEditing() { return g_editing.load(); }

bool ModernControls::onRawTouch(int type, int num, int x, int y)
{
	const bool editing = g_editing.load();
	if (!g_active.load() && !editing) return false;
	const float fx = (float)x, fy = (float)y;
	std::lock_guard<std::mutex> l(g_mu);
	g_ctl.editing = editing;

	if (editing)
	{
		const CtlRect ok = OkRect(g_sw, g_sh), rs = ResetRect(g_sw, g_sh);
		if (type == 2 && g_okFinger < 0 && (CtlIn(ok, fx, fy) || CtlIn(rs, fx, fy))) { g_okFinger = num; return true; }
		if (g_okFinger == num)
		{
			if (type == 1)
			{
				g_okFinger = -1;
				if (CtlIn(ok, fx, fy)) { ControlMode::SetConfig(g_ctl.cfg, true); g_editing.store(false); g_ctl.editing = false; }
				else if (CtlIn(rs, fx, fy)) { ControlMode::ResetConfig(); g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ControlMode::ConfigVersion(); }
			}
			return true;
		}
		g_ctl.OnTouch(type, num, fx, fy);
		return true;                  // em edicao nenhum toque vai para o jogo
	}

	if (g_bTouchCancel) { ReleaseAllLocked(); return false; }
	const int atkBefore = g_ctl.BtnFinger(B_ATTACK);
	const bool mine = g_ctl.OnTouch(type, num, fx, fy);
	Publish();

	// Mira arrastavel (opcional): o dedo do Ataque tambem arrasta a camera/mira (deslocamento relativo, ver AimDrag)
	{
		AimEv ev[4];
		if (type == 2 && g_aimFinger < 0 && g_ctl.BtnFinger(B_ATTACK) == num && atkBefore != num && g_ctl.cfg.aimDrag)
		{
			g_aim.sens = g_ctl.cfg.aimSens; g_aim.smooth = g_ctl.cfg.aimSmooth; g_aim.SetScreen(g_sw, g_sh);
			g_aimFinger = num;
			QueueAim(ev, g_aim.Begin(fx, fy, ev), num);
		}
		else if (type == 3 && g_aimFinger == num)
		{
			g_aim.sens = g_ctl.cfg.aimSens; g_aim.smooth = g_ctl.cfg.aimSmooth;
			QueueAim(ev, g_aim.Move(fx, fy, ev), num);
		}
		else if (type == 1 && g_aimFinger == num)
			EndAimLocked();
	}
	return mine;
}

void ModernControls::draw(ImGuiRenderer* r)
{
	ControlMode::EnsureLoaded();

	CPedGTA* ped = nullptr;
	const bool pedOk = LocalPed(ped);
	const bool editing = g_editing.load();
	const bool vehicle = pedOk && ped->IsInVehicle();
	bool jet = false;
	{ CPlayerPed* pp = pGame ? pGame->FindPlayerPed() : nullptr; if (pedOk && pp) jet = pp->IsInJetpackMode(); }   // IsInJetpackMode e do CPlayerPed (igual WidgetGta.cpp)
	const bool alive = pedOk && ped->m_fHealth > 0.0f;
	const bool selOpen = ControlsSelector::IsOpen();
	const bool modernNow = ControlMode::IsModern() && pedOk && !selOpen;
	const bool show = editing ? (modernNow && !MaximoMenu::IsOpen())
	                          : (modernNow && ModernActiveOnFoot(Mode::Modern, vehicle, jet, alive) && !UiBlocked());

	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	bool wasActive;
	{
		std::lock_guard<std::mutex> l(g_mu);
		g_sw = screen.x; g_sh = screen.y;
		const uint32_t ver = ControlMode::ConfigVersion();
		if (ver != g_seenVer) { g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ver; }
		g_ctl.SetScreen(screen.x, screen.y);
		const bool micOn = pSettings && pSettings->Get().bVoiceChatEnable;
		g_ctl.disabledMask = micOn ? 0u : (1u << B_MIC);
		wasActive = g_active.load();
		if (wasActive && !show) ReleaseAllLocked();          // saiu do modo/entrou em veiculo/abriu UI: solta tudo
		if (!editing && g_editing.load() == false) g_ctl.editing = false;
	}
	g_active.store(show && !editing);
	if (!show) { if (editing && !modernNow) g_editing.store(false); return; }

	ModernController snap;
	{
		std::lock_guard<std::mutex> l(g_mu);
		snap = g_ctl;                                          // copia leve p/ desenhar sem segurar o mutex
	}
	const ModernConfig& c = snap.cfg;
	const ModernLayout& L = snap.lay;
	const int A = (int)std::lround(255.0f * c.alpha);
	auto circle = [&](const CtlCircle& ci, const ImColor& fill, const ImColor& line, float lw) {
		constexpr int N = 40;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(ci.cx + std::cos(a) * ci.r, ci.cy + std::sin(a) * ci.r); }
		r->drawConvexPolyFilled(p, N, fill);
		r->drawPolyline(p, N, line, true, lw);
	};

	// joystick: base translucida + centro movel
	circle(L.base, ImColor(20, 20, 20, A / 2), ImColor(255, 255, 255, A), std::max(2.0f, L.base.r * 0.03f));
	const CtlVec k = snap.Knob();
	circle(CtlCircle{ k.x, k.y, L.knobR }, snap.StickDown() ? ImColor(255, 255, 255, std::min(255, A + 70)) : ImColor(230, 230, 230, A),
	       ImColor(255, 255, 255, std::min(255, A + 40)), 2.0f);

	// botoes
	static const char* const kLbl[B_COUNT] = { "CORRER", "PULO", "AGACHAR", "ATAQUE", "MIC" };
	for (int i = 0; i < B_COUNT; i++)
	{
		if (((snap.disabledMask >> i) & 1u)) continue;
		const bool down = snap.BtnDown(i);
		const CtlCircle& b = L.btn[i];
		circle(b, down ? ImColor(255, 255, 255, std::min(255, A + 60)) : ImColor(20, 20, 20, A), ImColor(255, 255, 255, std::min(255, A + 50)), std::max(2.0f, b.r * 0.06f));
		const float fs = b.r * (i == B_CROUCH ? 0.34f : 0.42f);
		const ImVec2 ts = r->calculateTextSize(kLbl[i], fs);
		r->drawText(ImVec2(b.cx - ts.x * 0.5f, b.cy - ts.y * 0.5f),
		            down ? ImColor(0, 0, 0, 255) : ImColor(255, 255, 255, std::min(255, A + 80)), kLbl[i], true, fs);
	}

	if (editing)
	{
		r->drawRect(ImVec2(0, 0), screen, ImColor(0, 0, 0, 90), true);
		const CtlRect ok = OkRect(screen.x, screen.y), rs = ResetRect(screen.x, screen.y);
		const float fs = screen.y * 0.04f;
		r->drawRect(ImVec2(ok.l, ok.t), ImVec2(ok.r, ok.b), ImColor(40, 140, 70, 235), true);
		r->drawRect(ImVec2(rs.l, rs.t), ImVec2(rs.r, rs.b), ImColor(150, 60, 50, 235), true);
		ImVec2 t1 = r->calculateTextSize("OK", fs), t2 = r->calculateTextSize("PADRÃO", fs);
		r->drawText(ImVec2((ok.l + ok.r) * 0.5f - t1.x * 0.5f, (ok.t + ok.b) * 0.5f - t1.y * 0.5f), ImColor(255, 255, 255, 255), "OK", false, fs);
		r->drawText(ImVec2((rs.l + rs.r) * 0.5f - t2.x * 0.5f, (rs.t + rs.b) * 0.5f - t2.y * 0.5f), ImColor(255, 255, 255, 255), "PADRÃO", false, fs);
		const char* msg = "Arraste os controles para a posição desejada";
		const ImVec2 tm = r->calculateTextSize(msg, fs * 0.8f);
		r->drawText(ImVec2(screen.x * 0.5f - tm.x * 0.5f, ok.b + fs * 0.4f), ImColor(255, 255, 255, 255), msg, true, fs * 0.8f);
	}
}
