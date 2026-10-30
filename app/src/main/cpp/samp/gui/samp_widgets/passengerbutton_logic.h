#pragma once
// Logica PURA do botao P2 (sem ImGui/jogo): testada no host (testes/host_test_passengerbutton.cpp).
#include <algorithm>
#include <cmath>

namespace PassengerLogic
{
	constexpr float kShowDist = 6.0f;      // metros ate o veiculo mais proximo para o botao aparecer (a entrada em si aceita ate 8 m)
	constexpr int   kCooldownMs = 1200;    // depois de usar, ignora novos toques (a entrada leva um instante)

	struct Circle { float cx, cy, r; };

	// Valida o retangulo do widget nativo do carro (l,t,r,b em px da tela do jogo). false = lixo/fora da tela.
	inline bool NativeRectOk(float l, float t, float r, float b, float screenW, float screenH)
	{
		if (!(r > l) || !(b > t)) return false;
		if (!std::isfinite(l) || !std::isfinite(t) || !std::isfinite(r) || !std::isfinite(b)) return false;
		if (r < 0 || b < 0 || l > screenW || t > screenH) return false;
		if ((r - l) > screenW * 0.5f || (b - t) > screenH * 0.5f) return false;
		return true;
	}

	// Posicao do P2: ao LADO ESQUERDO do botao nativo do carro (nativeOk) ou, sem referencia, no ponto medido
	// (botao nativo ~ 0.947w x 0.55h). Sempre dentro da tela.
	inline Circle Place(bool nativeOk, float l, float t, float r, float b, float screenW, float screenH)
	{
		const float rad = screenH * 0.052f;
		float cx, cy;
		if (nativeOk) { cx = l - (r - l) * 0.5f - rad * 1.35f; cy = (t + b) * 0.5f; }
		else          { cx = screenW * 0.947f - rad * 3.1f;    cy = screenH * 0.55f; }
		cx = std::clamp(cx, rad * 1.1f, screenW - rad * 1.1f);
		cy = std::clamp(cy, rad * 1.1f, screenH - rad * 1.1f);
		return Circle{ cx, cy, rad };
	}

	inline bool Hit(const Circle& c, float x, float y, float padK = 1.25f)
	{
		const float dx = x - c.cx, dy = y - c.cy, R = c.r * padK;
		return dx * dx + dy * dy <= R * R;
	}

	inline bool ShouldShow(bool pedOk, bool blockedUi, bool inVehicle, bool alive, float nearestDist)
	{
		return pedOk && !blockedUi && !inVehicle && alive && nearestDist >= 0.0f && nearestDist <= kShowDist;
	}
}
