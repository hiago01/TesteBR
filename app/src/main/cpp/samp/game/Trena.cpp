#include "Trena.h"
#include "../main.h"
#include "game.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sstream>
#include <deque>
#include <vector>

extern CNetGame* pNetGame;

namespace
{
	bool  g_has = false;
	float g_ax = 0, g_ay = 0, g_az = 0;

	bool Here(float& x, float& y, float& z)
	{
		if (!pNetGame) return false;
		CPlayerPool* pool = pNetGame->GetPlayerPool();
		if (!pool) return false;
		CLocalPlayer* lp = pool->GetLocalPlayer();
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		if (!pp || !pp->m_pPed) return false;
		const CVector p = pp->m_pPed->GetPosition();
		x = p.x; y = p.y; z = p.z;
		return true;
	}

	std::string Dist(float x, float y, float z, float bx, float by, float bz, const char* nome)
	{
		const float dx = x - bx, dy = y - by, dz = z - bz;
		char b[200];
		std::snprintf(b, sizeof(b), "[TRENA] ate %s: %.1f m (horizontal %.1f m, altura %.1f m)",
		              nome, std::sqrt(dx * dx + dy * dy + dz * dz), std::sqrt(dx * dx + dy * dy), std::fabs(dz));
		return b;
	}
}

std::string Trena_Command(const std::string& args)
{
	float x, y, z;
	if (!Here(x, y, z)) return "[TRENA] jogador local indisponivel";

	std::vector<std::string> a; { std::istringstream ss(args); std::string t; while (ss >> t) a.push_back(t); }
	char b[200];

	if (!a.empty() && (a[0] == "limpar" || a[0] == "reset")) { g_has = false; return "[TRENA] ponto A apagado"; }
	if (!a.empty() && a[0] == "a")
	{
		g_has = true; g_ax = x; g_ay = y; g_az = z;
		std::snprintf(b, sizeof(b), "[TRENA] ponto A marcado em %.1f %.1f %.1f. Ande ate o outro ponto e use /trena", x, y, z);
		return b;
	}
	if (a.size() >= 4 && a[0] == "ir")
		return Dist(x, y, z, (float)std::atof(a[1].c_str()), (float)std::atof(a[2].c_str()), (float)std::atof(a[3].c_str()), "a coordenada");

	std::snprintf(b, sizeof(b), "[TRENA] voce esta em %.1f %.1f %.1f", x, y, z);
	std::string r = b;
	if (g_has) r += "\n" + Dist(x, y, z, g_ax, g_ay, g_az, "o ponto A");
	else r += "\n[TRENA] sem ponto A: use /trena a (marca aqui) e depois /trena no outro lugar";
	return r;
}

std::string Coord_Command(const std::string& args)
{
	if (!g_pszStorage) return "[COORD] pasta de armazenamento indisponivel";
	const std::string path = std::string(g_pszStorage) + "SAMP/coordenadas.txt";

	std::string a = args;
	while (!a.empty() && a.front() == ' ') a.erase(a.begin());
	while (!a.empty() && a.back() == ' ') a.pop_back();

	if (a == "apagar")
	{
		std::ofstream(path, std::ios::trunc);
		return "[COORD] arquivo esvaziado";
	}
	if (a == "lista")
	{
		std::ifstream f(path); std::deque<std::string> last; std::string l;
		while (std::getline(f, l)) { if (!l.empty()) last.push_back(l); if (last.size() > 8) last.pop_front(); }
		if (last.empty()) return "[COORD] nenhuma coordenada salva (use /coord)";
		std::string r = "[COORD] ultimas salvas (arquivo SAMP/coordenadas.txt):";
		for (auto& x : last) r += "\n" + x;
		return r;
	}

	if (!pNetGame) return "[COORD] fora do jogo";
	CPlayerPool* pool = pNetGame->GetPlayerPool();
	CLocalPlayer* lp = pool ? pool->GetLocalPlayer() : nullptr;
	CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
	if (!pp || !pp->m_pPed) return "[COORD] jogador local indisponivel";
	// dentro de um veiculo: salva a posicao e a direcao DO VEICULO (onde ele deve ficar), nao a do ped
	CVector p = pp->m_pPed->GetPosition();
	float rad = pp->m_pPed->m_fCurrentRotation;
	bool emVeiculo = false; int modelo = 0;
	if (pp->IsInVehicle() && pp->m_pPed->pVehicle)
	{
		CVehicleGTA* v = pp->m_pPed->pVehicle;
		p = v->GetPosition();
		rad = v->GetHeading();                 // atan2(-frente.x, frente.y): mesma convencao do GetVehicleZAngle do SA-MP
		modelo = (int)v->m_nModelIndex;
		emVeiculo = true;
	}
	float ang = rad * 57.2957795f;
	ang = std::fmod(ang, 360.0f); if (ang < 0) ang += 360.0f;

	char b[220];
	std::snprintf(b, sizeof(b), "%.4f, %.4f, %.4f, %.2f", p.x, p.y, p.z, ang);
	std::string line = b;
	if (emVeiculo || !a.empty())
	{
		line += " //";
		if (emVeiculo) line += " veiculo modelo " + std::to_string(modelo);
		if (!a.empty()) line += " " + a;
	}

	std::ofstream f(path, std::ios::app);
	if (!f) return "[COORD] nao consegui abrir " + path;
	f << line << "\n";
	return std::string(emVeiculo ? "[COORD] (veiculo) salvo: " : "[COORD] salvo: ") + line;
}
