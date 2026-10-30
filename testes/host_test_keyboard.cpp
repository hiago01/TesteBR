// Teste de PC da logica do teclado nativo (KeyboardCore.h). Sem ImGui, sem Android.
//   g++ -std=c++14 -I app/src/main/cpp/samp/gui/samp_widgets testes/host_test_keyboard.cpp -o /tmp/kbt && /tmp/kbt
#include "KeyboardCore.h"
#include <cstdio>
#include <cstdlib>
using namespace KbCore;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const KeyDef* find(const std::vector<Row>& rows, const std::string& label)
{
	for (const Row& r : rows) for (const KeyDef& k : r) if (k.label == label) return &k;
	return nullptr;
}
static void tap(Machine& m, const std::string& label, long t = 0)
{
	auto rows = BuildPage(m.shownPage());
	const KeyDef* k = find(rows, label);
	if (!k) { std::printf("tecla '%s' nao existe na pagina %d\n", label.c_str(), (int)m.shownPage()); g_fail++; return; }
	m.press(*k, t);
}
static void tapKind(Machine& m, Kind kd, long t = 0)
{
	auto rows = BuildPage(m.shownPage());
	for (const Row& r : rows) for (const KeyDef& k : r) if (k.kind == kd) { m.press(k, t); return; }
	std::printf("tecla de tipo %d nao existe\n", (int)kd); g_fail++;
}

