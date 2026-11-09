#pragma once
// Celular - TECLADO NATIVO proprio (desenhado no Canvas; nao usa o teclado do Android nem View Java).
// Paginas: letras (QWERTY + shift/caps), numeros/simbolos, acentos (pt-BR). Modos numericos: telefone (+*#) e hora (:).
// O toque e confirmado no DOWN (resposta imediata); Apagar repete ao segurar. Logica pura, testada no PC.
#include "PhoneText.h"
#include "PhoneUI.h"
#include "../maximoos/GestureTracker.h"
#include <string>
#include <vector>

namespace Phone
{
	class Keyboard
	{
	public:
		enum class Mode { Text, Phone, Time };
		struct Event { enum Kind { None, Char, Back, Enter } kind = None; std::string ch; };

		void setMode(Mode m) { m_mode = m; m_page = 0; if (m != Mode::Text) m_shift = 0; }
		Mode mode() const { return m_mode; }
		void setReturnLabel(const std::string& s) { m_ret = s; }
		void reset() { m_page = 0; m_shift = 0; m_pressed = -1; m_backHeld = false; m_keys.clear(); }
		int  shiftState() const { return m_shift; }
		int  page() const { return m_page; }
		void setShift(int s) { if (m_mode == Mode::Text && m_page == 0) m_shift = s; }
		// maiuscula automatica no inicio do texto / depois de ". " ou quebra de linha
		void autoShift(const std::string& text)
		{
			if (m_mode != Mode::Text || m_shift == 2) return;
			bool up = text.empty();
			if (!up) { const char l = text.back(); up = (l == '\n') || (text.size() >= 2 && l == ' ' && (text[text.size() - 2] == '.' || text[text.size() - 2] == '!' || text[text.size() - 2] == '?')); }
			m_shift = up ? 1 : 0;
		}

		static float HeightLu(Mode m) { return m == Mode::Text ? 232.0f : 220.0f; }
		float height(const Metrics& mt) const { return mt.px(HeightLu(m_mode)); }

		// area = retangulo do teclado (largura da tela do aparelho)
		void draw(Canvas& cv, const Metrics& mt, const Rect& area)
		{
			build(mt, area);
			cv.fillRect(area, Color(209, 212, 218), 0);
			cv.fillRect({ area.x, area.y, area.w, std::max(1.0f, mt.px(0.6f)) }, Color(0, 0, 0, 40), 0);
			for (size_t i = 0; i < m_keys.size(); ++i)
			{
				const Key& k = m_keys[i];
				const bool pr = (int)i == m_pressed;
				Color bg = k.special ? Color(172, 177, 186) : Color(255, 255, 255);
				if (k.kind == Event::Enter) bg = PC::Blue;
				if (k.id == kShift && m_shift) bg = Color(255, 255, 255);
				if (pr) bg = k.special ? Color(255, 255, 255) : Color(190, 194, 202);
				cv.fillRect(k.r.moved(0, mt.px(1)), Color(0, 0, 0, 55), mt.px(6));
				cv.fillRect(k.r, bg, mt.px(6));
				const float fs = k.label.size() > 3 ? mt.font(14) : mt.font(k.special ? 15 : 21);
				Color fg = k.kind == Event::Enter ? Color(255, 255, 255) : Color(0, 0, 0);
				if (k.id == kBack) { drawBackGlyph(cv, k.r, mt, fg); continue; }
				if (k.id == kShift) { drawShiftGlyph(cv, k.r, mt, m_shift == 2 ? PC::Blue : fg, m_shift != 0); continue; }
				DrawTextIn(cv, k.r, k.label, fg, fs, Align::Center);
			}
		}

