#include "../main.h"
#include "game.h"
#include "util.h"
#include "movemode.h"
#include "../gui/samp_widgets/hudstate.h"
#include "Entity/CPedGTA.h"
#include "Tasks/TaskManager.h"
#include "Enums/eTaskType.h"
#include "Enums/ePedState.h"
#include "../vendor/inih/cpp/INIReader.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

void Hud_MirrorSprintPointer(void* ped, int pct);   // game/hooks.cpp

namespace
{
	// ------------------------------------------------------------------------------
	//  Constantes de movimento (modulo do stick, escala +-128 - a mesma que o projeto
	//  ja usa em pad.cpp: 0x80 = stick no maximo).
	//
	//  kDefaultWalk = 0x20 : valor que o PROPRIO projeto aplica ao jogador remoto com
	//                        KEY_WALK (pad.cpp, CPad__GetPedWalkLeftRight_hook). Confirmado no codigo.
	//  kDefaultJog  = 0x50 : NAO confirmado. Ponto medio entre walk (0x20) e maximo (0x80).
	//                        O motor (libGTASA) decide o gait pelo modulo do stick e nao ha
	//                        no repositorio um limiar documentado para o "trote". Por isso e
	//                        calibravel em tempo de execucao: /movimento ajuste jog <8-127>
	//                        (ou [movimento] jog_stick= em settings.ini).
	//  kMax         = 128  : stick cheio.
	//  kDeadzone    = 12   : abaixo disso a entrada e tratada como "joystick solto".
	// ------------------------------------------------------------------------------
	constexpr int kDefaultWalk = 0x20;
	constexpr int kDefaultJog  = 0x50;
	constexpr int kMax         = 128;
	constexpr int kDeadzone    = 12;
	constexpr int kAdjMin      = 8;
	constexpr int kAdjMax      = 127;

	constexpr eMoveMode kDefaultMode = eMoveMode::WALK;

	std::atomic<uint8_t> g_mode{ (uint8_t)kDefaultMode };
	std::atomic<int>     g_walkMag{ kDefaultWalk };
	std::atomic<int>     g_jogMag{ kDefaultJog };
	bool                 g_loaded = false;

	// ---------------- INI (edicao textual: preserva o resto do arquivo) ----------------
	std::string SettingsPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	std::string Trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && isspace((unsigned char)s[a])) a++;
		while (b > a && isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}
	std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

	// Grava key=value dentro de [section] (cria secao/chave se nao existir). Escrita atomica via .tmp.
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

	const char* ModeName(eMoveMode m)
	{
		switch (m)
		{
			case eMoveMode::JOG:    return "JOG";
			case eMoveMode::SPRINT: return "SPRINT";
			default:                return "WALK";
		}
	}

	bool ValidMag(int v) { return v >= kAdjMin && v <= kAdjMax; }

	void LoadIfNeeded()
	{
		if (g_loaded) return;
		std::string p = SettingsPath();
		if (p.empty()) return;            // storage ainda nao definido: tenta na proxima chamada
		g_loaded = true;

		INIReader reader(p);
		if (reader.ParseError() < 0) { FLog("[MOVIMENTO] settings.ini nao lido (%s), usando padrao", p.c_str()); return; }

		// valida o valor carregado; qualquer coisa invalida -> padrao (WALK)
		std::string m = Lower(reader.Get("movimento", "mode", "walk"));
		if      (m == "walk")   g_mode = (uint8_t)eMoveMode::WALK;
		else if (m == "jog")    g_mode = (uint8_t)eMoveMode::JOG;
		else if (m == "sprint") g_mode = (uint8_t)eMoveMode::SPRINT;
		else                    g_mode = (uint8_t)kDefaultMode;

		int w = (int)reader.GetInteger("movimento", "walk_stick", kDefaultWalk);
		int j = (int)reader.GetInteger("movimento", "jog_stick",  kDefaultJog);
		g_walkMag = ValidMag(w) ? w : kDefaultWalk;
		g_jogMag  = ValidMag(j) ? j : kDefaultJog;

		FLog("[MOVIMENTO] settings: mode=%s walk_stick=%d jog_stick=%d",
			 ModeName((eMoveMode)g_mode.load()), g_walkMag.load(), g_jogMag.load());
	}

	bool SaveKey(const char* key, const std::string& value)
	{
		std::string p = SettingsPath();
		if (p.empty()) return false;
		bool ok = IniSet(p, "movimento", key, value);
		FLog("[MOVIMENTO] %s %s=%s", ok ? "salvo:" : "FALHA ao salvar:", key, value.c_str());
		return ok;
	}

	// O modo so se aplica ao jogador local a pe, em locomocao normal.
	// Qualquer situacao especial -> false (o jogo segue sem interferencia).
	bool Eligible()
	{
		CPedGTA* ped = GamePool_FindPlayerPed();
		if (!ped) return false;
		if (ped->IsInVehicle()) return false;
		if (ped->m_fHealth <= 0.0f || ped->m_nPedState == PEDSTATE_DIE || ped->m_nPedState == PEDSTATE_DEAD) return false;
		if (ped->bIsInTheAir || ped->bIsDucking) return false;
		if (LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE]) return false;   // mirando / alvo travado (strafe)
		if (!ped->m_pIntelligence) return false;

		// natacao, escalada, queda, salto: nao mexer
		CTaskManager& tm = ped->GetTaskManager();
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_SWIM))   return false;
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_CLIMB))  return false;
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_FALL))   return false;
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_IN_AIR)) return false;
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_JUMP))   return false;
		return true;
	}

	inline bool StickActive(int16_t lr, int16_t ud)
	{
		const int a = lr, b = ud;
		return (a * a + b * b) >= (kDeadzone * kDeadzone);
	}

	inline int ClampStick(long v)
	{
		return (int)(v > kMax ? kMax : (v < -kMax ? -kMax : v));
	}
}

