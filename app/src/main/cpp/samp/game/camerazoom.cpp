#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"
#include "../vendor/inih/cpp/INIReader.h"
#include "camerazoom.h"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

extern CNetGame* pNetGame;

// offsets dentro do TheCamera (CCamera) - ver camerazoom.h
static const int OFF_ZOOM_CAR = 0xB8;   // Process_FollowCar_SA
static const int OFF_ZOOM_PED = 0xCC;   // Process_FollowPed_SA
static const unsigned WIDGET_LOOK = 0xAF;

namespace
{
	int  g_pedLevel = 0;      // 0 = ainda nao conhecido
	int  g_carLevel = 0;
	bool g_pinch    = false;  // false = pinca desativada (padrao)
	bool g_loaded   = false;
	int  g_frame    = 0;

	bool (*IsPinchZooming_orig)(unsigned id, void* pos, int n) = nullptr;

	// ---------------- helpers de INI (edicao textual: preserva o resto do arquivo) ----------------
	std::string SettingsPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	static std::string Trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && isspace((unsigned char)s[a])) a++;
		while (b > a && isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}
	static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

	// Grava key=value dentro de [section]. Cria a secao/chave se nao existir.
	bool IniSet(const std::string& path, const std::string& section, const std::string& key, const std::string& value)
	{
		std::vector<std::string> lines;
		bool crlf = false;
		{
			std::ifstream in(path, std::ios::binary);
			std::string l;
			while (std::getline(in, l))
			{
				if (!l.empty() && l.back() == '\r') { crlf = true; l.pop_back(); }
				lines.push_back(l);
			}
		}

		const std::string header = "[" + Lower(section) + "]";
		int secStart = -1, secEnd = (int)lines.size();
		for (int i = 0; i < (int)lines.size(); i++)
		{
			std::string t = Trim(lines[i]);
			if (!t.empty() && t[0] == '[')
			{
				if (secStart >= 0) { secEnd = i; break; }
				if (Lower(t) == header) secStart = i;
			}
		}

		const std::string newLine = key + "=" + value;
		if (secStart < 0)
		{
			if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
			lines.push_back("[" + section + "]");
			lines.push_back(newLine);
		}
		else
		{
			int lastNonEmpty = secStart, found = -1;
			for (int i = secStart + 1; i < secEnd; i++)
			{
				std::string t = Trim(lines[i]);
				if (t.empty()) continue;
				lastNonEmpty = i;
				if (t[0] == ';' || t[0] == '#') continue;
				size_t eq = t.find('=');
				if (eq != std::string::npos && Lower(Trim(t.substr(0, eq))) == Lower(key)) { found = i; break; }
			}
			if (found >= 0) lines[found] = newLine;
			else lines.insert(lines.begin() + lastNonEmpty + 1, newLine);
		}

		std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) return false;
			for (auto& l : lines) out << l << (crlf ? "\r\n" : "\n");
			if (!out.good()) return false;
		}
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	// ---------------- camera ----------------
	uintptr_t Cam()
	{
		static uintptr_t cam = CHook::getSym("TheCamera");
		return cam;
	}
	int& ZoomPed() { return *(int*)(Cam() + OFF_ZOOM_PED); }
	int& ZoomCar() { return *(int*)(Cam() + OFF_ZOOM_CAR); }
	bool ValidLevel(int v) { return v >= 1 && v <= 3; }

	void LoadIfNeeded()
	{
		if (g_loaded) return;
		std::string p = SettingsPath();
		if (p.empty()) return;                    // storage ainda nao definido: tenta no proximo frame
		g_loaded = true;

		INIReader reader(p);
		if (reader.ParseError() < 0) { FLog("[CAMERA] settings.ini nao lido (%s)", p.c_str()); return; }

		int ped = (int)reader.GetInteger("camera", "ped_zoom", 0);
		int car = (int)reader.GetInteger("camera", "car_zoom", 0);
		g_pedLevel = ValidLevel(ped) ? ped : 0;
		g_carLevel = ValidLevel(car) ? car : 0;
		g_pinch    = reader.GetBoolean("camera", "pinch", false);
		FLog("[CAMERA] settings: ped_zoom=%d car_zoom=%d pinch=%d", g_pedLevel, g_carLevel, (int)g_pinch);
	}

	void Save()
	{
		std::string p = SettingsPath();
		if (p.empty()) return;
		bool ok = true;
		if (g_pedLevel) ok &= IniSet(p, "camera", "ped_zoom", std::to_string(g_pedLevel));
		if (g_carLevel) ok &= IniSet(p, "camera", "car_zoom", std::to_string(g_carLevel));
		ok &= IniSet(p, "camera", "pinch", g_pinch ? "1" : "0");
		FLog("[CAMERA] salvo em settings.ini: ped_zoom=%d car_zoom=%d pinch=%d (%s)",
			 g_pedLevel, g_carLevel, (int)g_pinch, ok ? "ok" : "FALHOU");
	}

	bool LocalInVehicle()
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp || !lp->GetPlayerPed() || !lp->GetPlayerPed()->m_pPed) return false;
		return lp->GetPlayerPed()->m_pPed->IsInVehicle();
	}

	// CTouchInterface::IsPinchZooming(WidgetIDs, CVector2D*, int)
	// O ORIGINAL e sempre chamado: com n>=1 ele faz CWidget::SetEnabled(true) no widget, e sem isso
	// a regiao de "olhar" (arrastar para girar a camera) ficaria desligada. So o RESULTADO muda.
	bool IsPinchZooming_hook(unsigned id, void* pos, int n)
	{
		bool r = IsPinchZooming_orig ? IsPinchZooming_orig(id, pos, n) : false;
		if (id == WIDGET_LOOK && !g_pinch)
			return false;                          // 2 dedos NAO alteram mais o zoom da camera
		return r;                                  // outros widgets (ex.: mapa do menu) intactos
	}
}

