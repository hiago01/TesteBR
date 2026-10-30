#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../log.h"

#include <algorithm>
#include <cmath>

extern CGame* pGame;
extern UI* pUI;

using namespace KbCore;

namespace
{
	const ImColor kBg        (14, 14, 20, 232);
	const ImColor kKey       (68, 70, 80, 242);
	const ImColor kKeySpecial(40, 42, 50, 242);
	const ImColor kKeyDown   (245, 145, 50, 255);
	const ImColor kSend      (214, 110, 20, 245);
	const ImColor kAccent    (0xF5, 0x91, 0x32, 255);
	const ImColor kField     (255, 255, 255, 58);
	const ImColor kBtn       (214, 8, 92, 245);          // vermelho/magenta dos botoes do chat
	const ImColor kBtnDown   (255, 80, 140, 255);
	const ImColor kWhite     (1.0f, 1.0f, 1.0f, 1.0f);
	const ImColor kHint      (200, 200, 200, 130);

	const long kRepeatDelayMs = 420;
	const long kRepeatEveryMs = 55;
}

/* Keyboard */

Keyboard::Keyboard()
{
	m_caller = nullptr;
	m_draftOwner = nullptr;
	m_cachedPage = Page::Lower;
	m_cachedW = m_cachedH = -1.0f;
	m_pressT = m_lastRepeatT = 0;
	m_backspaceFired = false;
	m_wantCaretFromTap = false;
	m_tapX = 0.0f;
	m_scroll = 0.0f;
	m_blinkBase = 0;
	m_core.open();
}

long Keyboard::nowMs() const { return (long)GetTickCount(); }

float Keyboard::inputH() const { return UISettings::keyboardRowHeight(); }
float Keyboard::rowH()   const { return UISettings::keyboardRowHeight() * 0.80f; }

// Ancora o teclado no rodape da tela, com a altura que ele realmente precisa.
void Keyboard::applyGeometry()
{
	const float w      = UISettings::keyboardSize().x;
	const float bottom = UISettings::keyboardPos().y + UISettings::keyboardSize().y;
	const float total  = inputH() + rowH() * 4.0f + rowH() * 0.12f;

	this->setFixedSize(ImVec2(w, total));
	this->setPosition(ImVec2(UISettings::keyboardPos().x, bottom - total));
	m_cachedW = -1.0f;       // forca recalcular as teclas
}

void Keyboard::performLayout()
{
	applyGeometry();
	// sem filhos: o teclado desenha e trata o toque sozinho
}

void Keyboard::ensureKeyRects()
{
	Page shown = m_core.shownPage();
	if (shown == m_cachedPage && m_cachedW == width() && m_cachedH == height() && !m_rects.empty()) return;

	m_cachedPage = shown;
	m_cachedW = width();
	m_cachedH = height();
	m_rows = BuildPage(shown);
	const float gap = std::max(3.0f, rowH() * 0.07f);
	m_rects = LayoutRows(m_rows, width(), rowH() * 4.0f, gap);
}

void Keyboard::inputRects(ImVec2& fa, ImVec2& fb, ImVec2 ba[3], ImVec2 bb[3]) const
{
	const ImVec2 p = absolutePosition();
	const float H  = inputH();
	const float bh = H * 0.78f, bw = bh * 1.35f;
	const float pad = H * 0.11f, gap = pad * 0.8f;
	const float top = p.y + (H - bh) * 0.5f;

	// ordem na tela: [campo ........] [v] [^] [/]
	float x = p.x + width() - pad - bw;
	for (int i = 2; i >= 0; i--)           // 2 = "/", 1 = "^", 0 = "v"
	{
		ba[i] = ImVec2(x, top);
		bb[i] = ImVec2(x + bw, top + bh);
		x -= (bw + gap);
	}
	fa = ImVec2(p.x + pad, p.y + (H - bh) * 0.5f);
	fb = ImVec2(x + gap - pad * 0.6f + 0.0f, p.y + (H - bh) * 0.5f + bh);
}

