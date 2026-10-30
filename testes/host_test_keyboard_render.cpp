// Teste de PC: roda o teclado nativo de verdade (desenho + toque) com ImGui sem tela.
#define private public
#define protected public
#include "samp/settings.h"
#include "samp/gui/gui.h"
#undef private
#undef protected
#include <cstdio>
#include <cstdlib>
#include <cmath>

static uint32_t g_now = 1000;
uint32_t GetTickCount() { return g_now; }
static bool g_gameInput = true;
#include "samp/game/game.h"
void CGame::EnableGameInput(bool b) { g_gameInput = b; }
CGame* pGame = nullptr;
CSettings* pSettings = nullptr;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHOU linha %d: %s\n", __LINE__, #c); } } while (0)

struct Caller : Widget { std::string got; int n = 0; void keyboardEvent(const std::string& s) override { got = s; n++; } };

static ImGuiRenderer* g_r = nullptr;
static ImVec2 g_disp;
static const char* g_dumpPrefix = nullptr;   // KB_DUMP=/caminho/prefixo  -> grava o quadro para virar imagem (ver tools_host/kb_dump_to_png.py)
static unsigned char* g_atlas = nullptr; static int g_atw = 0, g_ath = 0;
static void dumpFrame(ImDrawList* dl, const char* tag)
{
	if (!g_dumpPrefix) return;
	char path[512]; std::snprintf(path, sizeof path, "%s_%s.bin", g_dumpPrefix, tag);
	FILE* f = std::fopen(path, "wb"); if (!f) return;
	int hdr[4] = { (int)g_disp.x, (int)g_disp.y, g_atw, g_ath };
	std::fwrite(hdr, 4, 4, f); std::fwrite(g_atlas, 1, (size_t)g_atw * g_ath * 4, f);
	int nv = dl->VtxBuffer.Size, ni = dl->IdxBuffer.Size, nc = dl->CmdBuffer.Size;
	std::fwrite(&nv, 4, 1, f); std::fwrite(dl->VtxBuffer.Data, sizeof(ImDrawVert), nv, f);
	std::fwrite(&ni, 4, 1, f); std::fwrite(dl->IdxBuffer.Data, sizeof(ImDrawIdx), ni, f);
	std::fwrite(&nc, 4, 1, f);
	for (int i = 0; i < nc; i++) { float c[4] = { dl->CmdBuffer[i].ClipRect.x, dl->CmdBuffer[i].ClipRect.y, dl->CmdBuffer[i].ClipRect.z, dl->CmdBuffer[i].ClipRect.w }; unsigned e = dl->CmdBuffer[i].ElemCount; std::fwrite(c, 4, 4, f); std::fwrite(&e, 4, 1, f); }
	std::fclose(f);
}

// desenha um quadro e confere que TODO vertice fica dentro da tela
static void frame(Keyboard* kb, const char* tag)
{
	ImGui::NewFrame();
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	g_r = new ImGuiRenderer(dl, ImGui::GetFont());
	kb->draw(g_r);
	int bad = 0;
	// vertices fora da tela so contam se o comando NAO estiver recortado (clip) dentro do campo
	{
		unsigned idxOff = 0;
		for (int ci = 0; ci < dl->CmdBuffer.Size; ci++)
		{
			const ImDrawCmd& cmd = dl->CmdBuffer[ci];
			const bool clipped = cmd.ClipRect.x > 0.5f || cmd.ClipRect.y > 0.5f || cmd.ClipRect.z < g_disp.x - 0.5f || cmd.ClipRect.w < g_disp.y - 0.5f;
			CHECK(cmd.ClipRect.z <= g_disp.x + 1.0f + 16384.0f);   // (so existe)
			for (unsigned k = 0; k < cmd.ElemCount; k++)
			{
				const ImVec2& p = dl->VtxBuffer[dl->IdxBuffer[idxOff + k]].pos;
				if (!clipped && (p.x < -1.0f || p.y < -1.0f || p.x > g_disp.x + 1.0f || p.y > g_disp.y + 1.0f)) bad++;
			}
			idxOff += cmd.ElemCount;
		}
	}
	if (bad) std::printf("  [%s] %d vertices fora da tela\n", tag, bad);
	CHECK(bad == 0);
	CHECK(dl->VtxBuffer.Size > 0 || !kb->visible());
	dumpFrame(dl, tag);
	delete g_r;
	ImGui::EndFrame();
}

static void touch(Keyboard* kb, ImVec2 p)
{
	kb->touchEvent(p, TouchType::push);
	kb->touchEvent(p, TouchType::pop);
}

