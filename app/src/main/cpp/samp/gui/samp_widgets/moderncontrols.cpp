#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "controlmode.h"
#include "maximomenu.h"
#include "controlsselector.h"
#include "inventory.h"
#include "../../game/Plugins/RpAnimBlendPlugin/RpAnimBlend.h"
#include "../../game/RW/RenderWare.h"
#include "../../vendor/stb/stb_image.h"   // implementacao (STB_IMAGE_IMPLEMENTATION) esta em game/sprite2d.cpp
#include "ctlicons.h"
#include "passengerbutton_logic.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

using namespace CtlLogic;

extern CNetGame* pNetGame;
extern bool bNeedEnterVehicle;         // buttonpanel.cpp: P2 (consumido por CLocalPlayer::HandlePassengerEntry)
extern bool bNeedEnterAsDriver;        // net/localplayer.cpp: P1 (consumido por CLocalPlayer::HandleDriverEntry)
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
	std::atomic<bool>     g_micVeh{ false };   // dentro de veiculo (modo moderno): so o botao do MICROFONE aparece e recebe toque
	std::atomic<bool>     g_editing{ false };
	std::atomic<int>      g_lr{ 0 }, g_ud{ 0 };
	std::atomic<uint32_t> g_held{ 0 };
	std::atomic<int>      g_soundReq{ 0 };      // pedido de som do editor (1 toque, 2 voltar/recolher); tocado na thread do jogo
	std::atomic<bool>     g_busy{ false };      // personagem pulando/no ar ou segurando na parede (ver ModernControlsBrakeTick)
	std::atomic<bool>     g_busyJumpUsed{ false };   // o unico pulo permitido neste estado ja foi usado
	std::atomic<bool>     g_brake{ false };     // personagem na animacao de freada (RUN_STOP/RUN_STOPR); escrito na thread do jogo, lido no desenho
	std::atomic<bool>     g_aimLock{ false };   // MIRAR travada (botao MIRAR alterna); atirar ou sair do modo desliga
	std::atomic<int64_t>  g_enterMs{ -1000000 };   // ultimo P1/P2 aceito (recarga)
	std::atomic<bool>     g_runLock{ false };   // CORRIDA LOCK ligada: o personagem corre reto sem segurar o joystick
	std::atomic<uint32_t> g_edges{ 0 };
	std::atomic<bool>     g_invReq{ false };   // B_INV tocado (tratado na thread de desenho)
	std::atomic<int>      g_slotReq{ -1 };     // B_SLOTS tocado: casa 0..kHotbarN-1 (tratado na thread de desenho)
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

	// ---- icones dos botoes (atlas embutido, mesmo caminho de textura da HUD) ----
	RwRaster* s_icoRaster = nullptr;
	bool      s_icoFailed = false;

	RwRaster* CreateIconRaster()
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(CtlIcons::kPng, (int)CtlIcons::kPngSize, &w, &h, &ch, 4);
		if (!px) { FLog("[CTLICONS] stbi falhou: %s", stbi_failure_reason()); return nullptr; }
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage)
		{
			FLog("[CTLICONS] funcoes RenderWare indisponiveis");
			stbi_image_free(px);
			return nullptr;
		}
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image)) { if (image) RwImageDestroy(image); stbi_image_free(px); return nullptr; }
		for (int y = 0; y < h; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		stbi_image_free(px);

		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888;
			rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image))
		{
			if (RwRasterDestroy) RwRasterDestroy(raster);
			RwImageDestroy(image);
			return nullptr;
		}
		RwImageDestroy(image);
		FLog("[CTLICONS] atlas %dx%d criado", w, h);
		return raster;
	}

	RwRaster* IconRaster()
	{
		if (s_icoRaster) return s_icoRaster;
		if (s_icoFailed) return nullptr;
		s_icoRaster = CreateIconRaster();
		if (!s_icoRaster) s_icoFailed = true;     // nao tenta todo quadro: os botoes voltam a mostrar o texto
		return s_icoRaster;
	}

	// Arma na mao que bate (maos 0 e corpo a corpo 1-15) -> icone de soco; qualquer outra -> icone de tiro.
	bool PunchWeapon(CPedGTA* ped)
	{
		if (!ped) return true;
		const int slot = (int)ped->m_nActiveWeaponSlot;
		if (slot < 0 || slot >= 13) return true;
		const int t = (int)ped->m_aWeapons[slot].dwType;
		return t >= 0 && t <= 15;
	}

	// MIRAR so aparece com arma que mira. Sem mira (botao escondido): maos (0), corpo a corpo (1-15), arremesso (16-18),
	// camera (43), oculos (44/45) e paraquedas (46). Mesma leitura de arma ativa usada em pad.cpp (IsFirearm).
	bool AimAllowedFor(CPedGTA* ped)
	{
		if (!ped) return true;
		const int slot = (int)ped->m_nActiveWeaponSlot;
		if (slot < 0 || slot >= 13) return false;
		const int t = (int)ped->m_aWeapons[slot].dwType;
		if (t >= 0 && t <= 18) return false;
		if (t >= 43 && t <= 46) return false;
		return true;
	}

	void Publish()   // chamar com g_mu travado, depois de mudar o controlador
	{
		const Output o = g_ctl.Read(true);
		g_lr.store(o.lr); g_ud.store(o.ud); g_held.store(o.held);
		if (o.pressedEdge) {
			uint32_t edge = o.pressedEdge;
			if (edge & (1u << B_AIM)) { g_aimLock.store(!g_aimLock.load()); edge &= ~(1u << B_AIM); }   // toggle: 1 toque trava, outro solta
			if (edge & (1u << B_ATTACK)) g_aimLock.store(false);                                     // atirou: a mira travada desativa
			if (edge & (1u << B_RUNLOCK)) { g_runLock.store(!g_runLock.load()); edge &= ~(1u << B_RUNLOCK); }   // toggle
			if (edge & ((1u << B_P1) | (1u << B_P2)))                                                // entrar no veiculo: 1 pedido por toque, com recarga
			{
				const int64_t now = NowMs();
				if (now - g_enterMs.load() >= PassengerLogic::kCooldownMs && !bNeedEnterVehicle && !bNeedEnterAsDriver)
				{
					g_enterMs.store(now);
					if (edge & (1u << B_P1)) bNeedEnterAsDriver = true; else bNeedEnterVehicle = true;
				}
				edge &= ~((1u << B_P1) | (1u << B_P2));
			}
			if (edge & (1u << B_INV))   { g_invReq.store(true); edge &= ~(1u << B_INV); }                                   // inventario: aberto pelo servidor (/inv)
			if (edge & (1u << B_SLOTS)) { g_slotReq.store(g_ctl.slotPressed); edge &= ~(1u << B_SLOTS); }                    // slot rapido: usa o item
			if (edge & (1u << B_JUMP))
			{
				const int64_t now = NowMs();
				if (now - g_jumpMs.load() < kJumpCooldownMs) edge &= ~(1u << B_JUMP);   // 2o toque rapido: descartado
				else g_jumpMs.store(now);
			}
			// pulando/na parede: so UM pulo por estado (o 2o e descartado)
			if ((edge & (1u << B_JUMP)) && g_busy.load() && g_busyJumpUsed.exchange(true)) edge &= ~(1u << B_JUMP);
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
		g_aimLock.store(false);
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
bool ModernInput::AimWanted()      { return g_active.load() && ((g_held.load() & (1u << B_ATTACK)) || g_aimLock.load()); }   // Ataque segurado OU mira travada
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
	static int64_t s_busySince = -1, s_busySeen = -1;
	static bool    s_busyPrev = false, s_busyCapped = false;
	bool b = false, air = false;
	CPedGTA* ped = nullptr;
	if (g_active.load() && LocalPed(ped) && ped->m_pRwClump)
	{
		auto has = [&](uint32_t id) {
			CAnimBlendAssociation* a = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, id);
			return a && a->IsRunning() && a->m_fBlendAmount > 0.05f;
		};
		const uint32_t ids[2] = { (uint32_t)ANIM_ID_RUN_STOP, (uint32_t)ANIM_ID_RUN_STOPR };
		for (uint32_t id : ids) if (has(id)) { b = true; break; }
		// pulando / no ar / segurando na parede (escalada): mesma leitura das associacoes de animacao
		const uint32_t airIds[] = { (uint32_t)ANIM_ID_JUMP_LAUNCH, (uint32_t)ANIM_ID_JUMP_LAUNCH_R, (uint32_t)ANIM_ID_JUMP_GLIDE,
		                            (uint32_t)ANIM_ID_FALL_FALL, (uint32_t)ANIM_ID_FALL_GLIDE,
		                            (uint32_t)ANIM_ID_CLIMB_JUMP, (uint32_t)ANIM_ID_CLIMB_IDLE, (uint32_t)ANIM_ID_CLIMB_PULL,
		                            (uint32_t)ANIM_ID_CLIMB_STAND, (uint32_t)ANIM_ID_CLIMB_JUMP_B, (uint32_t)ANIM_ID_CLIMB_JUMP2FALL };
		for (uint32_t id : airIds) if (has(id)) { air = true; break; }
	}
	const int64_t now = NowMs();
	if (!b) s_since = -1;
	else { if (s_since < 0) s_since = now; if (now - s_since > 1500) b = false; }   // trava de seguranca: nunca esconde por mais de 1,5 s
	g_brake.store(b);

	// estado "ocupado" (pulando/na parede): segura 150 ms depois da ultima leitura para nao piscar entre animacoes
	if (!air) s_busyCapped = false;
	if (air) s_busySeen = now;
	bool busy = !s_busyCapped && (air || (s_busySeen > 0 && now - s_busySeen < 150));
	if (busy) { if (s_busySince < 0) s_busySince = now; if (now - s_busySince > 8000) { busy = false; s_busyCapped = true; } }   // trava: no maximo 8 s
	else s_busySince = -1;
	if (busy && !s_busyPrev) { g_runLock.store(false); g_aimLock.store(false); g_busyJumpUsed.store(false); }   // entrou: LOCK desliga e o unico pulo volta a valer
	if (!busy) g_busyJumpUsed.store(false);
	s_busyPrev = busy;
	g_busy.store(busy);
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
	if (!g_active.load() && !g_micVeh.load() && !editing) return false;
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
	if (type == 2 && g_brake.load() && !g_micVeh.load()) return false;
	if (g_micVeh.load() && !g_active.load())
	{
		// veiculo: so o dedo que comeca no MICROFONE (ou que ja o segura) e nosso; todo o resto vai para o jogo (direcao, freio...)
		if (g_ctl.BtnFinger(B_MIC) != num)
		{
			const CtlCircle& mb = g_ctl.lay.btn[B_MIC];
			const float dx = fx - mb.cx, dy = fy - mb.cy;
			if (type != 2 || dx * dx + dy * dy > mb.r * mb.r * 1.44f) return false;
		}
	}
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
	// Dentro de veiculo o HUD moderno some (volta o nativo), mas o MICROFONE (VOIP) continua aparecendo e funcionando.
	const bool micVeh = !editing && !show && modernNow && vehicle && alive && !UiBlocked();

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
		// MIRAR: escondido (sem desenhar nem receber toque) quando a arma atual nao mira; no editor de HUD continua visivel.
		if (!editing && pedOk && !AimAllowedFor(ped)) { g_ctl.disabledMask |= (1u << B_AIM); g_aimLock.store(false); }
		if (!editing && g_runLock.load()) { g_ctl.disabledMask |= (1u << B_AIM); g_aimLock.store(false); }   // Lock (corrida) ativo: o botao MIRAR some (nao desenha nem recebe toque)
		// Mira ativa (MIRAR travada, ou ATAQUE segurado com arma de fogo): o botao CORRER tambem some (nao desenha nem recebe toque); ao sair da mira volta.
		if (!editing && (g_aimLock.load() || (((g_held.load() >> B_ATTACK) & 1u) && pedOk && !PunchWeapon(ped)))) g_ctl.disabledMask |= (1u << B_RUN);
		// P1/P2 (entrar como motorista/passageiro): so a pe, vivo, sem UI e com um veiculo do SA-MP a ate kShowDist metros; no editor aparecem sempre.
		if (!editing)
		{
			bool showEnter = false;
			if (pedOk && pNetGame && pNetGame->GetVehiclePool())
			{
				CVehiclePool* vp = pNetGame->GetVehiclePool();
				CVehicle* v = vp->GetAt(vp->FindNearestToLocalPlayerPed());
				const float dist = (v && v->m_pVehicle) ? v->m_pVehicle->GetDistanceFromLocalPlayerPed() : -1.0f;
				const bool busyEnter = ped->IsEnteringCar() || ped->IsExitingVehicle();
				showEnter = PassengerLogic::ShouldShow(true, UiBlocked(), vehicle, alive, dist, busyEnter);
			}
			if (!showEnter) g_ctl.disabledMask |= (1u << B_P1) | (1u << B_P2);
		}
		if (!editing) { if (!g_ctl.cfg.showInv) g_ctl.disabledMask |= (1u << B_INV); if (!g_ctl.cfg.showSlots) g_ctl.disabledMask |= (1u << B_SLOTS); }   // desligados nos ajustes (no editor continuam visiveis)
		// pulando/na parede: bloqueia TODOS os botoes, menos o PULO (que vale uma vez so); o joystick segue livre
		g_ctl.blockedMask = 0u;
		if (!editing && g_busy.load())
		{
			g_ctl.blockedMask = ((1u << B_COUNT) - 1u) & ~(1u << B_JUMP);
			if (g_busyJumpUsed.load()) g_ctl.blockedMask |= (1u << B_JUMP);
		}
		if (micVeh)    // veiculo: so o MIC existe (nao desenha nem recebe toque os outros)
		{
			g_ctl.disabledMask = ((1u << B_COUNT) - 1u) & ~(1u << B_MIC);
			g_ctl.blockedMask = 0u;
		}
		wasActive = g_active.load();
		const bool wasMic = g_micVeh.load();
		if ((wasActive && !show) || (wasMic && !micVeh && !show)) ReleaseAllLocked();          // saiu do modo/entrou em veiculo/abriu UI: solta tudo
		if (!editing && g_editing.load() == false) g_ctl.editing = false;
	}
	g_active.store(show && !editing);
	if (!editing)   // inventario / slots rapidos: as acoes saem na thread de desenho (nao na de toque)
	{
		if (g_invReq.exchange(false)) InventoryUI::RequestOpen();
		const int sr = g_slotReq.exchange(-1);
		if (sr >= 0 && show) InventoryUI::HotbarUse(sr);
	}
	else { g_invReq.store(false); g_slotReq.store(-1); }
	g_micVeh.store(micVeh);
	if (!show && !micVeh)
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
		const bool hide = !editing && !micVeh && g_brake.load();
		s_fm = hide ? std::max(0.0f, s_fm - dt / 0.12f) : std::min(1.0f, s_fm + dt / 0.15f);
		if (editing) s_fm = 1.0f;
	}
	const float fm = s_fm;
	auto Cf = [&](int R, int G, int B, int A) { return ImColor(R, G, B, (int)std::lround((float)A * fm)); };
	const ImColor kYellow(30, 144, 255, 255);       // (nome antigo) agora AZUL: contorno do controle selecionado
	auto circle = [&](const CtlCircle& ci, const ImColor& fill, const ImColor& line, float lw) {
		constexpr int N = 40;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(ci.cx + std::cos(a) * ci.r, ci.cy + std::sin(a) * ci.r); }
		r->drawConvexPolyFilled(p, N, fill);
		r->drawPolyline(p, N, line, true, lw);
	};
	auto ring = [&](const CtlCircle& ci, float grow, float lw) {   // contorno azul do controle selecionado (so na edicao)
		constexpr int N = 48;
		ImVec2 p[N];
		const float rr = ci.r * grow;
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(ci.cx + std::cos(a) * rr, ci.cy + std::sin(a) * rr); }
		r->drawPolyline(p, N, kYellow, true, lw);
	};

	// joystick: base translucida + centro movel (opacidade propria)
	if (!micVeh)
	{
		const int Aj = alphaOf(B_COUNT);
		circle(L.base, Cf(20, 20, 20, Aj / 2), Cf(255, 255, 255, Aj), std::max(2.0f, L.base.r * 0.03f));
		const CtlVec k = snap.Knob();
		circle(CtlCircle{ k.x, k.y, L.knobR }, snap.StickDown() ? Cf(255, 255, 255, std::min(255, Aj + 70)) : Cf(230, 230, 230, Aj),
		       Cf(255, 255, 255, std::min(255, Aj + 40)), 2.0f);
		if (editing && snap.sel == B_COUNT) ring(L.base, 1.08f, std::max(3.0f, L.base.r * 0.045f));
	}

	// botoes
	static const char* const kLbl[B_COUNT] = { "CORRER", "PULO", "AGACHAR", "ATAQUE", "MIC", "LOCK", "MIRAR", "P1", "P2", "INV", "SLOTS" };
	for (int i = 0; i < B_COUNT; i++)
	{
		if (((snap.disabledMask >> i) & 1u)) continue;
		const bool down = (i == B_RUNLOCK) ? g_runLock.load() : (i == B_AIM) ? g_aimLock.load() : snap.BtnDown(i);   // LOCK/MIRAR ficam acesos enquanto ligados
		const CtlCircle& b = L.btn[i];
		const int A = alphaOf(i);
		const float bk = (((snap.blockedMask >> i) & 1u) != 0) ? 0.3f : 1.0f;      // bloqueado (pulando/na parede): esmaecido
		auto Cb = [&](int R, int G, int B2, int A2) { return ImColor(R, G, B2, (int)std::lround((float)A2 * fm * bk)); };
		if (i == B_SLOTS)       // barra de slots rapidos: casas arredondadas com os primeiros slots do inventario do servidor
		{
			const float side = HotbarSide(b), pitch = HotbarPitch(b), left = HotbarLeft(b), top = b.cy - b.r;
			const float rnd = side * 0.18f, lw = std::max(2.0f, side * 0.04f);
			for (int k = 0; k < kHotbarN; k++)
			{
				const ImVec2 a0(left + (float)k * pitch, top), a1(a0.x + side, a0.y + side);
				const bool dn = snap.BtnDown(B_SLOTS) && snap.slotPressed == k;
				r->drawRoundedRect(a0, a1, dn ? Cb(40, 110, 230, std::min(255, A + 40)) : Cb(10, 16, 30, A), rnd, true);
				r->drawRoundedRect(a0, a1, Cb(30, 144, 255, std::min(255, A + 90)), rnd, false, lw);
				int icon = 8, qty = 0, hfl = 0;
				if (InventoryUI::HotbarGet(k, icon, qty, &hfl))
				{
					const float m = side * 0.14f;
					const float fade = (qty <= 0 && !(hfl & 1)) ? 0.35f : 1.0f;      // item/municao em falta: apagado
					if (hfl & 1) r->drawRoundedRect(a0, a1, (hfl & 4) ? Cb(255, 210, 70, 240) : Cb(90, 210, 120, 230), rnd, false, std::max(2.0f, lw * 1.6f));   // equipada: verde = no corpo, amarelo = na mao
					InventoryUI::DrawHotbarIcon(r, icon, ImVec2(a0.x + m, a0.y + m), ImVec2(a1.x - m, a1.y - m), (int)std::lround((float)std::min(255, A + 170) * fm * bk * fade));
					if (qty > 1 || ((hfl & 1) && qty <= 0))
					{
						char q[12]; snprintf(q, sizeof q, "x%d", qty);
						const float fq = side * 0.22f; const ImVec2 ts = r->calculateTextSize(q, fq);
						r->drawText(ImVec2(a1.x - ts.x - side * 0.07f, a1.y - ts.y - side * 0.05f), Cb(255, 255, 255, std::min(255, A + 150)), q, true, fq);
					}
				}
				char nb[4]; snprintf(nb, sizeof nb, "%d", k + 1);
				r->drawText(ImVec2(a0.x + side * 0.09f, a0.y + side * 0.04f), Cb(255, 255, 255, std::min(255, A + 60)), nb, false, side * 0.2f);
			}
			if (editing && snap.sel == i)
				r->drawRoundedRect(ImVec2(left - side * 0.1f, top - side * 0.1f), ImVec2(left + HotbarWidth(b) + side * 0.1f, top + side * 1.1f), kYellow, rnd * 1.2f, false, std::max(3.0f, b.r * 0.08f));
			continue;
		}
		circle(b, down ? Cb(255, 255, 255, std::min(255, A + 60)) : Cb(20, 20, 20, A), Cb(255, 255, 255, std::min(255, A + 50)), std::max(2.0f, b.r * 0.06f));
		if (i == B_INV)         // mochila (desenhada por primitivas) + etiqueta "INVENTARIO"
		{
			const float h = b.r * 0.62f; const ImColor wh = Cb(255, 255, 255, std::min(255, A + 150)), dk = Cb(20, 24, 40, std::min(255, A + 120));
			r->drawRoundedRect(ImVec2(b.cx - h * 0.18f - h * 0.30f, b.cy - h * 1.02f), ImVec2(b.cx + h * 0.48f, b.cy - h * 0.40f), wh, h * 0.25f, false, std::max(2.0f, h * 0.14f));   // alca
			r->drawRoundedRect(ImVec2(b.cx - h * 0.70f, b.cy - h * 0.58f), ImVec2(b.cx + h * 0.70f, b.cy + h * 0.98f), wh, h * 0.34f, true);                                             // corpo
			r->drawRoundedRect(ImVec2(b.cx - h * 0.46f, b.cy + h * 0.10f), ImVec2(b.cx + h * 0.46f, b.cy + h * 0.66f), dk, h * 0.16f, false, std::max(2.0f, h * 0.12f));              // bolso
			r->drawLine(ImVec2(b.cx - h * 0.70f, b.cy - h * 0.10f), ImVec2(b.cx + h * 0.70f, b.cy - h * 0.10f), dk, std::max(2.0f, h * 0.10f));                                           // aba
			const char* lb = "INVENTÁRIO"; const float fl = b.r * 0.30f; const ImVec2 ts = r->calculateTextSize(lb, fl);
			const ImVec2 p0(b.cx - ts.x * 0.5f - fl * 0.55f, b.cy + b.r * 1.05f), p1(b.cx + ts.x * 0.5f + fl * 0.55f, b.cy + b.r * 1.05f + ts.y + fl * 0.45f);
			r->drawRoundedRect(p0, p1, Cb(10, 16, 30, std::min(255, A + 60)), (p1.y - p0.y) * 0.5f, true);
			r->drawRoundedRect(p0, p1, Cb(30, 144, 255, std::min(255, A + 90)), (p1.y - p0.y) * 0.5f, false, std::max(1.5f, fl * 0.12f));
			r->drawText(ImVec2(p0.x + fl * 0.55f, p0.y + fl * 0.22f), Cb(255, 255, 255, std::min(255, A + 150)), lb, false, fl);
			if (editing && snap.sel == i) ring(b, 1.18f, std::max(3.0f, b.r * 0.08f));
			continue;
		}
		RwRaster* ico = IconRaster();
		if (ico)
		{
			// icone no centro, sem texto. AGACHAR vira "levantar" quando ja esta agachado; ATAQUE: soco (maos/corpo a corpo) ou tiro.
			int ic = CtlIcons::I_RUN;
			switch (i)
			{
				case B_RUN:     ic = CtlIcons::I_RUN; break;
				case B_JUMP:    ic = CtlIcons::I_JUMP; break;
				case B_CROUCH:  ic = (pedOk && ped->bIsDucking) ? CtlIcons::I_STAND : CtlIcons::I_CROUCH; break;
				case B_ATTACK:  ic = PunchWeapon(pedOk ? ped : nullptr) ? CtlIcons::I_FIST : CtlIcons::I_GUN; break;
				case B_MIC:     ic = CtlIcons::I_MIC; break;
				case B_RUNLOCK: ic = CtlIcons::I_RUNLOCK; break;
				case B_AIM:     ic = CtlIcons::I_AIM; break;
				case B_P1:      ic = CtlIcons::I_P1; break;
				case B_P2:      ic = CtlIcons::I_P2; break;
				default: break;
			}
			const float half = b.r * 0.78f;                 // icone (com o contorno preto) ocupa ~78% do raio de cada lado
			const float iw = 1.0f / (float)CtlIcons::kWidth, ih = 1.0f / (float)CtlIcons::kHeight;
			const float ux = (float)((ic % CtlIcons::kCols) * CtlIcons::kCell), uy = (float)((ic / CtlIcons::kCols) * CtlIcons::kCell);
			const float dx = (i == B_AIM) ? half * 0.125f : 0.0f;   // a mira (alvo) pesa para a esquerda no desenho: desloca ~6% da celula para a direita
			r->drawImage(ImVec2(b.cx - half + dx, b.cy - half), ImVec2(b.cx + half + dx, b.cy + half), (ImTextureID)ico,
			             Cb(255, 255, 255, std::min(255, A + 120)),
			             ImVec2(ux * iw, uy * ih), ImVec2((ux + (float)CtlIcons::kCell) * iw, (uy + (float)CtlIcons::kCell) * ih));
		}
		else
		{   // plano B (textura indisponivel): texto como antes
			const float fs = b.r * (i == B_CROUCH ? 0.34f : 0.42f);
			const ImVec2 ts = r->calculateTextSize(kLbl[i], fs);
			r->drawText(ImVec2(b.cx - ts.x * 0.5f, b.cy - ts.y * 0.5f),
			            down ? Cb(0, 0, 0, 255) : Cb(255, 255, 255, std::min(255, A + 80)), kLbl[i], (fm * bk) > 0.99f, fs);
		}
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
			iconBtn(P.save, snap.dirty ? ImColor(30, 144, 255, 255) : ImColor(60, 60, 60, 255));
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
				fillR(CtlRect{ P.chk.l + 3, P.chk.t + 3, P.chk.r - 3, P.chk.b - 3 }, ImColor(30, 144, 255, 255));
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
				fillR(CtlRect{ trk.l, cy - th, trk.l + (trk.r - trk.l) * t, cy + th }, has ? ImColor(30, 144, 255, 255) : ImColor(90, 90, 90, 255));
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
