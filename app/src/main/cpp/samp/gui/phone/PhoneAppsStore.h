#pragma once
// Celular - LOJA (baixar/remover apps) e seletor de PAPEL DE PAREDE (usado pelos Ajustes). Tudo vem do servidor (PhoneStore.h):
// sem servico ou sem resposta o app avisa de forma honesta; nada e simulado.
#include "PhoneAppsGram.h"
#include "PhoneStore.h"

namespace Phone
{
	// icone quadrado arredondado estilo iOS (igual ao da tela inicial, so que menor)
	inline void DrawAppIconTile(Canvas& cv, const Metrics& m, const Rect& q, const std::string& id, Color col)
	{
		const float rad = q.w * 0.23f;
		cv.fillRect(q, Color::mix(col, Color(0, 0, 0), 0.30f), rad);
		cv.fillRect({ q.x, q.y, q.w, q.h * 0.62f }, col, rad);
		cv.fillRect({ q.x, q.y + q.h * 0.30f, q.w, q.h * 0.32f }, col, 0);
		DrawGlyph(cv, id, q.inset(q.w * 0.22f), Color(255, 255, 255));
		(void)m;
	}

	// ------------------------------------------------------------------------------------------------ Loja
	class StoreApp : public App
	{
	public:
		enum { ID_BACK = 1, ID_REFRESH = 2, ID_GET = 1000, ID_OPEN = 2000, ID_DEL = 3000 };
		explicit StoreApp(PhoneContext* c) : m_c(c) {}
		void onResume() override { m_press = 0; if (m_c->store) m_c->store->refresh(m_c->nowMs, true); }
		void onPause() override { m_press = 0; }
		void update(float dt) override { m_scroll.update(dt); }
		bool onBack() override { return false; }
		Rect hitRect(int id) const { return m_hits.find(id); }

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			m_hits.clear();
			NavResult nv = DrawNavBar(cv, m, content, "Loja", "", "Atualizar", true, false, m_press == ID_REFRESH);
			m_hits.add(ID_REFRESH, nv.action);
			const Rect body = nv.body; m_clip = body;
			Store::Service* st = m_c->store;
			if (st) { const std::string t = st->takeToast(); if (!t.empty()) m_msg.set(t, m_c->nowMs); }
			if (!st) return say(cv, m, body, "Loja indisponível nesta versão do jogo.");
			if (!m_c->networked && !st->meLoaded) return say(cv, m, body, "Sem conexão com o servidor.");
			if (!st->meLoaded) return say(cv, m, body, "Conectando à Loja...");
			if (!st->hasDevice) return say(cv, m, body, "Você ainda não tem um celular.");
			if (!st->catalogLoaded) return say(cv, m, body, "Carregando aplicativos...");