static bool tapLabel(Keyboard* kb, const std::string& label)
{
	kb->ensureKeyRects();
	const ImVec2 o = kb->absolutePosition();
	for (size_t r = 0; r < kb->m_rows.size(); r++)
		for (size_t c = 0; c < kb->m_rows[r].size(); c++)
			if (kb->m_rows[r][c].label == label)
			{
				const KbCore::Rect& rc = kb->m_rects[r][c];
				touch(kb, ImVec2(o.x + (rc.x0 + rc.x1) / 2, o.y + kb->inputH() + (rc.y0 + rc.y1) / 2));
				return true;
			}
	std::printf("tecla '%s' nao encontrada\n", label.c_str()); g_fail++; return false;
}
static bool tapKind(Keyboard* kb, KbCore::Kind kd)
{
	kb->ensureKeyRects();
	const ImVec2 o = kb->absolutePosition();
	for (size_t r = 0; r < kb->m_rows.size(); r++)
		for (size_t c = 0; c < kb->m_rows[r].size(); c++)
			if (kb->m_rows[r][c].kind == kd)
			{
				const KbCore::Rect& rc = kb->m_rects[r][c];
				touch(kb, ImVec2(o.x + (rc.x0 + rc.x1) / 2, o.y + kb->inputH() + (rc.y0 + rc.y1) / 2));
				return true;
			}
	std::printf("tecla tipo %d nao encontrada\n", (int)kd); g_fail++; return false;
}
static void tapButton(Keyboard* kb, int i)   // 0=v 1=^ 2=/
{
	ImVec2 fa, fb, ba[3], bb[3];
	kb->inputRects(fa, fb, ba, bb);
	touch(kb, ImVec2((ba[i].x + bb[i].x) / 2, (ba[i].y + bb[i].y) / 2));
}

