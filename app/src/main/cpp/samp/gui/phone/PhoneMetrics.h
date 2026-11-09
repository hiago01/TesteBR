#pragma once
// Celular do Maximo RP - geometria do aparelho VERTICAL (iPhone). Logica pura (sem ImGui/jogo), testada no PC.
// Reusa a struct Metrics do Maximo OS (gui/maximoos/ResolutionManager.h): todos os componentes e apps usam so Metrics.px()/font().
#include "../maximoos/ResolutionManager.h"
#include "../gfxsettings.h"

namespace Phone
{
	using MaximoOS::Metrics;
	using MaximoOS::Rect;

	// "unidades logicas" (lu) da TELA do aparelho: 390 x 844 (proporcao 19,5:9 de um iPhone moderno)
	constexpr float kScreenW = 390.0f, kScreenH = 844.0f;
	constexpr float kBezelLu = 13.0f;                  // moldura (lu). Com frameRadius <= 3,41 x moldura o canto QUADRADO da tela fica dentro do arco da moldura (sem 'cantinhos' pretos fora do aparelho)
	constexpr float kFrameRadiusLu = 44.0f, kScreenRadiusLu = kFrameRadiusLu - kBezelLu;   // concentricos
	constexpr float kStatusLu = 50.0f;                 // barra de status + ilha dinamica
	constexpr float kHomeBarLu = 26.0f;                // area do indicador de inicio

	// scaleK: tamanho do aparelho (Ajustes > Tamanho do celular: 0.75 .. 1.25). slideK: 0 = fora da tela (embaixo) .. 1 = no lugar.
	inline Metrics ComputePhoneMetrics(float sw, float sh, float scaleK = 1.0f, float slideK = 1.0f)
	{
		Metrics m;
		m.screenW = sw; m.screenH = sh;
		if (!(sw > 50.0f) || !(sh > 50.0f) || !std::isfinite(sw) || !std::isfinite(sh)) return m;
		scaleK = std::clamp(scaleK, 0.5f, 1.4f);
		slideK = std::clamp(slideK, 0.0f, 1.0f);

		const GfxSettings::Insets zi = GfxSettings::SafeInsets(sw, sh);                 // Safe Zone do menu Graficos
		const float totalLu = kScreenH + 2.0f * kBezelLu;
		const float totalWLu = kScreenW + 2.0f * kBezelLu;
		float fh = (sh - 2.0f * zi.y) * 0.90f * scaleK;                                 // 90% da altura util (nunca encosta nas bordas)
		fh = std::min(fh, (sh - 2.0f * zi.y) * 0.97f);
		float fw = fh * totalWLu / totalLu;
		if (fw > (sw - 2.0f * zi.x) * 0.5f) { fw = (sw - 2.0f * zi.x) * 0.5f; fh = fw * totalLu / totalWLu; }   // no maximo metade da largura
		const float s = fh / totalLu;
		const float marginX = std::max(sw * 0.03f, zi.x);
		m.frame = { sw - marginX - fw, (sh - fh) * 0.5f + (1.0f - slideK) * (fh * 0.55f), fw, fh };
		m.s = s;
		m.bezel = kBezelLu * s;
		m.frameRadius = kFrameRadiusLu * s;
		m.screenRadius = kScreenRadiusLu * s;
		m.screen = { m.frame.x + m.bezel, m.frame.y + m.bezel, kScreenW * s, kScreenH * s };
		m.statusBar = { m.screen.x, m.screen.y, m.screen.w, kStatusLu * s };
		m.navBar = { m.screen.x, m.screen.bottom() - kHomeBarLu * s, m.screen.w, kHomeBarLu * s };
		m.content = { m.screen.x, m.screen.y + kStatusLu * s, m.screen.w, m.screen.h - (kStatusLu + kHomeBarLu) * s };
		m.valid = m.content.w > 10 && m.content.h > 10;
		return m;
	}
}
