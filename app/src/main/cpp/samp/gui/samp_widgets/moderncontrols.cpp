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
#include <cstdio>
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
	std::atomic<bool>     g_runLock{ false };   // CORRIDA LOCK ligada: o personagem corre reto sem segurar o joystick
	std::atomic<uint32_t> g_edges{ 0 };
	std::atomic<int64_t>  g_jumpMs{ -1000000 };
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
	constexpr int kJumpCooldownMs = 900;   // toques repetidos dentro deste tempo NAO geram novo pulo (evita o pulo duplo/rolamento)

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

	void Publish()   // chamar com g_mu travado, depois de mudar o controlador
	{
		const Output o = g_ctl.Read(true);
		g_lr.store(o.lr); g_ud.store(o.ud); g_held.store(o.held);
		if (o.pressedEdge) {
			uint32_t edge = o.pressedEdge;
			if (edge & (1u << B_RUNLOCK)) { g_runLock.store(!g_runLock.load()); edge &= ~(1u << B_RUNLOCK); }   // toggle
			if (edge & (1u << B_JUMP))
			{
				const int64_t now = NowMs();
				if (now - g_jumpMs.load() < kJumpCooldownMs) edge &= ~(1u << B_JUMP);   // 2o toque rapido: descartado
				else g_jumpMs.store(now);
			}
			if (edge) g_edges.fetch_or(edge);
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
		g_runLock.store(false);                 // saiu do modo/veiculo/UI: o lock desliga
		EndAimLocked();
		g_ctl.CancelAll();
		Publish();
		g_edges.store(0);
		g_lr.store(0); g_ud.store(0); g_held.store(0);
	}

	bool ConsumeEdge(int b) { const uint32_t bit = 1u << b; return (g_edges.fetch_and(~bit) & bit) != 0; }
}

// ----------------------------------------------------------------------------- ModernInput
bool ModernInput::Active() { return g_active.load(); }

bool ModernInput::Stick(int16_t& lr, int16_t& ud)
{
	if (!g_active.load()) return false;
	lr = (int16_t)g_lr.load(); ud = (int16_t)g_ud.load();
	// CORRIDA LOCK: sem o joystick em uso, empurra o stick para frente (GTA SA: frente = ud negativo); o joystick, se tocado, manda.
	if (g_runLock.load() && lr == 0 && ud == 0) ud = -127;
	return true;
}
bool ModernInput::SprintHeld()    { return g_active.load() && (g_runLock.load() || ((g_held.load() & (1u << B_RUN)) && (g_lr.load() != 0 || g_ud.load() != 0))); }
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

