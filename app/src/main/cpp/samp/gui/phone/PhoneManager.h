#pragma once
// Celular do Maximo RP - PhoneManager: orquestra o aparelho vertical (tela inicial com paginas e dock, apps, recentes, notificacoes, animacoes).
// Logica pura (sem ImGui/jogo): recebe toques brutos (PhoneInput) e um Canvas; testada no PC. A integracao com o jogo esta em
// gui/samp_widgets/phone.cpp. Reusa do Maximo OS: Canvas, App/AppManager/AppRegistry, GestureTracker, NotificationCenter, Components.
#include "PhoneApp.h"
#include "PhoneInput.h"
#include "PhoneUI.h"
#include "../maximoos/Navigation.h"
#include "../maximoos/NotificationCenter.h"
#include <ctime>
#include <functional>
#include <map>

namespace Phone
{
	class PhoneManager
	{
	public:
		static constexpr int kCols = 4, kRows = 6, kDockN = 4;

		PhoneManager() : m_apps(m_reg) { m_ctx.openApp = [this](const std::string& id) { return openApp(id); }; }

		PhoneContext& context() { return m_ctx; }
		PhoneInput& input() { return m_input; }
		NotificationCenter& notifications() { return m_notifs; }
		const Metrics& metrics() const { return m_m; }
		bool isOpen() const { return m_open; }
		int  page() const { return m_page; }
		bool recentsOpen() const { return m_recents; }
		const std::string& foregroundId() { return m_apps.foregroundId(); }
		AppManager& apps() { return m_apps; }
		void setStatusProvider(std::function<StatusInfo()> f) { m_statusFn = std::move(f); }

		// ---------- instalacao dos apps (ordem define a posicao na grade) ----------
		bool addApp(const PhoneAppDef& d)
		{
			if (d.id.empty() || !d.factory) return false;
			AppInfo i; i.id = d.id; i.title = d.title; i.accent = d.color; i.page = d.page; i.dock = d.dock; i.factory = d.factory;
			if (!m_reg.add(i)) return false;
			m_defs[d.id] = d;
			return true;
		}
		const PhoneAppDef* def(const std::string& id) const { auto it = m_defs.find(id); return it == m_defs.end() ? nullptr : &it->second; }
		size_t appCount() const { return m_defs.size(); }
		const AppRegistry& registry() const { return m_reg; }

		void setScreenSize(float w, float h)
		{
			m_sw = w; m_sh = h;
			relayout();
		}

		// ---------- abrir / fechar ----------
		void open()
		{
			if (m_open) return;
			m_open = true; m_closing = false; m_input.setOpen(true);
			m_gest = GestureTracker(); m_recents = false; m_pageDrag = 0; m_barDrag = false;
			m_apps.resumeAll();
			relayout();
		}
		void close()
		{
			if (!m_open || m_closing) return;
			m_closing = true;                  // anima a saida; o fechamento real acontece em frame() quando slide chegar a 0
		}
		void toggle() { if (m_open && !m_closing) close(); else open(); }

		// Voltar do Android. true = consumido.
		bool back()
		{
			if (!m_open) return false;
			if (m_recents) { m_recents = false; return true; }
			const std::string before = m_apps.foregroundId();
			if (m_apps.back())
			{
				// so anima o fechamento se o app realmente fechou; Voltar tratado DENTRO do app (editor, conversa, foto) nao pode encolher o app ate o icone
				if (m_apps.foregroundId() != before) { m_animId = before; startAppAnim(-1, iconRectOf(before)); }
				return true;
			}
			close();
			return true;
		}
		bool openApp(const std::string& id)
		{
			const PhoneAppDef* d = def(id); if (!d) return false;
			m_recents = false;
			Rect from = iconRectOf(id);
			if (!m_apps.open(id, m_m)) return false;
			startAppAnim(1, from);
			return true;
		}
		void goHome() { m_recents = false; if (!m_apps.foregroundId().empty()) { m_animId = m_apps.foregroundId(); m_apps.goHome(); startAppAnim(-1, iconRectOf(m_animId)); } }

