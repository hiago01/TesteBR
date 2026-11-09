#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../settings.h"
#include "../../java/jniutil.h"
#include "../../vendor/armhook/patch.h"
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "maximomenu.h"
#include "../gfxsettings.h"
#include "worldmap.h"
#include "controlmode.h"
#include "controlsselector.h"
#include "moderncontrols.h"
#include "../../voice_new/VoiceMix.h"

#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

extern CNetGame *pNetGame;

extern UI*            pUI;
extern CGame*         pGame;
extern CSettings*     pSettings;
extern CJavaWrapper*  pJavaWrapper;

// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;
// definido em audiostream.cpp: BASS_CONFIG_GVOL_STREAM (0..10000)
extern volatile int g_iBassStreamVolume;

// =====================================================================================
//  Estado global / fila de toques (thread do jogo -> thread de desenho)
// =====================================================================================

namespace
{
	struct TouchEv { int type; int num; float x; float y; bool cancel; };

	std::atomic<bool> g_open{false};
	std::atomic<bool> g_flush{false};
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::mutex        g_evMutex;
	std::vector<TouchEv> g_events;

	std::atomic<bool> g_skid{true};

	// ---- configuracao persistida ([maximo] em SAMP/settings.ini) ----
	// So o que NAO existe no jogo original: marcas de derrapagem e volume dos streams do servidor.
	// O resto usa a tabela nativa MobileSettings::settings e o salvar nativo (Menu_SaveSettings).
	// draw/bright: -1 = ainda nao definido (na 1a vez assume o valor que o motor esta usando agora, sem mudar o visual).
	struct Cfg { int stream = 50; int draw = -1; int bright = -1; } g_cfg;   // stream: 0..100 (%) -> BASS 0..10000 (50 = padrao original)
	// Pedido de preset grafico vindo do LAUNCHER ([maximo] preset_request / preset_request_id). O jogo aplica uma unica vez por id
	// e grava preset_done_id. Sem pedido (id <= 0) ou ja feito: nada muda.
	int g_reqPreset = -1, g_reqId = 0, g_doneId = 0;
	bool g_cfgLoaded = false;
	bool g_dirty     = false;

	std::string IniPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	// ---- tabela nativa de opcoes do jogo (libGTASA.so ARM64 do projeto) ----
	// Confirmado por engenharia reversa manual (nm -D + decodificacao das funcoes):
	//  * simbolo `_ZN14MobileSettings8settingsE` = array de entradas de 0x28 bytes;
	//    valor atual em +0x10, minimo em +0x18, maximo em +0x1C (lidos em runtime e validados);
	//  * Menu_ApplyAudioSettings le as entradas 11/12/13 e chama SetEffectsMasterVolume /
	//    SetMusicMasterVolume / SetRadioAutoRetuneOnOff em `AudioEngine`;
	//  * DisplayScreen::DisplayScreen monta, nesta ordem: brilho(16), efeitos visuais(0),
	//    resolucao(1), distancia de desenho(2), sombras(5), reflexos de carros(7), legendas(24).
	//  * ControlsScreen::ControlsScreen (toque) monta, nesta ordem: layout dos botoes(9 "FEC_TOU"), direcao por toque(3 "FEC_STE"),
	//    acelerometro(8), inverter olhar(15), moto analogica(18), sensibilidade analogica(23 "MOB_ANA", faixa 20..45), auto subir(35).
	enum { N_VFX = 0, N_RES = 1, N_DRAW = 2, N_STEER = 3, N_SHADOW = 5, N_REFL = 7, N_ACCEL = 8, N_TLAYOUT = 9, N_FX = 11, N_MUSIC = 12,
	       N_RETUNE = 13, N_INVERT = 15, N_BRIGHT = 16, N_BIKE = 18, N_ANALOG = 23, N_SUBT = 24, N_CLIMB = 35, N_COUNT = 37 };

	uint8_t* NativeBase()
	{
		static uint8_t* p = (uint8_t*)dlsym(CHook::lib, "_ZN14MobileSettings8settingsE");
		return p;
	}
	int32_t* NativeField(int idx, int off)
	{
		uint8_t* b = NativeBase();
		if (!b || idx < 0 || idx >= N_COUNT) return nullptr;
		return (int32_t*)(b + idx * 0x28 + off);
	}
	bool NativeRange(int idx, int& mn, int& mx)
	{
		int32_t* lo = NativeField(idx, 0x18); int32_t* hi = NativeField(idx, 0x1C);
		if (!lo || !hi) return false;
		mn = *lo; mx = *hi;
		if (mn < 0 || mx <= mn || mx > 1000) { mn = 0; mx = 100; return false; }   // layout inesperado
		return true;
	}
	int NativeGet(int idx) { int32_t* v = NativeField(idx, 0x10); return v ? *v : 0; }
	void NativeSet(int idx, int v)
	{
		int32_t* p = NativeField(idx, 0x10);
		if (!p) return;
		int mn, mx; NativeRange(idx, mn, mx);
		*p = std::clamp(v, mn, mx);
		if (idx == N_FX || idx == N_MUSIC || idx == N_RETUNE)
		{
			static auto fn = (void(*)())dlsym(CHook::lib, "_Z23Menu_ApplyAudioSettingsv");
			if (fn) fn();
		}
		else if (idx == N_TLAYOUT)
		{
			// igual ao SelectScreen::SettingSelection::HandleInput nativo: ao mudar o layout, refaz os botoes de toque
			// (CTouchInterface::SetupLayoutObjects, estatica, sem argumentos). O modo de direcao (idx 3) o proprio
			// CTouchInterface::Update reaplica todo frame via SetupSteeringMode.
			static auto fn = (void(*)())dlsym(CHook::lib, "_ZN15CTouchInterface18SetupLayoutObjectsEv");
			if (fn) fn();
		}
	}
	void NativeSave()
	{
		static auto fn = (void(*)())dlsym(CHook::lib, "_Z17Menu_SaveSettingsv");
		if (fn) fn();
	}

	// ---- DISTANCIA DE RENDERIZACAO ----
	// Conferido no assembly do libGTASA.so: o jogo SO aplica a opcao (MobileSettings::settings[2], +0x10) dentro de
	// Menu_SwitchOffToGame (ao sair do menu de pausa NATIVO), com
	//     CRenderer::ms_lodDistScale = valor * 0.875 * 0.01 + 0.875
	// e CCamera::Process le esse float. Como o menu nativo foi desativado (MobileMenu::InitForPause bloqueado) e o
	// menu Maximo RP so gravava o numero na tabela, a opcao nunca chegava ao motor. Agora o proprio menu aplica.
	float* LodScalePtr()
	{
		static float* p = (float*)dlsym(CHook::lib, "_ZN9CRenderer15ms_lodDistScaleE");
		return p;
	}
	float LodScaleFor(int v) { return (float)v * 0.875f * 0.01f + 0.875f; }

	void ApplyDraw(int v)
	{
		if (int32_t* nd = NativeField(N_DRAW, 0x10)) *nd = v;     // mantem a tabela nativa coerente (Menu_SaveSettings grava dela)
		if (float* lp = LodScalePtr()) *lp = LodScaleFor(v);
	}

