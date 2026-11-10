#include "../../main.h"
#include "../../settings.h"
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "controlmode.h"

#include <atomic>
#include <mutex>
#include <string>

extern CSettings* pSettings;

namespace
{
	std::mutex             g_mu;
	CtlLogic::ModernConfig g_cfg;
	std::atomic<int>       g_mode{ 0 };            // Mode::Classic
	std::atomic<bool>      g_chosen{ false };
	std::atomic<uint32_t>  g_ver{ 1 };
	std::atomic<bool>      g_loaded{ false };
	std::string            g_loadedSection;         // secao para a qual g_cfg/g_mode foram carregados
	CtlLogic::HudLayout    g_huds[CtlLogic::kHudCount];   // HUD 1..4 salvos (g_cfg espelha o ativo)
	int                    g_hud = 0;               // HUD ativo (0..3)

	std::string IniPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	std::string CurSection()
	{
		return CtlLogic::SectionFor(pSettings ? pSettings->Get().szNickName : nullptr);
	}

	constexpr long kLayoutVersion = 2;       // 2 = HUD padrao de 8/nov (posicoes do print final + todas as opacidades em 50%)

	void LoadLocked(const std::string& sec)
	{
		using namespace CtlLogic;
		g_cfg = ModernConfig();                      // padroes
		for (auto& h : g_huds) h = HudLayout();
		g_hud = 0;
		g_mode.store((int)Mode::Classic);            // padrao: CLASSICO
		g_chosen.store(false);
		const std::string path = IniPath();
		CSimpleIniA ini;
		ini.SetUnicode();
		if (!path.empty() && ini.LoadFile(path.c_str()) >= 0)
		{
			const char* s = sec.c_str();
			g_mode.store((int)ModeFromString(ini.GetValue(s, "modo", "classico")));
			g_chosen.store(ini.GetLongValue(s, "escolhido", 0) != 0);
			g_cfg.joyPos.x = (float)ini.GetDoubleValue(s, "joy_x", g_cfg.joyPos.x);
			g_cfg.joyPos.y = (float)ini.GetDoubleValue(s, "joy_y", g_cfg.joyPos.y);
			g_cfg.joyScale = (float)ini.GetDoubleValue(s, "joy_escala", g_cfg.joyScale);
			g_cfg.btnScale = (float)ini.GetDoubleValue(s, "botoes_escala", g_cfg.btnScale);
			g_cfg.alpha    = (float)ini.GetDoubleValue(s, "transparencia", g_cfg.alpha);
			g_cfg.sens     = (float)ini.GetDoubleValue(s, "sensibilidade", g_cfg.sens);
			g_cfg.deadzone = (float)ini.GetDoubleValue(s, "zona_morta", g_cfg.deadzone);
			g_cfg.maxDist  = (float)ini.GetDoubleValue(s, "distancia_max", g_cfg.maxDist);
			g_cfg.aimDrag  = ini.GetLongValue(s, "mira_arrastavel", g_cfg.aimDrag ? 1 : 0) != 0;
			g_cfg.showInv   = ini.GetLongValue(s, "mostrar_inventario", g_cfg.showInv ? 1 : 0) != 0;
			g_cfg.showSlots = ini.GetLongValue(s, "mostrar_slots", g_cfg.showSlots ? 1 : 0) != 0;
			g_cfg.aimSens  = (float)ini.GetDoubleValue(s, "mira_sensibilidade", g_cfg.aimSens);
			g_cfg.aimSmooth = (float)ini.GetDoubleValue(s, "mira_suavizacao", g_cfg.aimSmooth);
			// Layouts HUD 1..4: chaves hud<N>_*. O HUD 1 usa as chaves antigas (joy_x, b_<botao>_x, transparencia...) como
			// padrao quando as novas ainda nao existem (migracao: quem ja tinha posicoes salvas nao perde nada).
			for (int i = 0; i < B_COUNT; i++)
			{
				std::string k = std::string("b_") + BtnKey(i);
				g_cfg.btnPos[i].x = (float)ini.GetDoubleValue(s, (k + "_x").c_str(), g_cfg.btnPos[i].x);
				g_cfg.btnPos[i].y = (float)ini.GetDoubleValue(s, (k + "_y").c_str(), g_cfg.btnPos[i].y);
			}
			for (int i = 0; i <= B_COUNT; i++) g_cfg.ctlAlpha[i] = g_cfg.alpha;      // opacidade antiga (global) como padrao
			// (inventario/slots seguem a mesma opacidade padrao dos demais: 50%)
			g_cfg.Clamp();
			const HudLayout legacy = ExtractLayout(g_cfg);
			for (int n = 0; n < kHudCount; n++)
			{
				HudLayout h = (n == 0) ? legacy : HudLayout();
				const std::string pf = "hud" + std::to_string(n + 1) + "_";
				h.joyPos.x = (float)ini.GetDoubleValue(s, (pf + "joy_x").c_str(), h.joyPos.x);
				h.joyPos.y = (float)ini.GetDoubleValue(s, (pf + "joy_y").c_str(), h.joyPos.y);
				h.ctlScale[B_COUNT] = (float)ini.GetDoubleValue(s, (pf + "joy_tam").c_str(), h.ctlScale[B_COUNT]);
				h.ctlAlpha[B_COUNT] = (float)ini.GetDoubleValue(s, (pf + "joy_opac").c_str(), h.ctlAlpha[B_COUNT]);
				for (int i = 0; i < B_COUNT; i++)
				{
					const std::string k = pf + BtnKey(i);
					h.btnPos[i].x = (float)ini.GetDoubleValue(s, (k + "_x").c_str(), h.btnPos[i].x);
					h.btnPos[i].y = (float)ini.GetDoubleValue(s, (k + "_y").c_str(), h.btnPos[i].y);
					h.ctlScale[i] = (float)ini.GetDoubleValue(s, (k + "_tam").c_str(), h.ctlScale[i]);
					h.ctlAlpha[i] = (float)ini.GetDoubleValue(s, (k + "_opac").c_str(), h.ctlAlpha[i]);
				}
				ModernConfig tmp; ApplyLayout(tmp, h);        // Clamp
				g_huds[n] = ExtractLayout(tmp);
			}
			g_hud = ClampHud((int)ini.GetLongValue(s, "hud_ativo", 1) - 1);
			// Versao do layout: abaixo da atual, todo mundo entra com o HUD padrao novo (posicoes + opacidade 50%). Migracao unica:
			// gravada em layout_v ao salvar; depois disso os ajustes do jogador voltam a valer.
			if (ini.GetLongValue(s, "layout_v", 0) < kLayoutVersion)
			{
				g_cfg.alpha = ModernConfig().alpha;
				for (auto& h : g_huds) h = HudLayout();
				g_hud = 0;
			}
		}
		if (kClassicMaintenance) { g_mode.store((int)Mode::Modern); g_chosen.store(true); }   // classico em manutencao: todos no MODERNO, sem tela de escolha
		ApplyLayout(g_cfg, g_huds[g_hud]);
		g_loadedSection = sec;
		g_loaded.store(true);
		g_ver.fetch_add(1);
	}