int main(int argc, char** argv)
{
	float W = argc > 1 ? (float)atof(argv[1]) : 2400, H = argc > 2 ? (float)atof(argv[2]) : 1080;
	g_disp = ImVec2(W, H);
	CSettings cs; pSettings = &cs;
	CGame game; pGame = &game;

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = g_disp;
	io.Fonts->AddFontDefault();
	unsigned char* px; int tw, th; io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
	g_atlas = px; g_atw = tw; g_ath = th; g_dumpPrefix = getenv("KB_DUMP");
	io.Fonts->TexID = (ImTextureID)1;
	io.DeltaTime = 1.0f / 60.0f;

	UISettings::Initialize(g_disp);

	Keyboard* kb = new Keyboard();
	kb->setFixedSize(UISettings::keyboardSize());
	kb->setPosition(UISettings::keyboardPos());
	kb->setVisible(false);
	kb->performLayout();
	Caller* chat = new Caller();

	std::printf("tela %.0fx%.0f  teclado: pos(%.0f,%.0f) tam(%.0fx%.0f) linha=%.0f\n", W, H,
	            kb->absolutePosition().x, kb->absolutePosition().y, kb->width(), kb->height(), UISettings::keyboardRowHeight());

	// --- geometria
	CHECK(kb->absolutePosition().x >= 0 && kb->absolutePosition().y >= 0);
	CHECK(std::fabs(kb->absolutePosition().y + kb->height() - H) < 1.5f);          // encostado no rodape
	CHECK(kb->height() < H * 0.62f);                                                // nao cobre mais da tela que o antigo (62%)
	CHECK(kb->width() <= W + 1.0f);

	// --- abrir
	kb->show(chat);
	CHECK(kb->visible() && !g_gameInput);
	frame(kb, "aberto");

	// --- digitar "oi" e enviar
	tapLabel(kb, "o"); tapLabel(kb, "i");
	CHECK(kb->m_core.field.text == "oi");
	frame(kb, "oi");
	tapKind(kb, KbCore::Kind::Send);
	CHECK(chat->n == 1 && chat->got == "oi");
	CHECK(!kb->visible() && g_gameInput);

	// --- "ação" com ç direto e ã pela pagina de acentos
	kb->show(chat);
	tapLabel(kb, "a");
	tapLabel(kb, "\xC3\xA7");                                                       // ç
	tapLabel(kb, "\xC3\xA1\xC3\xA3");                                               // abre acentos
	frame(kb, "acentos");
	tapLabel(kb, "\xC3\xA3");                                                       // ã
	tapLabel(kb, "o");
	{ std::string t = kb->m_core.field.text; std::printf("DBG campo='"); for (unsigned char c : t) std::printf("%02X ", c); std::printf("' page=%d\n", (int)kb->m_core.page); }
	CHECK(kb->m_core.field.text == std::string("a") + (char)0xE7 + (char)0xE3 + "o");
	frame(kb, "acao");
	tapKind(kb, KbCore::Kind::Send);
	CHECK(chat->got == std::string("a") + (char)0xE7 + (char)0xE3 + "o");
	CHECK((unsigned char)chat->got[1] == 0xE7 && (unsigned char)chat->got[2] == 0xE3);

	// --- botao "/" e historico
	kb->show(chat);
	tapButton(kb, 2);
	tapLabel(kb, "a");
	std::printf("DBG slash campo='%s'\n", kb->m_core.field.text.c_str());
	CHECK(kb->m_core.field.text == "/a");
	tapButton(kb, 1);                                                               // ^ : "oi"? historico tem "oi" e "ação"
	CHECK(kb->m_core.field.text == std::string("a") + (char)0xE7 + (char)0xE3 + "o");
	tapButton(kb, 1);
	CHECK(kb->m_core.field.text == "oi");
	tapButton(kb, 0);                                                               // v
	tapButton(kb, 0);
	CHECK(kb->m_core.field.text == "/a");                                           // rascunho de volta
	frame(kb, "historico");

	// --- Shift, Caps, simbolos, e todas as paginas desenham dentro da tela
	tapKind(kb, KbCore::Kind::Shift);   frame(kb, "shift");
	tapKind(kb, KbCore::Kind::Shift);   frame(kb, "caps");
	tapLabel(kb, "?123");               frame(kb, "simbolos");
	tapLabel(kb, "=\\<");               frame(kb, "simbolos2");
	tapLabel(kb, "ABC");                frame(kb, "letras");
	CHECK(kb->m_core.shift == KbCore::ShiftState::Lock);
	tapKind(kb, KbCore::Kind::Shift);                                               // desliga o Caps
	CHECK(kb->m_core.shift == KbCore::ShiftState::Off);

	// --- texto muito longo: rolagem do campo, tudo dentro da tela
	for (int i = 0; i < 160; i++) tapLabel(kb, "w");
	CHECK(kb->m_core.field.text.size() <= 144);
	frame(kb, "longo");
	CHECK(kb->m_scroll > 0.0f);

	// --- segurar backspace repete
	size_t before = kb->m_core.field.text.size();
	{
		kb->ensureKeyRects();
		const ImVec2 o = kb->absolutePosition();
		for (size_t r = 0; r < kb->m_rows.size(); r++)
			for (size_t c = 0; c < kb->m_rows[r].size(); c++)
				if (kb->m_rows[r][c].kind == KbCore::Kind::Backspace)
				{
					const KbCore::Rect& rc = kb->m_rects[r][c];
					ImVec2 p(o.x + (rc.x0 + rc.x1) / 2, o.y + kb->inputH() + (rc.y0 + rc.y1) / 2);
					kb->touchEvent(p, TouchType::push);
					CHECK(kb->m_core.field.text.size() == before - 1);               // apagou ao tocar
					for (int i = 0; i < 30; i++) { g_now += 50; frame(kb, "repete"); }
					kb->touchEvent(p, TouchType::pop);
					goto done_bs;
				}
	}
done_bs:
	CHECK(kb->m_core.field.text.size() < before - 5);                               // repetiu enquanto segurava
	{ size_t s = kb->m_core.field.text.size(); g_now += 1000; frame(kb, "solto"); CHECK(kb->m_core.field.text.size() == s); }

	// --- tocar fora fecha e guarda rascunho; reabrir no mesmo campo restaura
	std::string rasc = kb->m_core.field.text;
	kb->touchEvent(ImVec2(10, 10), TouchType::push);
	kb->touchEvent(ImVec2(10, 10), TouchType::pop);
	CHECK(!kb->visible() && g_gameInput);
	kb->show(chat);
	CHECK(kb->m_core.field.text == rasc);
	Caller* other = new Caller();
	kb->hide();
	kb->show(other);                                                                // outro campo: nao herda rascunho
	CHECK(kb->m_core.field.text.empty());
	kb->hide();

	// --- toque que NAO solta na mesma tecla nao digita
	kb->show(chat);
	{
		kb->ensureKeyRects();
		const ImVec2 o = kb->absolutePosition();
		const KbCore::Rect& a = kb->m_rects[0][0]; const KbCore::Rect& b = kb->m_rects[0][5];
		kb->touchEvent(ImVec2(o.x + (a.x0 + a.x1) / 2, o.y + kb->inputH() + (a.y0 + a.y1) / 2), TouchType::push);
		kb->touchEvent(ImVec2(o.x + (b.x0 + b.x1) / 2, o.y + kb->inputH() + (b.y0 + b.y1) / 2), TouchType::pop);
		CHECK(kb->m_core.field.text.empty());
	}
	kb->hide();

	// --- botoes da barra nunca se sobrepoem entre si nem ao campo
	{
		ImVec2 fa, fb, ba[3], bb[3];
		kb->inputRects(fa, fb, ba, bb);
		CHECK(fb.x <= ba[0].x + 0.5f && bb[0].x <= ba[1].x + 0.5f && bb[1].x <= ba[2].x + 0.5f);
		CHECK(bb[2].x <= kb->absolutePosition().x + kb->width() + 0.5f);
		CHECK(fb.x - fa.x > kb->width() * 0.4f);                                    // campo com espaco util
	}

	std::printf("%d verificacoes, %d falhas\n", g_n, g_fail);
	return g_fail ? 1 : 0;
}
