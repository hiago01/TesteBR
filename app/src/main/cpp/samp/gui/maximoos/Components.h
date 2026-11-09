#pragma once
// Maximo OS - componentes COMPARTILHADOS (header-only). Todo app usa estes; nenhum app desenha botao/painel/icone proprio.
//  Panel, Button, AppIcon, Pressable (estado de toque), ScrollView (rolagem com inercia), Toggle, Header de tela.
// Para acrescentar um componente novo: crie-o AQUI (nao no app) e documente em MAXIMO_OS_ARCHITECTURE.md.
#include "Canvas.h"
#include "GestureTracker.h"
#include "ResolutionManager.h"
#include "Theme.h"
#include <cmath>
#include <string>

namespace MaximoOS
{
	// ---- estado de pressao de um botao/area tocavel ----
	struct Pressable
	{
		bool pressed = false;
		// true quando houve um TOQUE completo dentro de r. Press visual: Down dentro -> pressed; sair do retangulo cancela.
		bool handle(const Touch& t, const Rect& r)
		{
			switch (t.kind)
			{
			case Touch::Down:   pressed = r.contains(t.pos); return false;
			case Touch::Move:   if (pressed && !r.contains(t.pos)) pressed = false; return false;
			case Touch::DragBegin: case Touch::Cancel: case Touch::LongPress: pressed = false; return false;
			case Touch::Up:     pressed = false; return false;
			case Touch::Tap:    return r.contains(t.pos) && r.contains(t.start);
			default: return false;
			}
		}
	};

	// ---- painel ----
	inline void DrawPanel(Canvas& cv, const Metrics& m, const Rect& r, Color fill = Colors::Panel, float radiusLu = 14.0f)
	{
		cv.fillRect(r, fill, m.px(radiusLu));
		cv.strokeRect(r, Colors::Line, m.px(radiusLu), std::max(1.0f, m.px(1.0f)));
	}

	// ---- botao ----
	inline void DrawButton(Canvas& cv, const Metrics& m, const Rect& r, const std::string& label, bool primary, bool pressed, bool enabled = true, Icon icon = Icon::None)
	{
		Color bg = primary ? Colors::Accent : Colors::PanelHi;
		if (pressed) bg = Color::mix(bg, Color(0, 0, 0), 0.25f);
		if (!enabled) bg = Color::mix(bg, Colors::Bg, 0.6f);
		cv.fillRect(r, bg, m.px(12));
		if (!primary) cv.strokeRect(r, Colors::Line, m.px(12), std::max(1.0f, m.px(1)));
		const Color fg = enabled ? Colors::Text : Colors::TextDim;
		Rect tr = r.inset(m.px(10), 0);
		if (icon != Icon::None)
		{
			const float is = std::min(r.h * 0.5f, m.px(22));
			Icons::Draw(cv, icon, { r.x + m.px(14), r.y + (r.h - is) * 0.5f, is, is }, fg);
			tr.x += is + m.px(10); tr.w -= is + m.px(10);
		}
		DrawTextIn(cv, tr, label, fg, m.font(Fonts::Body), icon == Icon::None ? Align::Center : Align::Left);
	}

	// ---- icone de aplicativo (grade da tela inicial / dock) ----
	struct AppIconStyle { bool showLabel = true; float scale = 1.0f; int badge = 0; bool dim = false; bool shadowLabel = false; };

	inline Rect AppIconSquare(const Metrics& m, const Rect& cell, bool showLabel)
	{
		const float labelH = showLabel ? m.px(34) : 0;
		float sz = std::min(cell.w * 0.62f, (cell.h - labelH) * 0.82f);
		sz = std::max(sz, 4.0f);
		return { cell.x + (cell.w - sz) * 0.5f, cell.y + (cell.h - labelH - sz) * 0.5f, sz, sz };
	}

	inline void DrawAppIcon(Canvas& cv, const Metrics& m, const Rect& cell, const std::string& title, Icon icon, Color accent, bool pressed, const AppIconStyle& st = AppIconStyle())
	{
		Rect q = AppIconSquare(m, cell, st.showLabel);
		const float k = (pressed ? 0.92f : 1.0f) * st.scale;
		const Vec2 c = q.center();
		q = { c.x - q.w * 0.5f * k, c.y - q.h * 0.5f * k, q.w * k, q.h * k };
		const float rad = q.w * 0.24f;
		const float a = st.dim ? 0.55f : 1.0f;
		cv.fillRect(q.moved(0, q.h * 0.04f), Color(0, 0, 0, 70), rad);                     // sombra
		cv.fillRect(q, Color::mix(accent, Color(0, 0, 0), 0.38f).alpha(a), rad);           // base
		cv.fillRect({ q.x, q.y, q.w, q.h * 0.58f }, accent.alpha(a), rad);                  // brilho superior
		cv.fillRect({ q.x, q.y + q.h * 0.3f, q.w, q.h * 0.3f }, accent.alpha(a), 0);        // emenda do brilho
		cv.fillRect({ q.x, q.y + q.h * 0.58f, q.w, q.h * 0.42f }, Color::mix(accent, Color(0, 0, 0), 0.38f).alpha(a), rad);
		cv.fillRect({ q.x, q.y + q.h * 0.55f, q.w, q.h * 0.1f }, Color::mix(accent, Color(0, 0, 0), 0.2f).alpha(a), 0);
		Icons::Draw(cv, icon, q.inset(q.w * 0.2f), Color(255, 255, 255, st.dim ? 170 : 255));
		if (st.badge > 0)
		{
			const float br = q.w * 0.17f;
			const Vec2 bc{ q.right() - br * 0.4f, q.y + br * 0.4f };
			cv.fillCircle(bc, br, Colors::Danger);
			DrawTextIn(cv, { bc.x - br, bc.y - br, br * 2, br * 2 }, st.badge > 9 ? "9+" : std::to_string(st.badge), Colors::Text, std::max(Metrics::kMinFontPx - 2.0f, br * 1.2f), Align::Center);
		}
		if (st.showLabel && st.shadowLabel)
			DrawTextIn(cv, { cell.x + m.px(2), cell.bottom() - m.px(34) + m.px(2) + std::max(1.0f, m.px(1)), cell.w - m.px(4), m.px(26) }, title, Color(0, 0, 0, 150), m.font(Fonts::Caption), Align::Center);
		if (st.showLabel)
			DrawTextIn(cv, { cell.x + m.px(2), cell.bottom() - m.px(34) + m.px(2), cell.w - m.px(4), m.px(26) }, title, Colors::Text.alpha(st.dim ? 0.85f : 1.0f), m.font(Fonts::Caption), Align::Center);
	}