		// ---------- quadro ----------
		void frame(Canvas& cv, int64_t nowMs, float dtMs)
		{
			if (!m_open) return;
			{ const std::string& fgId = m_apps.foregroundId(); if (fgId == "mensagens" || fgId == "telefone") m_notifs.markAppRead(fgId); }   // abriu o app: o selo (bolinha) some
			m_now = nowMs; m_ctx.nowMs = nowMs;
			// animacao de entrada/saida do aparelho
			const float step = dtMs / 240.0f;
			m_slide = m_closing ? std::max(0.0f, m_slide - step) : std::min(1.0f, m_slide + step);
			if (m_closing && m_slide <= 0.0f) { m_open = false; m_closing = false; m_apps.pauseAll(); m_input.setOpen(false); m_input.setCaptureRect(0, 0, 0, 0); return; }
			relayout();

			std::vector<RawTouch> raw; m_input.drain(raw);
			for (const RawTouch& r : raw)
			{
				m_touches.clear();
				m_gest.feed(r.type, r.num, r.x, r.y, r.cancel, nowMs, m_touches);
				for (const Touch& t : m_touches) dispatch(t);
			}
			m_touches.clear(); m_gest.tick(nowMs, m_touches);
			for (const Touch& t : m_touches) dispatch(t);
			if (!m_open) return;

			// atualiza
			if (m_anim.t < 1.0f) m_anim.t = std::min(1.0f, m_anim.t + dtMs / kAnimMs);
			updatePages(dtMs);
			if (App* a = m_apps.foreground()) { a->update(dtMs); if (a->consumeFinish()) goHome(); }
			// notificacao nova -> banner
			draw(cv);
		}

		// ---------- consultas para testes ----------
		Rect dockSlotRect(int i) const { return m_dockSlot[i]; }
		Rect gridCellRect(int idx) const { return gridCell(idx, 0.0f); }
		std::vector<const AppInfo*> pageApps(int page) const
		{
			std::vector<const AppInfo*> v;
			for (auto& a : m_reg.list()) if (a.page == page) v.push_back(&a);
			return v;
		}
		int pageCount() const { int mx = 0; for (auto& a : m_reg.list()) mx = std::max(mx, a.page); return mx + 1; }
		void dispatchForTest(const Touch& t) { dispatch(t); }

	private:
		static constexpr float kAnimMs = 230.0f;

		struct AppAnim { float t = 1.0f; int dir = 0; Rect from; };

		void relayout()
		{
			if (!(m_sw > 50.0f) || !(m_sh > 50.0f)) return;
			const float eased = 1.0f - (1.0f - m_slide) * (1.0f - m_slide);
			m_m = ComputePhoneMetrics(m_sw, m_sh, m_ctx.settings.sizePct / 100.0f, eased);
			m_gest.slop = std::max(8.0f, m_m.px(8));
			if (!m_m.valid) return;
			m_apps.resize(m_m);
			// dock
			const float dw = m_m.px(64), gap = (m_m.screen.w - m_m.px(40) - dw * kDockN) / (kDockN - 1);
			const float dy = m_m.navBar.y - m_m.px(96) + m_m.px(18);
			for (int i = 0; i < kDockN; ++i) m_dockSlot[i] = { m_m.screen.x + m_m.px(20) + i * (dw + gap), dy, dw, dw };
			m_dockPanel = { m_m.screen.x + m_m.px(10), m_m.navBar.y - m_m.px(96) + m_m.px(2), m_m.screen.w - m_m.px(20), m_m.px(92) };
			// area de captura = moldura inteira (+ folga)
			const float pad = m_m.px(4);
			m_input.setCaptureRect(m_m.frame.x - pad, m_m.frame.y - pad, m_m.frame.w + 2 * pad, m_m.frame.h + 2 * pad);
		}