Keyboard::Hit Keyboard::hitTest(const ImVec2& pos) const
{
	Hit h;
	if (!contains(pos)) return h;

	const ImVec2 p = absolutePosition();
	if (pos.y < p.y + inputH())
	{
		ImVec2 fa, fb, ba[3], bb[3];
		inputRects(fa, fb, ba, bb);
		auto in = [&](const ImVec2& a, const ImVec2& b) { return pos.x >= a.x && pos.x < b.x && pos.y >= a.y && pos.y < b.y; };
		if (in(ba[0], bb[0])) h.target = Target::Newer;       // "v": volta para o mais novo
		else if (in(ba[1], bb[1])) h.target = Target::Older;  // "^": mensagem anterior
		else if (in(ba[2], bb[2])) h.target = Target::Slash;
		else if (in(fa, fb)) h.target = Target::Field;
		return h;
	}

	const float lx = pos.x - p.x;
	const float ly = pos.y - (p.y + inputH());
	for (size_t r = 0; r < m_rects.size() && r < m_rows.size(); r++)
		for (size_t c = 0; c < m_rects[r].size(); c++)
			if (m_rects[r][c].has(lx, ly) && m_rows[r][c].kind != Kind::Spacer)
			{
				h.target = Target::Key; h.row = (int)r; h.col = (int)c;
				return h;
			}
	return h;
}

void Keyboard::onAction(Action a)
{
	if (a == Action::TextChanged) m_blinkBase = nowMs();     // cursor sempre visivel logo apos digitar
	else if (a == Action::Send)   send();
}

void Keyboard::act(const Hit& h)
{
	switch (h.target)
	{
	case Target::Key:
		if (h.row >= 0 && h.row < (int)m_rows.size() && h.col >= 0 && h.col < (int)m_rows[h.row].size())
			onAction(m_core.press(m_rows[h.row][h.col], nowMs()));
		break;
	case Target::Older: onAction(m_core.historyOlder()); break;
	case Target::Newer: onAction(m_core.historyNewer()); break;
	case Target::Slash: onAction(m_core.slash());        break;
	default: break;
	}
}

void Keyboard::touchEvent(const ImVec2& pos, TouchType type)
{
	// fora do teclado (ou teclado fechado): comportamento padrao do Widget (toque fora = fechar)
	if (!visible() || (type == TouchType::push && !contains(pos)))
	{
		Widget::touchEvent(pos, type);
		return;
	}

	ensureKeyRects();

	if (type == TouchType::push)
	{
		m_pressed = hitTest(pos);
		m_pressT = m_lastRepeatT = nowMs();
		m_backspaceFired = false;

		// apagar age ao TOCAR (e repete segurando), como nos teclados normais
		if (m_pressed.target == Target::Key && m_pressed.row >= 0 &&
			m_rows[m_pressed.row][m_pressed.col].kind == Kind::Backspace)
		{
			act(m_pressed);
			m_backspaceFired = true;
		}
	}
	else if (type == TouchType::pop)
	{
		Hit up = hitTest(pos);
		Hit down = m_pressed;
		m_pressed = Hit();

		if (up.target != Target::None && up == down && !m_backspaceFired)
		{
			if (up.target == Target::Field) { m_wantCaretFromTap = true; m_tapX = pos.x; }
			else act(up);
		}
		m_backspaceFired = false;
	}
	// move: nada a fazer (a tecla so dispara se o dedo soltar em cima dela)
}

void Keyboard::activateEvent(bool active)
{
	if (active == false) this->hide();
}

void Keyboard::show(Widget* caller)
{
	applyGeometry();

	// rascunho: so volta se for o mesmo campo que o jogador tinha aberto (como o teclado do Android fazia)
	m_core.open();
	if (!m_draft.empty() && m_draftOwner == caller) m_core.field.set(m_draft);

	m_pressed = Hit();
	m_scroll = 0.0f;
	m_blinkBase = nowMs();
	m_caller = caller;

	bool wasVisible = visible();
	this->setVisible(true);
	if (!wasVisible) pGame->EnableGameInput(false);
}

void Keyboard::hide()
{
	close(true);
}

void Keyboard::close(bool saveDraft)
{
	if (!visible()) { m_caller = nullptr; return; }

	// fechou sem enviar: guarda o que estava digitado (ao enviar, nao guarda)
	if (saveDraft) { m_draft = m_core.field.text; m_draftOwner = m_caller; }
	else           { m_draft.clear();             m_draftOwner = nullptr; }

	this->setVisible(false);
	pGame->EnableGameInput(true);
	m_pressed = Hit();
	m_caller = nullptr;
}

void Keyboard::send()
{
	const std::string input = m_core.commit();      // bytes Latin-1; vai para o servidor como esta

	Widget* caller = m_caller;
	if (caller) caller->keyboardEvent(input);
	this->close(false);
}