eMoveMode CMoveMode::Get()
{
	LoadIfNeeded();
	return (eMoveMode)g_mode.load();
}

void CMoveMode::ApplyToStick(int16_t& lr, int16_t& ud)
{
	if (!StickActive(lr, ud)) return;      // joystick solto: nada a fazer (o modo continua guardado)
	LoadIfNeeded();
	if (!Eligible()) return;

	int target;
	switch ((eMoveMode)g_mode.load())
	{
		case eMoveMode::JOG:    target = g_jogMag.load();  break;
		case eMoveMode::SPRINT: target = kMax;             break;
		default:                target = g_walkMag.load(); break;
	}

	// Mantem a direcao; so o modulo do stick passa a ser o do modo escolhido.
	const float mag = sqrtf((float)((int)lr * lr + (int)ud * ud));
	const float s   = (float)target / mag;
	lr = (int16_t)ClampStick(lrintf((float)lr * s));
	ud = (int16_t)ClampStick(lrintf((float)ud * s));
}

uint32_t CMoveMode::ApplySprint(uint32_t original, int16_t lr, int16_t ud)
{
	LoadIfNeeded();

	// Botao de correr: cada TOQUE alterna WALK <-> SPRINT (borda de subida; segurar nao fica trocando).
	static SprintButtonEdge s_btn;
	{
		using namespace std::chrono;
		const long long now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		if (s_btn.OnSample(original != 0, now) && Eligible())
		{
			const eMoveMode next = ToggledMode((eMoveMode)g_mode.load());
			if (next == eMoveMode::SPRINT && !Hud::CanSprint())
				FLog("[MOVIMENTO] botao de correr: sem energia, continua andando");   // cansado: so corre depois de recuperar
			else
			{
				g_mode = (uint8_t)next;
				FLog("[MOVIMENTO] botao de correr: modo %s", ModeName(next));
			}
		}
	}

	// Energia da HUD zerada: derruba a corrida para andar (ver game/stamina.h)
	if ((eMoveMode)g_mode.load() == eMoveMode::SPRINT && !Hud::CanSprint() && Eligible())
	{
		g_mode = (uint8_t)eMoveMode::WALK;
		FLog("[MOVIMENTO] sem energia: voltou a andar");
	}

	// ENERGIA (HUD laranja): o jogo NAO chama HandleSprintEnergy na corrida a pe (so moto/nado), entao o gasto e a
	// recuperacao rodam aqui, todo frame. Correndo = modo SPRINT + stick ativo + a pe. O tick usa o relogio (dt), por isso
	// chamadas repetidas no mesmo frame nao contam em dobro.
	const bool eligible = Eligible();
	const bool sprinting = eligible && StickActive(lr, ud) && (eMoveMode)g_mode.load() == eMoveMode::SPRINT;
	{
		const int pct = Hud::StaminaTick(sprinting);
		if (eligible) Hud_MirrorSprintPointer(GamePool_FindPlayerPed(), pct);
	}

	if (!StickActive(lr, ud)) return original;
	if (!eligible) return original;

	// SPRINT: corrida rapida normal do jogo enquanto houver entrada.
	// WALK/JOG: o botao ja foi tratado acima (o toque troca o modo); aqui ele nao promove a corrida por si so.
	return ((eMoveMode)g_mode.load() == eMoveMode::SPRINT) ? 1u : 0u;
}