		// celula da grade: idx 0..23 numa pagina; xOff = deslocamento horizontal da pagina (px)
		Rect gridCell(int idx, float xOff) const
		{
			const float top = m_m.screen.y + m_m.px(66);
			const float cw = m_m.screen.w / kCols, ch = m_m.px(98);
			const int c = idx % kCols, r = idx / kCols;
			return { m_m.screen.x + c * cw + xOff, top + r * ch, cw, ch };
		}
		Rect iconRectOf(const std::string& id) const
		{
			const AppInfo* ai = m_reg.find(id);
			if (!ai) return Rect();
			if (ai->dock >= 0 && ai->dock < kDockN) return m_dockSlot[ai->dock];
			int idx = 0;
			for (auto& a : m_reg.list()) { if (a.page != ai->page) continue; if (a.id == id) break; ++idx; }
			return AppIconSquare(m_m, gridCell(idx, 0.0f), true);
		}
		void startAppAnim(int dir, const Rect& from) { m_anim.t = 0.0f; m_anim.dir = dir; if (!from.empty()) m_anim.from = from; }

		// ---------------- toque ----------------
		void dispatch(const Touch& t)
		{
			if (!m_m.valid) return;
			// toast: toque no banner
			if (t.kind == Touch::Tap)
			{
				if (const Notification* n = m_notifs.toast(m_now))
				{
					if (bannerRect().contains(t.pos)) { const std::string app = n->appId; const int id = n->id; m_notifs.markRead(id); m_notifs.dismissToast(); if (!app.empty()) openApp(app); return; }
				}
			}
			if (m_recents) { dispatchRecents(t); return; }

			// gesto da barra inferior (indicador de inicio): arrastar para cima
			const Rect bar{ m_m.screen.x, m_m.navBar.y - m_m.px(10), m_m.screen.w, m_m.navBar.h + m_m.px(10) };
			if (t.kind == Touch::Down) { m_barDrag = bar.contains(t.pos); m_barDy = 0; }
			if (m_barDrag)
			{
				if (t.kind == Touch::Drag) m_barDy += t.delta.y;
				else if (t.kind == Touch::DragEnd)
				{
					const float up = -m_barDy, vy = -t.velocity.y;
					m_barDrag = false;
					if (up > m_m.px(30) || vy > 500.0f)
					{
						const bool fast = vy > 900.0f || up > m_m.px(260);
						if (!m_apps.foregroundId().empty() && fast) goHome();
						else if (up > m_m.px(60)) { m_recents = true; m_recentSel = 0; m_recentScroll = 0; }
					}
					return;
				}
				else if (t.kind == Touch::Up || t.kind == Touch::Cancel) { m_barDrag = false; }
				if (t.kind == Touch::Down || t.kind == Touch::Move || t.kind == Touch::Drag || t.kind == Touch::DragBegin) return;
			}

			if (!m_apps.foregroundId().empty())
			{
				if (App* a = m_apps.foreground()) a->onTouch(t, m_m, m_m.content);
				return;
			}
			dispatchHome(t);
		}

		void dispatchHome(const Touch& t)
		{
			const int pages = pageCount();
			switch (t.kind)
			{
			case Touch::Down: m_pressedKey = hitKey(t.pos); m_pageDrag = 0; break;
			case Touch::Move: if (!m_pressedKey.empty() && hitKey(t.pos) != m_pressedKey) m_pressedKey.clear(); break;
			case Touch::DragBegin: m_pressedKey.clear(); break;
			case Touch::Drag: m_pageDrag += t.delta.x; break;
			case Touch::DragEnd:
			{
				const float thr = m_m.screen.w * 0.22f;
				int np = m_page;
				if (m_pageDrag < -thr || t.velocity.x < -700.0f) np = m_page + 1;
				else if (m_pageDrag > thr || t.velocity.x > 700.0f) np = m_page - 1;
				m_page = std::clamp(np, 0, pages - 1);
				m_pageDrag = 0; m_pressedKey.clear();
				break;
			}
			case Touch::Up: case Touch::Cancel: m_pressedKey.clear(); break;
			case Touch::Tap:
			{
				const std::string k = hitKey(t.pos);
				m_pressedKey.clear();
				if (!k.empty() && m_pressedKey.empty()) openApp(k);
				break;
			}
			default: break;
			}
		}
		std::string hitKey(Vec2 p) const
		{
			for (int i = 0; i < kDockN; ++i) if (m_dockSlot[i].inset(-m_m.px(6)).contains(p)) { for (auto& a : m_reg.list()) if (a.dock == i) return a.id; }
			int idx = 0;
			for (auto& a : m_reg.list())
			{
				if (a.page != m_page) continue;
				if (AppIconSquare(m_m, gridCell(idx, m_pageDrag), true).inset(-m_m.px(6)).contains(p)) return a.id;
				++idx;
			}
			return std::string();
		}

