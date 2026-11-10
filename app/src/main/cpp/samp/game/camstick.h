#pragma once
// =====================================================================================
//  JOYSTICK COM A CAMERA DO CELULAR (traseira = modo 2, selfie = modo 3). Logica pura (sem jogo), testada no PC.
//
//  Com o app Camera aberto o joystick NAO anda mais com o personagem: ele so MOVE A CAMERA (gira em volta/para o lado).
//  Motivo (video): a camera da foto e recriada a cada quadro a partir do heading do personagem (PhoneCamMath.h) e o motor anda o
//  personagem em relacao ao olhar da camera; na selfie (camera olhando para tras) "para cima" virava o personagem 180 graus e a
//  visao girava sem parar. Sem andar nao ha esse conflito.
//
//    - o motor recebe sempre lr = 0 e ud = 0 (personagem parado);
//    - esquerda/direita gira o personagem (e com ele a camera) em velocidade constante, proporcional a inclinacao:
//        traseira: direita = vira a direita;  selfie: direita = a camera vai para a direita em volta do personagem.
//    - cima/baixo: sem efeito.
//  O giro e aplicado por quem chama (TakeTurn, uma vez por quadro). Estado so enquanto a Camera esta aberta (SetMode(0) limpa tudo).
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

	struct State { std::atomic<int> mode{ 0 }; std::atomic<int> rawLR{ 0 }; };
	inline State& S() { static State s; return s; }

	inline void SetMode(int mode)
	{
		const bool on = (mode == 2 || mode == 3);
		S().mode.store(on ? mode : 0);
		if (!on) S().rawLR.store(0);
	}
	inline int  Mode()   { return S().mode.load(); }
	inline bool Active() { return Mode() != 0; }

	inline float Norm(int v) { return std::clamp((float)v / 128.0f, -1.0f, 1.0f); }
	inline float Dead(float x)                    // zona morta com rampa (sem salto na borda)
	{
		const float a = std::fabs(x); if (a <= kDead) return 0.0f;
		const float k = (a - kDead) / (1.0f - kDead); return x < 0 ? -k : k;
	}

	// Chamado pelos hooks de CPad (game/pad.cpp) com o stick bruto (convencao do CPad: lr + = direita). So chame com Active().
	// Devolve ao motor o stick ZERADO (personagem parado) e guarda o eixo lateral para o giro.
	inline void Map(int16_t rawLr, int16_t /*rawUd*/, int16_t& engLr, int16_t& engUd)
	{
		S().rawLR.store(rawLr);
		engLr = 0; engUd = 0;
	}

	// Giro (rad) a aplicar ao heading do personagem neste quadro (positivo = anti-horario, como m_fCurrentRotation). Uma vez por quadro.
	inline float TakeTurn(float dtSec)
	{
		const int mode = Mode(); if (!mode) return 0.0f;
		const float dt = std::clamp(dtSec, 0.0f, kMaxDt);
		const float x = Dead(Norm(S().rawLR.load()));
		return x * kTurnRate * dt * (mode == 2 ? -1.0f : 1.0f);
	}
}
