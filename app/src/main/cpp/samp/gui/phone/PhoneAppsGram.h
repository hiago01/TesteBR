#pragma once
// Celular - CAMERA, GALERIA e GRAM (rede social de fotos).
//  Camera : a tela do aparelho fica transparente (o jogo aparece por tras); o obturador captura a tela do JOGO (sem o celular).
//  Galeria: fotos locais (grade 3 colunas), ver, apagar, postar no Gram.
//  Gram   : feed global (fotos de todos os jogadores do servidor), curtir, comentar, postar, apagar a propria foto.
// Os apps so conhecem Canvas e PhoneContext (photos / gram); sem suporte avisam de forma honesta.
#include "PhoneForm.h"
#include "PhoneGram.h"
#include "PhoneText.h"
#include "PhoneUI.h"

namespace Phone
{
	// ---- foto na tela: textura ja pronta, ou cinza com aviso. Recorta para cobrir o retangulo (cover) ou encaixa inteira (contain). ----
	inline void DrawPhoto(Canvas& cv, PhoneContext* c, const std::string& key, const Rect& r, const Metrics& m, bool contain = false)
	{
		PhotoTex t;
		if (c && c->photos && c->photos->texture(key, t) && t.id && t.w > 0 && t.h > 0)
		{
			const float ra = r.w / r.h, ia = (float)t.w / (float)t.h;
			if (contain)
			{
				Rect d = r;
				if (ia > ra) { d.h = r.w / ia; d.y = r.y + (r.h - d.h) * 0.5f; } else { d.w = r.h * ia; d.x = r.x + (r.w - d.w) * 0.5f; }
				cv.image(d, t.id);
				return;
			}
			Vec2 a{ 0, 0 }, b{ 1, 1 };
			if (ia > ra) { const float k = ra / ia; a.x = (1 - k) * 0.5f; b.x = 1 - a.x; } else { const float k = ia / ra; a.y = (1 - k) * 0.5f; b.y = 1 - a.y; }
			cv.image(r, t.id, a, b);
			return;
		}
		cv.fillRect(r, Color(220, 220, 226), 0);
		const bool bad = c && c->photos && c->photos->failed(key);
		DrawTextIn(cv, r, bad ? "Foto indisponível" : "...", PC::Gray, m.font(13), Align::Center);
	}

	inline std::string NoPhotoText() { return "Fotos não disponíveis nesta versão do jogo."; }

