// runstyle.cpp - ver runstyle.h
#include "../main.h"
#include "game.h"
#include "util.h"
#include "runstyle.h"
#include "Entity/CPedGTA.h"
#include "../vendor/armhook/patch.h"     // CHook
#include "Enums/AnimationEnums.h"
#include "Enums/ePedState.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

extern char* g_pszStorage;

namespace
{
	struct Style { const char* nome; const char* desc; int group; };
	// Grupos de caminhada JA existentes no jogo (ids do enum do projeto). Nao cria animacao nova.
	const Style kStyles[] = {
		{ "civil",     "homem comum",            ANIM_GROUP_MAN },
		{ "cj",        "estilo do CJ",           ANIM_GROUP_PLAYER },
		{ "musculoso", "bracos abertos",         ANIM_GROUP_MUSCULAR },
		{ "gangue",    "gangue 1",               ANIM_GROUP_GANG1 },
		{ "gangue2",   "gangue 2",               ANIM_GROUP_GANG2 },
		{ "corredor",  "corredor",               ANIM_GROUP_JOGGER },
		{ "gordo",     "gordo",                  ANIM_GROUP_FATMAN },
		{ "idoso",     "idoso",                  ANIM_GROUP_OLDMAN },
		{ "arrastado", "arrastado",              ANIM_GROUP_SHUFFLE },
		{ "bebado",    "bebado",                 ANIM_GROUP_DRUNKMAN },
		{ "swat",      "tatico",                 ANIM_GROUP_SWAT },
		{ "mulher",    "mulher",                 ANIM_GROUP_WOMAN },
		{ "sexy",      "mulher sexy",            ANIM_GROUP_SEXYWOMAN },
		{ "apressada", "mulher apressada",       ANIM_GROUP_BUSYWOMAN },
		{ "corredora", "mulher corredora",       ANIM_GROUP_JOGWOMAN },
	};
	constexpr int kN = (int)(sizeof(kStyles) / sizeof(kStyles[0]));
	constexpr uintptr_t kGroupOffset = 0x630;     // o mesmo offset que /reanim ativas le como "grupoAnd"

	std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
	std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }

	int FindStyle(const std::string& n) { const std::string l = Lower(n); for (int i = 0; i < kN; i++) if (l == kStyles[i].nome) return i; return -1; }
	bool IsWalkGroup(int g) { return g == ANIM_GROUP_PLAYER || g == ANIM_GROUP_MUSCULAR || g == ANIM_GROUP_PLAYERROCKET || (g >= ANIM_GROUP_MAN && g <= ANIM_GROUP_SKATE); }

	// ---- arquivo SAMP/corrida.ini: [padrao] skin=estilo | [jogador] skin=estilo ("*" = todas as skins) ----
	std::map<std::string, std::map<std::string, std::string>> g_ini;
	bool g_loaded = false;
	int  g_gen = 0;      // muda a cada alteracao (invalida o cache do Tick)

	std::string Path() { return (g_pszStorage && g_pszStorage[0]) ? std::string(g_pszStorage) + "SAMP/corrida.ini" : std::string(); }

	void Load()
	{
		if (g_loaded) return;
		const std::string p = Path(); if (p.empty()) return;
		g_loaded = true; g_ini.clear();
		std::ifstream in(p); std::string line, sec;
		while (std::getline(in, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;
			if (line[0] == '[') { size_t e = line.find(']'); if (e != std::string::npos) sec = Lower(line.substr(1, e - 1)); continue; }
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			g_ini[sec][Lower(Trim(line.substr(0, eq)))] = Lower(Trim(line.substr(eq + 1)));
		}
		FLog("[CORRIDA] corrida.ini lido (%zu secoes)", g_ini.size());
	}

	bool Save()
	{
		const std::string p = Path(); if (p.empty()) return false;
		const std::string tmp = p + ".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc); if (!o) return false;
			o << "; Estilos de andar/correr. Use /corrida lista no jogo. Linhas: skin=estilo (ou *=estilo para todas).\n";
			o << "; [padrao] = padrao do servidor por skin (voce escolhe) | [jogador] = escolha salva do jogador.\n";
			for (const char* s : { "padrao", "jogador" })
			{
				o << "\n[" << s << "]\n";
				auto it = g_ini.find(s); if (it == g_ini.end()) continue;
				for (auto& kv : it->second) o << kv.first << "=" << kv.second << "\n";
			}
			if (!o.good()) return false;
		}
		return rename(tmp.c_str(), p.c_str()) == 0;
	}

	CPedGTA* Ped() { return GamePool_FindPlayerPed(); }

	// estilo desejado para a skin (indice em kStyles) ou -1 = nativo
	int Wanted(int skin)
	{
		Load();
		const std::string k = std::to_string(skin);
		for (const char* sec : { "jogador", "padrao" })
		{
			auto it = g_ini.find(sec); if (it == g_ini.end()) continue;
			for (const std::string& key : { k, std::string("*") })
			{
				auto f = it->second.find(key); if (f == it->second.end()) continue;
				if (f->second == "nativo") return -1;
				int i = FindStyle(f->second); if (i >= 0) return i;
			}
		}
		return -1;
	}

	// estado do Tick
	int g_lastSkin = -1, g_lastGen = -1, g_orig = -1, g_applied = -1;
	bool g_offsetOk = true;

	void RefreshMoveAnim(CPedGTA* ped)
	{
		// CPed::SetMoveAnim() reaplica a locomocao do grupo atual (se o simbolo existir; senao pega no proximo passo)
		static void (*fn)(void*) = (void(*)(void*))(CHook::lib ? dlsym(CHook::lib, "_ZN4CPed11SetMoveAnimEv") : nullptr);
		if (fn) fn(ped);
	}
}

