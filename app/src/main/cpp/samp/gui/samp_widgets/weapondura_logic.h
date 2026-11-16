#pragma once
// Logica pura (testavel no host) da barra de durabilidade das armas exclusivas: verde (100%) -> amarelo -> vermelho (0% = enferrujada).
#include <algorithm>

namespace WeaponDura
{
	// pct: 0..100 (0 = enferrujada). Valores fora da faixa sao limitados.
	inline int Clamp(int pct) { return std::max(0, std::min(100, pct)); }

	// cor da barra: 100% verde (70,200,90) -> 50% amarelo (235,200,60) -> 0% vermelho (225,55,55)
	inline void Color(int pct, int& r, int& g, int& b)
	{
		const int p = Clamp(pct);
		struct C { int r, g, b; };
		const C red{225, 55, 55}, yel{235, 200, 60}, grn{70, 200, 90};
		const C& a = p >= 50 ? yel : red;
		const C& c = p >= 50 ? grn : yel;
		const int t = p >= 50 ? (p - 50) : p;          // 0..50
		r = a.r + (c.r - a.r) * t / 50;
		g = a.g + (c.g - a.g) * t / 50;
		b = a.b + (c.b - a.b) * t / 50;
	}

	// largura preenchida (0..1). Enferrujada mostra um tico vermelho para a barra nao sumir.
	inline float Fill(int pct) { const int p = Clamp(pct); return p <= 0 ? 0.06f : (float)p / 100.0f; }
}