	// ------------------------------------------------------------------------------------------------ Camera
	class CameraApp : public App
	{
	public:
		enum { ID_SHUTTER = 1, ID_THUMB, ID_FLIP };
		explicit CameraApp(PhoneContext* c) : m_c(c) {}
		void onResume() override { m_press = 0; m_c->cameraSelfie = false; m_serial = m_c->photos ? m_c->photos->shotSerial() : 0; refreshThumb(); }
		void update(float) override
		{
			if (!m_c->photos) return;
			const int s = m_c->photos->shotSerial();
			const bool busy = m_c->photos->shooting();
			if (s != m_serial) { m_serial = s; m_msg.set("Foto salva na Galeria", m_c->nowMs, false); m_flashMs = m_c->nowMs; refreshThumb(); }
			else if (m_wasBusy && !busy && m_shot)
			{
				// a captura terminou sem foto nova: mostra o motivo (antes o erro assincrono ficava invisivel)
				const std::string e = m_c->photos->lastError();
				m_msg.set(e.empty() ? "Não foi possível tirar a foto." : e, m_c->nowMs);
			}
			if (!busy) m_shot = false;
			m_wasBusy = busy;
		}
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			// tela transparente: so faixas escuras em cima/embaixo e o enquadramento
			const float topH = m.px(46), botH = m.px(118);
			const Rect top{ content.x, content.y, content.w, topH }, bot{ content.x, content.bottom() - botH, content.w, botH };
			cv.fillRect(top, Color(0, 0, 0, 170), 0);
			cv.fillRect(bot, Color(0, 0, 0, 190), 0);
			DrawTextIn(cv, top, m_c->cameraSelfie ? "Selfie" : "Foto", Color(255, 255, 255), m.font(16), Align::Center);
			m_hit.clear();
			{   // virar a camera: traseira (foto) <-> frontal (selfie: a camera passa para a frente do personagem)
				const Rect fb{ top.right() - m.px(92), top.y + m.px(9), m.px(80), m.px(28) };
				cv.fillRect(fb, m_press == ID_FLIP ? Color(255, 255, 255, 90) : Color(255, 255, 255, 50), m.px(14));
				DrawTextIn(cv, fb, m_c->cameraSelfie ? "Traseira" : "Selfie", Color(255, 255, 255), m.font(12), Align::Center);
				m_hit.add(ID_FLIP, fb);
			}
			m_vf = Rect{ content.x, top.bottom(), content.w, bot.y - top.bottom() };   // o que se ve pelo visor = o que vira a foto
			// cantos do enquadramento
			const Rect f{ content.x + m.px(14), top.bottom() + m.px(12), content.w - m.px(28), content.h - topH - botH - m.px(24) };
			const float L = m.px(22), th = std::max(1.5f, m.px(2));
			const Color wc(255, 255, 255, 200);
			cv.line({ f.x, f.y }, { f.x + L, f.y }, wc, th); cv.line({ f.x, f.y }, { f.x, f.y + L }, wc, th);
			cv.line({ f.right(), f.y }, { f.right() - L, f.y }, wc, th); cv.line({ f.right(), f.y }, { f.right(), f.y + L }, wc, th);
			cv.line({ f.x, f.bottom() }, { f.x + L, f.bottom() }, wc, th); cv.line({ f.x, f.bottom() }, { f.x, f.bottom() - L }, wc, th);
			cv.line({ f.right(), f.bottom() }, { f.right() - L, f.bottom() }, wc, th); cv.line({ f.right(), f.bottom() }, { f.right(), f.bottom() - L }, wc, th);
			DrawTextIn(cv, { f.x, f.y + m.px(6), f.w, m.px(20) }, "A foto sai exatamente do que você vê aqui", Color(255, 255, 255, 190), m.font(11.5f), Align::Center);
			// obturador + miniatura
			const Vec2 sc{ bot.center().x, bot.center().y };
			const float R = m.px(32);
			const bool busy = m_c->photos && m_c->photos->shooting();
			cv.fillCircle(sc, R, Color(255, 255, 255, 235));
			cv.strokeCircle(sc, R + m.px(5), Color(255, 255, 255), std::max(1.5f, m.px(2.5f)));
			if (m_press == ID_SHUTTER || busy) cv.fillCircle(sc, R * 0.82f, Color(200, 200, 205));
			m_hit.add(ID_SHUTTER, { sc.x - R - m.px(8), sc.y - R - m.px(8), (R + m.px(8)) * 2, (R + m.px(8)) * 2 });
			const Rect th2{ bot.x + m.px(26), sc.y - m.px(25), m.px(50), m.px(50) };
			if (!m_thumb.empty()) DrawPhoto(cv, m_c, "L:" + m_thumb, th2, m); else cv.fillRect(th2, Color(255, 255, 255, 40), 0);
			cv.strokeRect(th2, Color(255, 255, 255, 220), 0, std::max(1.0f, m.px(1.5f)));
			m_hit.add(ID_THUMB, th2);
			// flash
			const int64_t age = m_c->nowMs - m_flashMs;
			if (m_flashMs && age >= 0 && age < 260) cv.fillRect(content, Color(255, 255, 255, (int)(190 * (1.0f - age / 260.0f))), 0);
			m_msg.draw(cv, m, Rect{ content.x, content.y, content.w, content.h - botH }, m_c->nowMs);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (t.kind == Touch::Down) m_press = m_hit.at(t.pos);
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_press = 0;
			if (t.kind != Touch::Tap) return true;
			const int id = m_hit.at(t.pos);
			if (id == ID_FLIP) m_c->cameraSelfie = !m_c->cameraSelfie;
			else if (id == ID_SHUTTER) shoot();
			else if (id == ID_THUMB && m_c->openApp) m_c->openApp("galeria");
			return true;
		}
		void shoot()
		{
			if (!m_c->photos || !m_c->photos->canShoot()) { m_msg.set(NoPhotoText(), m_c->nowMs); return; }
			m_c->photos->setShotRect(m_vf.x, m_vf.y, m_vf.w, m_vf.h);
			if (m_c->photos->shoot()) m_shot = true;
			else { const std::string e = m_c->photos->lastError(); m_msg.set(e.empty() ? "Aguarde a foto anterior." : e, m_c->nowMs); }
		}
		const std::string& thumb() const { return m_thumb; }
	private:
		bool m_wasBusy = false, m_shot = false;
		Rect m_vf{ 0, 0, 0, 0 };
		void refreshThumb() { m_thumb.clear(); if (m_c->photos) { auto l = m_c->photos->list(); if (!l.empty()) m_thumb = l[0]; } }
		PhoneContext* m_c; HitList m_hit; int m_press = 0, m_serial = 0; int64_t m_flashMs = 0; std::string m_thumb; InlineMsg m_msg;
	};

	// ------------------------------------------------------------------------------------------------ Galeria
	class GalleryApp : public App
	{
	public:
		enum { ID_BACK = 1, ID_DEL, ID_POST, ID_ITEM = 1000 };
		explicit GalleryApp(PhoneContext* c) : m_c(c) {}
		void onResume() override { reload(); m_press = 0; }
		void onPause() override { m_confirm = false; }
		bool onBack() override { if (m_view >= 0) { m_view = -1; m_confirm = false; return true; } return false; }
		void update(float dt) override { m_scroll.update(dt); if (m_c->photos && m_c->photos->shotSerial() != m_serial) reload(); }
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, m_view >= 0 ? Color(0, 0, 0) : PC::Light, 0);
			m_nav.clear(); m_hits.clear();
			if (m_view >= 0 && m_view < (int)m_names.size()) drawView(cv, m, content); else drawGrid(cv, m, content);
			m_msg.draw(cv, m, content, m_c->nowMs);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			m_scroll.onTouch(t);
			if (t.kind == Touch::Down) m_press = hit(t.pos);
			else if (t.kind == Touch::Move) { if (m_press && hit(t.pos) != m_press) m_press = 0; }
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_press = 0;
			if (t.kind != Touch::Tap) return true;
			const int id = hit(t.pos);
			if (id == ID_BACK) onBack();
			else if (id >= ID_ITEM && id - ID_ITEM < (int)m_names.size()) { m_view = id - ID_ITEM; m_confirm = false; }
			else if (id == ID_DEL && m_view >= 0)
			{
				if (!m_confirm) { m_confirm = true; m_msg.set("Toque de novo para apagar a foto", m_c->nowMs, false); }
				else { if (m_c->photos) m_c->photos->remove(m_names[m_view]); m_view = -1; m_confirm = false; reload(); }
			}
			else if (id == ID_POST && m_view >= 0 && m_view < (int)m_names.size())
			{
				m_c->launchArg = "post:" + m_names[m_view];
				if (m_c->openApp) m_c->openApp("gram");
			}
			return true;
		}
		size_t count() const { return m_names.size(); } int viewing() const { return m_view; }
	private:
		void reload() { m_names.clear(); m_serial = 0; if (m_c->photos) { m_names = m_c->photos->list(); m_serial = m_c->photos->shotSerial(); } if (m_view >= (int)m_names.size()) m_view = -1; }
		int hit(Vec2 p) const { const int id = m_nav.at(p); return id ? id : m_hits.at(p, &m_clip); }

		void drawGrid(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Fotos", "", "", true, false, false);
			m_clip = nv.body;
			if (!m_c->photos) { DrawTextIn(cv, nv.body.inset(m.px(24), 0), NoPhotoText(), PC::Gray, m.font(15), Align::Center); return; }
			if (m_names.empty())
			{
				DrawTextIn(cv, { nv.body.x, nv.body.center().y - m.px(30), nv.body.w, m.px(28) }, "Nenhuma foto", PC::Gray, m.font(19), Align::Center);
				DrawTextIn(cv, { nv.body.x + m.px(20), nv.body.center().y, nv.body.w - m.px(40), m.px(24) }, "Tire fotos no app Câmera.", PC::Gray, m.font(14), Align::Center);
				return;
			}
			const int cols = 3; const float gap = m.px(2), cell = (nv.body.w - gap * (cols - 1)) / cols;
			const int rows = ((int)m_names.size() + cols - 1) / cols;
			m_scroll.setLayout(nv.body, rows * (cell + gap));
			cv.pushClip(nv.body);
			for (size_t i = 0; i < m_names.size(); ++i)
			{
				const int rr = (int)i / cols, cc = (int)i % cols;
				const Rect r0 = m_scroll.itemRect(rr * (cell + gap), cell); const Rect r{ nv.body.x + cc * (cell + gap), r0.y, cell, cell };
				if (!m_scroll.visible(r)) continue;
				DrawPhoto(cv, m_c, "L:" + m_names[i], r, m);
				if (m_press == ID_ITEM + (int)i) cv.fillRect(r, Color(0, 0, 0, 60), 0);
				m_hits.add(ID_ITEM + (int)i, r);
			}
			cv.popClip();
		}
		void drawView(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "", "Fotos", "", false, m_press == ID_BACK, false);
			m_nav.add(ID_BACK, nv.back);
			const float barH = m.px(64);
			const Rect img{ nv.body.x, nv.body.y, nv.body.w, nv.body.h - barH };
			DrawPhoto(cv, m_c, "L:" + m_names[m_view], img, m, true);
			const Rect bar{ nv.body.x, img.bottom(), nv.body.w, barH };
			const float bw = (bar.w - m.px(48)) * 0.5f;
			const Rect b1{ bar.x + m.px(16), bar.y + m.px(10), bw, bar.h - m.px(20) }, b2{ b1.right() + m.px(16), b1.y, bw, b1.h };
			DrawFilledButton(cv, m, b1, m_confirm ? "Confirmar" : "Apagar", PC::Red, Color(255, 255, 255), m_press == ID_DEL);
			DrawFilledButton(cv, m, b2, "Postar no Gram", PC::Blue, Color(255, 255, 255), m_press == ID_POST);
			m_hits.add(ID_DEL, b1); m_hits.add(ID_POST, b2); m_clip = content;
		}
		PhoneContext* m_c; std::vector<std::string> m_names; int m_view = -1, m_press = 0, m_serial = 0; bool m_confirm = false;
		HitList m_nav, m_hits; Rect m_clip; ScrollView m_scroll; InlineMsg m_msg;
	};

	// ------------------------------------------------------------------------------------------------ Gram
	class GramApp : public KeyboardApp
	{
	public:
		enum { ID_BACK = 1, ID_ACTION, ID_SEND, ID_COMPOSER, ID_REFRESH, ID_MORE, ID_LIKE = 1000, ID_COMMENT = 2000, ID_DEL = 3000, ID_PICK = 4000 };
		enum Screen { Feed = 0, Comments, Pick, Compose };
		explicit GramApp(PhoneContext* c) : KeyboardApp(c) {}
		void onResume() override
		{
			Gram::Service* g = m_c->gram;
			if (m_c->launchArg.compare(0, 5, "post:") == 0) { m_photo = m_c->launchArg.substr(5); m_c->launchArg.clear(); m_screen = Compose; m_caption.clear(); hideKeyboard(); if (g) g->postState = 0; }
			if (g && m_screen == Feed && (!g->feedLoaded || m_c->nowMs - m_lastRefresh > 60000)) { g->refresh(m_c->nowMs); m_lastRefresh = m_c->nowMs; }
		}
		void onPaused() override { m_press = 0; m_confirmDel = 0; }
		bool onBackApp() override
		{
			if (m_screen == Comments || m_screen == Pick) { m_screen = Feed; m_scroll = ScrollView(); return true; }
			if (m_screen == Compose) { m_screen = Feed; m_scroll = ScrollView(); return true; }
			return false;
		}
		void onUpdate(float dt) override
		{
			m_scroll.update(dt);
			Gram::Service* g = m_c->gram; if (!g) return;
			const std::string t = g->takeToast(); if (!t.empty()) m_msg.set(t, m_c->nowMs);
			if (m_screen == Compose && g->postState == 2) { g->postState = 0; m_screen = Feed; m_scroll = ScrollView(); m_caption.clear(); hideKeyboard(); m_msg.set("Foto publicada!", m_c->nowMs, false); }
			else if (g->postState == 3) { g->postState = 0; m_msg.set(g->postError, m_c->nowMs); }
		}
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			if (!m_c->gram) { drawNoService(cv, m, content, "Gram indisponível neste servidor."); m_msg.draw(cv, m, content, m_c->nowMs); return; }
			if (m_screen == Feed) drawFeed(cv, m, content);
			else if (m_screen == Comments) drawComments(cv, m, content);
			else if (m_screen == Pick) drawPick(cv, m, content);
			else drawCompose(cv, m, content);
			m_msg.draw(cv, m, content, m_c->nowMs);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (kbTouch(t)) return true;
			m_scroll.onTouch(t);
			trackPress(t);
			if (t.kind != Touch::Tap || !m_c->gram) return true;
			Gram::Service* g = m_c->gram; const int id = hitAt(t.pos);
			if (id == ID_BACK) { if (m_kbOn) hideKeyboard(); onBackApp(); return true; }
			if (m_screen == Feed)
			{
				if (id == ID_ACTION) { m_screen = Pick; m_scroll = ScrollView(); }
				else if (id == ID_REFRESH) { g->refresh(m_c->nowMs); m_lastRefresh = m_c->nowMs; }
				else if (id == ID_MORE) g->more(m_c->nowMs);
				else if (id >= ID_LIKE && id < ID_COMMENT && id - ID_LIKE < (int)g->posts.size()) g->like(g->posts[id - ID_LIKE].id, m_c->nowMs);
				else if (id >= ID_COMMENT && id < ID_DEL && id - ID_COMMENT < (int)g->posts.size()) openComments(g->posts[id - ID_COMMENT].id);
				else if (id >= ID_DEL && id < ID_PICK && id - ID_DEL < (int)g->posts.size())
				{
					const long long pid = g->posts[id - ID_DEL].id;
					if (m_confirmDel != pid) { m_confirmDel = pid; m_msg.set("Toque de novo para apagar sua foto", m_c->nowMs, false); }
					else { g->remove(pid, m_c->nowMs); m_confirmDel = 0; }
				}
			}
			else if (m_screen == Comments)
			{
				if (id == ID_SEND) sendComment();
				else if (id == ID_COMPOSER) showKeyboard(Keyboard::Mode::Text, "Enviar", m_text);
			}
			else if (m_screen == Pick)
			{
				if (id >= ID_PICK && id - ID_PICK < (int)m_names.size()) { m_photo = m_names[id - ID_PICK]; m_caption.clear(); m_screen = Compose; g->postState = 0; }
			}
			else
			{
				if (id == ID_ACTION) publish();
				else if (id == ID_COMPOSER) showKeyboard(Keyboard::Mode::Text, "Concluir", m_caption);
			}
			return true;
		}
		int screen() const { return m_screen; } const std::string& photo() const { return m_photo; }
		void setCaption(const std::string& s) { m_caption = s; } void setComment(const std::string& s) { m_text = s; }
		void sendComment()
		{
			Gram::Service* g = m_c->gram; if (!g) return;
			const std::string txt = Text::Trim(m_text); if (txt.empty()) return;
			const std::string err = g->comment(m_post, txt, m_c->nowMs);
			if (!err.empty()) { m_msg.set(err, m_c->nowMs); return; }
			m_text.clear(); m_kb.autoShift(m_text);
		}
		void publish()
		{
			Gram::Service* g = m_c->gram; if (!g || !m_c->photos) { m_msg.set(NoPhotoText(), m_c->nowMs); return; }
			if (g->postState == 1) return;
			std::string jpeg;
			if (!m_c->photos->read(m_photo, jpeg)) { m_msg.set("Não consegui abrir essa foto.", m_c->nowMs); return; }
			const std::string err = g->post(jpeg, Text::Trim(m_caption), m_c->nowMs);
			if (!err.empty()) { m_msg.set(err, m_c->nowMs); return; }
			hideKeyboard();
		}

	protected:
		void onKeyEvent(const Keyboard::Event& e) override
		{
			if (m_screen == Comments) { if (Keyboard::Apply(m_text, e, 140, false, Keyboard::Mode::Text)) sendComment(); else m_kb.autoShift(m_text); }
			else if (m_screen == Compose) { if (Keyboard::Apply(m_caption, e, 140, false, Keyboard::Mode::Text)) hideKeyboard(); else m_kb.autoShift(m_caption); }
		}

	private:
		void openComments(long long id)
		{
			m_post = id; m_screen = Comments; m_text.clear(); hideKeyboard(); m_scroll = ScrollView(); m_scroll.jumpToEnd();
			m_c->gram->loadComments(id, m_c->nowMs);
		}
		void drawNoService(Canvas& cv, const Metrics& m, const Rect& content, const std::string& why)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Gram", "", "", true, false, false);
			DrawTextIn(cv, { nv.body.x + m.px(20), nv.body.center().y - m.px(14), nv.body.w - m.px(40), m.px(28) }, why, PC::Gray, m.font(16), Align::Center);
		}
		static std::string Initial(const std::string& n) { return n.empty() ? "?" : Text::FirstChar(n); }

		void drawFeed(Canvas& cv, const Metrics& m, const Rect& content)
		{
			Gram::Service* g = m_c->gram;
			NavResult nv = DrawNavBar(cv, m, content, "Gram", "", m_c->photos ? "Nova" : "", true, false, m_press == ID_ACTION);
			beginFrame(nv.body); if (m_c->photos) m_nav.add(ID_ACTION, nv.action);
			const Rect body = nv.body; const float mx = m.px(12), cw = body.w - 2 * mx;
			if (!m_c->networked) { DrawTextIn(cv, { body.x + m.px(20), body.center().y - m.px(14), body.w - m.px(40), m.px(28) }, "Sem conexão com o servidor.", PC::Gray, m.font(16), Align::Center); return; }
			const float fs = m.font(14), lineH = m.px(19), headH = m.px(50), imgH = cw * 0.56f, actH = m.px(44);
			// alturas
			std::vector<std::vector<std::string>> caps(g->posts.size()); std::vector<float> hs(g->posts.size());
			float total = m.px(44);       // linha "Atualizar"
			for (size_t i = 0; i < g->posts.size(); ++i)
			{
				if (!g->posts[i].caption.empty()) { caps[i] = Text::Wrap(cv, g->posts[i].caption, fs, cw - m.px(24)); if (caps[i].size() > 3) caps[i].resize(3); }
				hs[i] = headH + imgH + actH + (caps[i].empty() ? 0.0f : caps[i].size() * lineH + m.px(10)) + m.px(14);
				total += hs[i];
			}
			total += m.px(54);
			m_scroll.setLayout(body, total);
			cv.pushClip(body);
			float y = 0;
			{
				const Rect r = m_scroll.itemRect(y, m.px(44)); const Rect rr{ body.x + mx, r.y + m.px(6), cw, m.px(34) };
				DrawTextIn(cv, rr, g->loadingFeed ? "Carregando..." : "Atualizar", PC::Blue, m.font(15), Align::Center);
				m_hits.add(ID_REFRESH, rr); y += m.px(44);
			}
			if (g->posts.empty() && !g->loadingFeed)
				DrawTextIn(cv, { body.x + m.px(20), body.y + m.px(90), body.w - m.px(40), m.px(24) }, g->feedLoaded ? "Ainda não há fotos. Seja o primeiro!" : "Toque em Atualizar.", PC::Gray, m.font(15), Align::Center);
			for (size_t i = 0; i < g->posts.size(); ++i)
			{
				const Gram::Post& p = g->posts[i]; const float h = hs[i];
				const Rect r = m_scroll.itemRect(y, h); y += h;
				if (!m_scroll.visible(r)) continue;
				const Rect card{ body.x + mx, r.y, cw, h - m.px(10) };
				cv.fillRect(card, PC::White, m.px(12));
				// cabecalho
				const float av = m.px(32); const Vec2 ac{ card.x + m.px(12) + av * 0.5f, card.y + headH * 0.5f };
				cv.fillCircle(ac, av * 0.5f, Color(225, 48, 108));
				DrawTextIn(cv, { ac.x - av * 0.5f, ac.y - av * 0.5f, av, av }, Initial(p.name), Color(255, 255, 255), m.font(15), Align::Center);
				DrawTextIn(cv, { card.x + m.px(54), card.y + m.px(7), card.w - m.px(130), m.px(22) }, p.name, PC::LabelL, m.font(15));
				DrawTextIn(cv, { card.x + m.px(54), card.y + m.px(27), card.w - m.px(130), m.px(18) }, Gram::AgeText(p.ageS), PC::Gray, m.font(12));
				if (p.number == m_c->myNumber)
				{
					const Rect d{ card.right() - m.px(74), card.y + m.px(8), m.px(66), m.px(34) };
					DrawTextIn(cv, d, m_confirmDel == p.id ? "Confirmar" : "Apagar", PC::Red, m.font(13), Align::Right); m_hits.add(ID_DEL + (int)i, d);
				}
				// foto
				const Rect ir{ card.x, card.y + headH, card.w, imgH };
				DrawPhoto(cv, m_c, "R:" + p.media, ir, m);
				// acoes
				const float ay = ir.bottom();
				const Rect hr{ card.x + m.px(8), ay, m.px(78), actH };
				drawHeart(cv, m, { hr.x + m.px(14), ay + actH * 0.5f }, m.px(11), p.liked);
				DrawTextIn(cv, { hr.x + m.px(32), ay, m.px(46), actH }, std::to_string(p.likes), PC::LabelL, m.font(14));
				m_hits.add(ID_LIKE + (int)i, hr);
				const Rect cr{ hr.right() + m.px(6), ay, m.px(84), actH };
				Icons::Draw(cv, Icon::Message, { cr.x + m.px(4), ay + actH * 0.5f - m.px(10), m.px(20), m.px(20) }, PC::LabelL);
				DrawTextIn(cv, { cr.x + m.px(30), ay, m.px(50), actH }, std::to_string(p.comments), PC::LabelL, m.font(14));
				m_hits.add(ID_COMMENT + (int)i, cr);
				float cy = ay + actH;
				for (auto& l : caps[i]) { DrawTextIn(cv, { card.x + m.px(12), cy, card.w - m.px(24), lineH }, l, PC::LabelL, fs); cy += lineH; }
			}
			// fim
			{
				const Rect r = m_scroll.itemRect(y, m.px(54)); const Rect rr{ body.x + mx, r.y + m.px(8), cw, m.px(36) };
				if (g->hasMore) { DrawTextIn(cv, rr, g->loadingFeed ? "Carregando..." : "Ver mais", PC::Blue, m.font(15), Align::Center); m_hits.add(ID_MORE, rr); if (m_scroll.offset() >= m_scroll.maxOffset() - m.px(40) && !g->loadingFeed && m_c->nowMs - m_lastMore > 1200) { m_lastMore = m_c->nowMs; g->more(m_c->nowMs); } }
			}
			cv.popClip();
		}
		static void drawHeart(Canvas& cv, const Metrics& m, Vec2 c, float r, bool on)
		{
			const Color col = on ? Color(255, 59, 48) : Color(60, 60, 67, 200);
			if (on)
			{
				cv.fillCircle({ c.x - r * 0.5f, c.y - r * 0.25f }, r * 0.55f, col); cv.fillCircle({ c.x + r * 0.5f, c.y - r * 0.25f }, r * 0.55f, col);
				cv.fillTriangle({ c.x - r * 1.0f, c.y - r * 0.05f }, { c.x + r * 1.0f, c.y - r * 0.05f }, { c.x, c.y + r * 1.05f }, col);
			}
			else
			{
				const float th = std::max(1.5f, m.px(1.6f));
				cv.strokeCircle({ c.x - r * 0.5f, c.y - r * 0.25f }, r * 0.55f, col, th); cv.strokeCircle({ c.x + r * 0.5f, c.y - r * 0.25f }, r * 0.55f, col, th);
				cv.line({ c.x - r * 0.98f, c.y + r * 0.05f }, { c.x, c.y + r * 1.0f }, col, th); cv.line({ c.x + r * 0.98f, c.y + r * 0.05f }, { c.x, c.y + r * 1.0f }, col, th);
			}
		}

		void drawComments(Canvas& cv, const Metrics& m, const Rect& content)
		{
			Gram::Service* g = m_c->gram;
			NavResult nv = DrawNavBar(cv, m, content, "Comentários", "Gram", "", true, m_press == ID_BACK, false);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back);
			const float barH = m.px(56);
			const Rect view{ body.x, body.y, body.w, body.h - barH }, bar{ body.x, view.bottom(), body.w, barH };
			const float fs = m.font(14), lineH = m.px(19), mx = m.px(14);
			std::vector<std::vector<std::string>> lines; float total = m.px(10);
			for (auto& k : g->comments) { lines.push_back(Text::Wrap(cv, k.body, fs, view.w - 2 * mx)); total += m.px(20) + lines.back().size() * lineH + m.px(10); }
			m_scroll.setLayout(view, total + m.px(10));
			cv.pushClip(view);
			if (g->comments.empty()) DrawTextIn(cv, { view.x, view.center().y - m.px(14), view.w, m.px(28) }, g->loadingComments ? "Carregando..." : "Nenhum comentário ainda", PC::Gray, m.font(15), Align::Center);
			float y = m.px(10);
			for (size_t i = 0; i < g->comments.size(); ++i)
			{
				const float h = m.px(20) + lines[i].size() * lineH + m.px(10);
				const Rect r = m_scroll.itemRect(y, h); y += h;
				if (!m_scroll.visible(r)) continue;
				DrawTextIn(cv, { view.x + mx, r.y, view.w - 2 * mx, m.px(20) }, g->comments[i].name + "  " + Gram::AgeText(g->comments[i].ageS), PC::DimL, m.font(12));
				for (size_t k = 0; k < lines[i].size(); ++k) DrawTextIn(cv, { view.x + mx, r.y + m.px(20) + k * lineH, view.w - 2 * mx, lineH }, lines[i][k], PC::LabelL, fs);
			}
			cv.popClip();
			drawComposer(cv, m, bar, m_text, "Adicionar comentário", ID_COMPOSER, ID_SEND);
			drawKeyboard(cv, m);
		}
		void drawComposer(Canvas& cv, const Metrics& m, const Rect& bar, const std::string& txt, const char* hint, int idField, int idSend)
		{
			cv.fillRect(bar, Color(249, 249, 251), 0);
			cv.fillRect({ bar.x, bar.y, bar.w, std::max(1.0f, m.px(0.6f)) }, PC::Sep, 0);
			const float sb = m.px(34);
			const Rect field{ bar.x + m.px(12), bar.y + m.px(9), bar.w - m.px(24) - sb - m.px(8), bar.h - m.px(18) };
			cv.fillRect(field, Color(255, 255, 255), field.h * 0.5f);
			cv.strokeRect(field, Color(60, 60, 67, 60), field.h * 0.5f, std::max(1.0f, m.px(1)));
			const float tfs = m.font(15);
			if (txt.empty() && !m_kbOn) DrawTextIn(cv, field.inset(m.px(14), 0), hint, PC::Gray, tfs);
			else
			{
				const std::string shown = TailFit(cv, txt, tfs, field.w - m.px(30));
				DrawTextIn(cv, field.inset(m.px(14), 0), shown, PC::LabelL, tfs);
				if (m_kbOn && ((m_c->nowMs / 500) % 2 == 0)) cv.fillRect({ field.x + m.px(14) + cv.measure(shown, tfs).x + m.px(1), field.center().y - tfs * 0.6f, std::max(1.5f, m.px(1.5f)), tfs * 1.2f }, PC::Blue, 0);
			}
			m_hits.add(idField, field);
			const Rect sr{ field.right() + m.px(8), bar.y + (bar.h - sb) * 0.5f, sb, sb };
			cv.fillCircle(sr.center(), sb * 0.5f, Text::Trim(txt).empty() ? Color(190, 190, 198) : PC::Blue);
			cv.fillTriangle({ sr.center().x, sr.center().y - sb * 0.24f }, { sr.center().x - sb * 0.22f, sr.center().y + sb * 0.1f }, { sr.center().x + sb * 0.22f, sr.center().y + sb * 0.1f }, Color(255, 255, 255));
			cv.fillRect({ sr.center().x - sb * 0.05f, sr.center().y, sb * 0.1f, sb * 0.22f }, Color(255, 255, 255), 0);
			m_hits.add(idSend, sr);
		}

		void drawPick(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Escolher foto", "Cancelar", "", true, m_press == ID_BACK, false);
			beginFrame(nv.body); m_nav.add(ID_BACK, nv.back);
			m_names.clear(); if (m_c->photos) m_names = m_c->photos->list();
			if (m_names.empty())
			{
				DrawTextIn(cv, { nv.body.x + m.px(20), nv.body.center().y - m.px(20), nv.body.w - m.px(40), m.px(28) }, "Você ainda não tem fotos.", PC::Gray, m.font(17), Align::Center);
				DrawTextIn(cv, { nv.body.x + m.px(20), nv.body.center().y + m.px(10), nv.body.w - m.px(40), m.px(24) }, "Tire uma no app Câmera.", PC::Gray, m.font(14), Align::Center);
				return;
			}
			const int cols = 3; const float gap = m.px(2), cell = (nv.body.w - gap * (cols - 1)) / cols;
			const int rows = ((int)m_names.size() + cols - 1) / cols;
			m_scroll.setLayout(nv.body, rows * (cell + gap));
			cv.pushClip(nv.body);
			for (size_t i = 0; i < m_names.size(); ++i)
			{
				const int rr = (int)i / cols, cc = (int)i % cols;
				const Rect r0 = m_scroll.itemRect(rr * (cell + gap), cell); const Rect r{ nv.body.x + cc * (cell + gap), r0.y, cell, cell };
				if (!m_scroll.visible(r)) continue;
				DrawPhoto(cv, m_c, "L:" + m_names[i], r, m);
				m_hits.add(ID_PICK + (int)i, r);
			}
			cv.popClip();
		}

		void drawCompose(Canvas& cv, const Metrics& m, const Rect& content)
		{
			Gram::Service* g = m_c->gram; const bool sending = g->postState == 1;
			NavResult nv = DrawNavBar(cv, m, content, "Nova foto", "Cancelar", sending ? "Enviando" : "Publicar", true, m_press == ID_BACK, m_press == ID_ACTION && !sending);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); if (!sending) m_nav.add(ID_ACTION, nv.action);
			const float mx = m.px(14);
			const float barH = m.px(56);
			const Rect img{ body.x + mx, body.y + m.px(10), body.w - 2 * mx, std::min((body.w - 2 * mx) * 0.56f, body.h - barH - m.px(20)) };
			DrawPhoto(cv, m_c, "L:" + m_photo, img, m);
			if (sending) { cv.fillRect(img, Color(0, 0, 0, 110), 0); DrawTextIn(cv, img, "Enviando foto...", Color(255, 255, 255), m.font(16), Align::Center); }
			const Rect bar{ body.x, std::min(img.bottom() + m.px(10), body.bottom() - barH), body.w, barH };
			DrawTextIn(cv, { bar.x + mx, bar.y - m.px(2), bar.w, m.px(16) }, "Legenda (opcional, até 140 caracteres)", PC::DimL, m.font(11.5f));
			drawComposer(cv, m, { bar.x, bar.y + m.px(14), bar.w, bar.h }, m_caption, "Escreva uma legenda", ID_COMPOSER, ID_ACTION);
			drawKeyboard(cv, m);
		}

		std::vector<std::string> m_names; std::string m_text, m_caption, m_photo; long long m_post = 0, m_confirmDel = 0; int m_screen = Feed;
		int64_t m_lastRefresh = 0, m_lastMore = 0; ScrollView m_scroll; InlineMsg m_msg;
	};
}
