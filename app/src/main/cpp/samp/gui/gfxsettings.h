#pragma once
// Configuracoes graficas do menu Maximo RP (categoria Graficos): Aspect Ratio, Resolution Scale e Safe Zone.
// Somente estado + contas puras (sem GL, sem hook). Persistencia: [maximo] aspectRatioMode / resolutionScale / safeZoneScale
// em SAMP/settings.ini (feita por maximomenu.cpp).
#include <atomic>
#include <algorithm>

namespace GfxSettings
{
	// ---- Resolution Scale: % da largura da tela usada para renderizar o MUNDO (a HUD e desenhada depois do upscale = nitida) ----
	inline constexpr int kResOpts[6] = { 50, 60, 70, 80, 90, 100 };
	// ---- Safe Zone: % TOTAL de reducao da area util em cada eixo (metade em cada borda) ----
	inline constexpr int kSafeOpts[5] = { 0, 5, 10, 15, 20 };

	// Aspect Ratio: 0 = AUTO (proporcao fisica da tela; unico modo suportado com seguranca nesta build)
	inline std::atomic<int> g_aspectMode{ 0 };
	// indices em kResOpts / kSafeOpts. g_resIdx = -1: ainda nao escolhido (mantem o valor que o motor ja usa)
	inline std::atomic<int> g_resIdx{ -1 };
	inline std::atomic<int> g_safeIdx{ 0 };

	inline int SafePct() { return kSafeOpts[std::clamp(g_safeIdx.load(), 0, 4)]; }

	struct Insets { float x, y; };
	// margem (px) em CADA borda para uma tela sw x sh
	inline Insets SafeInsets(float sw, float sh)
	{
		const float p = (float)SafePct() * 0.5f * 0.01f;
		return { sw * p, sh * p };
	}
	// Desloca o CENTRO de um elemento para dentro: bordas andam a margem inteira, o centro da tela nao anda.
	// (x' = ix + x * (1 - 2*ix/sw)). Tamanho e alinhamento do elemento ficam como estavam.
	inline float ShiftIn(float c, float size, float ins)
	{
		if (!(size > 1.0f) || ins <= 0.0f) return 0.0f;
		return ins * (1.0f - 2.0f * c / size);
	}
}