		// Down confirma a tecla; Up/Cancel/DragBegin soltam. Chame update(dt) a cada quadro para a repeticao do apagar.
		Event onTouch(const MaximoOS::Touch& t)
		{
			using MaximoOS::Touch;
			Event ev;
			if (t.kind == Touch::Down)
			{
				m_pressed = -1;
				for (size_t i = 0; i < m_keys.size(); ++i) if (m_keys[i].r.inset(-m_gapHalf).contains(t.pos)) { m_pressed = (int)i; break; }
				if (m_pressed >= 0) ev = press(m_keys[m_pressed], t.t);
			}
			else if (t.kind == Touch::Move)
			{
				// dedo arrastou para outra tecla antes de soltar: so troca o destaque (nao digita de novo)
				if (m_pressed >= 0 && !m_keys[m_pressed].r.inset(-m_gapHalf).contains(t.pos)) { m_pressed = -1; m_backHeld = false; }
			}
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) { m_pressed = -1; m_backHeld = false; }
			return ev;
		}
		// retorna quantos Apagar de repeticao ocorreram neste quadro
		int update(float dtMs)
		{
			if (!m_backHeld) { m_holdMs = 0; m_acc = 0; return 0; }
			m_holdMs += dtMs;
			if (m_holdMs < kRepeatDelay) return 0;
			m_acc += dtMs;
			const int n = (int)(m_acc / kRepeatEvery);
			m_acc -= n * kRepeatEvery;
			return n;
		}

		// aplica um evento a um texto. maxBytes limita o tamanho; multiline: Enter vira '\n'. Retorna true se Enter foi pedido (campo de uma linha).
		static bool Apply(std::string& s, const Event& e, size_t maxBytes, bool multiline, Mode mode = Mode::Text)
		{
			if (e.kind == Event::Back) { Text::PopBack(s); return false; }
			if (e.kind == Event::Enter) { if (multiline) { if (s.size() + 1 <= maxBytes) s += '\n'; return false; } return true; }
			if (e.kind == Event::Char && !e.ch.empty())
			{
				if (mode == Mode::Phone && !(e.ch[0] >= '0' && e.ch[0] <= '9') && e.ch != "+" && e.ch != "*" && e.ch != "#") return false;
				if (mode == Mode::Time && !(e.ch[0] >= '0' && e.ch[0] <= '9') && e.ch != ":") return false;
				if (s.size() + e.ch.size() <= maxBytes) s += e.ch;
			}
			return false;
		}

		// ---- consulta (testes) ----
		bool keyCenter(const std::string& label, MaximoOS::Vec2& out) const
		{
			for (auto& k : m_keys) if (k.label == label) { out = k.r.center(); return true; }
			return false;
		}
		size_t keyCount() const { return m_keys.size(); }
		bool backspaceCenter(MaximoOS::Vec2& out) const { for (auto& k : m_keys) if (k.id == kBack) { out = k.r.center(); return true; } return false; }
		bool shiftCenter(MaximoOS::Vec2& out) const { for (auto& k : m_keys) if (k.id == kShift) { out = k.r.center(); return true; } return false; }

	private:
		enum { kNone = 0, kShift = 1, kBack = 2, kPage = 3, kSpace = 4, kEnter = 5, kChar = 6 };
		struct Key { Rect r; std::string label, ch; int id = kChar; Event::Kind kind = Event::Char; bool special = false; int target = 0; };
		static constexpr float kRepeatDelay = 420.0f, kRepeatEvery = 65.0f;

		void add(const Rect& r, const std::string& label, int id, Event::Kind kind, bool special, const std::string& ch = std::string(), int target = 0)
		{ Key k; k.r = r; k.label = label; k.ch = ch.empty() ? label : ch; k.id = id; k.kind = kind; k.special = special; k.target = target; m_keys.push_back(k); }

		void rowOf(const std::vector<std::string>& chars, float y, float h, float x0, float w, float kw, float gap)
		{
			const float total = chars.size() * kw + (chars.size() - 1) * gap;
			float x = x0 + (w - total) * 0.5f;
			for (auto& c : chars) { const std::string lab = (m_shift && m_page != 1) ? Text::Upper(c) : c; add({ x, y, kw, h }, lab, kChar, Event::Char, false); x += kw + gap; }
		}
		static std::vector<std::string> Chars(const char* const* a, int n) { return std::vector<std::string>(a, a + n); }

