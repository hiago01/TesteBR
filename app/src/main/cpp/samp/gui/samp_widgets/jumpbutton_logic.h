#pragma once
// Logica PURA do botao de PULO (sem ImGui/jogo): testada no host (testes/jumpbutton_check.cpp).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace JumpLogic
{
	constexpr int kCooldownMs = 350;   // anti-repeticao entre toques
	constexpr int kHoldMs     = 120;   // CPad::GetJump (segurado) fica verdadeiro por este tempo apos o toque

	struct Circle { float cx, cy, r; };

	inline bool NativeRectOk(float l, float t, float r, float b, float sw, float sh)
	{
		if (!(r > l) || !(b > t)) return false;
		if (!std::isfinite(l) || !std::isfinite(t) || !std::isfinite(r) || !std::isfinite(b)) return false;
		if (r < 0 || b < 0 || l > sw || t > sh) return false;
		if ((r - l) > sw * 0.5f || (b - t) > sh * 0.5f) return false;
		return true;
	}

	// Ao LADO ESQUERDO do botao nativo de correr (sprint) ou, sem referencia, perto do canto inferior direito.
	inline Circle Place(bool nativeOk, float l, float t, float r, float b, float sw, float sh)
	{
		const float rad = sh * 0.052f;
		float cx, cy;
		if (nativeOk) { cx = l - (r - l) * 0.5f - rad * 1.35f; cy = (t + b) * 0.5f; }
		else          { cx = sw * 0.947f - rad * 3.1f;         cy = sh * 0.85f; }
		cx = std::clamp(cx, rad * 1.1f, sw - rad * 1.1f);
		cy = std::clamp(cy, rad * 1.1f, sh - rad * 1.1f);
		return Circle{ cx, cy, rad };
	}

	inline bool Hit(const Circle& c, float x, float y, float padK = 1.25f)
	{
		const float dx = x - c.cx, dy = y - c.cy, R = c.r * padK;
		return dx * dx + dy * dy <= R * R;
	}

	inline bool ShouldShow(bool pedOk, bool blockedUi, bool inVehicle, bool alive)
	{
		return pedOk && !blockedUi && !inVehicle && alive;
	}

	// Pedido de pulo vindo do botao -> lido pelos hooks de CPad (JumpJustDown / GetJump).
	struct Request
	{
		std::atomic<int64_t> pressMs{ -1000000 };
		std::atomic<bool>    fresh{ false };

		void Press(int64_t nowMs) { pressMs.store(nowMs); fresh.store(true); }
		bool ConsumeJustDown()    { return fresh.exchange(false); }                      // 1 vez por toque
		bool Held(int64_t nowMs) const { return (nowMs - pressMs.load()) < kHoldMs; }    // curto, so para GetJump
		void Clear()              { fresh.store(false); pressMs.store(-1000000); }
	};
}