	// Primeira vez: assume o que o motor usa agora (draw: inverte a formula; brilho: valor da tabela), sem mudar o visual.
	void InitFromEngine()
	{
		int mn = 0, mx = 100;
		if (g_cfg.draw < 0)
		{
			NativeRange(N_DRAW, mn, mx);
			if (float* lp = LodScalePtr())
				g_cfg.draw = std::clamp((int)std::lround((*lp - 0.875f) / (0.875f * 0.01f)), mn, mx);
			else
				g_cfg.draw = std::clamp(NativeGet(N_DRAW), mn, mx);
		}
		if (g_cfg.bright < 0)
		{
			NativeRange(N_BRIGHT, mn, mx);
			g_cfg.bright = std::clamp(NativeGet(N_BRIGHT), mn, mx);
		}
	}

	void ApplyRes();   // definida mais abaixo (escala de resolucao)

	// Cada frame: se algo do jogo desfez a distancia/brilho escolhidos (a tabela nativa e reescrita por varias rotinas do
	// motor), restaura e registra no log (SAMP/samp_log.txt) para sabermos quem desfez.
	void KeepSettings()
	{
		static int s_logs = 0;
		if (g_cfg.draw < 0 || g_cfg.bright < 0) InitFromEngine();

		int32_t* nd = NativeField(N_DRAW, 0x10);
		float* lp = LodScalePtr();
		if ((nd && *nd != g_cfg.draw) || (lp && std::fabs(*lp - LodScaleFor(g_cfg.draw)) > 1e-4f))
		{
			if (s_logs++ < 12)
				FLog("[MaximoMenu] distancia de renderizacao desfeita pelo jogo (tabela=%d escala=%.4f) -> restaurando %d",
					 nd ? (int)*nd : -1, lp ? *lp : -1.0f, g_cfg.draw);
			ApplyDraw(g_cfg.draw);
		}
		ApplyRes();                                      // escala de resolucao escolhida (se o jogo/preset desfez)
		int32_t* nb = NativeField(N_BRIGHT, 0x10);
		if (nb && *nb != g_cfg.bright)
		{
			if (s_logs++ < 12) FLog("[MaximoMenu] brilho desfeito pelo jogo (tabela=%d) -> restaurando %d", (int)*nb, g_cfg.bright);
			*nb = g_cfg.bright;
		}
	}

	// ---- ESCALA DE RESOLUCAO (50..100 %) ----
	// Conferido no assembly do libGTASA.so (funcao Idle): a cada frame o jogo calcula
	//     largura = 640 + (OS_ScreenGetWidth - 640) * MobileSettings::settings[1] / 100     (altura mantem a proporcao, multiplos de 4)
	// e chama emu_SetAltRenderTarget(largura, altura), que SO recria o alvo quando o tamanho muda. O mundo e desenhado nesse alvo
	// menor; Render2dStuff (hooks.cpp) faz emu_FlushAltRenderTarget (upscale) ANTES da HUD -> HUD/controles continuam nitidos.
	// Entao a escala em % so converte "% da largura da tela" no valor 0..100 dessa opcao nativa (sem hook novo, sem FBO novo).
	int ScreenW()
	{
		static auto fn = (unsigned(*)())dlsym(CHook::lib, "_Z17OS_ScreenGetWidthv");
		return fn ? (int)fn() : 0;
	}
	int ResNativeFor(int pct)       // valor da opcao nativa para "pct % da largura"; -1 = tela pequena demais (sem escala)
	{
		const int W = ScreenW();
		if (W <= 640) return -1;
		const double v = ((double)pct * 0.01 * (double)W - 640.0) * 100.0 / (double)(W - 640);
		return std::clamp((int)std::lround(v), 0, 100);
	}
	int ResIdxFromNative()          // aproxima o que o motor usa agora (para mostrar sem mudar nada)
	{
		const int W = ScreenW();
		if (W <= 640) return 5;
		const int v = std::clamp(NativeGet(N_RES), 0, 100);
		const double pct = (640.0 + (double)(W - 640) * v / 100.0) * 100.0 / (double)W;
		int best = 5; double bd = 1e9;
		for (int i = 0; i < 6; i++) { const double d = std::fabs(pct - GfxSettings::kResOpts[i]); if (d < bd) { bd = d; best = i; } }
		return best;
	}
	void ApplyRes()
	{
		const int idx = GfxSettings::g_resIdx.load();
		if (idx < 0 || idx > 5) return;                       // ainda nao escolhido: nao mexe no que o motor usa
		const int v = ResNativeFor(GfxSettings::kResOpts[idx]);
		if (v < 0) return;
		if (NativeGet(N_RES) != v) NativeSet(N_RES, v);       // so escreve quando difere (o jogo reaplica sozinho no proximo frame)
	}
	int IdxOf(const int* opts, int n, long val, int def)
	{
		for (int i = 0; i < n; i++) if (opts[i] == val) return i;
		return def;
	}
	void RestoreGfx()
	{
		GfxSettings::g_aspectMode.store(0);
		GfxSettings::g_resIdx.store(5);                       // 100 %
		GfxSettings::g_safeIdx.store(0);                      // 0 % = area padrao
		ApplyRes();
		g_dirty = true;
		FLog("[MaximoMenu] graficos restaurados: aspecto=AUTO escala=100%% safezone=0%%");
	}

	void ApplyStream(int pct)
	{
		g_iBassStreamVolume = std::clamp(pct, 0, 100) * 100;
	}

	void LoadCfg()
	{
		const std::string path = IniPath();
		if (path.empty()) return;          // armazenamento ainda nao pronto: tenta de novo no proximo frame
		g_cfgLoaded = true;

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(path.c_str()) < 0) return;

		g_reqPreset = (int)ini.GetLongValue("maximo", "preset_request", -1);
		g_reqId     = (int)ini.GetLongValue("maximo", "preset_request_id", 0);
		g_doneId    = (int)ini.GetLongValue("maximo", "preset_done_id", 0);
		g_skid.store(ini.GetLongValue("maximo", "skidmarks", 1) != 0);
		g_cfg.stream = std::clamp((int)ini.GetLongValue("maximo", "vol_stream", 50), 0, 100);
		VoiceMix::SetMaster((int)ini.GetLongValue("maximo", "vol_voip", 100));
		long d = ini.GetLongValue("maximo", "draw_dist", -1), b = ini.GetLongValue("maximo", "brightness", -1);
		int mn = 0, mx = 100;
		if (d >= 0) { NativeRange(N_DRAW, mn, mx);   g_cfg.draw   = std::clamp((int)d, mn, mx); }
		if (b >= 0) { NativeRange(N_BRIGHT, mn, mx); g_cfg.bright = std::clamp((int)b, mn, mx); }