// Legado: o caminho antigo (teclado do Android) entregava o texto por JNI. Mantido so para o
// simbolo JNI existir; trata como se tivesse sido digitado no teclado nativo.
void Keyboard::sendForGB(JNIEnv *pEnv, jobject thiz, jbyteArray str)
{
	if (!str) return;
	jsize len = pEnv->GetArrayLength(str);
	jbyte* elements = pEnv->GetByteArrayElements(str, NULL);
	if (!elements) return;
	std::string input((const char*)elements, (size_t)len);
	pEnv->ReleaseByteArrayElements(str, elements, JNI_ABORT);

	if (m_caller) m_caller->keyboardEvent(input);
	close(false);
}

/* ---- desenho ---- */

void Keyboard::drawKey(ImGuiRenderer* r, const KeyDef& k, const ImVec2& a, const ImVec2& b, bool pressed, float fs) const
{
	const float rd = std::max(4.0f, rowH() * 0.16f);
	const float w = b.x - a.x, h = b.y - a.y;

	bool active = false;
	if (k.kind == Kind::Shift) active = m_core.shift != ShiftState::Off;

	ImColor fill = kKey;
	if (k.kind != Kind::Char && k.kind != Kind::Space) fill = kKeySpecial;
	if (k.kind == Kind::Send) fill = kSend;
	if (active) fill = kAccent;
	if (pressed) fill = kKeyDown;
	r->drawRoundedRect(a, b, fill, rd);

	auto P = [&](float fx, float fy) { return ImVec2(a.x + w * fx, a.y + h * fy); };

	switch (k.kind)
	{
	case Kind::Shift:
	{
		// seta para cima: triangulo + haste
		r->drawTriangle(P(0.50f, 0.20f), P(0.74f, 0.50f), P(0.26f, 0.50f), kWhite, true);
		r->drawRect(P(0.42f, 0.48f), P(0.58f, 0.74f), kWhite, true);
		if (m_core.shift == ShiftState::Lock) r->drawRect(P(0.30f, 0.82f), P(0.70f, 0.87f), kWhite, true);
		break;
	}
	case Kind::Backspace:
	{
		ImVec2 pts[5] = { P(0.28f, 0.50f), P(0.42f, 0.28f), P(0.74f, 0.28f), P(0.74f, 0.72f), P(0.42f, 0.72f) };
		r->drawConvexPolyFilled(pts, 5, kWhite);
		const float t = std::max(2.0f, h * 0.05f);
		r->drawLine(P(0.50f, 0.40f), P(0.64f, 0.60f), kKeySpecial, t);
		r->drawLine(P(0.64f, 0.40f), P(0.50f, 0.60f), kKeySpecial, t);
		break;
	}
	case Kind::Space:
	{
		const float t = std::max(2.0f, h * 0.06f);
		r->drawLine(P(0.18f, 0.58f), P(0.82f, 0.58f), kWhite, t);
		r->drawLine(P(0.18f, 0.48f), P(0.18f, 0.58f), kWhite, t);
		r->drawLine(P(0.82f, 0.48f), P(0.82f, 0.58f), kWhite, t);
		break;
	}
	case Kind::Send:
		r->drawTriangle(P(0.34f, 0.26f), P(0.34f, 0.74f), P(0.70f, 0.50f), kWhite, true);
		break;
	case Kind::Spacer:
		break;
	default:
	{
		std::string label = k.label;
		if (k.kind == Kind::Char && m_core.page == Page::Accents)
			label = Latin1ToUtf8(std::string(1, (char)AccentCase(k.value, m_core.shift != ShiftState::Off)));
		if (label.empty()) break;
		float f = fs;
		ImVec2 sz = r->calculateTextSize(label, f);
		if (sz.x > w * 0.9f && sz.x > 0.0f) { f *= (w * 0.9f) / sz.x; sz = r->calculateTextSize(label, f); }
		r->drawText(ImVec2(a.x + (w - sz.x) * 0.5f, a.y + (h - sz.y) * 0.5f), kWhite, label, false, f);
		break;
	}
	}
}

