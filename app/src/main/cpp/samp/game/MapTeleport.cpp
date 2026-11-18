#include "MapTeleport.h"

#include "../main.h"
#include "game.h"
#include "World.h"
#include "../net/netgame.h"
#include "../net/playerpool.h"
#include "../net/localplayer.h"
#include "../util/CUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>

extern CNetGame* pNetGame;
extern CGame* pGame;

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr float kMaxCoord   = 30000.0f;   // igual ao limite do servidor (Mapa_SendTp)
	constexpr float kDefaultHold = 60.0f;     // altura de espera sem dica: perto o bastante do chao para o jogo carregar o lugar
	constexpr float kMinHold = -20.0f, kMaxHold = 800.0f;
	constexpr double kCheckEvery = 0.12;      // s entre consultas de chao
	constexpr double kMinDwell   = 2.2;       // s minimos esperando o mundo carregar antes de aceitar o chao
	constexpr int    kStableNeeded = 6;       // consultas seguidas com o mesmo chao (+-kTol)
	constexpr float  kTol = 0.35f;
	constexpr double kTimeout = 10.0;         // s: depois disso, desiste (devolve o ped) ou usa o ultimo chao visto
	constexpr double kDropAfter = 4.0;        // s: a consulta de chao nunca respondeu (ex.: fora do mapa original) -> solta o ped e deixa a fisica pousar
	constexpr double kDropMax   = 10.0;       // s maximos de queda/pouso
	constexpr float  kRestVz = 0.01f;         // |velocidade vertical| abaixo disso = parado no chao
	constexpr int    kRestFrames = 12;        // quadros seguidos parado
	constexpr double kSettle = 2.0;           // s depois de posicionar: se carregar algo mais alto (rua/ponte), sobe para cima
	constexpr float  kStand = 1.3f;           // ped acima do chao (a origem do ped fica ~1 m acima dos pes)

	enum class Phase { Idle, Hold, Drop, Settle };

	struct State
	{
		std::mutex mtx;
		bool  reqPending = false; float reqX = 0, reqY = 0, reqZ = -1000.0f;   // pedido vindo da rede
		Phase phase = Phase::Idle;
		float x = 0, y = 0, hold = kDefaultHold;
		float ox = 0, oy = 0, oz = 0;            // origem (para devolver se falhar)
		Clock::time_point t0, lastCheck;
		bool  haveGz = false; float gz = 0; int stable = 0;
		float placedGz = 0;
		int   rest = 0; float lastZ = 0;
	} g;

	double Since(const Clock::time_point& t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
	bool Fin(float v) { return std::isfinite(v); }

	CPlayerPed* LocalPed()
	{
		if (!pNetGame) return nullptr;
		CPlayerPool* pool = pNetGame->GetPlayerPool();
		CLocalPlayer* lp = pool ? pool->GetLocalPlayer() : nullptr;
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		return (pp && pp->m_pPed) ? pp : nullptr;
	}

	// Linha vertical de cima para baixo: true + z do primeiro chao/predio/objeto com colisao carregada.
	bool GroundAt(float x, float y, float& z)
	{
		CVector o(x, y, 1000.0f), t(x, y, -100.0f);
		CColPoint cp;
		CEntityGTA* ent = nullptr;
		if (!CWorld::ProcessLineOfSight(&o, &t, &cp, &ent, true, false, false, true, false, false, false, false)) return false;
		z = cp.m_vecPoint.z;
		return Fin(z) && z > -99.0f && z < 999.0f;
	}

	void Report(const char* cmd)
	{
		if (pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED) pNetGame->SendChatCommand(cmd);
	}

	void Finish(CPlayerPed* pp, bool ok, float finalZ)
	{
		if (!ok && pp)
		{
			pp->m_pPed->SetVelocity(CVector());
			pp->m_pPed->SetPosn(g.ox, g.oy, g.oz);        // sem chao: volta de onde saiu (nao deixa o admin cair no vazio)
		}
		g.phase = Phase::Idle;
		char b[48];
		if (ok) { std::snprintf(b, sizeof(b), "/mapax tpok %d", (int)std::lround(finalZ)); Report(b); }
		else Report("/mapax tpfalha");
		FLog("[MAPTP] %s x=%.1f y=%.1f z=%.1f", ok ? "ok" : "falhou (sem chao)", g.x, g.y, finalZ);
	}
}

void MapTeleport_Start(float x, float y, float zHint)
{
	if (!Fin(x) || !Fin(y) || std::fabs(x) > kMaxCoord || std::fabs(y) > kMaxCoord) return;
	std::lock_guard<std::mutex> lk(g.mtx);
	g.reqPending = true; g.reqX = x; g.reqY = y; g.reqZ = Fin(zHint) ? zHint : -1000.0f;
}

bool MapTeleport_Active() { return g.phase != Phase::Idle; }

void MapTeleport_Reset()
{
	std::lock_guard<std::mutex> lk(g.mtx);
	g.reqPending = false; g.phase = Phase::Idle;
}

