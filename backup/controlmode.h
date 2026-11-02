#pragma once
#include "controlmode_logic.h"
#include <cstdint>

// =====================================================================================
//  Estado dos controles CLASSICO x MODERNO (modo escolhido, configuracao do moderno, persistencia por jogador).
//
//  - CLASSICO (padrao): nada muda; os controles nativos do GTA SA ficam exatamente como estavam.
//  - MODERNO: joystick analogico + botoes proprios (moderncontrols.cpp). Os nativos de "a pe" ficam desligados.
//  - Persistencia: SAMP/settings.ini, secao [controles_<nick>] (chave propria; ausente = classico).
// =====================================================================================
namespace ControlMode
{
	bool                    Loaded();                     // o INI do jogador atual ja foi lido (so entao Chosen() vale)
	void                    EnsureLoaded();               // carrega do INI quando houver armazenamento e nick (idempotente)
	CtlLogic::Mode          Get();
	bool                    IsModern();                   // modo escolhido == moderno (independente de a pe/veiculo)
	bool                    Chosen();                     // o jogador ja confirmou a escolha alguma vez
	void                    Set(CtlLogic::Mode m);        // grava na hora (troca sem reiniciar)
	void                    MarkChosen();
	CtlLogic::ModernConfig  GetConfig();
	void                    SetConfig(const CtlLogic::ModernConfig& c, bool save);
	void                    ResetConfig();                // so o moderno; o classico nao e tocado
	uint32_t                ConfigVersion();              // muda a cada SetConfig (o widget recalcula o layout)
	void                    Save();
}

// Lido pelos hooks de CPad (game/pad.cpp, thread do jogo). So tem efeito com o modo moderno ativo a pe.
namespace ModernInput
{
	bool Active();                                    // moderno + a pe + vivo + sem UI por cima
	bool Stick(int16_t& lr, int16_t& ud);             // true = use estes valores no lugar dos nativos
	bool SprintHeld();                                // botao Corrida segurado com o joystick em uso
	bool JumpJustDown();                              // 1x por toque
	bool JumpHeld();                                  // ~120 ms apos o toque
	bool CrouchJustDown();                            // 1x por toque
	bool AttackHeld();
	bool AimWanted();                                 // Ataque segurado (o pad.cpp so mira se houver arma de fogo)
	bool AttackJustDown();                            // 1x por toque
}
