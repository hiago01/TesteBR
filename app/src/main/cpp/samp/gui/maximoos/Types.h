#pragma once
// Maximo OS - tipos basicos (sem ImGui, sem jogo): testados no PC (testes/maximoos/).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace MaximoOS
{
	struct Vec2 { float x = 0, y = 0; };
	inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
	inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }

	struct Rect
	{
		float x = 0, y = 0, w = 0, h = 0;
		float right() const { return x + w; }
		float bottom() const { return y + h; }
		Vec2  center() const { return { x + w * 0.5f, y + h * 0.5f }; }
		bool  contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
		Rect  inset(float d) const { return { x + d, y + d, std::max(0.0f, w - 2 * d), std::max(0.0f, h - 2 * d) }; }
		Rect  inset(float dx, float dy) const { return { x + dx, y + dy, std::max(0.0f, w - 2 * dx), std::max(0.0f, h - 2 * dy) }; }
		Rect  moved(float dx, float dy) const { return { x + dx, y + dy, w, h }; }
		bool  inside(const Rect& o, float eps = 0.01f) const { return x >= o.x - eps && y >= o.y - eps && right() <= o.right() + eps && bottom() <= o.bottom() + eps; }
		bool  empty() const { return w <= 0 || h <= 0; }
	};

	struct Color
	{
		uint8_t r = 255, g = 255, b = 255, a = 255;
		Color() = default;
		Color(int r_, int g_, int b_, int a_ = 255) : r((uint8_t)r_), g((uint8_t)g_), b((uint8_t)b_), a((uint8_t)a_) {}
		Color alpha(float k) const { return Color(r, g, b, (int)std::lround(std::clamp(a * k, 0.0f, 255.0f))); }
		static Color mix(Color x, Color y, float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			auto m = [t](int p, int q) { return (int)std::lround(p + (q - p) * t); };
			return Color(m(x.r, y.r), m(x.g, y.g), m(x.b, y.b), m(x.a, y.a));
		}
	};
}
