#pragma once
// Maximo OS - ResolutionManager: UNICO lugar que decide tamanho/posicao/escala/fonte de toda a interface do tablet.
// Logica pura (testada no PC em varias resolucoes). Todos os componentes e apps usam Metrics; nada usa pixels fixos.
#include "Types.h"

namespace MaximoOS
{
	// Resolucao "de projeto" da TELA do tablet (unidades logicas, "lu"). Tudo e desenhado nela e escalado por Metrics::s.
	constexpr float kDesignW = 1120.0f;
	constexpr float kDesignH = 720.0f;                 // proporcao da tela do tablet = 1,556 (16:10,3)
	constexpr float kFrameAspect = 1.40f;              // moldura completa (tela + bezel) = proporcao do tablet de referencia

	struct Metrics
	{
		float screenW = 0, screenH = 0;                // tela do celular (pixels)
		Rect  frame;                                   // moldura inteira do tablet
		Rect  screen;                                  // area da tela do tablet (dentro da moldura)
		float s = 1.0f;                                // pixels por unidade logica
		float bezel = 0, frameRadius = 0, screenRadius = 0;
		Rect  statusBar, navBar, content;              // content = entre status e nav
		bool  valid = false;

		float px(float lu) const { return lu * s; }                                    // comprimento logico -> pixels
		float font(float lu) const { return std::max(lu * s, kMinFontPx); }            // fonte nunca menor que o minimo legivel
		static constexpr float kMinFontPx = 11.0f;
	};

	inline Metrics ComputeMetrics(float sw, float sh)
	{
		Metrics m;
		m.screenW = sw; m.screenH = sh;
		if (!(sw > 50.0f) || !(sh > 50.0f) || !std::isfinite(sw) || !std::isfinite(sh)) return m;

		// area disponivel: margem de 3% nas laterais e 4% em cima/embaixo (nunca encosta nas bordas do celular)
		const float availW = sw * 0.94f, availH = sh * 0.92f;
		float fh = availH, fw = fh * kFrameAspect;
		if (fw > availW) { fw = availW; fh = fw / kFrameAspect; }
		m.frame = { (sw - fw) * 0.5f, (sh - fh) * 0.5f, fw, fh };
		m.bezel = fh * 0.026f;
		m.frameRadius = fh * 0.045f;
		m.screenRadius = std::max(2.0f, m.frameRadius - m.bezel * 0.6f);
		m.screen = m.frame.inset(m.bezel);
		m.s = m.screen.w / kDesignW;

		const float status = 34.0f * m.s, nav = 46.0f * m.s;
		m.statusBar = { m.screen.x, m.screen.y, m.screen.w, status };
		m.navBar = { m.screen.x, m.screen.bottom() - nav, m.screen.w, nav };
		m.content = { m.screen.x, m.screen.y + status, m.screen.w, m.screen.h - status - nav };
		m.valid = m.content.w > 10 && m.content.h > 10;
		return m;
	}

	// Grade de icones (tela inicial): quantas colunas/linhas cabem em 'area' com celulas de cellW x cellH (pixels).
	struct GridSpec { int cols = 1, rows = 1, perPage = 1; float cellW = 0, cellH = 0; Rect area; };

	inline GridSpec ComputeGrid(const Rect& area, float minCellW, float minCellH, int maxCols = 6)
	{
		GridSpec g;
		g.area = area;
		g.cols = std::clamp((int)std::floor(area.w / std::max(1.0f, minCellW)), 1, maxCols);
		g.rows = std::max(1, (int)std::floor(area.h / std::max(1.0f, minCellH)));
		g.cellW = area.w / g.cols;
		g.cellH = std::min(area.h / g.rows, minCellH * 1.35f);
		g.perPage = g.cols * g.rows;
		return g;
	}

	// celula 'i' dentro da pagina (0..perPage-1), a grade fica alinhada ao topo e centralizada na horizontal
	inline Rect GridCell(const GridSpec& g, int i)
	{
		const int c = i % g.cols, r = i / g.cols;
		return { g.area.x + c * g.cellW, g.area.y + r * g.cellH, g.cellW, g.cellH };
	}
}