	// ---- chave liga/desliga ----
	inline Rect ToggleRect(const Metrics& m, float rightX, float cy) { const float w = m.px(52), h = m.px(28); return { rightX - w, cy - h * 0.5f, w, h }; }
	inline void DrawToggle(Canvas& cv, const Metrics& m, const Rect& r, bool on)
	{
		cv.fillRect(r, on ? Colors::Accent : Colors::Line, r.h * 0.5f);
		const float k = r.h * 0.38f;
		cv.fillCircle({ on ? r.right() - r.h * 0.5f : r.x + r.h * 0.5f, r.y + r.h * 0.5f }, k, Colors::Text);
	}

	// ---- cabecalho de tela de app: [<] Titulo ----
	struct HeaderResult { Rect backBtn; Rect body; };
	inline HeaderResult DrawScreenHeader(Canvas& cv, const Metrics& m, const Rect& content, const std::string& title, bool showBack, bool backPressed)
	{
		const float hh = m.px(56);
		Rect bar{ content.x, content.y, content.w, hh };
		cv.fillRect(bar, Colors::Panel, 0);
		cv.fillRect({ bar.x, bar.bottom() - 1, bar.w, std::max(1.0f, m.px(1)) }, Colors::Line, 0);
		HeaderResult res;
		res.backBtn = showBack ? Rect{ bar.x + m.px(8), bar.y + m.px(6), m.px(44), hh - m.px(12) } : Rect{};
		float tx = bar.x + m.px(20);
		if (showBack)
		{
			if (backPressed) cv.fillRect(res.backBtn, Colors::PanelHi, m.px(10));
			Icons::Draw(cv, Icon::Back, res.backBtn.inset(m.px(11)), Colors::Text);
			tx = res.backBtn.right() + m.px(8);
		}
		DrawTextIn(cv, { tx, bar.y, bar.right() - tx - m.px(16), hh }, title, Colors::Text, m.font(Fonts::Header));
		res.body = { content.x, bar.bottom(), content.w, content.h - hh };
		return res;
	}

	// ---- rolagem vertical com inercia (logica pura; o app desenha os itens deslocados por -offset) ----
	class ScrollView
	{
	public:
		void setLayout(const Rect& viewport, float contentH)
		{
			m_vp = viewport; m_contentH = contentH;
			m_offset = std::clamp(m_offset, 0.0f, maxOffset());
			if (m_pinEnd) { m_offset = maxOffset(); m_pinEnd = false; }
		}
		float maxOffset() const { return std::max(0.0f, m_contentH - m_vp.h); }
		void jumpToEnd() { m_pinEnd = true; }          // no proximo setLayout vai para o fim (conversas, editor de texto)
		float offset() const { return m_offset; }
		const Rect& viewport() const { return m_vp; }
		bool dragging() const { return m_drag; }

		// retorna true se o toque comecou/continua dentro da area de rolagem
		bool onTouch(const Touch& t)
		{
			switch (t.kind)
			{
			case Touch::Down:      m_drag = false; m_inside = m_vp.contains(t.pos); if (m_inside) m_vel = 0; return m_inside;
			case Touch::DragBegin: m_drag = m_inside; return m_inside;
			case Touch::Drag:      if (m_drag) { m_offset = std::clamp(m_offset - t.delta.y, 0.0f, maxOffset()); } return m_drag;
			case Touch::DragEnd:   if (m_drag) m_vel = std::clamp(-t.velocity.y, -kMaxVel, kMaxVel); m_drag = false; return true;
			case Touch::Cancel: case Touch::Up: m_drag = false; return false;
			default: return false;
			}
		}
		void update(float dtMs)
		{
			if (m_drag || std::fabs(m_vel) < 8.0f) { if (!m_drag) m_vel = 0; return; }
			m_offset += m_vel * dtMs / 1000.0f;
			m_vel *= std::exp(-dtMs * 0.0045f);                         // atrito
			if (m_offset <= 0.0f) { m_offset = 0.0f; m_vel = 0; }
			else if (m_offset >= maxOffset()) { m_offset = maxOffset(); m_vel = 0; }
		}
		// retangulo de um item (y/h em coordenadas de CONTEUDO) na tela
		Rect itemRect(float y, float h) const { return { m_vp.x, m_vp.y + y - m_offset, m_vp.w, h }; }
		bool visible(const Rect& r) const { return r.bottom() > m_vp.y && r.y < m_vp.bottom(); }
	private:
		static constexpr float kMaxVel = 6000.0f;
		Rect m_vp; float m_contentH = 0, m_offset = 0, m_vel = 0; bool m_drag = false, m_inside = false, m_pinEnd = false;
	};
}