	void SaveLocked()
	{
		using namespace CtlLogic;
		const std::string path = IniPath();
		if (path.empty() || g_loadedSection.empty()) return;
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(path.c_str());                  // preserva as outras secoes ([maximo], [movimento]...)
		const char* s = g_loadedSection.c_str();
		ini.SetValue(s, "modo", ModeToString((Mode)g_mode.load()));
		ini.SetLongValue(s, "escolhido", g_chosen.load() ? 1 : 0);
		ini.SetDoubleValue(s, "joy_escala", g_cfg.joyScale);
		ini.SetDoubleValue(s, "botoes_escala", g_cfg.btnScale);
		ini.SetDoubleValue(s, "transparencia", g_cfg.alpha);
		ini.SetDoubleValue(s, "sensibilidade", g_cfg.sens);
		ini.SetDoubleValue(s, "zona_morta", g_cfg.deadzone);
		ini.SetDoubleValue(s, "distancia_max", g_cfg.maxDist);
		ini.SetLongValue(s, "mira_arrastavel", g_cfg.aimDrag ? 1 : 0);
		ini.SetLongValue(s, "mostrar_inventario", g_cfg.showInv ? 1 : 0);
		ini.SetLongValue(s, "mostrar_slots", g_cfg.showSlots ? 1 : 0);
		ini.Delete(s, "fade_freada", true);     // chave removida: o fade na freada agora e sempre ativo; limpa o que versoes antigas gravaram
		ini.SetDoubleValue(s, "mira_sensibilidade", g_cfg.aimSens);
		ini.SetDoubleValue(s, "mira_suavizacao", g_cfg.aimSmooth);
		// o HUD ativo sempre reflete g_cfg
		g_huds[g_hud] = ExtractLayout(g_cfg);
		ini.SetLongValue(s, "hud_ativo", g_hud + 1);
		ini.SetLongValue(s, "layout_v", kLayoutVersion);
		for (int n = 0; n < kHudCount; n++)
		{
			const HudLayout& h = g_huds[n];
			const std::string pf = "hud" + std::to_string(n + 1) + "_";
			ini.SetDoubleValue(s, (pf + "joy_x").c_str(), h.joyPos.x);
			ini.SetDoubleValue(s, (pf + "joy_y").c_str(), h.joyPos.y);
			ini.SetDoubleValue(s, (pf + "joy_tam").c_str(), h.ctlScale[B_COUNT]);
			ini.SetDoubleValue(s, (pf + "joy_opac").c_str(), h.ctlAlpha[B_COUNT]);
			for (int i = 0; i < B_COUNT; i++)
			{
				const std::string k = pf + BtnKey(i);
				ini.SetDoubleValue(s, (k + "_x").c_str(), h.btnPos[i].x);
				ini.SetDoubleValue(s, (k + "_y").c_str(), h.btnPos[i].y);
				ini.SetDoubleValue(s, (k + "_tam").c_str(), h.ctlScale[i]);
				ini.SetDoubleValue(s, (k + "_opac").c_str(), h.ctlAlpha[i]);
			}
		}
		if (ini.SaveFile(path.c_str()) < 0)
			FLog("[CONTROLES] falha ao salvar %s", path.c_str());
	}
}