		// ---------------- recentes ----------------
		Rect recentCard(int i) const
		{
			const float cw = m_m.screen.w * 0.62f, ch = m_m.screen.h * 0.62f, gap = m_m.px(18);
			const float x0 = m_m.screen.center().x - cw * 0.5f;
			return { x0 + i * (cw + gap) - m_recentScroll, m_m.screen.y + m_m.screen.h * 0.17f + m_cardDy[std::min(i, 5)], cw, ch };
		}
		Rect closeAllRect() const { return { m_m.screen.center().x - m_m.px(70), m_m.navBar.y - m_m.px(58), m_m.px(140), m_m.px(40) }; }
		void dispatchRecents(const Touch& t)
		{
			const std::vector<std::string> list = m_apps.recents();
			const float cw = m_m.screen.w * 0.62f, pitch = cw + m_m.px(18);
			switch (t.kind)
			{
			case Touch::Down: m_cardDrag = -1; m_dragMode = 0; break;
			case Touch::DragBegin:
				for (size_t i = 0; i < list.size() && i < 6; ++i) if (recentCard((int)i).contains(t.start)) m_cardDrag = (int)i;
				m_dragMode = 0;
				break;
			case Touch::Drag:
				if (m_dragMode == 0) m_dragMode = (std::fabs(t.pos.y - t.start.y) > std::fabs(t.pos.x - t.start.x) && m_cardDrag >= 0) ? 2 : 1;
				if (m_dragMode == 1) m_recentScroll = std::clamp(m_recentScroll - t.delta.x, 0.0f, std::max(0.0f, (list.size() - 1) * pitch));
				else if (m_dragMode == 2 && m_cardDrag >= 0) m_cardDy[std::min(m_cardDrag, 5)] += t.delta.y;
				break;
			case Touch::DragEnd:
				if (m_dragMode == 2 && m_cardDrag >= 0)
				{
					const bool kill = t.velocity.y < -700.0f || m_cardDy[std::min(m_cardDrag, 5)] < -m_m.px(120);
					if (kill && m_cardDrag < (int)list.size()) m_apps.close(list[m_cardDrag]);
				}
				else if (m_dragMode == 1)
				{
					const int n = std::max(1, (int)list.size());
					const int tgt = std::clamp((int)std::lround(m_recentScroll / pitch), 0, n - 1);
					m_recentScroll = tgt * pitch;
				}
				for (auto& d : m_cardDy) d = 0;
				m_cardDrag = -1; m_dragMode = 0;
				if (m_apps.recents().empty()) m_recents = false;
				break;
			case Touch::Tap:
				if (!list.empty() && closeAllRect().contains(t.pos)) { m_apps.closeAll(); m_recents = false; break; }
				for (size_t i = 0; i < list.size() && i < 6; ++i)
					if (recentCard((int)i).contains(t.pos)) { m_recents = false; openApp(list[i]); return; }
				m_recents = false;
				break;
			default: break;
			}
		}

		void updatePages(float) {}

		Rect bannerRect() const { return { m_m.screen.x + m_m.px(10), m_m.screen.y + m_m.px(46), m_m.screen.w - m_m.px(20), m_m.px(64) }; }

