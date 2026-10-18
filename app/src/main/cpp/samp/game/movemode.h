#pragma once
#include <cstdint>
#include <string>

// =====================================================================================
//  Modo de movimento persistente do joystick: WALK / JOG / SPRINT  (so o jogador local)
//
//  Integracao (caminho real do jogo, sem pacotes artificiais):
//    O joystick virtual chega ao motor por CPad::GetPedWalkLeftRight / GetPedWalkUpDown,
//    e a corrida rapida por CPad::GetSprint. Esses tres ja sao hookados em game/pad.cpp
//    (ramo "local player"). Este modulo so molda o valor devolvido por eles:
//
//      WALK   : modulo do stick fixo em kWalk  (mesmo valor 0x20 que o projeto ja usa no
//               ramo remoto para KEY_WALK) + GetSprint suprimido
//      JOG    : modulo do stick fixo em kJog   (intermediario - ver LIMITACOES no README
//               da entrega: o motor nao tem JOG independente; calibravel por comando)
//      SPRINT : stick no maximo (128) + GetSprint = 1 enquanto houver entrada
//
//    A DIRECAO do stick e preservada (so o modulo muda). Como LocalPlayerKeys.wKeyLR/UD
//    recebem o valor ja moldado, o SA-MP sincroniza exatamente o que o motor usou
//    (fluxo normal de OnFootSync; nenhum pacote extra).
//
//  O modo e independente do estado instantaneo: soltar o joystick nao o apaga.
//  Persistencia: [movimento] mode= em SAMP/settings.ini (gravado so ao mudar o modo).
// =====================================================================================
enum class eMoveMode : uint8_t
{
	WALK   = 0,
	JOG    = 1,
	SPRINT = 2,
};

namespace CMoveMode
{
	eMoveMode   Get();                                  // modo atual (carrega o INI na 1a chamada)
	std::string Command(const std::string& args);       // "/movimento ..." (args = texto apos o comando)

	// Chamado por game/pad.cpp (ramo local). Seguro para chamar varias vezes por frame:
	// sem alocacao, sem I/O, sem loops.
	//  - ApplyToStick: altera lr/ud (valores com sinal, faixa +-128) se o modo se aplica agora.
	//  - ApplySprint : devolve o valor final de GetSprint (original = valor do jogo).
	void     ApplyToStick(int16_t& lr, int16_t& ud);
	uint32_t ApplySprint(uint32_t original, int16_t lr, int16_t ud);
}
