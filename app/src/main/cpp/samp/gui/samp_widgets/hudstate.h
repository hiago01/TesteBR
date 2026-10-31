#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// =====================================================================================
//  HUD do jogador (Maximo RP) - canto superior direito
//
//    [ LOGO MAXIMO RP ]
//    (vida) (colete) (fome) (energia) (sede)          <- circulos coloridos + anel de nivel
//    (R$) [R$ 67.214]  (pessoa) [0/10]  (estrela) [24.600]   <- circulo + "pilula" de valor
//
//  Substitui a barra de vida / colete / dinheiro NATIVOS do jogo (CWidgetPlayerInfo), que sao
//  escondidos em hooks.cpp (RenderHealthBar / RenderArmorBar) e patches.cpp (dinheiro).
//
//  Origem dos dados:
//    - vida e colete ........ ped local (tempo real)
//    - dinheiro ............. RPCs padrao GivePlayerMoney / ResetPlayerMoney (acompanhados aqui)
//                             ou o valor enviado pelo servidor no RPC 201 (tem prioridade)
//    - fome / sede / energia  RPC 201 (servidor -> client), ver servidor/maximo_hud.inc
//    - pilula "pessoa" e "estrela" ... RPC 201
//
//  Este header so tem o ESTADO (sem ImGui), para o net/scriptrpc.cpp poder incluir.
//  O widget que desenha fica em hud.h / hud.cpp.
// =====================================================================================

namespace Hud
{
	struct Data
	{
		std::atomic<bool>  visible{true};          // /hud on|off  ou flag do RPC 201
		std::atomic<int>   hunger{100};            // 0..100
		std::atomic<int>   thirst{100};            // 0..100
		std::atomic<int>   energy{100};            // 0..100
		std::atomic<int>   money{0};               // dinheiro acompanhado pelos RPCs padrao
		std::atomic<bool>  moneyFromServer{false}; // true = 'money' veio do RPC 201
		std::atomic<int>   slotA{0};               // pilula "pessoa": valor atual
		std::atomic<int>   slotB{0};               // pilula "pessoa": maximo (0 = nao mostra "/max")
		std::atomic<int>   extra{0};               // pilula "estrela"
		std::atomic<float> scale{1.0f};            // multiplicador de tamanho (/hud escala)
	};

	extern Data g;

	// RPCs padrao do SA-MP (chamados de net/scriptrpc.cpp)
	void OnMoneyGiven(int amount);
	void OnMoneyReset();

	// RPC 201 (ver servidor/maximo_hud.inc)
	void OnServerPacket(uint8_t flags, uint8_t hunger, uint8_t thirst, uint8_t energy,
						int32_t money, uint16_t slotA, uint16_t slotB, int32_t extra);

	// Folego do jogador local (game/stamina.h): a energia da HUD cai correndo e sobe andando/parado.
	// StaminaTick e chamado pelo hook de CPlayerPed::HandleSprintEnergy (so jogador local); CanSprint() e lido por game/movemode.cpp.
	void StaminaTick(bool sprinting);
	bool CanSprint();

	// Nova conexao: volta tudo ao padrao.
	void Reset();

	// Comando de teste do chat: "/hud ..." -> texto de resposta.
	std::string DebugCommand(const std::string& args);
}