		// ---------------- desenho ----------------
		void draw(Canvas& cv)
		{
			const Metrics& m = m_m;
			if (!m.valid) return;
			// corpo
			cv.fillRect(m.frame.moved(0, m.px(3)), Color(0, 0, 0, 70), m.frameRadius);
			cv.fillRect(m.frame, Colors::Bezel, m.frameRadius);
			cv.strokeRect(m.frame, Colors::BezelEdge, m.frameRadius, std::max(1.5f, m.px(1.6f)));
			cv.pushClip(m.screen);
			const std::string fg = m_apps.foregroundId();
			const PhoneAppDef* fd = fg.empty() ? nullptr : def(fg);
			const bool animating = m_anim.t < 1.0f;
			// app "seeThrough" (Camera) totalmente aberto: a tela mostra o jogo, sem papel de parede
			const bool clearScreen = fd && fd->seeThrough && !animating && !m_recents;
			if (!clearScreen)
			{
				cv.fillGradientV({ m.screen.x, m.screen.y, m.screen.w, m.screen.h * 0.45f }, PC::Wall0, PC::Wall1);
				cv.fillGradientV({ m.screen.x, m.screen.y + m.screen.h * 0.45f, m.screen.w, m.screen.h * 0.55f + 1 }, PC::Wall1, PC::Wall2);
			}
			const bool appVisible = fd && !(animating && m_anim.dir > 0 && m_anim.t < 0.55f);
			const bool homeVisible = fg.empty() || animating;
			if (homeVisible && !m_recents) drawHome(cv);
			if (m_recents) drawRecents(cv);
			if (!m_recents && fd && appVisible)
			{
				const float e = animating ? Transition::ease(m_anim.t) : 1.0f;
				const float k = (m_anim.dir > 0) ? e : (animating ? 1.0f - e : 1.0f);
				drawApp(cv, *fd, animating ? k : 1.0f);
			}
			else if (!m_recents && animating && m_anim.dir < 0 && !m_animId.empty())
			{
				drawAppGhost(cv, m_animId, 1.0f - Transition::ease(m_anim.t));
			}
			drawStatus(cv, fd && appVisible && fd->light && !m_recents);
			drawBanner(cv);
			// indicador de inicio
			{
				const bool lightBg = fd && fd->light && appVisible && !m_recents;
				const float bw = m.px(134), bh = std::max(3.0f, m.px(5));
				cv.fillRect({ m.screen.center().x - bw * 0.5f, m.navBar.y + m.px(14) + m_barDy * 0.0f, bw, bh }, lightBg ? Color(0, 0, 0, 220) : Color(255, 255, 255, 230), bh * 0.5f);
			}
			cv.popClip();
			// mascara dos cantos arredondados da tela (o clip do ImGui e retangular): so a regiao entre o canto do quadrado e o arco,
			// leque de triangulos por canto. NAO cobre as bordas retas (antes um aro grosso comia ~15 lu de conteudo de cada lado).
			{
				const Rect s = m.screen; const float R = std::min(m.screenRadius, std::min(s.w, s.h) * 0.5f);
				const Vec2 cp[4] = { { s.x, s.y }, { s.right(), s.y }, { s.right(), s.bottom() }, { s.x, s.bottom() } };
				const Vec2 ct[4] = { { s.x + R, s.y + R }, { s.right() - R, s.y + R }, { s.right() - R, s.bottom() - R }, { s.x + R, s.bottom() - R } };
				const float a0[4] = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };
				const int N = 12;
				for (int c = 0; c < 4; ++c)
				{
					Vec2 prev{ ct[c].x + std::cos(a0[c]) * R, ct[c].y + std::sin(a0[c]) * R };
					for (int i = 1; i <= N; ++i)
					{
						const float an = a0[c] + 1.57079633f * i / N;
						const Vec2 cur{ ct[c].x + std::cos(an) * R, ct[c].y + std::sin(an) * R };
						cv.fillTriangle(cp[c], prev, cur, Colors::Bezel);
						prev = cur;
					}
				}
			}
			cv.strokeRect(m.frame, Colors::BezelEdge, m.frameRadius, std::max(1.5f, m.px(1.6f)));
			// ilha dinamica
			const float iw = m.px(112), ih = m.px(32);
			cv.fillRect({ m.screen.center().x - iw * 0.5f, m.screen.y + m.px(11), iw, ih }, Color(0, 0, 0), ih * 0.5f);
		}