			std::vector<const Store::AppEntry*> avail, have;
			for (auto& a : st->apps)
			{
				if (a.core) continue;                                  // apps de sistema nao entram na Loja
				if (a.installed) have.push_back(&a); else avail.push_back(&a);
			}
			const float mx = m.px(14), cw = body.w - 2 * mx, rowH = m.px(68), headH = m.px(34);
			float total = m.px(8);
			total += headH + (avail.empty() ? m.px(52) : avail.size() * rowH);
			if (!have.empty()) total += headH + have.size() * rowH;
			total += m.px(24);
			m_scroll.setLayout(body, total);
			cv.pushClip(body);
			float y = m.px(8);
			auto header = [&](const char* t)
			{
				const Rect r = m_scroll.itemRect(y, headH);
				DrawTextIn(cv, { body.x + mx + m.px(4), r.y, cw, r.h }, t, PC::DimL, m.font(12));
				y += headH;
			};
			auto row = [&](const Store::AppEntry& a, bool installed, size_t idx)
			{
				const Rect r = m_scroll.itemRect(y, rowH); y += rowH;
				if (!m_scroll.visible(r)) return;
				const Rect rr{ body.x + mx, r.y, cw, rowH - m.px(6) };
				cv.fillRect(rr, PC::White, m.px(12));
				const float is = m.px(46);
				DrawAppIconTile(cv, m, { rr.x + m.px(10), rr.y + (rr.h - is) * 0.5f, is, is }, a.id, Color((a.color >> 16) & 255, (a.color >> 8) & 255, a.color & 255));
				const bool known = !m_c->appExists || m_c->appExists(a.id);
				const float bx = rr.x + m.px(10) + is + m.px(10);
				const float bw = installed ? m.px(128) : m.px(74);
				DrawTextIn(cv, { bx, rr.y + m.px(8), rr.right() - bx - bw - m.px(12), m.px(22) }, a.title, PC::LabelL, m.font(15));
				DrawTextIn(cv, { bx, rr.y + m.px(30), rr.right() - bx - bw - m.px(12), m.px(20) }, known ? a.descr : "Requer atualizar o jogo", PC::Gray, m.font(12));
				const bool busy = st->busy(a.id);
				const int pid = (int)idx;
				if (installed)
				{
					const Rect rem{ rr.right() - m.px(10) - m.px(58), rr.y + (rr.h - m.px(30)) * 0.5f, m.px(58), m.px(30) };
					const Rect open{ rem.x - m.px(6) - m.px(60), rem.y, m.px(60), m.px(30) };
					DrawFilledButton(cv, m, open, "Abrir", PC::Blue, Color(255, 255, 255), m_press == ID_OPEN + pid, known && !busy);
					DrawFilledButton(cv, m, rem, busy ? "..." : "Tirar", Color(229, 229, 234), PC::Red, m_press == ID_DEL + pid, !busy);
					if (known && !busy) m_hits.add(ID_OPEN + pid, open);
					if (!busy) m_hits.add(ID_DEL + pid, rem);
				}
				else
				{
					const Rect get{ rr.right() - m.px(10) - m.px(66), rr.y + (rr.h - m.px(30)) * 0.5f, m.px(66), m.px(30) };
					DrawFilledButton(cv, m, get, busy ? "..." : "Obter", PC::Blue, Color(255, 255, 255), m_press == ID_GET + pid, known && !busy);
					if (known && !busy) m_hits.add(ID_GET + pid, get);
				}
			};
			header("DISPONÍVEIS");
			if (avail.empty())
			{
				const Rect r = m_scroll.itemRect(y, m.px(52));
				DrawTextIn(cv, { body.x + mx, r.y, cw, r.h }, "Nenhum app novo por enquanto.", PC::Gray, m.font(14), Align::Center);
				y += m.px(52);
			}
			m_list.clear(); for (auto* a : avail) m_list.push_back(a->id);
			const size_t nAvail = avail.size();
			for (size_t i = 0; i < avail.size(); ++i) row(*avail[i], false, i);
			if (!have.empty())
			{
				header("INSTALADOS");
				for (size_t i = 0; i < have.size(); ++i) { m_list.push_back(have[i]->id); row(*have[i], true, nAvail + i); }
			}
			cv.popClip();
			m_msg.draw(cv, m, body, m_c->nowMs);
		}

		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			m_scroll.onTouch(t);
			if (t.kind == Touch::Down) m_press = m_hits.at(t.pos, &m_clip);
			else if (t.kind == Touch::Move) { if (m_press && m_hits.at(t.pos, &m_clip) != m_press) m_press = 0; }
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_press = 0;
			if (t.kind != Touch::Tap) return true;
			const int id = m_hits.at(t.pos, &m_clip);
			Store::Service* st = m_c->store; if (!st) return true;
			if (id == ID_REFRESH) { st->refresh(m_c->nowMs, true); m_msg.set("Atualizando...", m_c->nowMs, false, 1200); }
			else if (id >= ID_DEL && id - ID_DEL < (int)m_list.size()) st->uninstall(m_list[id - ID_DEL], m_c->nowMs);
			else if (id >= ID_OPEN && id - ID_OPEN < (int)m_list.size()) { if (m_c->openApp) m_c->openApp(m_list[id - ID_OPEN]); }
			else if (id >= ID_GET && id - ID_GET < (int)m_list.size()) st->install(m_list[id - ID_GET], m_c->nowMs);
			return true;
		}
	private:
		void say(Canvas& cv, const Metrics& m, const Rect& body, const std::string& t)
		{
			DrawTextIn(cv, { body.x + m.px(20), body.center().y - m.px(14), body.w - m.px(40), m.px(28) }, t, PC::Gray, m.font(16), Align::Center);
			m_msg.draw(cv, m, body, m_c->nowMs);
		}
		PhoneContext* m_c; HitList m_hits; Rect m_clip; ScrollView m_scroll; InlineMsg m_msg; int m_press = 0; std::vector<std::string> m_list;
	};

	// ------------------------------------------------------------------------------------------------ Seletor de papel de parede (tela dos Ajustes)
	class WallpaperPicker
	{
	public:
		enum { ID_BACK = 1, ID_PRESET = 1000, ID_PHOTO = 2000 };
		explicit WallpaperPicker(PhoneContext* c) : m_c(c) {}
		void open() { m_scroll = ScrollView(); m_press = 0; m_names.clear(); if (m_c->photos) m_names = m_c->photos->list(); if (m_names.size() > 30) m_names.resize(30); if (m_c->store) m_c->store->refresh(m_c->nowMs); }
		void update(float dt) { m_scroll.update(dt); }

		void draw(Canvas& cv, const Metrics& m, const Rect& content)
		{
			cv.fillRect(content, PC::Light, 0);
			m_hits.clear();
			NavResult nv = DrawNavBar(cv, m, content, "Papel de parede", "Ajustes", "", true, m_press == ID_BACK, false);
			m_hits.add(ID_BACK, nv.back);
			const Rect body = nv.body; m_clip = body;
			Store::Service* st = m_c->store;
			if (st) { const std::string t = st->takeToast(); if (!t.empty()) m_msg.set(t, m_c->nowMs, t.find("atualizado") == std::string::npos); }
			if (!st || !st->hasDevice)
			{
				DrawTextIn(cv, { body.x + m.px(20), body.center().y - m.px(14), body.w - m.px(40), m.px(28) }, st && st->meLoaded ? "Você ainda não tem um celular." : "Conectando...", PC::Gray, m.font(16), Align::Center);
				return;
			}
			const float mx = m.px(14), gap = m.px(10), cw = body.w - 2 * mx;
			const int cols = 3; const float tw = (cw - gap * (cols - 1)) / cols, th = tw * 1.55f;
			const size_t np = st->wallpapers.size();
			const int prow = (int)((np + cols - 1) / cols), frow = (int)((m_names.size() + cols - 1) / cols);
			float total = m.px(150) + m.px(34) + prow * (th + gap) + m.px(34) + std::max(1, frow) * (th + gap) + m.px(40);
			m_scroll.setLayout(body, total);
			cv.pushClip(body);
			float y = m.px(8);
			// previa do atual
			{
				const float pw = m.px(84), ph = m.px(132);
				const Rect r = m_scroll.itemRect(y, ph);
				const Rect pr{ body.center().x - pw * 0.5f, r.y, pw, ph };
				cv.fillRect(pr.inset(-m.px(3)), Color(30, 30, 34), m.px(12));
				cv.pushClip(pr);
				Store::DrawWallpaper(cv, m_c, pr);
				cv.popClip();
				y += m.px(150);
			}
			auto head = [&](const char* t) { const Rect r = m_scroll.itemRect(y, m.px(34)); DrawTextIn(cv, { body.x + mx + m.px(4), r.y, cw, r.h }, t, PC::DimL, m.font(12)); y += m.px(34); };
			head("PREDEFINIDOS");
			for (size_t i = 0; i < np; ++i)
			{
				const int c = (int)(i % cols), rw = (int)(i / cols);
				const Rect r = m_scroll.itemRect(y + rw * (th + gap), th);
				const Rect tr{ body.x + mx + c * (tw + gap), r.y, tw, th };
				if (!m_scroll.visible(tr)) continue;
				Color a, b, cc; Store::WallpaperColorsOrDerived(st->wallpapers[i].id, a, b, cc);
				cv.pushClip(tr);
				cv.fillGradientV({ tr.x, tr.y, tr.w, tr.h * 0.45f }, a, b);
				cv.fillGradientV({ tr.x, tr.y + tr.h * 0.45f, tr.w, tr.h * 0.55f + 1 }, b, cc);
				cv.popClip();
				const bool cur = st->wallpaper == st->wallpapers[i].id;
				cv.strokeRect(tr, cur ? PC::Blue : Color(0, 0, 0, 40), m.px(8), cur ? m.px(3) : m.px(1));
				DrawTextIn(cv, { tr.x, tr.bottom() - m.px(26), tr.w, m.px(22) }, st->wallpapers[i].title, Color(255, 255, 255), m.font(11), Align::Center);
				m_hits.add(ID_PRESET + (int)i, tr);
			}
			y += prow * (th + gap);
			head("MINHAS FOTOS");
			if (m_names.empty())
			{
				const Rect r = m_scroll.itemRect(y, th);
				DrawTextIn(cv, { body.x + mx, r.y, cw, th }, m_c->photos ? "Tire fotos na Câmera para usar aqui." : "Fotos não disponíveis nesta versão.", PC::Gray, m.font(13), Align::Center);
			}
			for (size_t i = 0; i < m_names.size(); ++i)
			{
				const int c = (int)(i % cols), rw = (int)(i / cols);
				const Rect r = m_scroll.itemRect(y + rw * (th + gap), th);
				const Rect tr{ body.x + mx + c * (tw + gap), r.y, tw, th };
				if (!m_scroll.visible(tr)) continue;
				cv.pushClip(tr); DrawPhoto(cv, m_c, "L:" + m_names[i], tr, m); cv.popClip();
				if (st->uploading) cv.fillRect(tr, Color(255, 255, 255, 120), 0);
				m_hits.add(ID_PHOTO + (int)i, tr);
			}
			cv.popClip();
			m_msg.draw(cv, m, body, m_c->nowMs);
		}

		// true = tratou; fechar (voltar aos Ajustes) devolve em closeReq
		bool onTouch(const Touch& t)
		{
			m_scroll.onTouch(t);
			if (t.kind == Touch::Down) m_press = m_hits.at(t.pos, &m_clip);
			else if (t.kind == Touch::Move) { if (m_press && m_hits.at(t.pos, &m_clip) != m_press) m_press = 0; }
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_press = 0;
			if (t.kind != Touch::Tap) return true;
			const int id = m_hits.at(t.pos, &m_clip);
			Store::Service* st = m_c->store;
			if (id == ID_BACK) { m_close = true; return true; }
			if (!st) return true;
			if (id >= ID_PHOTO && id - ID_PHOTO < (int)m_names.size())
			{
				std::string jpeg;
				if (!m_c->photos || !m_c->photos->read(m_names[id - ID_PHOTO], jpeg)) { m_msg.set("Não consegui abrir a foto.", m_c->nowMs); return true; }
				const std::string e = st->uploadWallpaper(jpeg, m_c->nowMs);
				m_msg.set(e.empty() ? "Enviando foto..." : e, m_c->nowMs, !e.empty(), e.empty() ? 1500 : 3200);
			}
			else if (id >= ID_PRESET && id - ID_PRESET < (int)st->wallpapers.size()) st->setWallpaper(st->wallpapers[id - ID_PRESET].id, m_c->nowMs);
			return true;
		}
		Rect tileRect(int i) const { return m_hits.find(ID_PRESET + i); }
		Rect photoRect(int i) const { return m_hits.find(ID_PHOTO + i); }
		bool takeClose() { const bool c = m_close; m_close = false; return c; }
	private:
		PhoneContext* m_c; HitList m_hits; Rect m_clip; ScrollView m_scroll; InlineMsg m_msg; int m_press = 0; bool m_close = false; std::vector<std::string> m_names;
	};
}