using namespace CtlLogic;

bool ControlMode::Loaded() { return g_loaded.load(); }

void ControlMode::EnsureLoaded()
{
	if (IniPath().empty() || !pSettings) return;       // armazenamento/nick ainda nao prontos: tenta no proximo frame
	std::lock_guard<std::mutex> l(g_mu);
	const std::string sec = CurSection();
	if (sec != g_loadedSection) LoadLocked(sec);
}

Mode ControlMode::Get()        { return (Mode)g_mode.load(); }
bool ControlMode::IsModern()   { return g_mode.load() == (int)Mode::Modern; }
bool ControlMode::Chosen()     { return g_chosen.load(); }
uint32_t ControlMode::ConfigVersion() { return g_ver.load(); }

void ControlMode::Set(Mode m)
{
	std::lock_guard<std::mutex> l(g_mu);
	if (CtlLogic::kClassicMaintenance) m = Mode::Modern;     // classico em manutencao
	g_mode.store((int)m);
	g_chosen.store(true);
	SaveLocked();
	FLog("[CONTROLES] modo = %s", ModeToString(m));
}

void ControlMode::MarkChosen()
{
	std::lock_guard<std::mutex> l(g_mu);
	g_chosen.store(true);
	SaveLocked();
}

ModernConfig ControlMode::GetConfig() { std::lock_guard<std::mutex> l(g_mu); return g_cfg; }

void ControlMode::SetConfig(const ModernConfig& c, bool save)
{
	std::lock_guard<std::mutex> l(g_mu);
	g_cfg = c; g_cfg.Clamp();
	g_huds[g_hud] = ExtractLayout(g_cfg);         // o HUD ativo acompanha o que o menu mudou
	g_ver.fetch_add(1);
	if (save) SaveLocked();
}

void ControlMode::ResetConfig()
{
	std::lock_guard<std::mutex> l(g_mu);
	g_cfg = ModernConfig();                       // so os ajustes do MODERNO; nada do classico
	for (auto& h : g_huds) h = HudLayout();       // os 4 HUDs voltam ao padrao
	g_hud = 0;
	g_ver.fetch_add(1);
	SaveLocked();
}

void ControlMode::Save() { std::lock_guard<std::mutex> l(g_mu); SaveLocked(); }

int ControlMode::ActiveHud() { std::lock_guard<std::mutex> l(g_mu); return g_hud; }

void ControlMode::GetHuds(HudLayout* out, int& cur)
{
	std::lock_guard<std::mutex> l(g_mu);
	g_huds[g_hud] = ExtractLayout(g_cfg);
	for (int i = 0; i < kHudCount; i++) out[i] = g_huds[i];
	cur = g_hud;
}

void ControlMode::CommitHuds(const HudLayout* in, int cur, const ModernConfig& cfg)
{
	std::lock_guard<std::mutex> l(g_mu);
	for (int i = 0; i < kHudCount; i++) g_huds[i] = in[i];
	g_hud = ClampHud(cur);
	g_cfg = cfg;                                   // ajustes globais vindos do editor
	ApplyLayout(g_cfg, g_huds[g_hud]);             // layout ativo = o HUD escolhido
	g_ver.fetch_add(1);
	SaveLocked();
}

void ControlMode::SetActiveHud(int n)
{
	std::lock_guard<std::mutex> l(g_mu);
	n = ClampHud(n);
	if (n == g_hud) return;
	g_huds[g_hud] = ExtractLayout(g_cfg);
	g_hud = n;
	ApplyLayout(g_cfg, g_huds[g_hud]);
	g_ver.fetch_add(1);
	SaveLocked();
}
