#pragma once
// Maximo OS - StatusBar: relogio, sinal, bateria e sino de notificacoes. Os valores vem de StatusInfo (preenchido pela integracao;
// na etapa 1 sao valores de demonstracao/relogio do aparelho, nunca dados do servidor).
#include "Components.h"

namespace MaximoOS
{
	struct StatusInfo
	{
		std::string clock = "--:--";
		int signal = 4;          // 0..4
		int battery = 100;       // 0..100, -1 = oculto
		bool demo = false;       // true = valores ilustrativos da etapa 1
		int  weekday = -1;       // 0 = domingo .. 6; -1 = desconhecido
		int  day = 0, month = 0, year = 0;   // data do aparelho (month 1..12); 0 = desconhecida
	};

	struct StatusBarResult { Rect bell; };

	inline StatusBarResult DrawStatusBar(Canvas& cv, const Metrics& m, const StatusInfo& st, int unread, bool bellPressed)
	{
		const Rect& r = m.statusBar;
		cv.fillRect(r, Color(0, 0, 0, 90), 0);
		const float fs = m.font(Fonts::Caption);
		const float pad = m.px(18);
		DrawTextIn(cv, { r.x + pad, r.y, m.px(120), r.h }, st.clock, Colors::Text, fs);
		DrawTextIn(cv, { r.center().x - m.px(80), r.y, m.px(160), r.h }, "MÁXIMO RP", Colors::Text.alpha(0.7f), fs, Align::Center);
		float x = r.right() - pad;
		StatusBarResult res;
		// bateria
		if (st.battery >= 0)
		{
			const float bw = m.px(30), bh = m.px(14);
			const Rect b{ x - bw, r.y + (r.h - bh) * 0.5f, bw, bh };
			cv.strokeRect(b, Colors::TextDim, m.px(3), std::max(1.0f, m.px(1.2f)));
			const float k = std::clamp(st.battery / 100.0f, 0.0f, 1.0f);
			cv.fillRect({ b.x + m.px(2), b.y + m.px(2), (b.w - m.px(4)) * k, b.h - m.px(4) }, st.battery <= 15 ? Colors::Danger : Colors::Ok, m.px(1.5f));
			cv.fillRect({ b.right() + 1, b.y + bh * 0.3f, m.px(2.5f), bh * 0.4f }, Colors::TextDim, 0);
			x = b.x - m.px(14);
		}
		// sinal (4 barras)
		{
			const float bw = m.px(4), gap = m.px(2.5f), hmax = m.px(15);
			const float w = 4 * bw + 3 * gap;
			x -= w;
			for (int i = 0; i < 4; ++i)
			{
				const float h = hmax * (0.35f + 0.65f * i / 3.0f);
				cv.fillRect({ x + i * (bw + gap), r.y + (r.h + hmax) * 0.5f - h, bw, h }, i < st.signal ? Colors::Text : Colors::Line, m.px(1));
			}
			x -= m.px(16);
		}
		// sino
		{
			const float s = m.px(24);
			res.bell = { x - s, r.y, s + m.px(8), r.h };
			if (bellPressed) cv.fillRect(res.bell, Colors::PanelHi.alpha(0.6f), m.px(8));
			Icons::Draw(cv, Icon::Bell, { x - s, r.y + (r.h - s) * 0.5f, s, s }, unread > 0 ? Colors::Text : Colors::TextDim);
			if (unread > 0)
			{
				const float br = m.px(5);
				cv.fillCircle({ x - m.px(2), r.y + r.h * 0.5f - m.px(8) }, br, Colors::Danger);
			}
		}
		return res;
	}
}
