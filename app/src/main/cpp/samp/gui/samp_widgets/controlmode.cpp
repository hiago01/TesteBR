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

	std::string IniPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	std::string CurSection()
	{
		return CtlLogic::SectionFor(pSettings ? pSettings->Get().szNickName : nullptr);
	}

	void LoadLocked(const std::string& sec)
	{
		using namespace CtlLogic;
		g_cfg = ModernConfig();                      // padroes
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
			for (int i = 0; i < B_COUNT; i++)
			{
				std::string k = std::string("b_") + BtnKey(i);
				g_cfg.btnPos[i].x = (float)ini.GetDoubleValue(s, (k + "_x").c_str(), g_cfg.btnPos[i].x);
				g_cfg.btnPos[i].y = (float)ini.GetDoubleValue(s, (k + "_y").c_str(), g_cfg.btnPos[i].y);
			}
		}
		g_cfg.Clamp();
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
		ini.SetDoubleValue(s, "joy_x", g_cfg.joyPos.x);
		ini.SetDoubleValue(s, "joy_y", g_cfg.joyPos.y);
		ini.SetDoubleValue(s, "joy_escala", g_cfg.joyScale);
		ini.SetDoubleValue(s, "botoes_escala", g_cfg.btnScale);
		ini.SetDoubleValue(s, "transparencia", g_cfg.alpha);
		ini.SetDoubleValue(s, "sensibilidade", g_cfg.sens);
		ini.SetDoubleValue(s, "zona_morta", g_cfg.deadzone);
		ini.SetDoubleValue(s, "distancia_max", g_cfg.maxDist);
		for (int i = 0; i < B_COUNT; i++)
		{
			std::string k = std::string("b_") + BtnKey(i);
			ini.SetDoubleValue(s, (k + "_x").c_str(), g_cfg.btnPos[i].x);
			ini.SetDoubleValue(s, (k + "_y").c_str(), g_cfg.btnPos[i].y);
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
	g_ver.fetch_add(1);
	if (save) SaveLocked();
}

void ControlMode::ResetConfig()
{
	std::lock_guard<std::mutex> l(g_mu);
	g_cfg = ModernConfig();                       // so os ajustes do MODERNO; nada do classico
	g_ver.fetch_add(1);
	SaveLocked();
}

void ControlMode::Save() { std::lock_guard<std::mutex> l(g_mu); SaveLocked(); }