		void build(const Metrics& mt, const Rect& a)
		{
			m_keys.clear();
			const float pad = mt.px(4), gap = mt.px(6);
			m_gapHalf = gap * 0.5f;
			const float w = a.w - 2 * pad;
			if (m_mode != Mode::Text) { buildPad(mt, a, pad, gap); return; }
			const float rows = 4, top = a.y + mt.px(8), h = (a.h - mt.px(8) - mt.px(6) - (rows - 1) * gap) / rows;
			const float kw = (w - 9 * gap) / 10.0f;
			static const char* L1[10] = { "q","w","e","r","t","y","u","i","o","p" };
			static const char* L2[9] = { "a","s","d","f","g","h","j","k","l" };
			static const char* L3[7] = { "z","x","c","v","b","n","m" };
			static const char* S1[10] = { "1","2","3","4","5","6","7","8","9","0" };
			static const char* S2[10] = { "-","/",":",";","(",")","$","&","@","\"" };
			static const char* S3[5] = { ".",",","?","!","'" };
			static const char* A1[10] = { "\xC3\xA1","\xC3\xA0","\xC3\xA2","\xC3\xA3","\xC3\xA9","\xC3\xA8","\xC3\xAA","\xC3\xAD","\xC3\xAC","\xC3\xAE" };        // á à â ã é è ê í ì î
			static const char* A2[10] = { "\xC3\xB3","\xC3\xB2","\xC3\xB4","\xC3\xB5","\xC3\xBA","\xC3\xB9","\xC3\xBB","\xC3\xA7","\xC3\xB1","\xC3\xBC" };        // ó ò ô õ ú ù û ç ñ ü
			const float y0 = top, y1 = top + h + gap, y2 = top + 2 * (h + gap), y3 = top + 3 * (h + gap);
			if (m_page == 0)
			{
				rowOf(Chars(L1, 10), y0, h, a.x + pad, w, kw, gap);
				rowOf(Chars(L2, 9), y1, h, a.x + pad, w, kw, gap);
				const float side = (w - 7 * kw - 8 * gap) * 0.5f;
				add({ a.x + pad, y2, side, h }, "", kShift, Event::None, true);
				rowOf(Chars(L3, 7), y2, h, a.x + pad + side + gap, w - 2 * side - 2 * gap, kw, gap);
				add({ a.right() - pad - side, y2, side, h }, "", kBack, Event::Back, true);
			}
			else
			{
				const bool sym = (m_page == 1);
				rowOf(sym ? Chars(S1, 10) : Chars(A1, 10), y0, h, a.x + pad, w, kw, gap);
				rowOf(sym ? Chars(S2, 10) : Chars(A2, 10), y1, h, a.x + pad, w, kw, gap);
				const float side = (w - 5 * kw - 6 * gap) * 0.5f;
				add({ a.x + pad, y2, side, h }, sym ? "\xC3\xA1\xC3\xA7" : "123", kPage, Event::None, true, std::string(), sym ? 2 : 1);   // "áç" / "123"
				rowOf(Chars(S3, 5), y2, h, a.x + pad + side + gap, w - 2 * side - 2 * gap, kw, gap);
				add({ a.right() - pad - side, y2, side, h }, "", kBack, Event::Back, true);
			}
			const float bw = kw * 1.5f;
			add({ a.x + pad, y3, bw, h }, m_page == 0 ? "123" : "ABC", kPage, Event::None, true, std::string(), m_page == 0 ? 1 : 0);
			const float rw = kw * 2.4f;
			add({ a.x + pad + bw + gap, y3, w - bw - rw - 2 * gap, h }, "espa\xC3\xA7o", kSpace, Event::Char, false, " ");
			add({ a.right() - pad - rw, y3, rw, h }, m_ret.empty() ? "OK" : m_ret, kEnter, Event::Enter, true);
		}
		void buildPad(const Metrics& mt, const Rect& a, float pad, float gap)
		{
			const float w = a.w - 2 * pad, top = a.y + mt.px(8), h = (a.h - mt.px(8) - mt.px(6) - 3 * gap) / 4.0f;
			const float cw = (w - 3 * gap) / 4.0f;
			const char* d[9] = { "1","2","3","4","5","6","7","8","9" };
			for (int i = 0; i < 9; ++i) add({ a.x + pad + (i % 3) * (cw + gap), top + (i / 3) * (h + gap), cw, h }, d[i], kChar, Event::Char, false);
			const float y3 = top + 3 * (h + gap);
			if (m_mode == Mode::Phone) add({ a.x + pad, y3, cw, h }, "+", kChar, Event::Char, false);
			else add({ a.x + pad, y3, cw, h }, ":", kChar, Event::Char, false);
			add({ a.x + pad + (cw + gap), y3, cw, h }, "0", kChar, Event::Char, false);
			if (m_mode == Mode::Phone) add({ a.x + pad + 2 * (cw + gap), y3, cw, h }, "#", kChar, Event::Char, false);
			add({ a.x + pad + 3 * (cw + gap), top, cw, h }, "", kBack, Event::Back, true);
			add({ a.x + pad + 3 * (cw + gap), top + (h + gap), cw, 3 * h + 2 * gap }, m_ret.empty() ? "OK" : m_ret, kEnter, Event::Enter, true);
		}

