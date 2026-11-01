#pragma once
// Maximo OS - HomeScreen (etapa 2): papel de parede, relogio, 4 widgets, grade paginada de apps (6x4, deslizar entre paginas),
// indicadores de pagina e dock translucido com 8 apps. Layout em unidades logicas (lu) escaladas por Metrics::s.
// Widgets sao honestos: sem dado real no cliente mostram "indisponivel"/"nenhum" (nunca valores inventados).
#include "AppModel.h"
#include "NotificationCenter.h"
#include "StatusBar.h"
#include <cmath>

namespace MaximoOS
{
	struct HomeLayout
	{
		Rect clock, date, calendar, weather, news, media;   // coluna esquerda
		Rect grid, dots, dock;                              // grade, indicadores, dock
		Rect dockSlot[8];
		float cellW = 0, cellH = 0;
		static constexpr int kCols = 6, kRows = 4, kPerPage = 24, kDock = 8;
	};

	inline HomeLayout ComputeHomeLayout(const Metrics& m)
	{
		HomeLayout L; const Rect c = m.content;
		auto R = [&](float x, float y, float w, float h) { return Rect{ c.x + m.px(x), c.y + m.px(y), m.px(w), m.px(h) }; };
		L.clock = R(36, 10, 268, 66); L.date = R(38, 76, 268, 24);
		L.calendar = R(36, 112, 128, 112); L.weather = R(172, 112, 132, 112);
		L.news = R(36, 232, 268, 138); L.media = R(36, 378, 268, 104);
		L.grid = R(332, 14, 756, 488);
		L.dots = R(332, 504, 756, 22);
		const float dockW = 8 * 78 + 28, dockH = 80;
		L.dock = { c.x + (c.w - m.px(dockW)) * 0.5f, c.bottom() - m.px(dockH + 12), m.px(dockW), m.px(dockH) };
		for (int i = 0; i < 8; ++i) L.dockSlot[i] = { L.dock.x + m.px(14 + i * 78), L.dock.y + m.px(8), m.px(78), m.px(64) };
		L.cellW = L.grid.w / HomeLayout::kCols; L.cellH = L.grid.h / HomeLayout::kRows;
		return L;
	}