		// graficos (valores ausentes/invalidos -> padrao; resolutionScale ausente -> continua o valor atual do motor)
		GfxSettings::g_aspectMode.store(0);   // so AUTO e suportado: qualquer outro valor gravado e ignorado
		GfxSettings::g_resIdx.store(IdxOf(GfxSettings::kResOpts, 6, ini.GetLongValue("maximo", "resolutionScale", -1), -1));
		GfxSettings::g_safeIdx.store(IdxOf(GfxSettings::kSafeOpts, 5, ini.GetLongValue("maximo", "safeZoneScale", 0), 0));
	}

	void SaveCfg()
	{
		g_dirty = false;
		NativeSave();
		ControlMode::Save();      // ajustes do MODERNO (secao propria; nao mexe no classico)
		const std::string path = IniPath();
		if (path.empty()) return;

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(path.c_str());   // se nao existir, cria

		ini.SetLongValue("maximo", "skidmarks", g_skid.load() ? 1 : 0);
		ini.SetLongValue("maximo", "vol_stream", g_cfg.stream);
		ini.SetLongValue("maximo", "vol_voip", VoiceMix::GetMaster());
		if (g_doneId > 0) ini.SetLongValue("maximo", "preset_done_id", g_doneId);
		if (g_cfg.draw >= 0)   ini.SetLongValue("maximo", "draw_dist", g_cfg.draw);
		if (g_cfg.bright >= 0) ini.SetLongValue("maximo", "brightness", g_cfg.bright);
		ini.SetLongValue("maximo", "aspectRatioMode", GfxSettings::g_aspectMode.load());
		if (GfxSettings::g_resIdx.load() >= 0) ini.SetLongValue("maximo", "resolutionScale", GfxSettings::kResOpts[std::clamp(GfxSettings::g_resIdx.load(), 0, 5)]);
		ini.SetLongValue("maximo", "safeZoneScale", GfxSettings::SafePct());
		if (pSettings)
			ini.SetBoolValue("gui", "fps", pSettings->Get().iFPSCounter);

		if (ini.SaveFile(path.c_str()) < 0)
			FLog("[MaximoMenu] falha ao salvar %s", path.c_str());
	}

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}
}

// =====================================================================================
//  API estatica
// =====================================================================================

bool MaximoMenu::IsOpen() { return g_open.load(); }

void MaximoMenu::Open()
{
	if (g_open.load()) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	for (auto& c : g_claimed) c.store(false);
	g_flush.store(true);     // o hook solta os dedos que o jogo considera pressionados
	g_open.store(true);
}

void MaximoMenu::Close()
{
	if (!g_open.exchange(false)) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	// g_claimed e mantido: o dedo que ainda esta na tela continua sendo engolido ate o UP,
	// para o jogo nao receber MOVE/UP sem o DOWN correspondente.
	if (g_dirty) SaveCfg();
}

void MaximoMenu::Toggle() { if (g_open.load()) Close(); else Open(); }

bool MaximoMenu::ConsumeFlushRequest() { return g_flush.exchange(false); }

bool MaximoMenu::SkidmarksEnabled() { return g_skid.load(); }

bool MaximoMenu::OnBackPressed()
{
	// Menu aberto: Voltar fecha. Menu fechado: Voltar abre o menu Maximo RP (em vez do Pause
	// nativo, que fica desativado). O jogador nunca e desconectado por este botao.
	// Debounce: onKeyDown e onBackPressed podem ambos disparar no mesmo toque (Android 13+).
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 300) return true;
	s_last = now;
	Toggle();
	return true;
}

bool MaximoMenu::HandleRawTouch(int type, int num, int x, int y)
{
	const int n = num & 3;
	const bool open = g_open.load();

	if (!open && !g_claimed[n].load())
		return false;                       // fluxo normal do jogo

	if (open)
	{
		g_claimed[n].store(true);
		std::lock_guard<std::mutex> l(g_evMutex);
		if (g_events.size() < 256)
			g_events.push_back({type, n, (float)x, (float)y, g_bTouchCancel});
	}

	if (type == 1)                          // pop/cancel: dedo solto, libera
		g_claimed[n].store(false);

	return true;                            // consumido: nada chega ao GTA/SA-MP
}

// =====================================================================================
//  Paginas
// =====================================================================================

namespace
{
	enum { K_INFO = 0, K_TOGGLE, K_SLIDER, K_ACTION, K_CHOICE };
	enum { ID_NONE = 0, ID_SKID, ID_FPSCOUNT, ID_STREAM, ID_NATIVE, ID_RESUME, ID_QUIT, ID_PRESET, ID_CTLRESET,
	       ID_CTLMODE, ID_MC_EDIT, ID_MC_RESET, ID_MC_JOYSCALE, ID_MC_BTNSCALE, ID_MC_ALPHA, ID_MC_SENS, ID_MC_DEAD, ID_MC_MAXD, ID_MC_JOYX, ID_MC_JOYY, ID_MC_AIMSENS, ID_MC_AIMSMOOTH, ID_MC_AIMDRAG, ID_MC_SHOWINV, ID_MC_SHOWSLOTS,
	       ID_VOIP_VOL, ID_VOIP_PRESET, ID_VOIP_HDR, ID_VOIP_RESETP, ID_VOIP_PLAYER,
	       ID_RESSCALE, ID_ASPECT, ID_SAFEZONE, ID_GFXRESET };

	struct Row { const char* label; int kind; int id; int nidx = -1; };

	struct Page { const char* name; std::vector<Row> rows; };

	// Opcoes sem funcao/simbolo comprovado nesta build aparecem como K_INFO ("Indisponivel").
	const char* const kTabs[6] = { "MAPA", "CONTROLES", "JOGO", "ÁUDIO", "CONFIGURAÇÕES", "SAIR" };
	enum { T_MAPA, T_CONTROLES, T_JOGO, T_AUDIO, T_CONFIG, T_SAIR };

	// ---- PRESETS DE QUALIDADE GRAFICA ----
	// Cada preset ajusta: distancia de renderizacao (%), efeitos visuais, sombras e reflexos dos veiculos. NAO mexe em
	// brilho, resolucao nem marcas de derrapagem. Faixas lidas da tabela do libGTASA: efeitos 0..3, sombras 0..2, reflexos 0..3,
	// distancia 0..100 (37 = o que o motor usa por padrao; 100 = escala 1.75, bem mais pesado). Valores escolhidos seguindo os
	// niveis que o proprio jogo usa por tipo de aparelho (MobileSettings::Initialize).
	enum { P_MINIMA = 0, P_BAIXA, P_MEDIA, P_ALTA, P_ULTRA, P_COUNT, P_CUSTOM = P_COUNT };
	struct Preset { const char* name; int draw, vfx, shadow, refl; };
	const Preset kPresets[P_COUNT] = {
		{ "Mínima", 20, 0, 0, 0 },
		{ "Baixa",  37, 1, 0, 0 },
		{ "Média",  60, 1, 1, 1 },
		{ "Alta",   80, 2, 2, 2 },
		{ "Ultra", 100, 3, 2, 3 },
	};

