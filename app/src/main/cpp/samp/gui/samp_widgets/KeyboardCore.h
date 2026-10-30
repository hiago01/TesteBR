#pragma once
// ============================================================================
//  KeyboardCore.h - LOGICA do teclado nativo do chat (sem ImGui, sem Android).
//
//  Tudo que decide "o que acontece quando toco em X" fica aqui, para poder ser testado no PC
//  (testes/host_test_keyboard.cpp) sem celular. O desenho/toque fica em keyboard.cpp.
//
//  Texto: UM BYTE POR CARACTERE, em Latin-1/CP1252 (a, ç = 0xE7, ã = 0xE3 ...), que e o que o
//  .pwn do servidor (salvo em ANSI) espera. Para DESENHAR, Latin1ToUtf8() converte.
// ============================================================================
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace KbCore {

// ---------------------------------------------------------------- texto
inline std::string Latin1ToUtf8(const std::string& s)
{
	std::string o;
	o.reserve(s.size() + 8);
	for (unsigned char c : s)
	{
		if (c < 0x80) o += (char)c;
		else if (c >= 0xA0) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
		else o += '?';                                   // 0x80-0x9F: nao digitavel neste teclado
	}
	return o;
}

// Campo de texto com cursor.
struct TextField
{
	std::string text;
	size_t      caret  = 0;
	size_t      maxLen = 144;

	void clear() { text.clear(); caret = 0; }
	void set(const std::string& s) { text = s.substr(0, maxLen); caret = text.size(); }

	bool insert(unsigned char c)
	{
		if (c < 0x20 || c == 0x7F) return false;          // nunca controle
		if (text.size() >= maxLen) return false;
		if (caret > text.size()) caret = text.size();
		text.insert(text.begin() + (std::ptrdiff_t)caret, (char)c);
		caret++;
		return true;
	}
	bool backspace()
	{
		if (caret == 0 || text.empty()) return false;
		if (caret > text.size()) caret = text.size();
		text.erase(caret - 1, 1);
		caret--;
		return true;
	}
	void setCaret(size_t p) { caret = p > text.size() ? text.size() : p; }
};

// ---------------------------------------------------------------- historico (setas do chat)
//  idx 0 = o que estava sendo digitado (rascunho); idx 1 = ultima enviada; idx 2 = a anterior...
//  older()  = seta para CIMA   (mensagem mais antiga)
//  newer()  = seta para BAIXO  (volta em direcao ao rascunho)
class History
{
public:
	static const size_t MAX = 20;

	void add(const std::string& s)
	{
		if (s.empty()) return;
		if (!m_items.empty() && m_items.front() == s) { resetNav(); return; }
		m_items.insert(m_items.begin(), s);
		if (m_items.size() > MAX) m_items.pop_back();
		resetNav();
	}
	void resetNav() { m_idx = 0; m_draft.clear(); }
	size_t size() const { return m_items.size(); }

	// 'current' = texto atual do campo (guardado como rascunho ao sair do indice 0).
	// Devolve true e preenche 'out' se o campo deve mudar.
	bool older(const std::string& current, std::string& out)
	{
		if (m_idx >= m_items.size()) return false;
		if (m_idx == 0) m_draft = current;
		m_idx++;
		out = m_items[m_idx - 1];
		return true;
	}
	bool newer(std::string& out)
	{
		if (m_idx == 0) return false;
		m_idx--;
		out = (m_idx == 0) ? m_draft : m_items[m_idx - 1];
		return true;
	}

private:
	std::vector<std::string> m_items;
	size_t      m_idx = 0;
	std::string m_draft;
};

// ---------------------------------------------------------------- teclas e paginas
enum class Page { Lower, Upper, Symbols, Symbols2, Accents };
enum class Kind { Char, Shift, Backspace, Space, Send, GoPage, Spacer };

struct KeyDef
{
	std::string label;      // texto desenhado (UTF-8); vazio = icone desenhado por codigo
	unsigned char value;    // Kind::Char: byte inserido
	Kind  kind;
	float weight;           // largura relativa na linha
	Page  target;           // Kind::GoPage: pagina de destino
};

using Row = std::vector<KeyDef>;

inline KeyDef C(unsigned char v, float w = 1.0f)
{
	return KeyDef{ Latin1ToUtf8(std::string(1, (char)v)), v, Kind::Char, w, Page::Lower };
}
inline KeyDef Go(const char* label, Page p, float w) { return KeyDef{ label, 0, Kind::GoPage, w, p }; }
inline KeyDef Sp(float w)                             { return KeyDef{ "", 0, Kind::Spacer, w, Page::Lower }; }
inline KeyDef Ic(Kind k, float w)                     { return KeyDef{ "", 0, k, w, Page::Lower }; }

inline Row Letters(const char* s)
{
	Row r;
	for (const char* p = s; *p; ++p) r.push_back(C((unsigned char)*p));
	return r;
}

// Devolve as 4 linhas de uma pagina.
inline std::vector<Row> BuildPage(Page page)
{
	std::vector<Row> rows(4);
	switch (page)
	{
	case Page::Lower:
	case Page::Upper:
	{
		const bool up = (page == Page::Upper);
		rows[0] = Letters(up ? "QWERTYUIOP" : "qwertyuiop");
		rows[1] = Letters(up ? "ASDFGHJKL"  : "asdfghjkl");
		rows[1].push_back(C(up ? 0xC7 : 0xE7));                          // Ç / ç
		rows[2].push_back(Ic(Kind::Shift, 1.5f));
		{ Row z = Letters(up ? "ZXCVBNM" : "zxcvbnm"); rows[2].insert(rows[2].end(), z.begin(), z.end()); }
		rows[2].push_back(Ic(Kind::Backspace, 1.5f));
		rows[3] = { Go("?123", Page::Symbols, 1.5f), Go("\xC3\xA1\xC3\xA3", Page::Accents, 1.4f),   // "áã"
		            C(','), Ic(Kind::Space, 4.0f), C('.'), Ic(Kind::Send, 1.6f) };
		break;
	}
	case Page::Symbols:
		rows[0] = Letters("1234567890");
		rows[1] = Letters("@#$%&-+()/");
		rows[2].push_back(Go("=\\<", Page::Symbols2, 1.5f));
		{ Row z = Letters("*\"':;!?"); rows[2].insert(rows[2].end(), z.begin(), z.end()); }
		rows[2].push_back(Ic(Kind::Backspace, 1.5f));
		rows[3] = { Go("ABC", Page::Lower, 1.5f), C(','), Ic(Kind::Space, 4.5f), C('.'), C('_'), Ic(Kind::Send, 1.6f) };
		break;
	case Page::Symbols2:
		rows[0] = Letters("~`|^\\{}[]<");
		rows[1] = Letters(">=_*#%&+-/");
		rows[2].push_back(Go("?123", Page::Symbols, 1.5f));
		{ Row z = Letters("\"':;!?,."); rows[2].insert(rows[2].end(), z.begin(), z.end()); }
		rows[2].push_back(Ic(Kind::Backspace, 1.5f));
		rows[3] = { Go("ABC", Page::Lower, 1.5f), Ic(Kind::Space, 6.0f), C('.'), Ic(Kind::Send, 1.6f) };
		break;
	case Page::Accents:
	{
		// minusculas; Shift ligado = MAIUSCULAS (ver AccentChar)
		static const unsigned char r0[10] = { 0xE1, 0xE0, 0xE2, 0xE3, 0xE9, 0xEA, 0xED, 0xF3, 0xF4, 0xF5 };  // a` ... o~
		static const unsigned char r1[10] = { 0xFA, 0xFC, 0xE7, 0xAA, 0xBA, 0xB0, 0xA7, 0xAB, 0xBB, 0xA1 };  // u' u: c, a o deg sec << >> !
		for (int i = 0; i < 10; i++) rows[0].push_back(C(r0[i]));
		for (int i = 0; i < 10; i++) rows[1].push_back(C(r1[i]));
		rows[2] = { Ic(Kind::Shift, 1.5f), C(0xF1), C(0xBF), Sp(5.0f), Ic(Kind::Backspace, 1.5f) };      // n~  ?
		rows[3] = { Go("ABC", Page::Lower, 1.5f), Go("?123", Page::Symbols, 1.4f), Ic(Kind::Space, 5.5f), Ic(Kind::Send, 1.6f) };
		break;
	}
	}
	return rows;
}

// Letra acentuada em maiuscula/minuscula (so muda quem tem par; os demais ficam iguais).
inline unsigned char AccentCase(unsigned char c, bool upper)
{
	if (upper) { if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return (unsigned char)(c - 0x20); return c; }
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (unsigned char)(c + 0x20);
	return c;
}

// ---------------------------------------------------------------- estado do teclado
enum class ShiftState { Off, Once, Lock };

// Resultado de tocar numa tecla.
enum class Action { None, TextChanged, Send };

class Machine
{
public:
	TextField field;
	History   history;
	Page      page  = Page::Lower;
	ShiftState shift = ShiftState::Off;

	void open()
	{
		field.clear();
		history.resetNav();
		page = Page::Lower;
		shift = ShiftState::Off;
		m_lastShiftMs = -100000;
	}

	// Pagina que deve ser DESENHADA agora (Lower/Upper conforme o Shift; Accents segue o Shift).
	Page shownPage() const
	{
		if (page == Page::Lower || page == Page::Upper) return shift == ShiftState::Off ? Page::Lower : Page::Upper;
		return page;
	}

	Action press(const KeyDef& k, long nowMs)
	{
		switch (k.kind)
		{
		case Kind::Char:
		{
			unsigned char v = k.value;
			if (page == Page::Accents) v = AccentCase(v, shift != ShiftState::Off);
			else if ((page == Page::Lower || page == Page::Upper) && shift != ShiftState::Off && v >= 'a' && v <= 'z') v = (unsigned char)(v - 32);
			bool ok = field.insert(v);
			if (shift == ShiftState::Once) shift = ShiftState::Off;
			if (page == Page::Accents) page = Page::Lower;              // acento digitado: volta as letras
			return ok ? Action::TextChanged : Action::None;
		}
		case Kind::Backspace: return field.backspace() ? Action::TextChanged : Action::None;
		case Kind::Space:
		{
			bool ok = field.insert(' ');
			if (shift == ShiftState::Once) shift = ShiftState::Off;
			return ok ? Action::TextChanged : Action::None;
		}
		case Kind::Send: return Action::Send;
		case Kind::Shift:
			if (shift == ShiftState::Lock) shift = ShiftState::Off;
			else if (shift == ShiftState::Once && (nowMs - m_lastShiftMs) < 400) shift = ShiftState::Lock;   // toque duplo
			else if (shift == ShiftState::Once) shift = ShiftState::Off;
			else shift = ShiftState::Once;
			m_lastShiftMs = nowMs;
			return Action::None;
		case Kind::GoPage:
			page = k.target;
			return Action::None;
		case Kind::Spacer: break;
		}
		return Action::None;
	}

	// Botao "/" da barra do campo: coloca "/" no cursor (nao apaga o que ja foi digitado).
	Action slash() { return field.insert('/') ? Action::TextChanged : Action::None; }

	Action historyOlder()
	{
		std::string out;
		if (!history.older(field.text, out)) return Action::None;
		field.set(out);
		return Action::TextChanged;
	}
	Action historyNewer()
	{
		std::string out;
		if (!history.newer(out)) return Action::None;
		field.set(out);
		return Action::TextChanged;
	}

	// Chamado ao enviar: guarda no historico e devolve o texto (bytes Latin-1).
	std::string commit()
	{
		std::string t = field.text;
		history.add(t);
		return t;
	}

private:
	long m_lastShiftMs = -100000;
};

// ---------------------------------------------------------------- geometria das teclas (testavel)
struct Rect { float x0, y0, x1, y1; bool has(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; } };

// Distribui as teclas de 'rows' dentro de (w x h), com margem 'gap' entre elas.
// Devolve, na mesma ordem de linhas/teclas, o retangulo de cada tecla (Spacer ocupa espaco mas nao e tocavel).
inline std::vector<std::vector<Rect>> LayoutRows(const std::vector<Row>& rows, float w, float h, float gap)
{
	std::vector<std::vector<Rect>> out;
	if (rows.empty() || w <= 0 || h <= 0) return out;
	const float rowH = h / (float)rows.size();
	for (size_t r = 0; r < rows.size(); r++)
	{
		float total = 0;
		for (const KeyDef& k : rows[r]) total += k.weight;
		// a linha 1 das letras (9 teclas + ç = 10) e a do simbolo ocupam a largura toda; linhas curtas ficam centralizadas
		float unit = w / (total > 10.0f ? total : 10.0f);
		float rowW = unit * total;
		float x = (w - rowW) * 0.5f;
		std::vector<Rect> line;
		for (const KeyDef& k : rows[r])
		{
			float kw = unit * k.weight;
			line.push_back(Rect{ x + gap * 0.5f, r * rowH + gap * 0.5f, x + kw - gap * 0.5f, (r + 1) * rowH - gap * 0.5f });
			x += kw;
		}
		out.push_back(line);
	}
	return out;
}

} // namespace KbCore
