#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "controlmode.h"
#include "maximomenu.h"
#include "controlsselector.h"
#include "../../game/Plugins/RpAnimBlendPlugin/RpAnimBlend.h"

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
	std::atomic<int>      g_soundReq{ 0 };      // pedido de som do editor (1 toque, 2 voltar/recolher); tocado na thread do jogo
	std::atomic<bool>     g_brake{ false };     // personagem na animacao de freada (RUN_STOP/RUN_STOPR); escrito na thread do jogo, lido no desenho
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

// Som do editor: o pedido nasce na thread de toque e e tocado aqui, na thread do jogo (CTouchInterface::Update), com os mesmos
// sons de interface ja usados pelo cliente na selecao de classe (1052 / 1053, via CGame::PlaySound).
void ModernControlsSoundTick()
{
	const int req = g_soundReq.exchange(0);
	if (!req || !pGame) return;
	CPedGTA* ped = nullptr;
	if (!LocalPed(ped)) return;
	const RwMatrix mat = ped->GetMatrix().ToRwMatrix();
	pGame->PlaySound(req == 2 ? 1053 : 1052, mat.pos.x, mat.pos.y, mat.pos.z);
}

// Thread do jogo (depois do Update nativo). Detecta a animacao de freada do ped local lendo as associacoes
// da propria clump (RpAnimBlendClumpGetAssociation por AnimationId). So publica um bool; o fade e feito no desenho.
void ModernControlsBrakeTick()
{
	static int64_t s_since = -1;
	bool b = false;
	CPedGTA* ped = nullptr;
	if (g_active.load() && LocalPed(ped) && ped->m_pRwClump)
	{
		const uint32_t ids[2] = { (uint32_t)ANIM_ID_RUN_STOP, (uint32_t)ANIM_ID_RUN_STOPR };
		for (uint32_t id : ids)
		{
			CAnimBlendAssociation* a = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, id);
			if (a && a->IsRunning() && a->m_fBlendAmount > 0.05f) { b = true; break; }
		}
	}
	const int64_t now = NowMs();
	if (!b) s_since = -1;
	else { if (s_since < 0) s_since = now; if (now - s_since > 1500) b = false; }   // trava de seguranca: nunca esconde por mais de 1,5 s
	g_brake.store(b);
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
		if (g_ctl.soundReq) { g_soundReq.store(g_ctl.soundReq); g_ctl.soundReq = 0; }
		if (g_ctl.editAction == 1 || g_ctl.editAction == 3)           // salvar (e sair / e continuar)
		{
			g_ctl.StoreHud();
			HudLayout h[kHudCount]; for (int i = 0; i < kHudCount; i++) h[i] = g_ctl.hudStore[i];
			ControlMode::CommitHuds(h, g_ctl.hud, g_ctl.cfg);          // grava no settings.ini
			g_seenVer = ControlMode::ConfigVersion();
			if (g_ctl.editAction == 1)
			{
				g_ctl.ApplyConfig(ControlMode::GetConfig());
				g_ctl.editAction = 0; g_ctl.editing = false; g_editing.store(false);
			}
			else { g_ctl.MarkSaved(); g_ctl.editAction = 0; }
		}
		else if (g_ctl.editAction == 2)
		{
			g_ctl.ApplyConfig(ControlMode::GetConfig()); g_seenVer = ControlMode::ConfigVersion();   // descarta o rascunho
			g_ctl.editAction = 0; g_ctl.editing = false; g_editing.store(false);
		}
		return true;
	}

	if (g_bTouchCancel) { ReleaseAllLocked(); return false; }
	// Freada: botoes e joystick escondidos NAO aceitam toque novo. Dedos ja presos continuam (move/soltar) para nada ficar preso.
	if (type == 2 && g_brake.load()) return false;
	const int atkBefore = g_ctl.BtnFinger(B_ATTACK);
	const bool mine = g_ctl.OnTouch(type, num, fx, fy);
	Publish();

	// Mira arrastavel (opcional): o dedo do Ataque tambem arrasta a camera/mira (deslocamento relativo, ver AimDrag)
	{
		AimEv ev[4];
		if (type == 2 && g_aimFinger < 0 && g_ctl.BtnFinger(B_ATTACK) == num && atkBefore != num)   // sempre: o toque sintetico faz o anel (CWidgetThumbCircle) aparecer como indicador da mira; so ARRASTA se aimDrag
		{
			g_aim.sens = g_ctl.cfg.aimSens; g_aim.smooth = g_ctl.cfg.aimSmooth; g_aim.SetScreen(g_sw, g_sh);
			g_aimFinger = num;
			QueueAim(ev, g_aim.Begin(fx, fy, ev), num);
		}
		else if (type == 3 && g_aimFinger == num && g_ctl.cfg.aimDrag)
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
		// CORRIDA LOCK ligado: o botao CORRER some (nao desenha nem recebe toque); no editor de HUD ele continua visivel.
		g_ctl.disabledMask = (!editing && g_runLock.load()) ? (1u << B_RUN) : 0u;
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
	// fade dos botoes durante a freada (o toque tambem fica desativado, ver onRawTouch). Descida ~120 ms, volta ~150 ms.
	static float s_fm = 1.0f; static int64_t s_fmMs = 0;
	{
		const int64_t nowMs = NowMs();
		float dt = s_fmMs ? (float)(nowMs - s_fmMs) / 1000.0f : 0.0f;
		s_fmMs = nowMs;
		dt = std::clamp(dt, 0.0f, 0.1f);
		const bool hide = !editing && g_brake.load();
		s_fm = hide ? std::max(0.0f, s_fm - dt / 0.12f) : std::min(1.0f, s_fm + dt / 0.15f);
		if (editing) s_fm = 1.0f;
	}
	const float fm = s_fm;
	auto Cf = [&](int R, int G, int B, int A) { return ImColor(R, G, B, (int)std::lround((float)A * fm)); };
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
		circle(L.base, Cf(20, 20, 20, Aj / 2), Cf(255, 255, 255, Aj), std::max(2.0f, L.base.r * 0.03f));
		const CtlVec k = snap.Knob();
		circle(CtlCircle{ k.x, k.y, L.knobR }, snap.StickDown() ? Cf(255, 255, 255, std::min(255, Aj + 70)) : Cf(230, 230, 230, Aj),
		       Cf(255, 255, 255, std::min(255, Aj + 40)), 2.0f);
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
		circle(b, down ? Cf(255, 255, 255, std::min(255, A + 60)) : Cf(20, 20, 20, A), Cf(255, 255, 255, std::min(255, A + 50)), std::max(2.0f, b.r * 0.06f));
		const float fs = b.r * (i == B_CROUCH ? 0.34f : 0.42f);
		const ImVec2 ts = r->calculateTextSize(kLbl[i], fs);
		r->drawText(ImVec2(b.cx - ts.x * 0.5f, b.cy - ts.y * 0.5f),
		            down ? Cf(0, 0, 0, 255) : Cf(255, 255, 255, std::min(255, A + 80)), kLbl[i], fm > 0.99f, fs);
		if (editing && snap.sel == i) ring(b, 1.18f, std::max(3.0f, b.r * 0.08f));
	}

	if (editing)
	{
		const EditPanel P = snap.Panel();
		const float fs = screen.y * 0.030f;
		const ImColor white(255, 255, 255, 255), black(0, 0, 0, 255), gray(205, 205, 205, 255), dark(18, 18, 18, 255);
		char buf[48];
		auto fillR = [&](const CtlRect& R, const ImColor& col) { r->drawRect(ImVec2(R.l, R.t), ImVec2(R.r, R.b), col, true); };
		auto textAt = [&](float x, float y, const char* s, const ImColor& col, float f) { r->drawText(ImVec2(x, y), col, s, false, f); };
		auto textC = [&](const CtlRect& R, const char* s, const ImColor& col, float f) {
			const ImVec2 ts = r->calculateTextSize(s, f);
			r->drawText(ImVec2((R.l + R.r) * 0.5f - ts.x * 0.5f, (R.t + R.b) * 0.5f - ts.y * 0.5f), col, s, false, f);
		};
		auto line = [&](float x0, float y0, float x1, float y1, const ImColor& col, float lw) { r->drawLine(ImVec2(x0, y0), ImVec2(x1, y1), col, lw); };
		auto disc = [&](float cx, float cy, float rad, const ImColor& col) {
			constexpr int N = 20; ImVec2 p[N];
			for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(cx + std::cos(a) * rad, cy + std::sin(a) * rad); }
			r->drawConvexPolyFilled(p, N, col);
		};
		auto tri = [&](const CtlRect& R, int dir, const ImColor& col) {     // 0 esq, 1 dir, 2 cima, 3 baixo
			const float cx = (R.l + R.r) * 0.5f, cy = (R.t + R.b) * 0.5f, u = std::min(R.r - R.l, R.b - R.t) * 0.22f;
			if (dir == 0)      r->drawTriangle(ImVec2(cx - u, cy), ImVec2(cx + u, cy - u * 1.3f), ImVec2(cx + u, cy + u * 1.3f), col, true);
			else if (dir == 1) r->drawTriangle(ImVec2(cx + u, cy), ImVec2(cx - u, cy - u * 1.3f), ImVec2(cx - u, cy + u * 1.3f), col, true);
			else if (dir == 2) r->drawTriangle(ImVec2(cx, cy - u), ImVec2(cx - u * 1.3f, cy + u), ImVec2(cx + u * 1.3f, cy + u), col, true);
			else               r->drawTriangle(ImVec2(cx, cy + u), ImVec2(cx - u * 1.3f, cy - u), ImVec2(cx + u * 1.3f, cy - u), col, true);
		};

		// fundo do painel (semi-transparente, sem moldura, como na referencia) + aba de recolher
		fillR(P.box, ImColor(0, 0, 0, 215));
		r->drawRect(ImVec2(P.tab.l, P.tab.t), ImVec2(P.tab.r, P.tab.b), ImColor(0, 0, 0, 215), true);
		{
			const float cx = (P.tab.l + P.tab.r) * 0.5f, cy = (P.tab.t + P.tab.b) * 0.5f, u = (P.tab.b - P.tab.t) * 0.22f;
			const float s = P.collapsed ? 1.0f : -1.0f;                       // v = abrir, ^ = recolher
			line(cx - u * 1.6f, cy - s * u * 0.7f, cx, cy + s * u * 0.7f, white, 2.5f);
			line(cx, cy + s * u * 0.7f, cx + u * 1.6f, cy - s * u * 0.7f, white, 2.5f);
		}

		// linha 1: [sair] [<] HUD n [>] [nuvem = salvar e continuar]
		auto iconBtn = [&](const CtlRect& R, const ImColor& bg) { fillR(R, bg); };
		iconBtn(P.exit, gray); iconBtn(P.cloud, gray);
		iconBtn(P.hudPrev, dark); iconBtn(P.hudLbl, black); iconBtn(P.hudNext, dark);
		tri(P.hudPrev, 0, white); tri(P.hudNext, 1, white);
		snprintf(buf, sizeof buf, "HUD %d", snap.hud + 1);
		textC(P.hudLbl, buf, white, fs * 1.15f);
		{   // sair: porta aberta + seta entrando
			const float cx = (P.exit.l + P.exit.r) * 0.5f, cy = (P.exit.t + P.exit.b) * 0.5f, u = (P.exit.b - P.exit.t) * 0.30f;
			ImVec2 d[4] = { ImVec2(cx - u * 0.1f, cy - u), ImVec2(cx + u, cy - u), ImVec2(cx + u, cy + u), ImVec2(cx - u * 0.1f, cy + u) };
			r->drawPolyline(d, 4, black, false, 2.5f);
			line(cx - u, cy, cx + u * 0.4f, cy, black, 2.5f); line(cx - u, cy, cx - u * 0.4f, cy - u * 0.45f, black, 2.5f); line(cx - u, cy, cx - u * 0.4f, cy + u * 0.45f, black, 2.5f);
		}
		{   // nuvem
			const float cx = (P.cloud.l + P.cloud.r) * 0.5f, cy = (P.cloud.t + P.cloud.b) * 0.5f, u = (P.cloud.b - P.cloud.t) * 0.30f;
			disc(cx - u * 0.55f, cy + u * 0.1f, u * 0.5f, black); disc(cx + u * 0.05f, cy - u * 0.2f, u * 0.62f, black); disc(cx + u * 0.65f, cy + u * 0.1f, u * 0.5f, black);
			r->drawRect(ImVec2(cx - u * 0.55f, cy + u * 0.1f), ImVec2(cx + u * 0.65f, cy + u * 0.6f), black, true);
		}

		if (!P.collapsed)
		{
			// linha 2: [compartilhar = copiar HUD para todos] [restaurar] [salvar]
			iconBtn(P.share, gray); iconBtn(P.reset, gray);
			iconBtn(P.save, snap.dirty ? ImColor(255, 190, 0, 255) : ImColor(60, 60, 60, 255));
			{
				const float cx = (P.share.l + P.share.r) * 0.5f, cy = (P.share.t + P.share.b) * 0.5f, u = (P.share.b - P.share.t) * 0.28f;
				line(cx - u * 0.7f, cy, cx + u * 0.7f, cy - u * 0.6f, black, 2.5f); line(cx - u * 0.7f, cy, cx + u * 0.7f, cy + u * 0.6f, black, 2.5f);
				disc(cx - u * 0.7f, cy, u * 0.34f, black); disc(cx + u * 0.7f, cy - u * 0.6f, u * 0.34f, black); disc(cx + u * 0.7f, cy + u * 0.6f, u * 0.34f, black);
			}
			{
				const float cx = (P.reset.l + P.reset.r) * 0.5f, cy = (P.reset.t + P.reset.b) * 0.5f, u = (P.reset.b - P.reset.t) * 0.30f;
				constexpr int N = 24; ImVec2 p[N];
				for (int i = 0; i < N; i++) { const float a = (0.5f + 5.2f * (float)i / (float)(N - 1)); p[i] = ImVec2(cx + std::cos(a) * u, cy + std::sin(a) * u); }
				r->drawPolyline(p, N, black, false, 3.0f);
				r->drawTriangle(ImVec2(p[N - 1].x + u * 0.45f, p[N - 1].y - u * 0.05f), ImVec2(p[N - 1].x - u * 0.15f, p[N - 1].y - u * 0.5f), ImVec2(p[N - 1].x - u * 0.15f, p[N - 1].y + u * 0.35f), black, true);
			}
			{
				const ImColor ic = snap.dirty ? black : ImColor(110, 110, 110, 255);
				const float cx = (P.save.l + P.save.r) * 0.5f, cy = (P.save.t + P.save.b) * 0.5f, u = (P.save.b - P.save.t) * 0.30f;
				r->drawRect(ImVec2(cx - u, cy - u), ImVec2(cx + u, cy + u), ic, false, 2.5f);
				r->drawRect(ImVec2(cx - u * 0.5f, cy - u), ImVec2(cx + u * 0.5f, cy - u * 0.35f), ic, false, 2.0f);
				r->drawRect(ImVec2(cx - u * 0.6f, cy + u * 0.1f), ImVec2(cx + u * 0.6f, cy + u), ic, false, 2.0f);
			}

			// Ajuste Inteligente
			r->drawRect(ImVec2(P.chk.l, P.chk.t), ImVec2(P.chk.r, P.chk.b), white, false, 2.0f);
			if (snap.smart)
			{
				fillR(CtlRect{ P.chk.l + 3, P.chk.t + 3, P.chk.r - 3, P.chk.b - 3 }, ImColor(255, 190, 0, 255));
			}
			textAt(P.chk.r + fs * 0.5f, (P.chkRow.t + P.chkRow.b) * 0.5f - fs * 0.55f, "Ajuste Inteligente", white, fs);
			textAt(P.hint.l, (P.hint.t + P.hint.b) * 0.5f - fs * 0.4f, "Ajustar ao alinhar com outros controles", ImColor(200, 200, 200, 255), fs * 0.8f);

			// Opacidade / Tamanho do controle selecionado
			const bool has = snap.HasSel();
			const ImColor txt = has ? white : ImColor(120, 120, 120, 255);
			auto slider = [&](const char* label, const CtlRect& lbl, const CtlRect& trk, float t, float value) {
				textAt(lbl.l, (lbl.t + lbl.b) * 0.5f - fs * 0.55f, label, txt, fs);
				snprintf(buf, sizeof buf, "%d%%", (int)std::lround(value * 100.0f));
				const ImVec2 vs = r->calculateTextSize(buf, fs * 0.9f);
				textAt(trk.r - vs.x, (lbl.t + lbl.b) * 0.5f - fs * 0.5f, buf, txt, fs * 0.9f);
				const float cy = (trk.t + trk.b) * 0.5f, th = std::max(2.0f, (trk.b - trk.t) * 0.18f);
				fillR(CtlRect{ trk.l, cy - th, trk.r, cy + th }, ImColor(20, 20, 20, 255));
				fillR(CtlRect{ trk.l, cy - th, trk.l + (trk.r - trk.l) * t, cy + th }, has ? ImColor(255, 190, 0, 255) : ImColor(90, 90, 90, 255));
				const float kx = trk.l + (trk.r - trk.l) * t, kw = std::max(3.0f, (trk.b - trk.t) * 0.22f);
				fillR(CtlRect{ kx - kw, cy - (trk.b - trk.t) * 0.45f, kx + kw, cy + (trk.b - trk.t) * 0.45f }, ImColor(235, 235, 235, 255));
			};
			slider("Opacidade", P.opaLbl, P.opaTrk, AlphaToT(snap.SelAlpha()), snap.SelAlpha());
			slider("Tamanho", P.sizeLbl, P.sizeTrk, ScaleToT(snap.SelScale()), snap.SelScale());

			// setas de posicao
			fillR(P.dpL, black); fillR(P.dpR, black); fillR(P.dpU, black); fillR(P.dpD, black);
			tri(P.dpL, 0, white); tri(P.dpR, 1, white); tri(P.dpU, 2, white); tri(P.dpD, 3, white);
		}
	}
}