	int CurrentPreset()
	{
		if (g_cfg.draw < 0) InitFromEngine();
		for (int i = 0; i < P_COUNT; i++)
			if (g_cfg.draw == kPresets[i].draw && NativeGet(N_VFX) == kPresets[i].vfx &&
				NativeGet(N_SHADOW) == kPresets[i].shadow && NativeGet(N_REFL) == kPresets[i].refl)
				return i;
		return P_CUSTOM;
	}

	void ApplyPreset(int i)
	{
		if (i < 0 || i >= P_COUNT) return;
		const Preset& p = kPresets[i];
		int mn, mx; NativeRange(N_DRAW, mn, mx);
		g_cfg.draw = std::clamp(p.draw, mn, mx);
		ApplyDraw(g_cfg.draw);
		NativeSet(N_VFX, p.vfx);
		NativeSet(N_SHADOW, p.shadow);
		NativeSet(N_REFL, p.refl);
		FLog("[MaximoMenu] preset grafico: %s (distancia=%d efeitos=%d sombras=%d reflexos=%d)", p.name, p.draw, p.vfx, p.shadow, p.refl);
	}

	const Page kSubGraficos = { "Gráficos", {
		{ "Qualidade gráfica (preset)",  K_CHOICE, ID_PRESET },
		{ "Distância de renderização",   K_SLIDER, ID_NATIVE, N_DRAW },
		{ "Efeitos visuais",             K_CHOICE, ID_NATIVE, N_VFX },
		{ "Sombras",                     K_CHOICE, ID_NATIVE, N_SHADOW },
		{ "Reflexos dos veículos",       K_CHOICE, ID_NATIVE, N_REFL },
		{ "Marcas de derrapagem",        K_TOGGLE, ID_SKID },
		{ "Qualidade das texturas",      K_INFO, ID_NONE },
		{ "Névoa",                       K_INFO, ID_NONE },
		{ "Limite de FPS",               K_INFO, ID_NONE },
		{ "Escala de resolução",         K_CHOICE, ID_RESSCALE },
		{ "Proporção da tela (Aspect Ratio)", K_INFO, ID_ASPECT },
		{ "Outras proporções (4:3, 16:9, 19.5:9, 20:9)", K_INFO, ID_NONE },
		{ "Zona segura (Safe Zone)",     K_SLIDER, ID_SAFEZONE },
		{ "Restaurar configurações gráficas", K_ACTION, ID_GFXRESET },
	}};
	const Page kSubTela = { "Tela", {
		{ "Brilho",                      K_SLIDER, ID_NATIVE, N_BRIGHT },
		{ "Legendas",                    K_TOGGLE, ID_NATIVE, N_SUBT },
		{ "Contador de FPS",             K_TOGGLE, ID_FPSCOUNT },
	}};
	const Page kSubChat = { "Bate-papo", {
		{ "Sem opções verificadas nesta versão", K_INFO, ID_NONE },
	}};
	const Page kPageAudio = { "Áudio", {
		{ "Efeitos sonoros",              K_SLIDER, ID_NATIVE, N_FX },
		{ "Rádio / música",               K_SLIDER, ID_NATIVE, N_MUSIC },
		{ "Sintonia automática",          K_TOGGLE, ID_NATIVE, N_RETUNE },
		{ "Volume do servidor (streams)", K_SLIDER, ID_STREAM },
		{ "Volume do VoIP (vozes)",       K_SLIDER, ID_VOIP_VOL },
		{ "Preset de voz (geral)",        K_CHOICE, ID_VOIP_PRESET },
		{ "Restaurar vozes dos jogadores", K_ACTION, ID_VOIP_RESETP },
		{ "Estação de rádio",             K_INFO, ID_NONE },
		{ "Áudio posicional",             K_INFO, ID_NONE },
		{ "Frequência máxima / HRTF",     K_INFO, ID_NONE },
	}};
	// Pagina de audio dinamica: linhas fixas + uma linha (preset) por jogador online. Os nomes ficam em armazenamento estavel
	// ate a proxima reconstrucao (feita a cada quadro, antes de qualquer uso das linhas).
	const Page* BuildAudioPage()
	{
		static Page page;
		static std::vector<std::string> names;
		names.clear();
		std::vector<int> ids;
		if (pNetGame && pNetGame->GetPlayerPool())
		{
			CPlayerPool* pool = pNetGame->GetPlayerPool();
			const int self = (int)pool->GetLocalPlayerID();
			for (int id = 0; id < MAX_PLAYERS && id < VoiceMix::kMaxPlayers; id++)
			{
				if (id == self || !pool->GetSlotState((PLAYERID)id)) continue;
				const char* n = pool->GetPlayerName((PLAYERID)id);
				names.emplace_back(std::string(n ? n : "?") + " (" + std::to_string(id) + ")");
				ids.push_back(id);
			}
		}
		page.name = kPageAudio.name;
		page.rows = kPageAudio.rows;
		page.rows.push_back({ ids.empty() ? "Vozes dos jogadores: ninguém online" : "Vozes dos jogadores (toque p/ trocar o preset)", K_INFO, ID_VOIP_HDR });
		for (size_t i = 0; i < ids.size(); i++)
			page.rows.push_back({ names[i].c_str(), K_CHOICE, ID_VOIP_PLAYER, ids[i] });
		return &page;
	}

	const Page kPageSair = { "Sair", {
		{ "Voltar ao jogo",               K_ACTION, ID_RESUME },
		{ "Sair do jogo",                 K_ACTION, ID_QUIT },
	}};
	// Mesma ordem/itens da tela nativa "Redefinir controles" (ControlsScreen, entrada por toque).
	const Page kPageControles = { "Controles", {
		{ "Layout dos botões (toque)",   K_CHOICE, ID_NATIVE, N_TLAYOUT },
		{ "Direção por toque",           K_CHOICE, ID_NATIVE, N_STEER },
		{ "Acelerômetro",                K_CHOICE, ID_NATIVE, N_ACCEL },
		{ "Inverter olhar",              K_TOGGLE, ID_NATIVE, N_INVERT },
		{ "Direção analógica da moto",   K_TOGGLE, ID_NATIVE, N_BIKE },
		{ "Sensibilidade analógica",     K_SLIDER, ID_NATIVE, N_ANALOG },
		{ "Subir automaticamente",       K_TOGGLE, ID_NATIVE, N_CLIMB },
		{ "Restaurar padrões",           K_ACTION, ID_CTLRESET },
		// ---- Sistema de controles (CLASSICO x MODERNO). Os ajustes abaixo valem so para o MODERNO; o classico nao e alterado. ----
		{ "Sistema de controles",        K_ACTION, ID_CTLMODE },
		{ "Moderno: tamanho do joystick", K_SLIDER, ID_MC_JOYSCALE },
		{ "Moderno: tamanho dos botões", K_SLIDER, ID_MC_BTNSCALE },
		{ "Moderno: transparência",      K_SLIDER, ID_MC_ALPHA },
		{ "Moderno: sensibilidade",      K_SLIDER, ID_MC_SENS },
		{ "Moderno: zona morta",         K_SLIDER, ID_MC_DEAD },
		{ "Moderno: distância máxima",   K_SLIDER, ID_MC_MAXD },
		{ "Moderno: joystick (horizontal)", K_SLIDER, ID_MC_JOYX },
		{ "Moderno: joystick (vertical)",   K_SLIDER, ID_MC_JOYY },
		{ "Moderno: mira arrastável (Ataque)", K_TOGGLE, ID_MC_AIMDRAG },
		{ "Moderno: botão Inventário",   K_TOGGLE, ID_MC_SHOWINV },
		{ "Moderno: slots rápidos",      K_TOGGLE, ID_MC_SHOWSLOTS },
		{ "Moderno: sensibilidade da mira", K_SLIDER, ID_MC_AIMSENS },
		{ "Moderno: suavização da mira", K_SLIDER, ID_MC_AIMSMOOTH },
		{ "Moderno: editar posições",    K_ACTION, ID_MC_EDIT },
		{ "Moderno: restaurar ajustes",  K_ACTION, ID_MC_RESET },
	}};
	const Page kPageNativa = { "", {
		{ "Indisponível: o menu nativo foi desativado", K_INFO, ID_NONE },
		{ "(função original não reimplementada)",       K_INFO, ID_NONE },
	}};

