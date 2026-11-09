#pragma once
// Celular - componentes de FORMULARIO compartilhados pelos apps: lista de areas tocaveis, botao, campo de texto, aviso temporario
// e a base KeyboardApp (teclado nativo + roteamento de toque). Nenhum app desenha campo/botao proprio.
#include "PhoneApp.h"
#include "PhoneKeyboard.h"

namespace Phone
{
	struct HitList
	{
		struct H { int id; Rect r; };
		std::vector<H> v;
		void clear() { v.clear(); }
		void add(int id, const Rect& r) { v.push_back({ id, r }); }
		// ultimo adicionado por cima; clip = area visivel (toque fora dela nao conta)
		int at(Vec2 p, const Rect* clip = nullptr) const
		{
			if (clip && !clip->contains(p)) return 0;
			for (size_t i = v.size(); i-- > 0;) if (v[i].r.contains(p)) return v[i].id;
			return 0;
		}
	};

	inline void DrawFilledButton(Canvas& cv, const Metrics& m, const Rect& r, const std::string& label, Color bg, Color fg, bool pressed, bool enabled = true)
	{
		Color b = pressed ? Color::mix(bg, Color(0, 0, 0), 0.22f) : bg;
		if (!enabled) b = Color::mix(bg, Color(200, 200, 205), 0.6f);
		cv.fillRect(r, b, m.px(12));
		DrawTextIn(cv, r, label, fg, m.font(16), Align::Center);
	}

	// mostra o FIM do texto (onde o cursor esta) quando nao cabe
	inline std::string TailFit(Canvas& cv, const std::string& s, float size, float maxW)
	{
		std::string o = s;
		while (!o.empty() && cv.measure(o, size).x > maxW)
		{
			size_t n = 1; while (n < o.size() && (static_cast<unsigned char>(o[n]) & 0xC0) == 0x80) ++n;
			o.erase(0, n);
		}
		return o;
	}

	// campo de texto em linha: [rotulo cinza] [texto]  (cursor piscando quando em foco)
	inline void DrawField(Canvas& cv, const Metrics& m, const Rect& r, const std::string& label, const std::string& text, const std::string& hint,
	                      bool focused, int64_t nowMs, const RowStyle& st)
	{
		RowStyle s = st; s.chevron = false; s.pressed = false;
		DrawRow(cv, m, r, std::string(), std::string(), s);
		const float lw = m.px(88);
		DrawTextIn(cv, { r.x + m.px(16), r.y, lw, r.h }, label, PC::Gray, m.font(15));
		const Rect tr{ r.x + m.px(16) + lw, r.y, r.w - lw - m.px(28), r.h };
		const float fs = m.font(16);
		if (text.empty() && !focused) DrawTextIn(cv, tr, hint, PC::Gray.alpha(0.7f), fs);
		else
		{
			const std::string shown = TailFit(cv, text, fs, tr.w - m.px(4));
			DrawTextIn(cv, tr, shown, PC::LabelL, fs);
			if (focused && ((nowMs / 500) % 2 == 0))
			{
				const float tw = cv.measure(shown, fs).x;
				cv.fillRect({ tr.x + tw + m.px(1), tr.y + tr.h * 0.5f - fs * 0.6f, std::max(1.5f, m.px(1.5f)), fs * 1.2f }, PC::Blue, 0);
			}
		}
		if (focused) cv.strokeRect(r.inset(m.px(0.5f)), PC::Blue.alpha(0.55f), st.first || st.last ? m.px(10) : 0, std::max(1.0f, m.px(1.2f)));
	}

	// aviso temporario no rodape do app (erro/status); nunca simula sucesso
	struct InlineMsg
	{
		std::string text; int64_t until = 0; bool error = true;
		void set(const std::string& t, int64_t now, bool err = true, int64_t ms = 3200) { text = t; until = now + ms; error = err; }
		void clear() { text.clear(); until = 0; }
		bool active(int64_t now) const { return !text.empty() && now < until; }
		void draw(Canvas& cv, const Metrics& m, const Rect& area, int64_t now) const
		{
			if (!active(now)) return;
			const float h = m.px(38), w = std::min(area.w - m.px(24), cv.measure(text, m.font(14)).x + m.px(32));
			const Rect r{ area.center().x - w * 0.5f, area.bottom() - h - m.px(10), w, h };
			cv.fillRect(r.moved(0, m.px(2)), Color(0, 0, 0, 50), h * 0.5f);
			cv.fillRect(r, error ? Color(60, 60, 67, 235) : Color(52, 120, 70, 235), h * 0.5f);
			DrawTextIn(cv, r, text, Color(255, 255, 255), m.font(14), Align::Center);
		}
	};