		void drawStatus(Canvas& cv, bool dark)
		{
			const Metrics& m = m_m;
			StatusInfo st; if (m_statusFn) st = m_statusFn();
			if (m_ctx.settings.airplane) st.signal = 0;
			const Color fg = dark ? Color(0, 0, 0) : Color(255, 255, 255);
			const float fs = m.font(15), y0 = m.statusBar.y + m.px(11), hh = m.px(32);
			DrawTextIn(cv, { m.screen.x + m.px(32), y0, m.px(70), hh }, st.clock, fg, fs);
			float x = m.screen.right() - m.px(26);
			if (st.battery >= 0)
			{
				const float bw = m.px(26), bh = m.px(12.5f);
				const Rect b{ x - bw, y0 + (hh - bh) * 0.5f, bw, bh };
				cv.strokeRect(b, fg.alpha(0.55f), m.px(3.5f), std::max(1.0f, m.px(1)));
				cv.fillRect({ b.x + m.px(1.8f), b.y + m.px(1.8f), (b.w - m.px(3.6f)) * std::clamp(st.battery / 100.0f, 0.0f, 1.0f), b.h - m.px(3.6f) }, st.battery <= 15 ? PC::Red : fg, m.px(2));
				x = b.x - m.px(8);
			}
			if (!m_ctx.settings.airplane && m_ctx.settings.wifi) { cv.fillCircle({ x - m.px(8), y0 + hh * 0.5f + m.px(3) }, m.px(2), fg); cv.strokeCircle({ x - m.px(8), y0 + hh * 0.5f + m.px(3) }, m.px(6), fg.alpha(0.8f), std::max(1.0f, m.px(1.4f))); x -= m.px(26); }
			const float bw = m.px(3.6f), gap = m.px(1.8f), hmax = m.px(12);
			x -= 4 * bw + 3 * gap;
			for (int i = 0; i < 4; ++i) { const float h = hmax * (0.35f + 0.65f * i / 3.0f); cv.fillRect({ x + i * (bw + gap), y0 + (hh + hmax) * 0.5f - h, bw, h }, i < st.signal ? fg : fg.alpha(0.3f), m.px(0.8f)); }
		}

		void drawHome(Canvas& cv)
		{
			const Metrics& m = m_m;
			const int pages = pageCount();
			const float xOffBase = -m_page * m.screen.w + m_pageDrag;
			for (int pg = std::max(0, m_page - 1); pg <= std::min(pages - 1, m_page + 1); ++pg)
			{
				int idx = 0;
				for (auto& a : m_reg.list())
				{
					if (a.page != pg) continue;
					const Rect cell = gridCell(idx++, xOffBase + pg * m.screen.w);
					if (cell.right() < m.screen.x || cell.x > m.screen.right()) continue;
					AppIconStyle st; st.badge = m_notifs.unreadFor(a.id);
					const PhoneAppDef* d = def(a.id);
					drawIcon(cv, cell, a.title, d, m_pressedKey == a.id, true, st.badge);
				}
			}
			// pontos de pagina
			const float dy = m_dockPanel.y - m.px(18), dr = m.px(3.2f), dg = m.px(12);
			const float dx0 = m.screen.center().x - (pages - 1) * dg * 0.5f;
			for (int i = 0; i < pages; ++i) cv.fillCircle({ dx0 + i * dg, dy }, dr, Color(255, 255, 255, i == m_page ? 240 : 100));
			// dock
			cv.fillRect(m_dockPanel, Color(255, 255, 255, 46), m.px(30));
			for (int i = 0; i < kDockN; ++i)
				for (auto& a : m_reg.list()) if (a.dock == i)
				{
					const Rect cell{ m_dockSlot[i].x - m.px(8), m_dockSlot[i].y - m.px(4), m_dockSlot[i].w + m.px(16), m_dockSlot[i].h + m.px(8) };
					drawIcon(cv, cell, a.title, def(a.id), m_pressedKey == a.id, false, m_notifs.unreadFor(a.id));
				}
		}
		// icone estilo iOS: quadrado arredondado com degrade e glifo
		void drawIcon(Canvas& cv, const Rect& cell, const std::string& title, const PhoneAppDef* d, bool pressed, bool label, int badge)
		{
			const Metrics& m = m_m;
			Rect q = AppIconSquare(m, cell, label);
			if (!label) q = { cell.x + m.px(8), cell.y + m.px(4), cell.w - m.px(16), cell.w - m.px(16) };
			const float k = pressed ? 0.92f : 1.0f; const Vec2 c = q.center();
			q = { c.x - q.w * 0.5f * k, c.y - q.h * 0.5f * k, q.w * k, q.h * k };
			const Color col = d ? d->color : Colors::Accent;
			const float rad = q.w * 0.23f;
			cv.fillRect(q.moved(0, q.h * 0.03f), Color(0, 0, 0, 60), rad);
			cv.fillRect(q, Color::mix(col, Color(0, 0, 0), 0.30f), rad);
			cv.fillRect({ q.x, q.y, q.w, q.h * 0.62f }, col, rad);
			cv.fillRect({ q.x, q.y + q.h * 0.30f, q.w, q.h * 0.32f }, col, 0);
			DrawGlyph(cv, d ? d->id : std::string(), q.inset(q.w * 0.22f), Color(255, 255, 255));
			if (badge > 0)
			{
				const float br = q.w * 0.19f; const Vec2 bc{ q.right() - br * 0.3f, q.y + br * 0.3f };
				cv.fillCircle(bc, br, PC::Red);
				DrawTextIn(cv, { bc.x - br, bc.y - br, br * 2, br * 2 }, badge > 9 ? "9+" : std::to_string(badge), Color(255, 255, 255), std::max(Metrics::kMinFontPx - 1.0f, br * 1.2f), Align::Center);
			}
			if (label) DrawTextIn(cv, { cell.x, cell.bottom() - m.px(30), cell.w, m.px(20) }, title, Color(255, 255, 255), m.font(11.5f), Align::Center);
		}

