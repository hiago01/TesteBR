#pragma once
// Maximo OS - OSManager: orquestra TUDO (registro de apps, AppManager, tela inicial, barra de status, navegacao, recentes,
// painel de notificacoes, toque -> gestos). Logica pura: recebe toques brutos e um Canvas; nao conhece ImGui nem o jogo.
// A integracao com o jogo (gui/samp_widgets/maximoos.cpp) so: (1) abastece o InputManager, (2) cria o ImGuiCanvas, (3) chama frame().
#include "AppModel.h"
#include "BuiltinApps.h"
#include "HomeScreen.h"
#include "InputManager.h"
#include "Navigation.h"
#include "NotificationCenter.h"
#include "StatusBar.h"
#include <ctime>
#include <functional>

namespace MaximoOS
{
	class OSManager
	{
	public:
		OSManager() : m_apps(m_reg) { registerBuiltinApps(); }

		// ---------- registro dos apps previstos (etapa 1: placeholders; o "Teste" e o unico com tela) ----------
		void registerBuiltinApps()
		{
			// page: pagina da grade (-1 = so no dock);  dock: posicao 0..7 (-1 = fora do dock).  A ORDEM define a posicao na grade.
			struct P { const char* id; const char* title; Icon ic; Color c; int page; int dock; };
			const P list[] = {
				// ---- pagina 1 (6 x 4, como a referencia) ----
				{ "comunicacao",   "Comunicação",   Icon::Radio,    Color(46, 184, 114),  0, 1 },
				{ "calendario",    "Calendário",    Icon::Calendar, Color(232, 84, 84),   0, -1 },
				{ "relogio",       "Relógio",       Icon::Clock,    Color(40, 46, 66),    0, -1 },
				{ "inicio",        "Início",        Icon::Home,     Color(240, 150, 50),  0, -1 },
				{ "galeria",       "Galeria",       Icon::Gallery,  Color(190, 90, 200),  0, 5 },
				{ "camera",        "Câmera",        Icon::Camera,   Color(96, 104, 124),  0, -1 },
				{ "lembretes",     "Lembretes",     Icon::Reminder, Color(236, 112, 70),  0, -1 },
				{ "anotacoes",     "Anotações",     Icon::Notes,    Color(238, 190, 52),  0, 7 },
				{ "gravacoes",     "Gravações",     Icon::Mic,      Color(200, 60, 80),   0, -1 },
				{ "cidadaos",      "Cidadãos",      Icon::People,   Color(52, 168, 130),  0, -1 },
				{ "mapa",          "Mapa",          Icon::Map,      Color(70, 160, 90),   0, -1 },
				{ "localizador",   "Localizador",   Icon::Pin,      Color(60, 190, 150),  0, -1 },
				{ "aplicativos",   "Aplicativos",   Icon::Apps,     Color(30, 144, 255),  0, -1 },
				{ "documentos",    "Documentos",    Icon::Document, Color(60, 130, 200),  0, -1 },
				{ "audio",         "Áudio",         Icon::Speaker,  Color(120, 80, 210),  0, -1 },
				{ "midia",         "Mídia",         Icon::Play,     Color(220, 70, 110),  0, -1 },
				{ "noticias",      "Notícias",      Icon::News,     Color(232, 70, 70),   0, 6 },
				{ "economia",      "Economia",      Icon::Coin,     Color(214, 168, 40),  0, -1 },
				{ "ferramentas",   "Ferramentas",   Icon::Wrench,   Color(110, 120, 140), 0, -1 },
				{ "configuracoes", "Configurações", Icon::Gear,     Color(120, 130, 150), 0, -1 },
				// ---- pagina 2: apps do prototipo anterior (preservados) ----
				{ "mdt",           "MDT",           Icon::Shield,   Color(30, 144, 255),  1, -1 },
				{ "veiculos",      "Veículos",      Icon::Car,      Color(240, 150, 50),  1, -1 },
				{ "ocorrencias",   "Ocorrências",   Icon::Alert,    Color(232, 84, 84),   1, -1 },
				{ "mandados",      "Mandados",      Icon::Document, Color(150, 110, 220), 1, -1 },
				{ "organizacoes",  "Organizações",  Icon::Building, Color(60, 130, 200),  1, -1 },
				{ "calculadora",   "Calculadora",   Icon::Calculator, Color(90, 100, 120), 1, -1 },
				// ---- so no dock ----
				{ "mensagens",     "Mensagens",     Icon::Message,  Color(68, 190, 110), -1, 0 },
				{ "musica",        "Música",        Icon::Music,    Color(240, 80, 120), -1, 2 },
				{ "correspondencia","Correspondência", Icon::Mail,   Color(40, 150, 240), -1, 3 },
				{ "arquivos",      "Arquivos",      Icon::Folder,   Color(230, 180, 60), -1, 4 },
			};
			for (const P& p : list)
			{
				AppInfo i; i.id = p.id; i.title = p.title; i.icon = p.ic; i.accent = p.c; i.placeholder = true; i.page = p.page; i.dock = p.dock;
				std::string t = p.title; Icon ic = p.ic; Color c = p.c;
				i.factory = [t, ic, c]() { return std::unique_ptr<App>(new PlaceholderApp(t, ic, c)); };
				m_reg.add(i);
			}
			AppInfo t; t.id = "teste"; t.title = "Teste"; t.icon = Icon::Wrench; t.accent = Color(160, 90, 220); t.page = 1;
			t.factory = []() { return std::unique_ptr<App>(new TestApp()); };
			m_reg.add(t);
		}