	namespace Pt
	{
		inline const char* Weekday(int d) { static const char* n[] = { "Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sábado" }; return (d >= 0 && d < 7) ? n[d] : ""; }
		inline const char* Month(int m) { static const char* n[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" }; return (m >= 1 && m <= 12) ? n[m - 1] : ""; }
	}

	class HomeScreen
	{
	public:
		// apps de uma pagina da grade, na ordem de registro
		static std::vector<const AppInfo*> PageApps(const AppRegistry& reg, int page)
		{
			std::vector<const AppInfo*> v;
			for (auto& a : reg.list()) if (a.page == page && (int)v.size() < HomeLayout::kPerPage) v.push_back(&a);
			return v;
		}
		static int PageCount(const AppRegistry& reg)
		{
			int mx = 0; for (auto& a : reg.list()) mx = std::max(mx, a.page + 1);
			return std::max(1, mx);
		}
		static const AppInfo* DockApp(const AppRegistry& reg, int slot) { for (auto& a : reg.list()) if (a.dock == slot) return &a; return nullptr; }

		// retorna o id do app tocado (vazio = nada)
		std::string onTouch(const Touch& t, const Metrics& m, const AppRegistry& reg)
		{
			const HomeLayout L = ComputeHomeLayout(m);
			const int pages = PageCount(reg);
			m_page = std::clamp(m_page, 0, pages - 1);
			std::string hit;
			switch (t.kind)
			{
			case Touch::Down: m_pressed = idAt(L, reg, t.pos); break;
			case Touch::Move: if (!m_pressed.empty() && idAt(L, reg, t.pos) != m_pressed) m_pressed.clear(); break;
			case Touch::DragBegin:
				m_pressed.clear();
				m_drag = L.grid.contains(t.start) || L.dots.contains(t.start);
				break;
			case Touch::Drag:
				if (m_drag)
				{
					m_off += t.delta.x;
					const float lim = L.grid.w * 0.35f;
					if (m_page == 0 && m_off > lim) m_off = lim;               // resistencia nas pontas
					if (m_page == pages - 1 && m_off < -lim) m_off = -lim;
				}
				break;
			case Touch::DragEnd:
				if (m_drag)
				{
					const float W = L.grid.w;
					int dir = 0;
					if (m_off < -W * 0.2f || t.velocity.x < -700.0f * m.s) dir = 1;
					else if (m_off > W * 0.2f || t.velocity.x > 700.0f * m.s) dir = -1;
					if (dir == 1 && m_page < pages - 1) { ++m_page; m_off += W; }
					else if (dir == -1 && m_page > 0) { --m_page; m_off -= W; }
				}
				m_drag = false; break;
			case Touch::Tap:
			{
				if (std::fabs(m_off) < 2.0f)
				{
					hit = idAt(L, reg, t.pos); if (hit.size() > 2) hit = hit.substr(2);
					if (hit.empty() && L.dots.contains(t.pos) && pages > 1) { const int p = std::clamp((int)((t.pos.x - L.dots.x) / L.dots.w * pages), 0, pages - 1); if (p != m_page) { m_off += (m_page - p) * L.grid.w; m_page = p; } }
				}
				m_pressed.clear(); break;
			}
			case Touch::Up: case Touch::Cancel: case Touch::LongPress: m_pressed.clear(); if (t.kind == Touch::Cancel) m_drag = false; break;
			default: break;
			}
			return hit;
		}

		void update(float dtMs)
		{
			if (m_drag) return;
			if (std::fabs(m_off) < 0.6f) { m_off = 0; return; }
			m_off *= std::exp(-dtMs * 0.016f);                               // assenta suavemente na pagina
		}

		// papel de parede ocupa a tela inteira do tablet (por baixo da barra de status e de navegacao)
		void drawWallpaper(Canvas& cv, const Metrics& m) const
		{
			const Rect s = m.screen;
			cv.fillGradientV({ s.x, s.y, s.w, s.h * 0.5f }, Color(238, 138, 52), Color(214, 70, 74));
			cv.fillGradientV({ s.x, s.y + s.h * 0.5f, s.w, s.h * 0.5f + 1 }, Color(214, 70, 74), Color(142, 66, 160));
			cv.fillCircle({ s.right() + s.w * 0.02f, s.y - s.h * 0.02f }, s.h * 0.46f, Color(150, 176, 232, 225));       // forma clara (canto superior direito)
			cv.fillCircle({ s.x + s.w * 0.02f, s.bottom() + s.h * 0.05f }, s.h * 0.36f, Color(120, 120, 190, 215));     // forma clara (canto inferior esquerdo)
			cv.fillCircle({ s.x + s.w * 0.70f, s.bottom() + s.h * 0.20f }, s.h * 0.78f, Color(20, 52, 130, 235));       // curva azul profunda
			cv.fillCircle({ s.x + s.w * 0.80f, s.bottom() + s.h * 0.28f }, s.h * 0.62f, Color(120, 66, 170, 200));      // curva roxa
			cv.fillCircle({ s.x + s.w * 0.30f, s.y - s.h * 0.30f }, s.h * 0.50f, Color(255, 190, 90, 60));              // brilho quente
			cv.strokeCircle({ s.x + s.w * 0.70f, s.bottom() + s.h * 0.20f }, s.h * 0.78f, Color(255, 255, 255, 28), std::max(1.0f, m.px(1.5f)));
		}

		void draw(Canvas& cv, const Metrics& m, const AppRegistry& reg, const AppManager& apps, const StatusInfo& st, const NotificationCenter& nc)
		{
			const HomeLayout L = ComputeHomeLayout(m);
			const int pages = PageCount(reg);
			m_page = std::clamp(m_page, 0, pages - 1);

			// ---- relogio ----
			DrawTextIn(cv, L.clock, st.clock, Colors::Text, m.font(54));
			std::string d = st.weekday >= 0 ? Pt::Weekday(st.weekday) : std::string();
			if (st.day > 0) d += (d.empty() ? "" : ", ") + std::to_string(st.day) + " de " + Pt::Month(st.month);
			DrawTextIn(cv, L.date, d, Colors::Text.alpha(0.92f), m.font(Fonts::Body));

			drawCalendar(cv, m, L.calendar, st);
			drawWeather(cv, m, L.weather);
			drawNews(cv, m, L.news);
			drawMedia(cv, m, L.media);

			// ---- grade (duas paginas visiveis durante o arrasto) ----
			cv.pushClip(L.grid.inset(0, -m.px(4)));
			for (int p = 0; p < pages; ++p)
			{
				const float ox = (p - m_page) * L.grid.w + m_off;
				if (ox > L.grid.w || ox + L.grid.w < 0) continue;
				const auto list = PageApps(reg, p);
				for (int k = 0; k < (int)list.size(); ++k)
				{
					const AppInfo& a = *list[k];
					const Rect cell{ L.grid.x + (k % HomeLayout::kCols) * L.cellW + ox, L.grid.y + (k / HomeLayout::kCols) * L.cellH, L.cellW, L.cellH };
					AppIconStyle sty; sty.badge = nc.unreadFor(a.id); sty.shadowLabel = true;
					DrawAppIcon(cv, m, cell, a.title, a.icon, a.accent, m_pressed == "g:" + a.id && std::fabs(m_off) < 2.0f, sty);
				}
			}
			cv.popClip();

			// ---- indicadores de pagina ----
			if (pages > 1)
			{
				const float dd = m.px(8), gap = m.px(12);
				float x = L.dots.center().x - (pages * dd + (pages - 1) * gap) * 0.5f;
				for (int p = 0; p < pages; ++p, x += dd + gap)
				{
					const float near = 1.0f - std::min(1.0f, std::fabs((p - m_page) * L.grid.w + m_off) / L.grid.w);
					cv.fillCircle({ x + dd * 0.5f, L.dots.center().y }, dd * 0.5f * (0.85f + 0.25f * near), Color(255, 255, 255, (int)(110 + 130 * near)));
				}
			}

			// ---- dock ----
			cv.fillRect(L.dock.moved(0, m.px(3)), Color(0, 0, 0, 50), m.px(28));
			cv.fillRect(L.dock, Color(255, 255, 255, 70), m.px(28));
			cv.strokeRect(L.dock, Color(255, 255, 255, 90), m.px(28), std::max(1.0f, m.px(1)));
			for (int i = 0; i < HomeLayout::kDock; ++i)
			{
				const AppInfo* a = DockApp(reg, i); if (!a) continue;
				const Rect s = L.dockSlot[i];
				const bool pr = (m_pressed == "d:" + a->id);
				if (pr) cv.fillRect(s.inset(m.px(3), m.px(1)), Color(255, 255, 255, 80), m.px(18));           // selecao
				const float isz = m.px(54);
				const Vec2 c = s.center();
				AppIconStyle sty; sty.showLabel = false; sty.badge = nc.unreadFor(a->id);
				DrawAppIcon(cv, m, { c.x - isz / 0.62f * 0.5f, c.y - isz / 0.82f * 0.5f, isz / 0.62f, isz / 0.82f }, a->title, a->icon, a->accent, pr, sty);
				if (apps.isRunning(a->id)) cv.fillCircle({ c.x, s.bottom() + m.px(2) }, m.px(2.6f), Colors::Text);   // app em execucao
			}
		}

		const HomeLayout layout(const Metrics& m) const { return ComputeHomeLayout(m); }
		int page() const { return m_page; }
		float offset() const { return m_off; }
		void setPage(int p) { m_page = std::max(0, p); m_off = 0; }
		void reset() { m_page = 0; m_off = 0; m_pressed.clear(); m_drag = false; }

	private:
		// id tocavel no ponto, com prefixo "d:" (dock) ou "g:" (grade): o mesmo app pode estar nos dois
		std::string idAt(const HomeLayout& L, const AppRegistry& reg, Vec2 p) const
		{
			if (L.dock.contains(p))
			{
				for (int i = 0; i < HomeLayout::kDock; ++i) if (L.dockSlot[i].contains(p)) { const AppInfo* a = DockApp(reg, i); return a ? "d:" + a->id : std::string(); }
				return std::string();
			}
			if (!L.grid.contains(p) || std::fabs(m_off) >= 2.0f) return std::string();
			const auto list = PageApps(reg, m_page);
			const int c = (int)((p.x - L.grid.x) / L.cellW), r = (int)((p.y - L.grid.y) / L.cellH);
			const int k = r * HomeLayout::kCols + c;
			if (c < 0 || c >= HomeLayout::kCols || r < 0 || k >= (int)list.size()) return std::string();
			return "g:" + list[k]->id;
		}

		static void glass(Canvas& cv, const Metrics& m, const Rect& r) { cv.fillRect(r, Color(12, 16, 32, 150), m.px(18)); cv.strokeRect(r, Color(255, 255, 255, 40), m.px(18), std::max(1.0f, m.px(1))); }

		void drawCalendar(Canvas& cv, const Metrics& m, const Rect& r, const StatusInfo& st) const
		{
			cv.fillRect(r, Color(248, 248, 250), m.px(18));
			std::string wd = st.weekday >= 0 ? Pt::Weekday(st.weekday) : std::string(); for (auto& ch : wd) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
			const size_t dash = wd.find('-'); if (dash != std::string::npos) wd.resize(dash);          // "SEGUNDA-FEIRA" -> "SEGUNDA"
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(8), r.w - m.px(24), m.px(18) }, wd, Colors::Danger, m.font(Fonts::Caption));
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(26), r.w - m.px(24), m.px(46) }, st.day > 0 ? std::to_string(st.day) : "--", Color(24, 26, 34), m.font(40));
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(72), r.w - m.px(24), m.px(16) }, st.day > 0 ? std::string(Pt::Month(st.month)) + " de " + std::to_string(st.year) : "", Color(90, 96, 112), m.font(Fonts::Tiny));
			cv.fillRect({ r.x + m.px(12), r.y + m.px(92), m.px(3), m.px(14) }, Colors::Accent, m.px(1.5f));
			DrawTextIn(cv, { r.x + m.px(20), r.y + m.px(90), r.w - m.px(30), m.px(18) }, "Sem compromissos", Color(90, 96, 112), m.font(Fonts::Tiny));
		}
		void drawWeather(Canvas& cv, const Metrics& m, const Rect& r) const
		{
			cv.fillGradientV(r.inset(m.px(2), 0), Color(52, 150, 214), Color(40, 104, 190));
			cv.strokeRect(r, Color(255, 255, 255, 60), m.px(18), std::max(1.0f, m.px(1)));
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(8), r.w - m.px(24), m.px(20) }, "Clima", Colors::Text, m.font(Fonts::Body));
			cv.fillCircle({ r.x + m.px(30), r.y + m.px(52) }, m.px(11), Color(255, 214, 90));
			cv.fillCircle({ r.x + m.px(44), r.y + m.px(58) }, m.px(12), Color(235, 242, 252, 235));
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(70), r.w - m.px(24), m.px(18) }, "Indisponível", Colors::Text, m.font(Fonts::Caption));
			DrawTextIn(cv, { r.x + m.px(12), r.y + m.px(88), r.w - m.px(24), m.px(16) }, "Sem dados", Colors::Text.alpha(0.8f), m.font(Fonts::Tiny));
		}
		void drawNews(Canvas& cv, const Metrics& m, const Rect& r) const
		{
			glass(cv, m, r);
			DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(10), r.w - m.px(60), m.px(22) }, "Comunicados", Colors::Text, m.font(Fonts::Body));
			cv.fillRect({ r.right() - m.px(40), r.y + m.px(10), m.px(24), m.px(22) }, Colors::Danger, m.px(6));
			Icons::Draw(cv, Icon::News, { r.right() - m.px(37), r.y + m.px(12), m.px(18), m.px(18) }, Colors::Text);
			cv.fillRect({ r.x + m.px(16), r.y + m.px(40), r.w - m.px(32), std::max(1.0f, m.px(1)) }, Color(255, 255, 255, 40), 0);
			DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(52), r.w - m.px(32), m.px(22) }, "Nenhum comunicado do servidor.", Colors::Text, m.font(Fonts::Caption));
			DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(78), r.w - m.px(32), m.px(20) }, "Integração ainda não implementada.", Colors::Text.alpha(0.7f), m.font(Fonts::Tiny));
		}
		void drawMedia(Canvas& cv, const Metrics& m, const Rect& r) const
		{
			glass(cv, m, r);
			const Rect art{ r.x + m.px(14), r.y + m.px(14), m.px(60), m.px(60) };
			cv.fillRect(art, Color(255, 255, 255, 30), m.px(12));
			Icons::Draw(cv, Icon::Music, art.inset(m.px(14)), Colors::Text.alpha(0.8f));
			DrawTextIn(cv, { art.right() + m.px(12), r.y + m.px(16), r.right() - art.right() - m.px(26), m.px(22) }, "Mídia", Colors::Text, m.font(Fonts::Body));
			DrawTextIn(cv, { art.right() + m.px(12), r.y + m.px(40), r.right() - art.right() - m.px(26), m.px(18) }, "Nada em reprodução", Colors::Text.alpha(0.85f), m.font(Fonts::Caption));
			DrawTextIn(cv, { art.right() + m.px(12), r.y + m.px(60), r.right() - art.right() - m.px(26), m.px(16) }, "Sem player compatível", Colors::Text.alpha(0.6f), m.font(Fonts::Tiny));
		}

		int m_page = 0; float m_off = 0; bool m_drag = false; std::string m_pressed;
	};
}