		void drawApp(Canvas& cv, const PhoneAppDef& d, float k)
		{
			const Metrics& m = m_m;
			App* a = m_apps.foreground(); if (!a) return;
			if (k < 0.999f)
			{
				// expandindo do icone: retangulo interpolado com a cor do app, e o conteudo aparece no fim
				const Rect from = m_anim.from.empty() ? Rect{ m.screen.center().x - m.px(30), m.screen.center().y - m.px(30), m.px(60), m.px(60) } : m_anim.from;
				const Rect to = m.screen;
				auto L = [k](float a2, float b) { return a2 + (b - a2) * k; };
				const Rect r{ L(from.x, to.x), L(from.y, to.y), L(from.w, to.w), L(from.h, to.h) };
				cv.fillRect(r, d.light ? PC::Light : PC::Dark, L(from.w * 0.23f, m.screenRadius));
				DrawGlyph(cv, d.id, Rect{ r.center().x - m.px(24), r.center().y - m.px(24), m.px(48), m.px(48) }, d.light ? d.color : Color(255, 255, 255, 230));
				return;
			}
			if (!d.seeThrough) cv.fillRect(m.screen, d.light ? PC::Light : PC::Dark, 0);
			cv.pushClip(m.content);
			a->draw(cv, m, m.content);
			cv.popClip();
		}
		void drawAppGhost(Canvas& cv, const std::string& id, float k)
		{
			const PhoneAppDef* d = def(id); if (!d || k <= 0.01f) return;
			const Metrics& m = m_m;
			const Rect from = m_anim.from.empty() ? Rect{ m.screen.center().x - m.px(30), m.screen.center().y - m.px(30), m.px(60), m.px(60) } : m_anim.from;
			auto L = [k](float a2, float b) { return b + (a2 - b) * k; };       // k=1 tela cheia -> k=0 icone
			const Rect r{ L(m.screen.x, from.x), L(m.screen.y, from.y), L(m.screen.w, from.w), L(m.screen.h, from.h) };
			cv.fillRect(r, d->light ? PC::Light : PC::Dark, L(m.screenRadius, from.w * 0.23f));
		}