void ModernControls::StartEdit()
{
	if (!ControlMode::IsModern()) return;
	HudLayout huds[kHudCount]; int cur = 0;
	ControlMode::GetHuds(huds, cur);
	{
		std::lock_guard<std::mutex> l(g_mu);
		g_ctl.ApplyConfig(ControlMode::GetConfig());
		g_seenVer = ControlMode::ConfigVersion();
		g_ctl.SetScreen(g_sw > 1.0f ? g_sw : 1920.0f, g_sh > 1.0f ? g_sh : 1080.0f);
		g_ctl.BeginEdit(huds, cur);          // copia de trabalho dos 4 HUDs; so vale ao apertar OK
	}
	g_editing.store(true);
}
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
		// Em edicao NENHUM toque vai para o jogo: o controlador so move/seleciona/ajusta; OK e CANCELAR chegam por editAction.
		const bool mine = g_ctl.OnTouch(type, num, fx, fy);
		(void)mine;
		if (g_ctl.editAction == 1)
		{
			g_ctl.StoreHud();
			HudLayout h[kHudCount]; for (int i = 0; i < kHudCount; i++) h[i] = g_ctl.hudStore[i];
			ControlMode::CommitHuds(h, g_ctl.hud, g_ctl.cfg);          // grava no settings.ini
			g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ControlMode::ConfigVersion();
			g_ctl.editAction = 0; g_ctl.editing = false; g_editing.store(false);
		}
		else if (g_ctl.editAction == 2)
		{
			g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ControlMode::ConfigVersion();   // descarta o rascunho
			g_ctl.editAction = 0; g_ctl.editing = false; g_editing.store(false);
		}
		return true;
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
	if (!show)
	{
		if (editing && !modernNow)      // o editor foi interrompido (saiu do moderno/morreu...): descarta o rascunho
		{
			std::lock_guard<std::mutex> l(g_mu);
			g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ControlMode::ConfigVersion();
			g_ctl.editing = false; g_ctl.editAction = 0;
			g_editing.store(false);
		}
		return;
	}

	ModernController snap;
	{
		std::lock_guard<std::mutex> l(g_mu);
		snap = g_ctl;                                          // copia leve p/ desenhar sem segurar o mutex
	}
	const ModernConfig& c = snap.cfg;
	const ModernLayout& L = snap.lay;
	auto alphaOf = [&](int idx) { return (int)std::lround(255.0f * c.ctlAlpha[idx]); };
	const ImColor kYellow(255, 204, 0, 255);
	auto circle = [&](const CtlCircle& ci, const ImColor& fill, const ImColor& line, float lw) {
		constexpr int N = 40;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(ci.cx + std::cos(a) * ci.r, ci.cy + std::sin(a) * ci.r); }
		r->drawConvexPolyFilled(p, N, fill);
		r->drawPolyline(p, N, line, true, lw);
	};
	auto ring = [&](const CtlCircle& ci, float grow, float lw) {   // contorno amarelo do controle selecionado (so na edicao)
		constexpr int N = 48;
		ImVec2 p[N];
		const float rr = ci.r * grow;
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(ci.cx + std::cos(a) * rr, ci.cy + std::sin(a) * rr); }
		r->drawPolyline(p, N, kYellow, true, lw);
	};

	// joystick: base translucida + centro movel (opacidade propria)
	{
		const int Aj = alphaOf(B_COUNT);
		circle(L.base, ImColor(20, 20, 20, Aj / 2), ImColor(255, 255, 255, Aj), std::max(2.0f, L.base.r * 0.03f));
		const CtlVec k = snap.Knob();
		circle(CtlCircle{ k.x, k.y, L.knobR }, snap.StickDown() ? ImColor(255, 255, 255, std::min(255, Aj + 70)) : ImColor(230, 230, 230, Aj),
		       ImColor(255, 255, 255, std::min(255, Aj + 40)), 2.0f);
		if (editing && snap.sel == B_COUNT) ring(L.base, 1.08f, std::max(3.0f, L.base.r * 0.045f));
	}

	// botoes
	static const char* const kLbl[B_COUNT] = { "CORRER", "PULO", "AGACHAR", "ATAQUE", "MIC", "LOCK" };
	for (int i = 0; i < B_COUNT; i++)
	{
		if (((snap.disabledMask >> i) & 1u)) continue;
		const bool down = (i == B_RUNLOCK) ? g_runLock.load() : snap.BtnDown(i);   // LOCK fica aceso enquanto ligado
		const CtlCircle& b = L.btn[i];
		const int A = alphaOf(i);
		circle(b, down ? ImColor(255, 255, 255, std::min(255, A + 60)) : ImColor(20, 20, 20, A), ImColor(255, 255, 255, std::min(255, A + 50)), std::max(2.0f, b.r * 0.06f));
		const float fs = b.r * (i == B_CROUCH ? 0.34f : 0.42f);
		const ImVec2 ts = r->calculateTextSize(kLbl[i], fs);
		r->drawText(ImVec2(b.cx - ts.x * 0.5f, b.cy - ts.y * 0.5f),
		            down ? ImColor(0, 0, 0, 255) : ImColor(255, 255, 255, std::min(255, A + 80)), kLbl[i], true, fs);
		if (editing && snap.sel == i) ring(b, 1.18f, std::max(3.0f, b.r * 0.08f));
	}

	if (editing)
	{
		const EditPanel P = MakeEditPanel(screen.x, screen.y);
		const float fs = (P.box.b - P.box.t) * 0.085f;
		auto rect = [&](const CtlRect& R, const ImColor& fill, const ImColor& line) {
			r->drawRect(ImVec2(R.l, R.t), ImVec2(R.r, R.b), fill, true);
			r->drawRect(ImVec2(R.l, R.t), ImVec2(R.r, R.b), line, false, 2.0f);
		};
		auto textC = [&](const CtlRect& R, const char* s, const ImColor& col) {
			const ImVec2 ts = r->calculateTextSize(s, fs);
			r->drawText(ImVec2((R.l + R.r) * 0.5f - ts.x * 0.5f, (R.t + R.b) * 0.5f - ts.y * 0.5f), col, s, false, fs);
		};
		const ImColor white(255, 255, 255, 255), btnFill(35, 45, 70, 235), btnLine(120, 150, 210, 255);
		rect(P.box, ImColor(12, 16, 28, 225), btnLine);

		// HUD 1..4
		char buf[48];
		rect(P.hudPrev, btnFill, btnLine); textC(P.hudPrev, "<", white);
		rect(P.hudNext, btnFill, btnLine); textC(P.hudNext, ">", white);
		snprintf(buf, sizeof buf, "HUD %d", snap.hud + 1);
		textC(P.hudLbl, buf, kYellow);

		// Tamanho / Opacidade do controle selecionado (cinza se nada selecionado)
		const bool has = snap.HasSel();
		const ImColor txt = has ? white : ImColor(130, 130, 130, 255);
		auto slider = [&](const char* label, const CtlRect& mnus, const CtlRect& pls, const CtlRect& trk, float t, float value) {
			const ImVec2 ts = r->calculateTextSize(label, fs);
			r->drawText(ImVec2(P.labelX, (trk.t + trk.b) * 0.5f - ts.y * 0.5f), txt, label, false, fs);
			rect(mnus, btnFill, btnLine); textC(mnus, "-", txt);
			rect(pls, btnFill, btnLine); textC(pls, "+", txt);
			const float cy = (trk.t + trk.b) * 0.5f, th = (trk.b - trk.t) * 0.12f;
			r->drawRect(ImVec2(trk.l, cy - th), ImVec2(trk.r, cy + th), ImColor(70, 80, 100, 255), true);
			r->drawRect(ImVec2(trk.l, cy - th), ImVec2(trk.l + (trk.r - trk.l) * t, cy + th), kYellow, true);
			const float kx = trk.l + (trk.r - trk.l) * t, kr = (trk.b - trk.t) * 0.28f;
			circle(CtlCircle{ kx, cy, kr }, has ? kYellow : ImColor(130, 130, 130, 255), ImColor(255, 255, 255, 255), 2.0f);
			snprintf(buf, sizeof buf, "%d%%", (int)std::lround(value * 100.0f));
			const ImVec2 vs = r->calculateTextSize(buf, fs * 0.9f);
			r->drawText(ImVec2((trk.l + trk.r) * 0.5f - vs.x * 0.5f, trk.t - vs.y * 0.05f), txt, buf, true, fs * 0.9f);
		};
		slider("Tamanho", P.sizeM, P.sizeP, P.sizeTrk, ScaleToT(snap.SelScale()), snap.SelScale());
		slider("Opacidade", P.opaM, P.opaP, P.opaTrk, AlphaToT(snap.SelAlpha()), snap.SelAlpha());

		rect(P.cancel, ImColor(150, 60, 50, 235), btnLine); textC(P.cancel, "CANCELAR", white);
		rect(P.reset, btnFill, btnLine);                    textC(P.reset, "PADRÃO", white);
		rect(P.ok, ImColor(40, 140, 70, 235), btnLine);     textC(P.ok, "OK", white);

		const char* msg = "Toque num controle para selecionar e arraste para mover";
		const ImVec2 tm = r->calculateTextSize(msg, fs * 0.8f);
		r->drawText(ImVec2(screen.x * 0.5f - tm.x * 0.5f, P.box.b + fs * 0.4f), ImColor(255, 255, 255, 255), msg, true, fs * 0.8f);
	}
}