		// ---------- configuracao ----------
		void setScreenSize(float w, float h)
		{
			if (w == m_m.screenW && h == m_m.screenH && m_m.valid) return;
			m_m = ComputeMetrics(w, h);
			m_gest.slop = std::max(8.0f, m_m.px(10));
			if (m_m.valid) m_apps.resize(m_m);
		}
		void setStatusProvider(std::function<StatusInfo()> f) { m_statusFn = std::move(f); }

		// ---------- abrir / fechar ----------
		void open()
		{
			if (m_open) return;
			m_open = true; if (!m_input.isOpen()) m_input.setOpen(true);   // a integracao pode ja ter armado a captura
			m_home.reset(); m_notifOpen = m_recentsOpen = false; m_route = Route::None;
			m_gest = GestureTracker(); m_gest.slop = std::max(8.0f, m_m.px(10));
			m_apps.resumeAll();
			m_trans.start(0);
		}
		void close()
		{
			if (!m_open) return;
			m_open = false; m_apps.pauseAll(); m_input.setOpen(false);
			m_notifOpen = m_recentsOpen = false; m_route = Route::None;
		}
		void toggle() { if (m_open) close(); else open(); }
		bool isOpen() const { return m_open; }

		// Voltar do Android / do tablet. true = consumido pelo tablet.
		bool back()
		{
			if (!m_open) return false;
			if (m_notifOpen) { m_notifOpen = false; return true; }
			if (m_recentsOpen) { m_recentsOpen = false; return true; }
			const bool hadApp = !m_apps.foregroundId().empty();
			if (m_apps.back()) { if (hadApp) m_trans.start(-1); return true; }
			close();                                       // ja estava na tela inicial: fecha o tablet
			return true;
		}
		void goHome() { m_notifOpen = m_recentsOpen = false; if (!m_apps.foregroundId().empty()) { m_apps.goHome(); m_trans.start(-1); } }
		bool openApp(const std::string& id) { m_notifOpen = m_recentsOpen = false; const bool ok = m_apps.open(id, m_m); if (ok) m_trans.start(1); return ok; }

		// ---------- quadro: consome toques, atualiza e desenha ----------
		void frame(Canvas& cv, int64_t nowMs, float dtMs)
		{
			if (!m_open || !m_m.valid) return;
			m_now = nowMs;
			// 1) toques brutos -> gestos
			m_raw.clear(); m_input.drain(m_raw);
			for (const RawTouch& r : m_raw)
			{
				m_touches.clear();
				m_gest.feed(r.type, r.num, r.x, r.y, r.cancel, nowMs, m_touches);
				for (const Touch& t : m_touches) dispatch(t);
			}
			m_touches.clear(); m_gest.tick(nowMs, m_touches);
			for (const Touch& t : m_touches) dispatch(t);
			if (!m_open) return;                                  // um toque pode ter fechado o tablet
			// 2) atualiza
			m_trans.update(dtMs);
			m_home.update(dtMs);
			if (App* a = m_apps.foreground()) { a->update(dtMs); if (a->consumeFinish()) goHome(); }
			if (m_notifOpen) m_notifs.markAllRead();
			// 3) desenha
			draw(cv);
		}