void CCameraZoom::Init()
{
	CHook::InlineHook("_ZN15CTouchInterface14IsPinchZoomingENS_9WidgetIDsEP9CVector2Di",
					  &IsPinchZooming_hook, &IsPinchZooming_orig);
}

void CCameraZoom::Process()
{
	LoadIfNeeded();
	if (!g_loaded) return;
	if (++g_frame < 60) return;                   // deixa o jogo inicializar a camera

	int& ped = ZoomPed();
	int& car = ZoomCar();
	bool dirty = false;

	if (g_pinch)
	{
		// pinca ligada: segue o que o jogador fez e salva quando mudar
		if (ValidLevel(ped) && ped != g_pedLevel) { g_pedLevel = ped; dirty = true; }
		if (ValidLevel(car) && car != g_carLevel) { g_carLevel = car; dirty = true; }
	}
	else
	{
		// pinca desativada: 1a vez guarda em qual nivel esta; depois mantem o nivel salvo
		if (!g_pedLevel && ValidLevel(ped)) { g_pedLevel = ped; dirty = true; }
		if (!g_carLevel && ValidLevel(car)) { g_carLevel = car; dirty = true; }
		if (g_pedLevel && ped != g_pedLevel) ped = g_pedLevel;
		if (g_carLevel && car != g_carLevel) car = g_carLevel;
	}

	if (dirty) Save();
}

std::string CCameraZoom::DebugCommand(const std::string& argsIn)
{
	LoadIfNeeded();

	std::vector<std::string> tok;
	{
		std::istringstream ss(Lower(Trim(argsIn)));
		std::string t;
		while (ss >> t) tok.push_back(t);
	}

	char buf[200];
	auto status = [&]() {
		snprintf(buf, sizeof(buf), "[CAMERA] a pe=%d | veiculo=%d | pinca=%s",
				 g_pedLevel ? g_pedLevel : ZoomPed(), g_carLevel ? g_carLevel : ZoomCar(), g_pinch ? "ligada" : "desativada");
		return std::string(buf);
	};
	const std::string usage = " | uso: /camera <1-3> | /camera pe <1-3> | /camera carro <1-3> | /camera pinca on|off";

	if (tok.empty() || tok[0] == "info" || tok[0] == "status")
		return status() + usage;

	if (tok[0] == "pinca" || tok[0] == "pinch")
	{
		if (tok.size() < 2 || (tok[1] != "on" && tok[1] != "off" && tok[1] != "1" && tok[1] != "0"))
			return "[CAMERA] use: /camera pinca on|off";
		g_pinch = (tok[1] == "on" || tok[1] == "1");
		Save();
		return status();
	}

	enum { TARGET_AUTO, TARGET_PED, TARGET_CAR } target = TARGET_AUTO;
	std::string num;
	if (tok[0] == "pe" || tok[0] == "ped" || tok[0] == "andando")                        { target = TARGET_PED; num = tok.size() > 1 ? tok[1] : ""; }
	else if (tok[0] == "carro" || tok[0] == "veiculo" || tok[0] == "car" || tok[0] == "vehicle") { target = TARGET_CAR; num = tok.size() > 1 ? tok[1] : ""; }
	else num = tok[0];

	char* end = nullptr;
	long v = strtol(num.c_str(), &end, 10);
	if (num.empty() || *end != '\0' || !ValidLevel((int)v))
		return "[CAMERA] nivel invalido (use 1, 2 ou 3)" + usage;

	if (target == TARGET_AUTO) target = LocalInVehicle() ? TARGET_CAR : TARGET_PED;

	if (target == TARGET_PED) { g_pedLevel = (int)v; ZoomPed() = (int)v; }
	else                      { g_carLevel = (int)v; ZoomCar() = (int)v; }
	Save();

	return std::string(target == TARGET_PED ? "[CAMERA] zoom a pe = " : "[CAMERA] zoom veiculo = ") + std::to_string(v) + " (salvo)";
}
