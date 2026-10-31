#pragma once
#include <algorithm>
#include <cmath>

// =====================================================================================
//  Folego/energia do jogador local, mostrado na HUD (circulo "energia" = Hud::g.energy).
//  Substitui a stamina NATIVA do GTA (CPlayerPed::HandleSprintEnergy, desligada em hooks.cpp).
//    - correndo (SPRINT) : energia cai kDrain por segundo
//    - andando / parado  : energia sobe kRegen por segundo
//    - chegou a 0        : "cansado" -> nao pode correr ate recuperar kRecover
//    - servidor (RPC 201): o valor do servidor so entra quando MUDA em relacao ao ultimo recebido
//                          (um reenvio igual nao desfaz o gasto local)
//  Logica pura (header-only, sem motor/ImGui) para ser testada no PC.
// =====================================================================================
struct Stamina
{
	static constexpr float kDrain   = 4.0f;    // 100 -> 0 em 25 s correndo
	static constexpr float kRegen   = 3.0f;    // 0 -> 100 em ~33 s sem correr
	static constexpr float kRecover = 20.0f;   // minimo para voltar a poder correr depois de zerar
	static constexpr float kMaxDt   = 0.1f;    // limita saltos (pausa, carregamento)

	float value      = 100.0f;
	bool  tired      = false;
	int   lastServer = -1;

	void Tick(bool sprinting, float dt)
	{
		dt = std::clamp(dt, 0.0f, kMaxDt);
		value += (sprinting ? -kDrain : kRegen) * dt;
		value = std::clamp(value, 0.0f, 100.0f);
		if (value <= 0.0f) tired = true;
		else if (tired && value >= kRecover) tired = false;
	}

	// v = energia enviada pelo servidor (0..100)
	void OnServer(int v)
	{
		v = std::clamp(v, 0, 100);
		if (v == lastServer) return;
		lastServer = v;
		value = (float)v;
		tired = (value <= 0.0f) || (tired && value < kRecover);
	}

	bool CanSprint() const { return !tired; }
	int  Percent() const   { return (int)std::lround(value); }
	void Reset()           { value = 100.0f; tired = false; lastServer = -1; }
};
