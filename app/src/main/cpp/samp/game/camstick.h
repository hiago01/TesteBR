#pragma once
// =====================================================================================
//  JOYSTICK DO PERSONAGEM COM A CAMERA DO CELULAR (traseira = modo 2, selfie = modo 3). Logica pura (sem jogo), testada no PC.
//
//  Problema (video): a camera da foto e recriada a cada quadro a partir do HEADING do personagem (PhoneCamMath.h), mas o motor
//  move o personagem em relacao a direcao da camera. Na selfie a camera olha para TRAS (-frente), entao "joystick para cima" virava
//  o personagem 180 graus -> a camera pulava para o outro lado -> virava de novo... (a visao girava sem parar). Em qualquer modo, um
//  analogico inclinado de lado fazia o personagem virar, a camera acompanhar e o motor virar de novo (rotacao descontrolada).
//
//  Solucao: durante a foto o joystick vira controle "tanque", coerente com uma camera presa ao personagem:
//    - cima    : anda para a FRENTE do personagem (selfie: vem em direcao a camera; traseira: afasta-se). Intensidade proporcional.
//    - baixo   : sem efeito (andar de re faria o motor girar o personagem 180 graus = o proprio bug). Para voltar, gire.
//    - esq/dir : GIRA o personagem/camera em velocidade constante (nao anda de lado). Traseira: direita = vira a direita.
//                Selfie: direita = a camera vai para a direita em volta do personagem (a camera esta na frente dele).
//  O motor recebe sempre lr = 0 e ud >= 0 (ou invertido na selfie, porque la o "olhar" da camera e oposto a frente do personagem),
//  ou seja: o personagem so anda ao longo do proprio heading e nunca e girado pelo motor. O giro e aplicado por nos (TakeTurn).
// =====================================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace CamStick
{
	constexpr float kDead      = 0.14f;          // zona morta (fracao do curso)
	constexpr float kTurnRate  = 1.75f;          // rad/s com o analogico todo de lado (~100 graus/s)
	constexpr float kMaxDt     = 0.05f;          // um quadro lento nunca da um salto de giro

	struct State { std::atomic<int> mode{ 0 }; std::atomic<int> rawLR{ 0 }, rawUD{ 0 }; };
	inline State& S() { static State s; return s; }

	inline void SetMode(int mode) { S().mode.store((mode == 2 || mode == 3) ? mode : 0); if (mode != 2 && mode != 3) { S().rawLR.store(0); S().rawUD.store(0); } }
	inline int  Mode()            { return S().mode.load(); }
	inline bool Active()          { return Mode() != 0; }

	inline float Norm(int v) { float x = (float)v / 128.0f; return std::clamp(x, -1.0f, 1.0f); }
	inline float Dead(float x)                    // zona morta com rampa (sem salto na borda)
	{
		const float a = std::fabs(x); if (a <= kDead) return 0.0f;
		const float k = (a - kDead) / (1.0f - kDead); return x < 0 ? -k : k;
	}

	// Valores para o MOTOR a partir do stick bruto (convencao do CPad: lr + = direita, ud - = para cima/frente; faixa +-128).
	// Guarda o bruto (para o giro, TakeTurn). So chame com Active().
	inline void Map(int16_t rawLr, int16_t rawUd, int16_t& engLr, int16_t& engUd)
	{
		S().rawLR.store(rawLr); S().rawUD.store(rawUd);
		const float up = Dead(-Norm(rawUd));                                     // > 0 = inclinado para cima
		const float fwd = up > 0 ? up : 0.0f;                                    // baixo: sem efeito (ver cabecalho)
		const int mag = std::min(127, (int)std::lround(fwd * 128.0f));
		engLr = 0;
		// traseira: a camera olha para a FRENTE do personagem -> frente = ud negativo. selfie: a camera olha para TRAS -> frente = ud positivo.
		engUd = (int16_t)(Mode() == 3 ? mag : -mag);
	}

	// Giro (rad) a aplicar ao heading do personagem neste quadro (positivo = anti-horario, como m_fCurrentRotation). Uma vez por quadro.
	inline float TakeTurn(float dtSec)
	{
		const int mode = Mode(); if (!mode) return 0.0f;
		const float dt = std::clamp(dtSec, 0.0f, kMaxDt);
		const float x = Dead(Norm(S().rawLR.load()));
		return x * kTurnRate * dt * (mode == 2 ? -1.0f : 1.0f);              // traseira: direita = vira a direita (heading diminui). selfie: direita = camera vai a direita (anti-horario)
	}
}