		Event press(const Key& k, int64_t now)
		{
			Event ev;
			switch (k.id)
			{
			case kShift:
				// 1 toque: liga/desliga; 2 toques seguidos (<400 ms): CAPS
				if (m_shift == 2) m_shift = 0;
				else if (m_shift == 1 && now - m_lastShiftT < 400) m_shift = 2;
				else m_shift = m_shift ? 0 : 1;
				m_lastShiftT = now;
				break;
			case kPage: m_page = k.target; if (m_page == 1) m_shift = 0; break;
			case kBack: ev.kind = Event::Back; m_backHeld = true; m_holdMs = 0; m_acc = 0; break;
			case kEnter: ev.kind = Event::Enter; break;
			default:
				ev.kind = Event::Char; ev.ch = k.label == "espa\xC3\xA7o" ? " " : k.label;
				if (m_shift == 1 && m_page == 0) m_shift = 0;       // maiuscula de uma letra so
				break;
			}
			return ev;
		}
		static void drawBackGlyph(Canvas& cv, const Rect& r, const Metrics& mt, Color c)
		{
			const Vec2 m = r.center(); const float w = mt.px(11), h = mt.px(8), t = std::max(1.6f, mt.px(1.6f));
			cv.line({ m.x - w, m.y }, { m.x - w * 0.3f, m.y - h }, c, t); cv.line({ m.x - w * 0.3f, m.y - h }, { m.x + w, m.y - h }, c, t);
			cv.line({ m.x - w, m.y }, { m.x - w * 0.3f, m.y + h }, c, t); cv.line({ m.x - w * 0.3f, m.y + h }, { m.x + w, m.y + h }, c, t);
			cv.line({ m.x + w, m.y - h }, { m.x + w, m.y + h }, c, t);
			cv.line({ m.x - w * 0.1f, m.y - h * 0.5f }, { m.x + w * 0.6f, m.y + h * 0.5f }, c, t); cv.line({ m.x - w * 0.1f, m.y + h * 0.5f }, { m.x + w * 0.6f, m.y - h * 0.5f }, c, t);
		}
		static void drawShiftGlyph(Canvas& cv, const Rect& r, const Metrics& mt, Color c, bool on)
		{
			const Vec2 m = r.center(); const float w = mt.px(10), h = mt.px(10);
			if (on) { cv.fillTriangle({ m.x, m.y - h }, { m.x - w, m.y }, { m.x + w, m.y }, c); cv.fillRect({ m.x - w * 0.4f, m.y, w * 0.8f, h }, c, 0); }
			else { const float t = std::max(1.6f, mt.px(1.6f)); cv.line({ m.x, m.y - h }, { m.x - w, m.y }, c, t); cv.line({ m.x, m.y - h }, { m.x + w, m.y }, c, t); cv.line({ m.x - w, m.y }, { m.x - w * 0.4f, m.y }, c, t); cv.line({ m.x + w, m.y }, { m.x + w * 0.4f, m.y }, c, t); cv.line({ m.x - w * 0.4f, m.y }, { m.x - w * 0.4f, m.y + h }, c, t); cv.line({ m.x + w * 0.4f, m.y }, { m.x + w * 0.4f, m.y + h }, c, t); cv.line({ m.x - w * 0.4f, m.y + h }, { m.x + w * 0.4f, m.y + h }, c, t); }
		}

		Mode m_mode = Mode::Text; int m_page = 0, m_shift = 0, m_pressed = -1;
		std::string m_ret; std::vector<Key> m_keys;
		bool m_backHeld = false; float m_holdMs = 0, m_acc = 0, m_gapHalf = 0; int64_t m_lastShiftT = -10000;
	};
}
