#pragma once
// Limitador PURO dos pedidos de entrada em veiculo (RPC EnterVehicle) do cliente. Testado no host
// (testes/host_test_entervehiclelimiter.cpp). Ponto unico de passagem: CLocalPlayer::SendEnterVehicleNotification.
//
// Regras (todas por pedido, tempo em ms, tolerante a volta do contador):
//   1. veiculo invalido (0xFFFF/0)                         -> nega
//   2. intervalo MINIMO entre dois pedidos quaisquer        -> kMinGapMs   (700)
//   3. mesmo veiculo + mesmo papel (motorista/passageiro)   -> kSameVehGapMs (1500)
// So pedidos ACEITOS atualizam o relogio (um flood de toques nao empurra a janela para frente nem a trava para sempre).
#include <cstdint>

struct EnterVehicleLimiter
{
	static constexpr uint32_t kMinGapMs     = 700;
	static constexpr uint32_t kSameVehGapMs = 1500;

	uint32_t lastTick   = 0;
	uint32_t lastSameTick = 0;
	uint16_t lastVeh    = 0xFFFF;
	bool     lastPass   = false;
	bool     any        = false;

	void Reset() { *this = EnterVehicleLimiter(); }

	static bool Elapsed(uint32_t now, uint32_t then, uint32_t gap) { return now < then || (now - then) >= gap; }

	// so verifica (nao grava)
	bool Check(uint32_t now, uint16_t veh, bool passenger) const
	{
		if (veh == 0 || veh == 0xFFFF) return false;
		if (any)
		{
			if (!Elapsed(now, lastTick, kMinGapMs)) return false;
			if (veh == lastVeh && passenger == lastPass && !Elapsed(now, lastSameTick, kSameVehGapMs)) return false;
		}
		return true;
	}

	// verifica e grava (so se aceito)
	bool Allow(uint32_t now, uint16_t veh, bool passenger)
	{
		if (!Check(now, veh, passenger)) return false;
		lastTick = now; lastSameTick = now; lastVeh = veh; lastPass = passenger; any = true;
		return true;
	}
};