int main()
{
	// --- Latin-1 -> UTF-8
	CHECK(Latin1ToUtf8("abc") == "abc");
	CHECK(Latin1ToUtf8(std::string(1, (char)0xE7)) == "\xC3\xA7");           // ç
	CHECK(Latin1ToUtf8(std::string(1, (char)0xC3)) == "\xC3\x83");           // Ã
	CHECK(Latin1ToUtf8(std::string(1, (char)0xAA)) == "\xC2\xAA");           // ª

	// --- digitar e apagar
	Machine m; m.open();
	tap(m, "o"); tap(m, "i");
	CHECK(m.field.text == "oi");
	tapKind(m, Kind::Backspace);
	CHECK(m.field.text == "o");
	tapKind(m, Kind::Backspace); tapKind(m, Kind::Backspace);                // apagar vazio nao quebra
	CHECK(m.field.text.empty() && m.field.caret == 0);

	// --- Shift uma vez: so a proxima letra
	m.open();
	tapKind(m, Kind::Shift, 1000);
	CHECK(m.shift == ShiftState::Once && m.shownPage() == Page::Upper);
	tap(m, "A"); tap(m, "b");
	CHECK(m.field.text == "Ab");
	CHECK(m.shift == ShiftState::Off && m.shownPage() == Page::Lower);

	// --- Shift duplo = CAPS LOCK; depois toque desliga
	m.open();
	tapKind(m, Kind::Shift, 1000); tapKind(m, Kind::Shift, 1200);
	CHECK(m.shift == ShiftState::Lock);
	tap(m, "O"); tap(m, "K");
	CHECK(m.field.text == "OK" && m.shift == ShiftState::Lock);
	tapKind(m, Kind::Shift, 5000);
	CHECK(m.shift == ShiftState::Off);
	// shift duas vezes devagar: liga e desliga (nao vira lock)
	m.open();
	tapKind(m, Kind::Shift, 1000); tapKind(m, Kind::Shift, 3000);
	CHECK(m.shift == ShiftState::Off);

	// --- acentos pt-BR: ç direto no teclado
	m.open();
	tap(m, "\xC3\xA7");                                                       // ç
	CHECK(m.field.text == std::string(1, (char)0xE7));
	// pagina de acentos: ã, volta sozinho para as letras
	m.open();
	tap(m, "\xC3\xA1\xC3\xA3");                                               // tecla "áã"
	CHECK(m.page == Page::Accents);
	tap(m, "\xC3\xA3");                                                       // ã
	CHECK(m.field.text == std::string(1, (char)0xE3) && m.page == Page::Lower);
	// acento em MAIUSCULA com shift
	m.open();
	tap(m, "\xC3\xA1\xC3\xA3");
	tapKind(m, Kind::Shift, 100);
	// (a pagina guarda minusculas; o rotulo mostrado e convertido pela UI)
	{
		Machine q; q.open();
		auto rows = BuildPage(Page::Accents);
		q.page = Page::Accents; q.shift = ShiftState::Once;
		for (const Row& r : rows) for (const KeyDef& k : r) if (k.kind == Kind::Char && k.value == 0xE3) q.press(k, 0);
		CHECK(q.field.text == std::string(1, (char)0xC3));                    // Ã
		CHECK(q.shift == ShiftState::Off);
	}
	CHECK(AccentCase(0xE3, true) == 0xC3 && AccentCase(0xC3, false) == 0xE3);
	CHECK(AccentCase(0xAA, true) == 0xAA && AccentCase(0xBA, true) == 0xBA);   // ª º nao tem par
	CHECK(AccentCase(0xF7, true) == 0xF7);                                     // divisao nao vira nada

	// --- paginas de simbolos e volta
	m.open();
	tap(m, "?123");
	CHECK(m.shownPage() == Page::Symbols);
	tap(m, "1"); tap(m, "@");
	CHECK(m.field.text == "1@");
	tap(m, "ABC");
	CHECK(m.shownPage() == Page::Lower);

	// --- barra "/" nao apaga o que ja esta no campo e vai no cursor
	m.open();
	tap(m, "a"); tap(m, "b");
	m.field.setCaret(1);
	m.slash();
	CHECK(m.field.text == "a/b" && m.field.caret == 2);

	// --- limite de tamanho e controles
	m.open();
	m.field.maxLen = 3;
	tap(m, "a"); tap(m, "b"); tap(m, "c"); tap(m, "d");
	CHECK(m.field.text == "abc");
	CHECK(!m.field.insert('\n') && !m.field.insert(0x01));
	m.field.maxLen = 144;

	// --- historico: enviar 3, subir ate o mais antigo, descer ate o rascunho
	m.open();
	m.field.set("/a"); m.commit();
	m.field.set("/b"); m.commit();
	m.field.set("/c"); m.commit();
	m.field.clear();
	m.field.set("rascunho");
	CHECK(m.historyOlder() == Action::TextChanged && m.field.text == "/c");
	CHECK(m.historyOlder() == Action::TextChanged && m.field.text == "/b");
	CHECK(m.historyOlder() == Action::TextChanged && m.field.text == "/a");
	CHECK(m.historyOlder() == Action::None && m.field.text == "/a");           // fim da lista
	CHECK(m.historyNewer() == Action::TextChanged && m.field.text == "/b");
	CHECK(m.historyNewer() == Action::TextChanged && m.field.text == "/c");
	CHECK(m.historyNewer() == Action::TextChanged && m.field.text == "rascunho");
	CHECK(m.historyNewer() == Action::None);
	// duplicado seguido nao entra duas vezes
	{ History h; h.add("x"); h.add("x"); CHECK(h.size() == 1); h.add(""); CHECK(h.size() == 1); }
	// limite
	{ History h; for (int i = 0; i < 50; i++) h.add("m" + std::to_string(i)); CHECK(h.size() == History::MAX); }

	// --- toda pagina tem 4 linhas e todas as teclas de caractere sao imprimiveis; nada fora da largura
	Page all[] = { Page::Lower, Page::Upper, Page::Symbols, Page::Symbols2, Page::Accents };
	for (Page p : all)
	{
		auto rows = BuildPage(p);
		CHECK(rows.size() == 4);
		bool hasSend = false, hasBack = false, hasSpace = false;
		for (const Row& r : rows) for (const KeyDef& k : r)
		{
			if (k.kind == Kind::Send) hasSend = true;
			if (k.kind == Kind::Backspace) hasBack = true;
			if (k.kind == Kind::Space) hasSpace = true;
			if (k.kind == Kind::Char) CHECK(k.value >= 0x20 && k.value != 0x7F && !k.label.empty());
		}
		CHECK(hasSend && hasBack && hasSpace);                                 // sempre da pra enviar, apagar e dar espaco
		auto lay = LayoutRows(rows, 1000, 200, 4);
		CHECK(lay.size() == 4);
		for (size_t r = 0; r < lay.size(); r++)
		{
			float prev = -1;
			for (const Rect& rc : lay[r])
			{
				CHECK(rc.x0 >= 0 && rc.x1 <= 1000.01f && rc.y0 >= 0 && rc.y1 <= 200.01f);
				CHECK(rc.x1 > rc.x0 && rc.y1 > rc.y0);
				CHECK(rc.x0 >= prev - 0.01f);                                  // sem sobreposicao
				prev = rc.x1;
			}
		}
	}
	// toda tecla acha-se a si mesma pelo toque no centro (hit-test)
	{
		auto rows = BuildPage(Page::Lower);
		auto lay = LayoutRows(rows, 1000, 200, 4);
		for (size_t r = 0; r < rows.size(); r++)
			for (size_t i = 0; i < rows[r].size(); i++)
			{
				const Rect& rc = lay[r][i];
				float cx = (rc.x0 + rc.x1) / 2, cy = (rc.y0 + rc.y1) / 2;
				int hits = 0;
				for (size_t j = 0; j < rows[r].size(); j++) if (lay[r][j].has(cx, cy)) hits++;
				CHECK(hits == 1);
			}
	}
	// Enviar
	m.open(); tap(m, "o"); 
	{ auto rows = BuildPage(Page::Lower); const KeyDef* s = nullptr; for (auto& r : rows) for (auto& k : r) if (k.kind == Kind::Send) s = &k; CHECK(m.press(*s, 0) == Action::Send); }

	std::printf("%d verificacoes, %d falhas\n", g_n, g_fail);
	return g_fail ? 1 : 0;
}