	// Base para apps com texto: teclado nativo embaixo, roteamento de toque, Voltar fecha o teclado primeiro.
	class KeyboardApp : public App
	{
	public:
		explicit KeyboardApp(PhoneContext* c) : m_c(c) {}
		void onPause() override { hideKeyboard(); m_kb.reset(); m_kbTouch = false; onPaused(); }
		void update(float dt) override
		{
			if (m_kbOn) { const int n = m_kb.update(dt); Keyboard::Event e; e.kind = Keyboard::Event::Back; for (int i = 0; i < n; ++i) onKeyEvent(e); }
			onUpdate(dt);
		}
		bool onBack() override { if (m_kbOn) { hideKeyboard(); return true; } return onBackApp(); }
		bool keyboardVisible() const { return m_kbOn; }
		Keyboard& keyboard() { return m_kb; }
		const Rect& keyboardRect() const { return m_kbRect; }
	protected:
		virtual void onKeyEvent(const Keyboard::Event&) {}
		virtual void onUpdate(float) {}
		virtual void onPaused() {}
		virtual bool onBackApp() { return false; }

		void showKeyboard(Keyboard::Mode mode, const std::string& returnLabel, const std::string& currentText)
		{
			m_kb.setMode(mode); m_kb.setReturnLabel(returnLabel); m_kb.autoShift(currentText); m_kbOn = true;
		}
		void hideKeyboard() { m_kbOn = false; m_kbRect = Rect(); }
		// retorna a area livre ACIMA do teclado (e posiciona o teclado no rodape de 'body')
		Rect layoutKeyboard(const Metrics& m, const Rect& body)
		{
			if (!m_kbOn) { m_kbRect = Rect(); return body; }
			const float h = std::min(m_kb.height(m), body.h * 0.62f);
			m_kbRect = { body.x, body.bottom() - h, body.w, h };
			return { body.x, body.y, body.w, body.h - h };
		}
		void drawKeyboard(Canvas& cv, const Metrics& m) { if (m_kbOn) m_kb.draw(cv, m, m_kbRect); }
		// true = o toque pertence ao teclado (o app nao deve tratar)
		bool kbTouch(const Touch& t)
		{
			if (!m_kbOn) { m_kbTouch = false; return false; }
			if (t.kind == Touch::Down) m_kbTouch = m_kbRect.contains(t.pos);
			if (!m_kbTouch)
				return m_kbRect.contains(t.pos) && (t.kind == Touch::Tap || t.kind == Touch::LongPress);   // Tap tardio sobre o teclado
			const Keyboard::Event ev = m_kb.onTouch(t);
			if (ev.kind != Keyboard::Event::None) onKeyEvent(ev);
			if (t.kind == Touch::Up || t.kind == Touch::Cancel) m_kbTouch = false;
			return true;
		}
		// ---- toque: areas tocaveis (navegacao sem clip, corpo com clip) ----
		int hitAt(Vec2 p) const { const int id = m_nav.at(p); return id ? id : m_hits.at(p, &m_clip); }
		void beginFrame(const Rect& bodyClip) { m_nav.clear(); m_hits.clear(); m_clip = bodyClip; }
		void trackPress(const Touch& t)
		{
			if (t.kind == Touch::Down) m_press = hitAt(t.pos);
			else if (t.kind == Touch::Move) { if (m_press && hitAt(t.pos) != m_press) m_press = 0; }
			else if (t.kind == Touch::DragBegin || t.kind == Touch::Cancel || t.kind == Touch::Up) m_press = 0;
		}
		PhoneContext* m_c;
		HitList m_nav, m_hits; Rect m_clip; int m_press = 0;
		Keyboard m_kb; bool m_kbOn = false, m_kbTouch = false; Rect m_kbRect;
	};
}
