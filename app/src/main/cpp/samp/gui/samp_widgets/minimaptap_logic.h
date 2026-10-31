#pragma once
// Toque no MINIMAPA -> abre o /mapa. Logica PURA (sem ImGui/jogo): testada no host (testes/minimaptap_check.cpp).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace MiniMapTap
{
	constexpr float   kSlop   = 40.0f;    // px: mais que isso e arrasto, nao toque
	constexpr int64_t kMaxMs  = 600;      // toque mais longo que isso nao conta
	constexpr int64_t kFreshMs = 500;     // o retangulo do radar so vale se foi desenhado ha menos disso

	struct Rect { float l, t, r, b; };

	// Retangulo do disco do radar em unidades do jogo, gravado por game/sprite2d.cpp a cada frame desenhado.
	inline std::atomic<float>   gL{0}, gT{0}, gR{0}, gB{0};
	inline std::atomic<int64_t> gStamp{-1000000};

	inline bool Contains(const Rect& r, float x, float y, float padK = 1.10f)
	{
		const float cx = (r.l + r.r) * 0.5f, cy = (r.t + r.b) * 0.5f;
		const float hw = (r.r - r.l) * 0.5f * padK, hh = (r.b - r.t) * 0.5f * padK;
		return std::fabs(x - cx) <= hw && std::fabs(y - cy) <= hh;
	}

	inline bool RectOk(const Rect& r, float sw, float sh)
	{
		if (!std::isfinite(r.l) || !std::isfinite(r.t) || !std::isfinite(r.r) || !std::isfinite(r.b)) return false;
		if (!(r.r > r.l) || !(r.b > r.t)) return false;
		if (r.r < 0 || r.b < 0 || r.l > sw || r.t > sh) return false;
		return (r.r - r.l) <= sw * 0.5f && (r.b - r.t) <= sh * 0.5f;
	}

	struct Tracker
	{
		int     finger = -1;
		float   sx = 0, sy = 0;
		int64_t t0 = 0;

		// type: 2 = push, 3 = move, 1 = pop. Retorna true = o evento e do minimapa (nao segue para o jogo).
		// open = true no pop que completa um TOQUE valido dentro do minimapa.
		bool OnTouch(int type, int num, float x, float y, bool rectValid, const Rect& r, int64_t now, bool cancel, bool& open)
		{
			open = false;
			if (type == 2)
			{
				if (finger >= 0 || !rectValid || !Contains(r, x, y)) return false;
				finger = num; sx = x; sy = y; t0 = now;
				return true;
			}
			if (finger != num) return false;
			if (type == 3) return true;
			if (type == 1)
			{
				finger = -1;
				const float dx = x - sx, dy = y - sy;
				if (!cancel && rectValid && Contains(r, x, y) && (dx * dx + dy * dy) <= kSlop * kSlop && (now - t0) <= kMaxMs) open = true;
				return true;
			}
			return false;
		}
	};
}