void MapTeleport_Update()
{
	float rx = 0, ry = 0, rz = -1000.0f; bool take = false;
	{
		std::lock_guard<std::mutex> lk(g.mtx);
		if (g.reqPending) { take = true; rx = g.reqX; ry = g.reqY; rz = g.reqZ; g.reqPending = false; }
	}
	if (!pGame || !pNetGame) { g.phase = Phase::Idle; return; }
	CPlayerPed* pp = LocalPed();

	if (take && pp && !pp->IsDead())
	{
		const CVector cur = pp->m_pPed->GetPosition();
		g.ox = cur.x; g.oy = cur.y; g.oz = cur.z;
		g.x = rx; g.y = ry;
		g.hold = (rz > -900.0f && rz < 900.0f) ? std::min(std::max(rz + 2.0f, kMinHold), kMaxHold) : kDefaultHold;
		g.t0 = Clock::now(); g.lastCheck = g.t0; g.haveGz = false; g.stable = 0;
		if (CLocalPlayer* lp = pNetGame->GetPlayerPool() ? pNetGame->GetPlayerPool()->GetLocalPlayer() : nullptr) lp->DisableSurf();
		if (pp->m_pPed->IsInVehicle()) pp->RemoveFromVehicleAndPutAt(g.x, g.y, g.hold);   // admin de carro desce no destino (so ele; o carro fica)
		else pp->m_pPed->SetPosn(g.x, g.y, g.hold);
		g.phase = Phase::Hold;
		FLog("[MAPTP] inicio x=%.1f y=%.1f espera z=%.1f origem=%.1f,%.1f,%.1f", g.x, g.y, g.hold, g.ox, g.oy, g.oz);
		return;
	}
	if (g.phase == Phase::Idle) return;
	if (!pp || pp->IsDead() || pNetGame->GetGameState() != GAMESTATE_CONNECTED) { g.phase = Phase::Idle; return; }

	if (g.phase == Phase::Hold)
	{
		// segura o ped no ar sobre o ponto (sem cair, sem velocidade) enquanto o jogo carrega a area
		pp->m_pPed->SetVelocity(CVector());
		pp->m_pPed->SetPosn(g.x, g.y, g.hold);

		const double el = Since(g.t0);
		if (Since(g.lastCheck) >= kCheckEvery)
		{
			g.lastCheck = Clock::now();
			float z = 0;
			if (GroundAt(g.x, g.y, z))
			{
				if (g.haveGz && std::fabs(z - g.gz) < kTol) g.stable++; else g.stable = 1;
				g.haveGz = true; g.gz = z;
			}
			else g.stable = 0;
		}
		const bool ready = g.haveGz && el >= kMinDwell && g.stable >= kStableNeeded;
		if (!g.haveGz && el >= kDropAfter)
		{
			// sem resposta da linha vertical: solta o ped sobre o ponto e deixa a fisica do jogo achar o chao (colisao de ped funciona em todo o mapa)
			g.phase = Phase::Drop; g.t0 = Clock::now(); g.rest = 0; g.lastZ = g.hold;
			FLog("[MAPTP] sem chao pela linha vertical: soltando o ped de z=%.1f", g.hold);
			return;
		}
		if (ready || (el >= kTimeout))
		{
			pp->m_pPed->SetVelocity(CVector());
			pp->m_pPed->SetPosn(g.x, g.y, g.gz + kStand);
			g.placedGz = g.gz;
			g.t0 = Clock::now(); g.lastCheck = g.t0;
			g.phase = Phase::Settle;
		}
		return;
	}

	if (g.phase == Phase::Drop)
	{
		const CVector cur = pp->m_pPed->GetPosition();
		const float vz = pp->m_pPed->GetMoveSpeed().z;
		if (cur.z < -60.0f || cur.z < g.hold - 400.0f) { Finish(pp, false, cur.z); return; }   // caiu no vazio: volta
		if (Since(g.t0) > 0.8)
		{
			if (std::fabs(vz) < kRestVz && std::fabs(cur.z - g.lastZ) < 0.05f) g.rest++; else g.rest = 0;
			if (g.rest >= kRestFrames) { g.placedGz = cur.z - 1.0f; Finish(pp, true, g.placedGz); return; }
		}
		g.lastZ = cur.z;
		if (Since(g.t0) >= kDropMax) Finish(pp, false, cur.z);
		return;
	}

	// Settle: ped solto; se uma rua/ponte carregar depois e ficar mais alta que o chao achado, sobe para cima dela
	if (Since(g.lastCheck) >= kCheckEvery)
	{
		g.lastCheck = Clock::now();
		float z = 0;
		if (GroundAt(g.x, g.y, z) && z > g.placedGz + 0.5f && z < g.placedGz + 60.0f)
		{
			const CVector cur = pp->m_pPed->GetPosition();
			if (cur.z < z + 0.8f)
			{
				pp->m_pPed->SetVelocity(CVector());
				pp->m_pPed->SetPosn(g.x, g.y, z + kStand);
				g.placedGz = z;
				FLog("[MAPTP] corrigiu para chao mais alto z=%.1f", z);
			}
		}
	}
	if (Since(g.t0) >= kSettle) Finish(pp, true, g.placedGz);
}
