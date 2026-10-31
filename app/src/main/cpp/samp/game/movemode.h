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

// BOTAO DE CORRER (toque): alterna WALK <-> SPRINT a cada toque.
//   modo WALK/JOG  -> o toque passa para SPRINT (correr)
//   modo SPRINT    -> o toque passa para WALK  (andar)
// A troca vale so na sessao (nao grava INI a cada toque: sem I/O na thread do jogo); ao abrir o jogo volta ao modo salvo.
// Logica pura (header-only) para ser testada no PC.
inline eMoveMode ToggledMode(eMoveMode m) { return m == eMoveMode::SPRINT ? eMoveMode::WALK : eMoveMode::SPRINT; }

struct SprintButtonEdge
{
	static constexpr long long kDebounceMs = 200;   // GetSprint e chamado varias vezes por frame e o toque pode "tremer"
	bool      prev   = false;
	long long lastMs = -100000;
	// Chame com o estado ORIGINAL do botao (pressionado ou nao). true = houve um toque novo (borda de subida).
	bool OnSample(bool pressed, long long nowMs)
	{
		const bool edge = pressed && !prev;
		prev = pressed;
		if (edge && nowMs - lastMs >= kDebounceMs) { lastMs = nowMs; return true; }
		return false;
	}
};

namespace CMoveMode
{
	eMoveMode   Get();                                  // modo atual (carrega o INI na 1a chamada)
	std::string Command(const std::string& args);       // "/movimento ..." (args = texto apos o comando)

	// Chamado por game/pad.cpp (ramo local). Seguro para chamar varias vezes por frame:
	// sem alocacao, sem I/O, sem loops.
	//  - ApplyToStick: altera lr/ud (valores com sinal, faixa +-128) se o modo se aplica agora.
	//  - ApplySprint : devolve o valor final de GetSprint (original = valor do jogo). Tambem trata o toque no botao
	//                  de correr: alterna WALK <-> SPRINT (ver ToggledMode).
	void     ApplyToStick(int16_t& lr, int16_t& ud);
	uint32_t ApplySprint(uint32_t original, int16_t lr, int16_t ud);
}