void Keyboard::draw(ImGuiRenderer* renderer)
{
	if (!visible()) return;
	ensureKeyRects();

	const ImVec2 p = absolutePosition();
	renderer->drawRect(p, p + size(), kBg, true);
	renderer->drawLine(p, ImVec2(p.x + width(), p.y), kAccent, UISettings::outlineSize());

	const long now = nowMs();

	// segurar o backspace repete
	if (m_pressed.target == Target::Key && m_backspaceFired && m_pressed.row >= 0 &&
		now - m_pressT > kRepeatDelayMs && now - m_lastRepeatT > kRepeatEveryMs)
	{
		m_lastRepeatT = now;
		act(m_pressed);
	}

	// ---------- barra de entrada
	ImVec2 fa, fb, ba[3], bb[3];
	inputRects(fa, fb, ba, bb);

	const float fs = std::min(UISettings::fontSize() * 0.9f, rowH() * 0.58f);
	const float rd = std::max(4.0f, inputH() * 0.14f);

	renderer->drawRoundedRect(fa, fb, kField, rd);

	// texto (Latin-1 -> UTF-8) com rolagem para o cursor aparecer sempre
	const std::string& raw = m_core.field.text;
	const float padX = inputH() * 0.14f;
	const float visW = std::max(10.0f, (fb.x - fa.x) - padX * 2.0f);
	const std::string before = Latin1ToUtf8(raw.substr(0, std::min(m_core.field.caret, raw.size())));
	const float caretW = before.empty() ? 0.0f : renderer->calculateTextSize(before, fs).x;

	if (m_wantCaretFromTap)
	{
		m_wantCaretFromTap = false;
		const float target = (m_tapX - (fa.x + padX)) + m_scroll;
		size_t best = 0; float bestD = 1e9f;
		for (size_t i = 0; i <= raw.size(); i++)
		{
			std::string pre = Latin1ToUtf8(raw.substr(0, i));
			float wpx = pre.empty() ? 0.0f : renderer->calculateTextSize(pre, fs).x;
			float d = std::fabs(wpx - target);
			if (d < bestD) { bestD = d; best = i; }
		}
		m_core.field.setCaret(best);
		m_blinkBase = now;
	}
	if (caretW - m_scroll > visW) m_scroll = caretW - visW;
	if (caretW - m_scroll < 0.0f) m_scroll = caretW;
	if (raw.empty()) m_scroll = 0.0f;

	renderer->pushClipRect(ImVec2(fa.x + padX * 0.5f, fa.y), ImVec2(fb.x - padX * 0.5f, fb.y), true);
	const float textY = fa.y + ((fb.y - fa.y) - fs) * 0.5f;
	if (raw.empty())
		renderer->drawText(ImVec2(fa.x + padX, textY), kHint, std::string("Digite a mensagem"), false, fs);
	else
		renderer->drawText(ImVec2(fa.x + padX - m_scroll, textY), kWhite, Latin1ToUtf8(raw), false, fs);

	if (((now - m_blinkBase) / 450) % 2 == 0)
	{
		const float cx = fa.x + padX - m_scroll + caretW;
		renderer->drawLine(ImVec2(cx, fa.y + (fb.y - fa.y) * 0.18f), ImVec2(cx, fb.y - (fb.y - fa.y) * 0.18f),
		                   kWhite, std::max(2.0f, UISettings::outlineSize()));
	}
	renderer->popClipRect();

	// ---------- botoes: v  ^  /
	const Target tg[3] = { Target::Newer, Target::Older, Target::Slash };
	const float th = std::max(3.0f, inputH() * 0.045f);
	for (int i = 0; i < 3; i++)
	{
		const bool down = (m_pressed.target == tg[i]);
		renderer->drawRoundedRect(ba[i], bb[i], down ? kBtnDown : kBtn, rd);

		const float w = bb[i].x - ba[i].x, h = bb[i].y - ba[i].y;
		auto P = [&](float fx, float fy) { return ImVec2(ba[i].x + w * fx, ba[i].y + h * fy); };
		if (i == 0)       { ImVec2 v[3] = { P(0.30f, 0.38f), P(0.50f, 0.64f), P(0.70f, 0.38f) }; renderer->drawPolyline(v, 3, kWhite, false, th); }
		else if (i == 1)  { ImVec2 v[3] = { P(0.30f, 0.62f), P(0.50f, 0.36f), P(0.70f, 0.62f) }; renderer->drawPolyline(v, 3, kWhite, false, th); }
		else              { renderer->drawLine(P(0.40f, 0.76f), P(0.60f, 0.24f), kWhite, th); }
	}

	// ---------- teclas
	const float baseY = p.y + inputH();
	for (size_t r = 0; r < m_rects.size() && r < m_rows.size(); r++)
		for (size_t c = 0; c < m_rects[r].size() && c < m_rows[r].size(); c++)
		{
			const KeyDef& k = m_rows[r][c];
			if (k.kind == Kind::Spacer) continue;
			const Rect& rc = m_rects[r][c];
			const bool down = (m_pressed.target == Target::Key && m_pressed.row == (int)r && m_pressed.col == (int)c);
			drawKey(renderer, k, ImVec2(p.x + rc.x0, baseY + rc.y0), ImVec2(p.x + rc.x1, baseY + rc.y1), down, fs);
		}
}