		void drawRecents(Canvas& cv)
		{
			const Metrics& m = m_m;
			cv.fillRect(m.screen, Color(0, 0, 0, 120), 0);
			const std::vector<std::string> list = m_apps.recents();
			DrawTextIn(cv, { m.screen.x, m.screen.y + m.px(64), m.screen.w, m.px(30) }, "Aplicativos abertos", Color(255, 255, 255), m.font(19), Align::Center);
			if (list.empty())
			{
				DrawTextIn(cv, { m.screen.x, m.screen.center().y - m.px(15), m.screen.w, m.px(30) }, "Nenhum aplicativo aberto", Color(255, 255, 255, 200), m.font(16), Align::Center);
				return;
			}
			for (size_t i = 0; i < list.size() && i < 6; ++i)
			{
				const Rect c = recentCard((int)i);
				if (c.right() < m.screen.x || c.x > m.screen.right()) continue;
				const PhoneAppDef* d = def(list[i]);
				cv.fillRect(c, d && d->light ? PC::Light : PC::Dark2, m.px(22));
				cv.strokeRect(c, Color(255, 255, 255, 60), m.px(22), std::max(1.0f, m.px(1)));
				if (d)
				{
					DrawGlyph(cv, d->id, Rect{ c.center().x - m.px(34), c.center().y - m.px(44), m.px(68), m.px(68) }, d->color);
					DrawTextIn(cv, { c.x, c.center().y + m.px(34), c.w, m.px(28) }, d->title, d->light ? PC::LabelL : Colors::Text, m.font(17), Align::Center);
				}
			}
			const Rect b = closeAllRect();
			cv.fillRect(b, Color(255, 255, 255, 70), m.px(20));
			DrawTextIn(cv, b, "Fechar todos", Color(255, 255, 255), m.font(15), Align::Center);
		}

		void drawBanner(Canvas& cv)
		{
			const Notification* n = m_notifs.toast(m_now);
			if (!n) return;
			const Metrics& m = m_m;
			const float age = (float)(m_now - n->time);
			const float slide = std::clamp(age / 220.0f, 0.0f, 1.0f);
			Rect b = bannerRect(); b.y -= (1.0f - slide) * m.px(120);
			cv.fillRect(b.moved(0, m.px(2)), Color(0, 0, 0, 60), m.px(22));
			cv.fillRect(b, Color(250, 250, 252, 245), m.px(22));
			const PhoneAppDef* d = n->appId.empty() ? nullptr : def(n->appId);
			const float is = m.px(34);
			cv.fillRect({ b.x + m.px(12), b.y + (b.h - is) * 0.5f, is, is }, d ? d->color : PC::Blue, m.px(8));
			if (d) DrawGlyph(cv, d->id, Rect{ b.x + m.px(12), b.y + (b.h - is) * 0.5f, is, is }.inset(is * 0.2f), Color(255, 255, 255));
			DrawTextIn(cv, { b.x + m.px(56), b.y + m.px(8), b.w - m.px(68), m.px(22) }, n->title, PC::LabelL, m.font(14));
			DrawTextIn(cv, { b.x + m.px(56), b.y + m.px(30), b.w - m.px(68), m.px(24) }, n->text, PC::DimL, m.font(13));
		}

		PhoneContext m_ctx;
		AppRegistry m_reg;
		AppManager m_apps;
		std::map<std::string, PhoneAppDef> m_defs;
		NotificationCenter m_notifs;
		PhoneInput m_input;
		GestureTracker m_gest;
		std::function<StatusInfo()> m_statusFn;
		Metrics m_m;
		Rect m_dockSlot[kDockN], m_dockPanel;
		std::vector<Touch> m_touches;
		AppAnim m_anim; std::string m_animId;
		bool m_open = false, m_closing = false, m_recents = false, m_barDrag = false;
		float m_slide = 0.0f, m_sw = 0, m_sh = 0, m_pageDrag = 0, m_barDy = 0, m_recentScroll = 0;
		float m_cardDy[6] = { 0, 0, 0, 0, 0, 0 };
		int m_page = 0, m_recentSel = 0, m_cardDrag = -1, m_dragMode = 0;
		std::string m_pressedKey;
		int64_t m_now = 0;
	};
}
