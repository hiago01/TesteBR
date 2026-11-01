#pragma once
// Maximo OS - HomeScreen: papel de parede, relogio grande e grade paginada de icones (um icone por app do AppRegistry).
#include "AppModel.h"
#include "NotificationCenter.h"
#include "StatusBar.h"

namespace MaximoOS
{
	class HomeScreen
	{
	public:
		// retorna o id do app tocado (vazio = nada). Deve receber os toques SO quando a tela inicial esta visivel.
		std::string onTouch(const Touch& t, const Metrics& m, const AppRegistry& reg)
		{
			const GridSpec g = grid(m);
			const int per = g.perPage;
			const int total = (int)reg.size();
			const int pages = std::max(1, (total + per - 1) / per);
			m_page = std::clamp(m_page, 0, pages - 1);
			std::string hit;
			switch (t.kind)
			{
			case Touch::Down: m_pressed = cellAt(g, reg, t.pos); break;
			case Touch::Move: if (m_pressed >= 0 && cellAt(g, reg, t.pos) != m_pressed) m_pressed = -1; break;
			case Touch::DragBegin: m_pressed = -1; break;
			case Touch::DragEnd:
				if (t.pos.x - t.start.x < -m.px(60) && m_page < pages - 1) ++m_page;
				else if (t.pos.x - t.start.x > m.px(60) && m_page > 0) --m_page;
				break;
			case Touch::Tap: { int i = cellAt(g, reg, t.pos); if (i >= 0) hit = reg.list()[i].id; m_pressed = -1; break; }
			case Touch::Up: case Touch::Cancel: case Touch::LongPress: m_pressed = -1; break;
			default: break;
			}
			return hit;
		}

		void draw(Canvas& cv, const Metrics& m, const AppRegistry& reg, const StatusInfo& st, const NotificationCenter& nc)
		{
			// papel de parede: degrade azul-marinho -> grafite, com faixa diagonal discreta
			cv.fillGradientV(m.content, Color(14, 38, 92), Color(9, 12, 22));
			cv.fillGradientV({ m.statusBar.x, m.statusBar.y, m.statusBar.w, m.statusBar.h }, Color(14, 38, 92), Color(14, 38, 92));
			cv.fillCircle({ m.content.right() - m.content.w * 0.12f, m.content.y + m.content.h * 0.1f }, m.content.h * 0.55f, Color(30, 144, 255, 22));
			cv.fillCircle({ m.content.x + m.content.w * 0.05f, m.content.bottom() }, m.content.h * 0.45f, Color(30, 144, 255, 16));

			// relogio grande + marca
			const float clockH = m.px(86);
			DrawTextIn(cv, { m.content.x + m.px(40), m.content.y + m.px(14), m.content.w * 0.5f, clockH * 0.7f }, st.clock, Colors::Text, m.font(54));
			DrawTextIn(cv, { m.content.x + m.px(42), m.content.y + m.px(14) + clockH * 0.7f, m.content.w * 0.5f, m.px(24) }, "MAXIMO OS", Colors::TextDim, m.font(Fonts::Caption));

			const GridSpec g = grid(m);
			const int per = g.perPage, total = (int)reg.size();
			const int pages = std::max(1, (total + per - 1) / per);
			m_page = std::clamp(m_page, 0, pages - 1);
			for (int k = 0; k < per; ++k)
			{
				const int i = m_page * per + k;
				if (i >= total) break;
				const AppInfo& a = reg.list()[i];
				AppIconStyle st2; st2.badge = nc.unreadFor(a.id); st2.dim = a.placeholder;
				DrawAppIcon(cv, m, GridCell(g, k), a.title, a.icon, a.accent, m_pressed == i, st2);
			}
			if (pages > 1)
			{
				const float d = m.px(8), gap = m.px(10);
				float x = m.content.center().x - (pages * d + (pages - 1) * gap) * 0.5f;
				const float y = m.content.bottom() - m.px(16);
				for (int p = 0; p < pages; ++p, x += d + gap) cv.fillCircle({ x + d * 0.5f, y }, d * 0.5f, p == m_page ? Colors::Text : Colors::Line);
			}
		}

		GridSpec grid(const Metrics& m) const
		{
			const Rect area{ m.content.x + m.px(40), m.content.y + m.px(110), m.content.w - m.px(80), m.content.h - m.px(110) - m.px(34) };
			return ComputeGrid(area, m.px(150), m.px(128), 6);
		}
		int page() const { return m_page; }
		void setPage(int p) { m_page = std::max(0, p); }
		void reset() { m_page = 0; m_pressed = -1; }
	private:
		int cellAt(const GridSpec& g, const AppRegistry& reg, Vec2 p) const
		{
			for (int k = 0; k < g.perPage; ++k)
			{
				const int i = m_page * g.perPage + k;
				if (i >= (int)reg.size()) break;
				if (GridCell(g, k).contains(p)) return i;
			}
			return -1;
		}
		int m_page = 0, m_pressed = -1;
	};
}