void RunStyle::Tick()
{
	CPedGTA* ped = Ped();
	if (!ped || !g_offsetOk) return;
	int* grp = (int*)((uintptr_t)ped + kGroupOffset);
	const int cur = *grp;
	if (!IsWalkGroup(cur)) return;                          // so mexe quando o ped esta num grupo de caminhada conhecido
	if (ped->m_fHealth <= 0.0f || ped->m_nPedState == PEDSTATE_DIE || ped->m_nPedState == PEDSTATE_DEAD) return;

	const int skin = (int)ped->m_nModelIndex;
	if (cur != g_applied) g_orig = cur;                     // o grupo "natural" da skin (o jogo o repoe ao trocar de skin)
	if (skin == g_lastSkin && g_gen == g_lastGen && cur == g_applied) return;   // nada a fazer
	g_lastSkin = skin; g_lastGen = g_gen;

	const int w = Wanted(skin);
	const int target = (w >= 0) ? kStyles[w].group : g_orig;
	if (target < 0 || target == cur) { g_applied = (w >= 0) ? target : -1; return; }
	*grp = target;
	g_applied = (w >= 0) ? target : -1;
	RefreshMoveAnim(ped);
	FLog("[CORRIDA] skin %d: grupo %d -> %d (%s)", skin, cur, target, w >= 0 ? kStyles[w].nome : "nativo");
}

std::string RunStyle::Command(const std::string& argsIn)
{
	Load();
	std::vector<std::string> t; { std::istringstream ss(Lower(Trim(argsIn))); std::string x; while (ss >> x) t.push_back(x); }
	CPedGTA* ped = Ped();
	const int skin = ped ? (int)ped->m_nModelIndex : -1;
	const std::string sk = std::to_string(skin);
	auto Atual = [&]() { int w = skin >= 0 ? Wanted(skin) : -1; return w >= 0 ? std::string(kStyles[w].nome) : std::string("nativo"); };

	if (t.empty())
		return "[CORRIDA] skin " + sk + " | estilo: " + Atual() + " | /corrida lista | /corrida <estilo> | /corrida todas <estilo> | /corrida padrao | /corrida reset";
	if (t[0] == "lista")
	{
		std::string o = "[CORRIDA] estilos: nativo";
		for (int i = 0; i < kN; i++) o += std::string(", ") + kStyles[i].nome;
		return o;
	}
	if (t[0] == "reset") { g_ini["jogador"].clear(); ++g_gen; return Save() ? "[CORRIDA] suas escolhas foram apagadas" : "[CORRIDA] apagado (FALHA ao salvar)"; }
	if (t[0] == "padrao") { if (skin < 0) return "[CORRIDA] ped indisponivel"; g_ini["jogador"].erase(sk); ++g_gen; Save(); return "[CORRIDA] skin " + sk + ": voltou ao padrao (" + Atual() + ")"; }
	if (t[0] == "defskin")
	{
		if (t.size() != 3) return "[CORRIDA] uso: /corrida defskin <skin> <estilo|nativo>";
		if (t[2] != "nativo" && FindStyle(t[2]) < 0) return "[CORRIDA] estilo invalido (/corrida lista)";
		g_ini["padrao"][t[1]] = t[2]; ++g_gen;
		return Save() ? "[CORRIDA] padrao da skin " + t[1] + " = " + t[2] + " (salvo)" : "[CORRIDA] FALHA ao salvar";
	}
	std::string key = sk, nome;
	if (t[0] == "todas" && t.size() == 2) { key = "*"; nome = t[1]; }
	else if (t.size() == 1) nome = t[0];
	else return "[CORRIDA] uso: /corrida <estilo> | /corrida todas <estilo> | /corrida lista";
	if (skin < 0 && key != "*") return "[CORRIDA] ped indisponivel";
	if (nome != "nativo" && FindStyle(nome) < 0) return "[CORRIDA] estilo invalido: " + nome + " (/corrida lista)";
	g_ini["jogador"][key] = nome; ++g_gen;
	const bool ok = Save();
	return std::string("[CORRIDA] ") + (key == "*" ? "todas as skins" : "skin " + key) + " = " + nome + (ok ? " (salvo)" : " (FALHA ao salvar)");
}