std::string CMoveMode::Command(const std::string& argsIn)
{
	LoadIfNeeded();

	std::vector<std::string> tok;
	{
		std::istringstream ss(Lower(Trim(argsIn)));
		std::string t;
		while (ss >> t) tok.push_back(t);
	}

	const std::string usage = "uso: /movimento walk | jog | sprint | reset";
	char buf[160];

	if (tok.empty())
	{
		snprintf(buf, sizeof(buf), "[MOVIMENTO] modo atual: %s | opcoes: walk, jog, sprint, reset", ModeName((eMoveMode)g_mode.load()));
		return buf;
	}

	if (tok[0] == "walk" || tok[0] == "jog" || tok[0] == "sprint")
	{
		eMoveMode m = tok[0] == "walk" ? eMoveMode::WALK : (tok[0] == "jog" ? eMoveMode::JOG : eMoveMode::SPRINT);
		g_mode = (uint8_t)m;
		SaveKey("mode", Lower(ModeName(m)));
		return std::string("[MOVIMENTO] modo ") + ModeName(m) + " ativado";
	}

	if (tok[0] == "reset")
	{
		g_mode    = (uint8_t)kDefaultMode;
		g_walkMag = kDefaultWalk;
		g_jogMag  = kDefaultJog;
		SaveKey("mode", Lower(ModeName(kDefaultMode)));
		SaveKey("walk_stick", std::to_string(kDefaultWalk));
		SaveKey("jog_stick",  std::to_string(kDefaultJog));
		return std::string("[MOVIMENTO] padrao restaurado: ") + ModeName(kDefaultMode);
	}

	// calibracao do modulo do stick: /movimento ajuste walk|jog <8-127>
	if (tok[0] == "ajuste")
	{
		if (tok.size() == 1)
		{
			snprintf(buf, sizeof(buf), "[MOVIMENTO] ajuste: walk=%d jog=%d (faixa %d-%d) | /movimento ajuste walk|jog <valor>",
					 g_walkMag.load(), g_jogMag.load(), kAdjMin, kAdjMax);
			return buf;
		}
		if (tok.size() != 3 || (tok[1] != "walk" && tok[1] != "jog"))
			return "[MOVIMENTO] uso: /movimento ajuste walk|jog <valor>";

		char* end = nullptr;
		long v = strtol(tok[2].c_str(), &end, 10);
		if (tok[2].empty() || *end != '\0' || !ValidMag((int)v))
		{
			snprintf(buf, sizeof(buf), "[MOVIMENTO] valor invalido (use %d a %d)", kAdjMin, kAdjMax);
			return buf;
		}
		if (tok[1] == "walk") { g_walkMag = (int)v; SaveKey("walk_stick", std::to_string(v)); }
		else                  { g_jogMag  = (int)v; SaveKey("jog_stick",  std::to_string(v)); }
		snprintf(buf, sizeof(buf), "[MOVIMENTO] ajuste %s = %ld (salvo)", tok[1].c_str(), v);
		return buf;
	}

	return "[MOVIMENTO] argumento invalido | " + usage;
}