	const Page* const kSubs[3] = { &kSubGraficos, &kSubTela, &kSubChat };

	// ---- ajustes do controle MODERNO como 0..100 (percentual da faixa de cada campo; limites = ModernConfig::Clamp) ----
	struct McField { float lo, hi; };
	bool McRange(int id, McField& f)
	{
		switch (id)
		{
			case ID_MC_JOYSCALE: f = { 0.5f, 1.6f };  return true;
			case ID_MC_BTNSCALE: f = { 0.5f, 1.6f };  return true;
			case ID_MC_ALPHA:    f = { 0.15f, 1.0f }; return true;
			case ID_MC_SENS:     f = { 0.5f, 2.0f };  return true;
			case ID_MC_DEAD:     f = { 0.0f, 0.45f }; return true;
			case ID_MC_MAXD:     f = { 0.6f, 1.6f };  return true;
			case ID_MC_JOYX:     f = { 0.05f, 0.95f };return true;
			case ID_MC_JOYY:     f = { 0.10f, 0.95f };return true;
			case ID_MC_AIMSENS:  f = { 0.3f, 3.0f };  return true;
			case ID_MC_AIMSMOOTH:f = { 0.0f, 0.85f }; return true;
		}
		return false;
	}
	float* McPtr(CtlLogic::ModernConfig& c, int id)
	{
		switch (id)
		{
			case ID_MC_JOYSCALE: return &c.joyScale;
			case ID_MC_BTNSCALE: return &c.btnScale;
			case ID_MC_ALPHA:    return &c.alpha;
			case ID_MC_SENS:     return &c.sens;
			case ID_MC_DEAD:     return &c.deadzone;
			case ID_MC_MAXD:     return &c.maxDist;
			case ID_MC_JOYX:     return &c.joyPos.x;
			case ID_MC_JOYY:     return &c.joyPos.y;
			case ID_MC_AIMSENS:  return &c.aimSens;
			case ID_MC_AIMSMOOTH:return &c.aimSmooth;
		}
		return nullptr;
	}
	int McGet(int id)
	{
		McField f; if (!McRange(id, f)) return 0;
		CtlLogic::ModernConfig c = ControlMode::GetConfig();
		const float* p = McPtr(c, id);
		return p ? (int)std::lround(std::clamp((*p - f.lo) / (f.hi - f.lo), 0.0f, 1.0f) * 100.0f) : 0;
	}
	void McSet(int id, int pct)
	{
		McField f; if (!McRange(id, f)) return;
		CtlLogic::ModernConfig c = ControlMode::GetConfig();
		float* p = McPtr(c, id);
		if (p) *p = f.lo + (f.hi - f.lo) * (float)std::clamp(pct, 0, 100) / 100.0f;
		if (id == ID_MC_ALPHA) for (int i = 0; i <= CtlLogic::B_COUNT; i++) c.ctlAlpha[i] = c.alpha;   // a transparencia geral vale para todos os controles do HUD ativo
		ControlMode::SetConfig(c, false);     // grava ao soltar o slider (g_dirty -> SaveCfg), sem I/O a cada quadro
	}

	int GetValue(const Row& r)
	{
		switch (r.id)
		{
			case ID_MC_JOYSCALE: case ID_MC_BTNSCALE: case ID_MC_ALPHA: case ID_MC_SENS:
			case ID_MC_DEAD: case ID_MC_MAXD: case ID_MC_JOYX: case ID_MC_JOYY: case ID_MC_AIMSENS: case ID_MC_AIMSMOOTH: return McGet(r.id);
			case ID_MC_AIMDRAG: return ControlMode::GetConfig().aimDrag ? 1 : 0;
			case ID_MC_SHOWINV: return ControlMode::GetConfig().showInv ? 1 : 0;
			case ID_MC_SHOWSLOTS: return ControlMode::GetConfig().showSlots ? 1 : 0;
			case ID_SKID:     return g_skid.load() ? 1 : 0;
			case ID_FPSCOUNT: return (pSettings && pSettings->Get().iFPSCounter) ? 1 : 0;
			case ID_STREAM:   return g_cfg.stream;
			case ID_VOIP_VOL: return VoiceMix::GetMaster();
			case ID_VOIP_PRESET: return VoiceMix::LevelOf(VoiceMix::GetMaster());
			case ID_VOIP_PLAYER: return VoiceMix::LevelOf(VoiceMix::GetPlayer(r.nidx));
			case ID_PRESET:   return CurrentPreset();
			case ID_RESSCALE: { const int i = GfxSettings::g_resIdx.load(); return i >= 0 ? i : ResIdxFromNative(); }
			case ID_SAFEZONE: return std::clamp(GfxSettings::g_safeIdx.load(), 0, 4);
			case ID_NATIVE:
				if (r.nidx == N_DRAW)   { if (g_cfg.draw < 0)   InitFromEngine(); return g_cfg.draw; }
				if (r.nidx == N_BRIGHT) { if (g_cfg.bright < 0) InitFromEngine(); return g_cfg.bright; }
				return NativeGet(r.nidx);
		}
		return 0;
	}

