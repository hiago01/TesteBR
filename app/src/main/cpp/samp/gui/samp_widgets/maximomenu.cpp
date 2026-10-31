#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../settings.h"
#include "../../java/jniutil.h"
#include "../../vendor/armhook/patch.h"
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "maximomenu.h"
#include "worldmap.h"

#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

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
	enum { N_VFX = 0, N_RES = 1, N_DRAW = 2, N_SHADOW = 5, N_REFL = 7, N_FX = 11, N_MUSIC = 12,
	       N_RETUNE = 13, N_BRIGHT = 16, N_SUBT = 24, N_COUNT = 37 };

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
		int32_t* nb = NativeField(N_BRIGHT, 0x10);
		if (nb && *nb != g_cfg.bright)
		{
			if (s_logs++ < 12) FLog("[MaximoMenu] brilho desfeito pelo jogo (tabela=%d) -> restaurando %d", (int)*nb, g_cfg.bright);
			*nb = g_cfg.bright;
		}
	}

	void ApplyStream(int pct)
	{
		g_iBassStreamVolume = std::clamp(pct, 0, 100) * 100;
	}

	void LoadCfg()
	{
		g_cfgLoaded = true;
		const std::string path = IniPath();
		if (path.empty()) return;

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(path.c_str()) < 0) return;

		g_skid.store(ini.GetLongValue("maximo", "skidmarks", 1) != 0);
		g_cfg.stream = std::clamp((int)ini.GetLongValue("maximo", "vol_stream", 50), 0, 100);
		long d = ini.GetLongValue("maximo", "draw_dist", -1), b = ini.GetLongValue("maximo", "brightness", -1);
		int mn = 0, mx = 100;
		if (d >= 0) { NativeRange(N_DRAW, mn, mx);   g_cfg.draw   = std::clamp((int)d, mn, mx); }
		if (b >= 0) { NativeRange(N_BRIGHT, mn, mx); g_cfg.bright = std::clamp((int)b, mn, mx); }
	}

	void SaveCfg()
	{
		g_dirty = false;
		NativeSave();
		const std::string path = IniPath();
		if (path.empty()) return;

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(path.c_str());   // se nao existir, cria

		ini.SetLongValue("maximo", "skidmarks", g_skid.load() ? 1 : 0);
		ini.SetLongValue("maximo", "vol_stream", g_cfg.stream);
		if (g_cfg.draw >= 0)   ini.SetLongValue("maximo", "draw_dist", g_cfg.draw);
		if (g_cfg.bright >= 0) ini.SetLongValue("maximo", "brightness", g_cfg.bright);
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
	enum { ID_NONE = 0, ID_SKID, ID_FPSCOUNT, ID_STREAM, ID_NATIVE, ID_RESUME, ID_QUIT, ID_PRESET };

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
	}};
	const Page kSubTela = { "Tela", {
		{ "Brilho",                      K_SLIDER, ID_NATIVE, N_BRIGHT },
		{ "Resolução",                   K_SLIDER, ID_NATIVE, N_RES },
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
		{ "Estação de rádio",             K_INFO, ID_NONE },
		{ "Áudio posicional",             K_INFO, ID_NONE },
		{ "Frequência máxima / HRTF",     K_INFO, ID_NONE },
	}};
	const Page kPageSair = { "Sair", {
		{ "Voltar ao jogo",               K_ACTION, ID_RESUME },
		{ "Sair do jogo",                 K_ACTION, ID_QUIT },
	}};
	const Page kPageNativa = { "", {
		{ "Indisponível: o menu nativo foi desativado", K_INFO, ID_NONE },
		{ "(função original não reimplementada)",       K_INFO, ID_NONE },
	}};

	const Page* const kSubs[3] = { &kSubGraficos, &kSubTela, &kSubChat };

	int GetValue(const Row& r)
	{
		switch (r.id)
		{
			case ID_SKID:     return g_skid.load() ? 1 : 0;
			case ID_FPSCOUNT: return (pSettings && pSettings->Get().iFPSCounter) ? 1 : 0;
			case ID_STREAM:   return g_cfg.stream;
			case ID_PRESET:   return CurrentPreset();
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
			case ID_SKID:     g_skid.store(v != 0); break;
			case ID_FPSCOUNT: if (pSettings) pSettings->Get().iFPSCounter = (v != 0); break;
			case ID_STREAM:   g_cfg.stream = std::clamp(v, 0, 100); ApplyStream(g_cfg.stream); break;
			case ID_PRESET:   ApplyPreset(v); break;
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
		if (r.id == ID_PRESET) { mn = 0; mx = P_COUNT - 1; return; }   // 0..4 (a volta de "Personalizada" cai em 0)
		if (r.id == ID_NATIVE) { NativeRange(r.nidx, mn, mx); return; }
		mn = 0; mx = (r.id == ID_STREAM) ? 100 : 1;
	}

	// ---- estado do desenho (so a thread de draw usa) ----
	int   g_tab = T_CONFIG;
	int   g_sub = 0;
	float g_scroll = 0.0f;
	int64_t g_quitArmedUntil = 0;
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
		case T_AUDIO: page = &kPageAudio; break;
		case T_SAIR:  page = &kPageSair; break;
		case T_CONFIG: page = kSubs[std::clamp(g_sub, 0, 2)]; break;
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
				if (g_drag.kind == 2) { g_tab = g_drag.idx; g_scroll = 0.0f; g_quitArmedUntil = 0; }
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
			char buf[24];
			const char* s = buf;
			if (row.id == ID_PRESET) s = (v >= 0 && v < P_COUNT) ? kPresets[v].name : "Personalizada";
			else if (row.nidx == N_SHADOW && v == 2) s = "Avançado";
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
			snprintf(buf, sizeof(buf), "%d%%", (mx == 100) ? v : (int)std::lround(100.0f * (v - mn) / (mx - mn)));
			const ImVec2 ts = r->calculateTextSize(buf, fs);
			r->drawText(ImVec2(trackX - 10.0f * k - ts.x, ty), cText, buf, false, fs);
		}
		else if (row.kind == K_ACTION)
		{
			const char* s = ">";
			if (row.id == ID_QUIT && NowMs() < g_quitArmedUntil) s = "Toque de novo para sair";
			const ImVec2 ts = r->calculateTextSize(s, fs);
			r->drawText(ImVec2(panX + panW - pad - ts.x, ty), (row.id == ID_QUIT) ? ImColor(235, 90, 80, 255) : cText, s, false, fs);
		}
	}
	r->popClipRect();
}