		// ---------- acesso (integracao/testes) ----------
		InputManager& input() { return m_input; }
		AppRegistry& registry() { return m_reg; }
		AppManager& apps() { return m_apps; }
		NotificationCenter& notifications() { return m_notifs; }
		HomeScreen& home() { return m_home; }
		const Metrics& metrics() const { return m_m; }
		bool notifPanelOpen() const { return m_notifOpen; }
		bool recentsOpen() const { return m_recentsOpen; }
		// rects de navegacao (testes)
		Rect navBack() const { return navButton(0); }
		Rect navHome() const { return navButton(1); }
		Rect navRecents() const { return navButton(2); }
		Rect bellRect() const { return m_bell; }

	private:
		enum class Route { None, Outside, Nav, Status, Overlay, Content };

		Rect navButton(int i) const
		{
			const float w = m_m.px(110), gap = m_m.px(60), tot = 3 * w + 2 * gap;
			const float x0 = m_m.navBar.center().x - tot * 0.5f;
			return { x0 + i * (w + gap), m_m.navBar.y, w, m_m.navBar.h };
		}
		Rect notifPanelRect() const { const float w = std::min(m_m.px(400), m_m.content.w * 0.9f); return { m_m.content.right() - w - m_m.px(10), m_m.content.y + m_m.px(4), w, m_m.content.h * 0.78f }; }