	void SetValue(const Row& r, int v)
	{
		switch (r.id)
		{
			case ID_MC_JOYSCALE: case ID_MC_BTNSCALE: case ID_MC_ALPHA: case ID_MC_SENS:
			case ID_MC_DEAD: case ID_MC_MAXD: case ID_MC_JOYX: case ID_MC_JOYY: case ID_MC_AIMSENS: case ID_MC_AIMSMOOTH: McSet(r.id, v); break;
			case ID_MC_AIMDRAG: { CtlLogic::ModernConfig c = ControlMode::GetConfig(); c.aimDrag = (v != 0); ControlMode::SetConfig(c, false); break; }
			case ID_MC_SHOWINV: { CtlLogic::ModernConfig c = ControlMode::GetConfig(); c.showInv = (v != 0); ControlMode::SetConfig(c, false); break; }
			case ID_MC_SHOWSLOTS: { CtlLogic::ModernConfig c = ControlMode::GetConfig(); c.showSlots = (v != 0); ControlMode::SetConfig(c, false); break; }
			case ID_SKID:     g_skid.store(v != 0); break;
			case ID_FPSCOUNT: if (pSettings) pSettings->Get().iFPSCounter = (v != 0); break;
			case ID_STREAM:   g_cfg.stream = std::clamp(v, 0, 100); ApplyStream(g_cfg.stream); break;
			case ID_PRESET:   ApplyPreset(v); break;
			case ID_RESSCALE: GfxSettings::g_resIdx.store(std::clamp(v, 0, 5)); ApplyRes(); break;
			case ID_SAFEZONE: GfxSettings::g_safeIdx.store(std::clamp(v, 0, 4)); break;
			case ID_VOIP_VOL: VoiceMix::SetMaster(v); break;
			case ID_VOIP_PRESET: if (v >= 0 && v < VoiceMix::kLevels) VoiceMix::SetMaster(VoiceMix::kLevelPct[v]); break;
			case ID_VOIP_PLAYER: if (v >= 0 && v < VoiceMix::kLevels) VoiceMix::SetPlayer(r.nidx, VoiceMix::kLevelPct[v]); return;   // por sessao (IDs mudam): nao grava
			case ID_NATIVE:
				if (r.nidx == N_DRAW)        { int mn, mx; NativeRange(N_DRAW, mn, mx);   g_cfg.draw = std::clamp(v, mn, mx);   ApplyDraw(g_cfg.draw); }
				else if (r.nidx == N_BRIGHT) { int mn, mx; NativeRange(N_BRIGHT, mn, mx); g_cfg.bright = std::clamp(v, mn, mx); NativeSet(N_BRIGHT, g_cfg.bright); }
				else NativeSet(r.nidx, v);
				break;
		}
		g_dirty = true;
	}

	void RangeOf(const Row& r, int& mn, int& mx)
	{
		if (r.id >= ID_MC_JOYSCALE && r.id <= ID_MC_AIMSMOOTH) { mn = 0; mx = 100; return; }
		if (r.id == ID_VOIP_VOL) { mn = 0; mx = VoiceMix::kMaxPct; return; }
		if (r.id == ID_VOIP_PRESET || r.id == ID_VOIP_PLAYER) { mn = 0; mx = VoiceMix::kLevels - 1; return; }
		if (r.id == ID_RESSCALE) { mn = 0; mx = 5; return; }
		if (r.id == ID_SAFEZONE) { mn = 0; mx = 4; return; }
		if (r.id == ID_PRESET) { mn = 0; mx = P_COUNT - 1; return; }   // 0..4 (a volta de "Personalizada" cai em 0)
		if (r.id == ID_NATIVE) { NativeRange(r.nidx, mn, mx); return; }
		mn = 0; mx = (r.id == ID_STREAM) ? 100 : 1;
	}

	// ---- estado do desenho (so a thread de draw usa) ----
	int   g_tab = T_CONFIG;
	int   g_sub = 0;
	float g_scroll = 0.0f;
	int64_t g_quitArmedUntil = 0;
	int64_t g_resetArmedUntil = 0;     // confirmacao de "Moderno: restaurar ajustes"
	bool  g_applied = false;

	struct Drag
	{
		int   finger = -1;
		int   kind = 0;       // 0 nada, 1 voltar, 2 aba, 3 sub, 4 linha, 5 slider
		int   idx = -1;
		float sx = 0, sy = 0, scroll0 = 0;
		bool  scrolling = false;
	} g_drag;

	const ImColor cPanel (45, 50, 53, 235);
	const ImColor cPanel2(35, 40, 43, 235);
	const ImColor cLine  (86, 150, 175, 255);
	const ImColor cText  (235, 238, 240, 255);
	const ImColor cDim   (140, 148, 152, 255);
	const ImColor cBlue  (40, 140, 230, 255);
	const ImColor cWhite (250, 250, 250, 255);
	const ImColor cDark  (30, 34, 36, 255);
}

MaximoMenu::MaximoMenu() : Widget()
{
	LoadCfg();
}

// =====================================================================================
//  Desenho + tratamento dos toques
// =====================================================================================