		void dispatch(const Touch& t)
		{
			if (t.kind == Touch::Down)
			{
				if (!m_m.screen.contains(t.pos)) m_route = Route::Outside;
				else if (m_m.navBar.contains(t.pos)) m_route = Route::Nav;
				else if (m_m.statusBar.contains(t.pos)) m_route = Route::Status;
				else if (m_notifOpen || m_recentsOpen) m_route = Route::Overlay;
				else m_route = Route::Content;
				m_navPressed = -1;
				if (m_route == Route::Nav) for (int i = 0; i < 3; ++i) if (navButton(i).contains(t.pos)) m_navPressed = i;
				m_bellPressed = (m_route == Route::Status && m_bell.contains(t.pos));
			}
			const Route r = m_route;
			switch (r)
			{
			case Route::Outside:
				if (t.kind == Touch::Tap && !m_m.frame.contains(t.pos) && !m_m.frame.contains(t.start)) close();
				break;
			case Route::Nav:
				if (t.kind == Touch::Tap)
				{
					const int i = navButton(0).contains(t.pos) ? 0 : navButton(1).contains(t.pos) ? 1 : navButton(2).contains(t.pos) ? 2 : -1;
					if (i == 0) back();
					else if (i == 1) { if (!m_apps.foregroundId().empty() || m_notifOpen || m_recentsOpen) goHome(); else close(); }
					else if (i == 2) { m_notifOpen = false; m_recentsOpen = !m_recentsOpen; }
				}
				if (t.kind == Touch::Tap || t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_navPressed = -1;
				break;
			case Route::Status:
				if (t.kind == Touch::Tap && m_bell.contains(t.pos)) { m_recentsOpen = false; m_notifOpen = !m_notifOpen; }
				else if (t.kind == Touch::Tap) { m_notifOpen = false; }
				if (t.kind == Touch::Tap || t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_bellPressed = false;
				break;
			case Route::Overlay: overlayTouch(t); break;
			case Route::Content:
				if (App* a = m_apps.foreground()) a->onTouch(t, m_m, m_m.content);
				else
				{
					const std::string id = m_home.onTouch(t, m_m, m_reg);
					if (!id.empty()) openApp(id);
				}
				break;
			case Route::None: break;
			}
			if (t.kind == Touch::Cancel) m_route = Route::None;
		}

		void overlayTouch(const Touch& t)
		{
			if (t.kind != Touch::Tap) return;
			if (m_recentsOpen)
			{
				for (size_t i = 0; i < m_recentRects.size(); ++i)
				{
					if (m_recentClose[i].contains(t.pos)) { m_apps.close(m_recentIds[i]); return; }
					if (m_recentRects[i].contains(t.pos)) { const std::string id = m_recentIds[i]; m_recentsOpen = false; openApp(id); return; }
				}
				if (m_recentsClearAll.contains(t.pos)) { m_apps.closeAll(); return; }
				m_recentsOpen = false;                       // toque fora dos cartoes: fecha
			}
			else if (m_notifOpen)
			{
				const Rect p = notifPanelRect();
				if (!p.contains(t.pos)) { m_notifOpen = false; return; }
				if (m_notifClear.contains(t.pos)) { m_notifs.clear(); return; }
				for (size_t i = 0; i < m_notifRects.size(); ++i)
					if (m_notifRects[i].contains(t.pos))
					{
						const Notification& n = m_notifs.list()[i];
						const std::string app = n.appId; m_notifs.markRead(n.id); m_notifOpen = false;
						if (!app.empty()) openApp(app);
						return;
					}
			}
		}

		StatusInfo status() const
		{
			if (m_statusFn) return m_statusFn();
			StatusInfo s; s.demo = true;
			std::time_t tt = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
			localtime_s(&tmv, &tt);
#else
			localtime_r(&tt, &tmv);
#endif
			char b[16]; std::snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min); s.clock = b;
			s.weekday = tmv.tm_wday; s.day = tmv.tm_mday; s.month = tmv.tm_mon + 1; s.year = tmv.tm_year + 1900;
			return s;
		}

		void draw(Canvas& cv)
		{
			const Metrics& m = m_m;
			cv.setAlpha(1.0f);
			cv.fillRect({ 0, 0, m.screenW, m.screenH }, Colors::Scrim, 0);                         // escurece o jogo atras
			cv.fillRect(m.frame.moved(0, m.px(6)), Color(0, 0, 0, 90), m.frameRadius);              // sombra
			cv.fillRect(m.frame, Colors::Bezel, m.frameRadius);
			cv.strokeRect(m.frame, Colors::BezelEdge, m.frameRadius, std::max(1.5f, m.px(1.6f)));
			cv.fillCircle({ m.frame.right() - m.bezel * 0.5f, m.frame.center().y }, m.bezel * 0.16f, Color(60, 66, 80));   // camera
			cv.fillRect(m.screen, Colors::Bg, m.screenRadius);

			cv.pushClip(m.screen);
			const StatusInfo st = status();
			if (!m_apps.foreground()) m_home.drawWallpaper(cv, m);          // por baixo da barra de status e da navegacao
			// conteudo (com fade na troca)
			cv.pushClip(m.content);
			cv.setAlpha(std::max(0.05f, m_trans.alpha()));
			if (App* a = m_apps.foreground()) a->draw(cv, m, m.content);
			else m_home.draw(cv, m, m_reg, m_apps, st, m_notifs);
			cv.setAlpha(1.0f);
			cv.popClip();

			m_bell = DrawStatusBar(cv, m, st, m_notifs.unread(), m_bellPressed).bell;
			drawNavBar(cv);
			if (m_recentsOpen) drawRecents(cv);
			if (m_notifOpen) drawNotifPanel(cv);
			else drawToast(cv);
			cv.popClip();
		}

		void drawNavBar(Canvas& cv)
		{
			const Metrics& m = m_m;
			cv.fillRect(m.navBar, Color(0, 0, 0, 140), 0);
			const Icon ic[3] = { Icon::Back, Icon::Home, Icon::Recents };
			for (int i = 0; i < 3; ++i)
			{
				const Rect b = navButton(i);
				if (m_navPressed == i) cv.fillRect(b.inset(m.px(6), m.px(4)), Colors::PanelHi, m.px(12));
				const float s = m.px(24);
				Icons::Draw(cv, ic[i], { b.center().x - s * 0.5f, b.center().y - s * 0.5f, s, s }, (i == 2 && m_recentsOpen) ? Colors::Accent : Colors::Text);
			}
		}

		void drawRecents(Canvas& cv)
		{
			const Metrics& m = m_m;
			cv.fillRect(m.content, Color(6, 8, 14, 225), 0);
			m_recentRects.clear(); m_recentClose.clear(); m_recentIds.clear(); m_recentsClearAll = Rect();
			const auto& ids = m_apps.recents();
			if (ids.empty())
			{
				DrawTextIn(cv, m.content, "Nenhum aplicativo recente", Colors::TextDim, m.font(Fonts::Body), Align::Center);
				return;
			}
			const int n = (int)ids.size();
			const float cw = std::min(m.px(220), (m.content.w - m.px(60)) / n - m.px(14));
			const float ch = std::min(m.px(300), m.content.h - m.px(110));
			const float tot = n * cw + (n - 1) * m.px(14);
			float x = m.content.center().x - tot * 0.5f;
			const float y = m.content.y + (m.content.h - ch) * 0.5f - m.px(14);
			for (const std::string& id : ids)
			{
				const AppInfo* ai = m_reg.find(id); if (!ai) continue;
				const Rect r{ x, y, cw, ch };
				DrawPanel(cv, m, r, Colors::Panel, 16);
				const float is = std::min(cw * 0.5f, m.px(84));
				AppIconStyle sty; sty.showLabel = false;
				DrawAppIcon(cv, m, { r.center().x - is * 0.9f, r.y + ch * 0.2f, is * 1.8f, is * 1.8f }, ai->title, ai->icon, ai->accent, false, sty);
				DrawTextIn(cv, { r.x + m.px(8), r.bottom() - m.px(60), r.w - m.px(16), m.px(32) }, ai->title, Colors::Text, m.font(Fonts::Body), Align::Center);
				const Rect cl{ r.right() - m.px(40), r.y + m.px(6), m.px(34), m.px(34) };
				Icons::Draw(cv, Icon::Close, cl.inset(m.px(7)), Colors::TextDim);
				m_recentRects.push_back(r); m_recentClose.push_back(cl); m_recentIds.push_back(id);
				x += cw + m.px(14);
			}
			m_recentsClearAll = { m.content.center().x - m.px(110), m.content.bottom() - m.px(58), m.px(220), m.px(44) };
			DrawButton(cv, m, m_recentsClearAll, "Fechar todos", false, false);
		}

		void drawNotifPanel(Canvas& cv)
		{
			const Metrics& m = m_m;
			cv.fillRect(m.content, Color(0, 0, 0, 90), 0);
			const Rect p = notifPanelRect();
			DrawPanel(cv, m, p, Colors::Panel, 16);
			DrawTextIn(cv, { p.x + m.px(18), p.y + m.px(8), p.w * 0.5f, m.px(40) }, "Notificações", Colors::Text, m.font(Fonts::Header));
			m_notifClear = { p.right() - m.px(110), p.y + m.px(10), m.px(96), m.px(34) };
			if (!m_notifs.list().empty()) DrawButton(cv, m, m_notifClear, "Limpar", false, false); else m_notifClear = Rect();
			m_notifRects.clear();
			if (m_notifs.list().empty())
			{
				DrawTextIn(cv, { p.x, p.y + m.px(60), p.w, p.h - m.px(60) }, "Sem notificações", Colors::TextDim, m.font(Fonts::Body), Align::Center);
				return;
			}
			const float rh = m.px(72);
			float y = p.y + m.px(58);
			for (const Notification& n : m_notifs.list())
			{
				if (y + rh > p.bottom()) break;
				const Rect r{ p.x + m.px(10), y, p.w - m.px(20), rh - m.px(6) };
				cv.fillRect(r, Colors::PanelHi, m.px(10));
				const Color lc = n.level == Notification::Alert ? Colors::Danger : n.level == Notification::Warn ? Colors::Warn : Colors::Accent;
				cv.fillRect({ r.x, r.y + m.px(8), m.px(4), r.h - m.px(16) }, lc, m.px(2));
				DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(6), r.w - m.px(28), m.px(26) }, n.title, Colors::Text, m.font(Fonts::Body));
				DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(32), r.w - m.px(28), m.px(26) }, n.text, Colors::TextDim, m.font(Fonts::Caption));
				m_notifRects.push_back(r);
				y += rh;
			}
		}

		void drawToast(Canvas& cv)
		{
			const Notification* n = m_notifs.toast(m_now);
			if (!n) return;
			const Metrics& m = m_m;
			const float w = std::min(m.px(380), m.content.w * 0.8f), h = m.px(66);
			const Rect r{ m.content.center().x - w * 0.5f, m.content.y + m.px(10), w, h };
			DrawPanel(cv, m, r, Colors::PanelHi, 14);
			DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(6), r.w - m.px(32), m.px(26) }, n->title, Colors::Text, m.font(Fonts::Body));
			DrawTextIn(cv, { r.x + m.px(16), r.y + m.px(32), r.w - m.px(32), m.px(26) }, n->text, Colors::TextDim, m.font(Fonts::Caption));
		}

		AppRegistry m_reg; AppManager m_apps; NotificationCenter m_notifs; HomeScreen m_home;
		InputManager m_input; GestureTracker m_gest; Transition m_trans; Metrics m_m;
		std::function<StatusInfo()> m_statusFn;
		std::vector<RawTouch> m_raw; std::vector<Touch> m_touches;
		bool m_open = false, m_notifOpen = false, m_recentsOpen = false, m_bellPressed = false;
		int m_navPressed = -1; Route m_route = Route::None; int64_t m_now = 0; Rect m_bell;
		std::vector<Rect> m_recentRects, m_recentClose, m_notifRects; std::vector<std::string> m_recentIds; Rect m_recentsClearAll, m_notifClear;
	};
}