void MaximoMenu::draw(ImGuiRenderer* r)
{
	if (!g_cfgLoaded) LoadCfg();

	// Aplica uma vez o volume salvo dos streams do servidor (o resto o proprio jogo restaura).
	if (!g_applied && pGame)
	{
		g_applied = true;
		ApplyStream(g_cfg.stream);
		InitFromEngine();
		if (g_cfg.draw >= 0) ApplyDraw(g_cfg.draw);     // aplica a distancia salva (ou a atual do motor, se nunca mexeu)
		if (g_reqId > 0 && g_reqId != g_doneId && g_reqPreset >= 0 && g_reqPreset < P_COUNT)
		{
			FLog("[MaximoMenu] aplicando preset %d pedido pelo launcher (id %d)", g_reqPreset, g_reqId);
			ApplyPreset(g_reqPreset);
			g_doneId = g_reqId;
			SaveCfg();                                  // grava gta_sa.set + preset_done_id agora
		}
	}
	if (g_applied) KeepSettings();                      // todo frame, menu aberto ou fechado

	if (!g_open.load())
	{
		g_drag = Drag();
		g_scroll = 0.0f;
		return;
	}

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float k = scr.y / 575.0f;

	const float W  = 832.0f * k;
	const float x0 = (scr.x - W) * 0.5f;
	const float y0 = (scr.y - 470.0f * k) * 0.5f;

	const float tabY = y0 + 32.0f * k, tabH = 28.0f * k, tabGap = 3.0f * k;
	const float tabW = (W - 5.0f * tabGap) / 6.0f;
	const float bodyY = y0 + 70.0f * k, bodyH = 395.0f * k;
	const bool  hasSub = (g_tab == T_CONFIG);
	const float subW = 275.0f * k;
	const float panX = hasSub ? x0 + subW + 3.0f * k : x0;
	const float panW = hasSub ? W - subW - 3.0f * k : W;
	const float rowH = 27.0f * k;
	const float fs   = 13.0f * k;
	const float pad  = 10.0f * k;

	const ImVec2 backA(x0 + W - 90.0f * k, y0 + 2.0f * k), backB(x0 + W, y0 + 28.0f * k);

	// ---- pagina atual ----
	const Page* page;
	switch (g_tab)
	{
		case T_AUDIO: page = BuildAudioPage(); break;
		case T_SAIR:  page = &kPageSair; break;
		case T_CONFIG: page = kSubs[std::clamp(g_sub, 0, 2)]; break;
		case T_CONTROLES: page = &kPageControles; break;
		default:      page = &kPageNativa; break;
	}
	const int nRows = (int)page->rows.size();
	const float contentH = nRows * rowH;
	const float maxScroll = std::max(0.0f, contentH - bodyH);
	g_scroll = std::clamp(g_scroll, 0.0f, maxScroll);

	const float trackW = 150.0f * k;
	const float trackX = panX + panW - pad - trackW;

	auto rowAt = [&](float y) -> int {
		if (y < bodyY || y > bodyY + bodyH) return -1;
		int i = (int)std::floor((y - bodyY + g_scroll) / rowH);
		return (i >= 0 && i < nRows) ? i : -1;
	};
	auto inRect = [](float x, float y, float ax, float ay, float bx, float by) {
		return x >= ax && x <= bx && y >= ay && y <= by;
	};
	auto sliderFromX = [&](float x, const Row& row) {
		int mn, mx; RangeOf(row, mn, mx);
		float t = std::clamp((x - trackX) / trackW, 0.0f, 1.0f);
		SetValue(row, mn + (int)std::lround(t * (mx - mn)));
	};

	// ---- processa os toques acumulados desde o ultimo frame ----
	std::vector<TouchEv> evs;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		evs.swap(g_events);
	}

	for (const TouchEv& e : evs)
	{
		if (e.type == 2 && g_drag.finger < 0)               // push (primeiro dedo livre)
		{
			g_drag = Drag();
			g_drag.finger = e.num; g_drag.sx = e.x; g_drag.sy = e.y; g_drag.scroll0 = g_scroll;

			if (inRect(e.x, e.y, backA.x, backA.y, backB.x, backB.y)) g_drag.kind = 1;
			else if (e.y >= tabY && e.y <= tabY + tabH && e.x >= x0 && e.x <= x0 + W)
			{
				int i = (int)((e.x - x0) / (tabW + tabGap));
				g_drag.kind = 2; g_drag.idx = std::clamp(i, 0, 5);
			}
			else if (hasSub && inRect(e.x, e.y, x0, bodyY, x0 + subW, bodyY + 3 * (rowH + 2.0f * k)))
			{
				int i = (int)((e.y - bodyY) / (rowH + 2.0f * k));
				g_drag.kind = 3; g_drag.idx = std::clamp(i, 0, 2);
			}
			else if (inRect(e.x, e.y, panX, bodyY, panX + panW, bodyY + bodyH))
			{
				int i = rowAt(e.y);
				g_drag.idx = i;
				g_drag.kind = 4;
				if (i >= 0 && page->rows[i].kind == K_SLIDER && e.x >= trackX - 24.0f * k)
				{
					g_drag.kind = 5;
					sliderFromX(e.x, page->rows[i]);
				}
			}
		}
		else if (e.num == g_drag.finger && e.type == 3)      // move
		{
			if (g_drag.kind == 5 && g_drag.idx >= 0)
				sliderFromX(e.x, page->rows[g_drag.idx]);
			else if (g_drag.kind == 4 || g_drag.kind == 0)
			{
				if (std::fabs(e.y - g_drag.sy) > 10.0f * k) g_drag.scrolling = true;
				if (g_drag.scrolling)
					g_scroll = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, maxScroll);
			}
		}
		else if (e.num == g_drag.finger && e.type == 1)      // pop / cancel
		{
			const bool ok = !e.cancel && !g_drag.scrolling;
			if (ok)
			{
				if (g_drag.kind == 1) { g_open.store(false); if (g_dirty) SaveCfg(); g_drag = Drag(); return; }
				if (g_drag.kind == 2 && g_drag.idx == T_MAPA) { g_open.store(false); if (g_dirty) SaveCfg(); g_drag = Drag(); WorldMapWidget::Open(); return; }
				if (g_drag.kind == 2) { g_tab = g_drag.idx; g_scroll = 0.0f; g_quitArmedUntil = 0; g_resetArmedUntil = 0; }
				if (g_drag.kind == 3) { g_sub = g_drag.idx; g_scroll = 0.0f; }
				if (g_drag.kind == 4 && g_drag.idx >= 0)
				{
					const Row& row = page->rows[g_drag.idx];
					if (row.kind == K_TOGGLE || row.kind == K_CHOICE)
					{
						int mn, mx; RangeOf(row, mn, mx);
						int cur = GetValue(row);
						SetValue(row, cur >= mx ? mn : cur + 1);
					}
					else if (row.kind == K_ACTION)
					{
						if (row.id == ID_CTLRESET)
						{
							// valor padrao da propria tabela (+0x14), igual ao "Restaurar padroes" nativo
							static const int kIdx[] = { N_TLAYOUT, N_STEER, N_ACCEL, N_INVERT, N_BIKE, N_ANALOG, N_CLIMB };
							for (int i : kIdx) { int32_t* d = NativeField(i, 0x14); if (d) NativeSet(i, *d); }
							g_dirty = true;
						}
						if (row.id == ID_CTLMODE) { g_open.store(false); if (g_dirty) SaveCfg(); g_drag = Drag(); ControlsSelector::Open(); return; }
						if (row.id == ID_MC_EDIT && ControlMode::IsModern()) { g_open.store(false); if (g_dirty) SaveCfg(); g_drag = Drag(); ModernControls::StartEdit(); return; }
						if (row.id == ID_MC_RESET)            // pede confirmacao: 1o toque arma, 2o toque (ate 3 s) restaura
						{
							if (NowMs() < g_resetArmedUntil) { ControlMode::ResetConfig(); g_resetArmedUntil = 0; }
							else g_resetArmedUntil = NowMs() + 3000;
						}
						if (row.id == ID_VOIP_RESETP) VoiceMix::ResetPlayers();
						if (row.id == ID_GFXRESET) RestoreGfx();
						if (row.id == ID_RESUME) { g_open.store(false); if (g_dirty) SaveCfg(); g_drag = Drag(); return; }
						if (row.id == ID_QUIT)
						{
							if (NowMs() < g_quitArmedUntil)
							{
								if (g_dirty) SaveCfg();
								if (pJavaWrapper) pJavaWrapper->exitGame();
							}
							else g_quitArmedUntil = NowMs() + 3000;
						}
					}
				}
			}
			if (g_dirty) SaveCfg();
			g_drag = Drag();
		}
		// toques de outros dedos: consumidos e ignorados
	}

	// ---- pintura ----
	r->drawRect(ImVec2(0, 0), scr, ImColor(0, 0, 0, 110), true);

	r->drawText(ImVec2(x0, y0), cWhite, "Máximo RP", false, 22.0f * k);

	// botao voltar
	r->drawRect(backA, backB, cPanel, true);
	{
		const ImVec2 ts = r->calculateTextSize("VOLTAR", 11.0f * k);
		r->drawText(ImVec2((backA.x + backB.x - ts.x) * 0.5f, (backA.y + backB.y - ts.y) * 0.5f), cText, "VOLTAR", false, 11.0f * k);
	}

	// abas
	for (int i = 0; i < 6; i++)
	{
		const float ax = x0 + i * (tabW + tabGap);
		const bool act = (i == g_tab);
		r->drawRect(ImVec2(ax, tabY), ImVec2(ax + tabW, tabY + tabH), act ? cWhite : cPanel, true);
		if (act) r->drawRect(ImVec2(ax, tabY - 3.0f * k), ImVec2(ax + tabW, tabY), cBlue, true);
		const ImVec2 ts = r->calculateTextSize(kTabs[i], 11.0f * k);
		r->drawText(ImVec2(ax + (tabW - ts.x) * 0.5f, tabY + (tabH - ts.y) * 0.5f), act ? cDark : cText, kTabs[i], false, 11.0f * k);
	}

	// sub-abas (so em CONFIGURACOES)
	if (hasSub)
	{
		for (int i = 0; i < 3; i++)
		{
			const float ay = bodyY + i * (rowH + 2.0f * k);
			const bool act = (i == g_sub);
			r->drawRect(ImVec2(x0, ay), ImVec2(x0 + subW, ay + rowH), act ? cWhite : cPanel, true);
			r->drawText(ImVec2(x0 + pad, ay + (rowH - fs) * 0.5f), act ? cDark : cText, kSubs[i]->name, false, fs);
		}
	}

	// painel de linhas
	r->drawRect(ImVec2(panX, bodyY), ImVec2(panX + panW, bodyY + bodyH), cPanel2, true);
	r->drawRect(ImVec2(panX, bodyY), ImVec2(panX + panW, bodyY + 2.0f * k), cLine, true);

	r->pushClipRect(ImVec2(panX, bodyY), ImVec2(panX + panW, bodyY + bodyH), true);
	for (int i = 0; i < nRows; i++)
	{
		const float ry = bodyY + i * rowH - g_scroll;
		if (ry + rowH < bodyY || ry > bodyY + bodyH) continue;

		const Row& row = page->rows[i];
		const bool sup = (row.kind != K_INFO);
		const float ty = ry + (rowH - fs) * 0.5f;

		if (i & 1) r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), ImColor(255, 255, 255, 10), true);
		r->drawText(ImVec2(panX + pad, ty), sup ? cText : cDim, row.label, false, fs);

		if (row.kind == K_INFO)
		{
			if (row.id == ID_ASPECT)
			{
				const char* s = "AUTO (recomendado)";
				const ImVec2 ts = r->calculateTextSize(s, fs);
				r->drawText(ImVec2(panX + panW - pad - ts.x, ty), cText, s, false, fs);
			}
			if (row.id == ID_NONE && g_tab != T_SAIR && g_tab != T_MAPA && g_tab != T_CONTROLES && g_tab != T_JOGO
				&& !(g_tab == T_CONFIG && g_sub == 2))
			{
				const char* s = "Indisponível";
				const ImVec2 ts = r->calculateTextSize(s, fs);
				r->drawText(ImVec2(panX + panW - pad - ts.x, ty), cDim, s, false, fs);
			}
		}
		else if (row.kind == K_TOGGLE)
		{
			const int v = GetValue(row);
			const char* s = (v >= 1) ? "Ativado" : "Desativado";
			const ImVec2 ts = r->calculateTextSize(s, fs);
			r->drawText(ImVec2(panX + panW - pad - ts.x, ty), cText, s, false, fs);
		}
		else if (row.kind == K_CHOICE)
		{
			int mn, mx; RangeOf(row, mn, mx);
			const int v = GetValue(row);
			char buf[48];
			const char* s = buf;
			if (row.id == ID_VOIP_PRESET || row.id == ID_VOIP_PLAYER)
			{
				const int pct = (row.id == ID_VOIP_PLAYER) ? VoiceMix::GetPlayer(row.nidx) : VoiceMix::GetMaster();
				if (v >= 0 && v < VoiceMix::kLevels) snprintf(buf, sizeof(buf), "%s (%d%%)", VoiceMix::kLevelName[v], pct);
				else snprintf(buf, sizeof(buf), "Personalizado (%d%%)", pct);
			}
			else if (row.id == ID_PRESET) s = (v >= 0 && v < P_COUNT) ? kPresets[v].name : "Personalizada";
			else if (row.id == ID_RESSCALE) { snprintf(buf, sizeof(buf), (v == 5) ? "%d%% (padrão)" : "%d%%", GfxSettings::kResOpts[std::clamp(v, 0, 5)]); }
			else if (row.nidx == N_SHADOW && v == 2) s = "Avançado";
			else if (row.nidx == N_TLAYOUT) s = (v == 0) ? "Clássico" : "Alternativo";
			else if (row.nidx == N_STEER)   s = (v == 0) ? "Analógico" : (v == 1) ? "Botões" : "Inclinação";
			else if (row.nidx == N_ACCEL)   s = (v == 0) ? "Desligado" : (v == 1) ? "Ligado" : "Ligado (invertido)";
			else if (v >= mx) s = "Máximo";
			else if (v <= mn) s = "Mínimo";
			else snprintf(buf, sizeof(buf), "Nível %d", v);
			const ImVec2 ts = r->calculateTextSize(s, fs);
			r->drawText(ImVec2(panX + panW - pad - ts.x, ty), cText, s, false, fs);
		}
		else if (row.kind == K_SLIDER)
		{
			int mn, mx; RangeOf(row, mn, mx);
			const int v = GetValue(row);
			const float t = std::clamp((float)(v - mn) / (float)(mx - mn), 0.0f, 1.0f);
			const float cy = ry + rowH * 0.5f;
			r->drawRect(ImVec2(trackX, cy - 2.0f * k), ImVec2(trackX + trackW, cy + 2.0f * k), ImColor(70, 76, 80, 255), true);
			r->drawRect(ImVec2(trackX, cy - 2.0f * k), ImVec2(trackX + trackW * t, cy + 2.0f * k), cBlue, true);
			r->drawRect(ImVec2(trackX + trackW * t - 3.0f * k, cy - 7.0f * k), ImVec2(trackX + trackW * t + 3.0f * k, cy + 7.0f * k), cWhite, true);

			char buf[24];
			// escala 0..100 mostra o valor do jogo direto; outras escalas viram porcentagem
			if (row.id == ID_SAFEZONE) snprintf(buf, sizeof(buf), "%d%%", GfxSettings::kSafeOpts[std::clamp(v, 0, 4)]); else
			snprintf(buf, sizeof(buf), "%d%%", (mx == 100 || row.nidx == N_ANALOG || row.id == ID_VOIP_VOL) ? v : (int)std::lround(100.0f * (v - mn) / (mx - mn)));
			const ImVec2 ts = r->calculateTextSize(buf, fs);
			r->drawText(ImVec2(trackX - 10.0f * k - ts.x, ty), cText, buf, false, fs);
		}
		else if (row.kind == K_ACTION)
		{
			const char* s = ">";
			if (row.id == ID_CTLMODE) s = ControlMode::IsModern() ? "Moderno >" : "Clássico >";
			if (row.id == ID_QUIT && NowMs() < g_quitArmedUntil) s = "Toque de novo para sair";
			if (row.id == ID_MC_RESET && NowMs() < g_resetArmedUntil) s = "Toque de novo para confirmar";
			const ImVec2 ts = r->calculateTextSize(s, fs);
			r->drawText(ImVec2(panX + panW - pad - ts.x, ty), (row.id == ID_QUIT || (row.id == ID_MC_RESET && NowMs() < g_resetArmedUntil)) ? ImColor(235, 90, 80, 255) : cText, s, false, fs);
		}
	}
	r->popClipRect();
}
