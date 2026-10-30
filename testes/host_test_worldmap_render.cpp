// Teste de PC: roda a interface do mapa de verdade (WorldMapUI.h + ImGui real sem tela) com toques simulados.
// Nao testa o jogo: tiles do radar, blip e comandos sao simulados por uma plataforma falsa.
#include "samp/gui/imguirenderer.h"
#include "samp/gui/samp_widgets/WorldMapUI.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#include "samp/settings.h"
CSettings* pSettings = nullptr;
using namespace WorldMap;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHOU linha %d: %s\n", __LINE__, #c); } } while (0)

struct Fake : IPlatform
{
	ImVec2 scr; float px = 100, py = 200, hd = 90; bool has = true;
	int64_t now = 100000; bool withTiles = true;
	bool gps = false; float gx = 0, gy = 0; int clears = 0;
	std::vector<std::string> cmds;
	ImVec2 screen() override { return scr; }
	bool player(float& x, float& y, float& h) override { x = px; y = py; h = hd; return has; }
	ImTextureID tile(int) override { return withTiles ? (ImTextureID)1 : (ImTextureID)0; }
	ImTextureID fullMap() override { return 0; }
	void setGps(float x, float y, float) override { gps = true; gx = x; gy = y; }
	void clearGps() override { gps = false; clears++; }
	void sendCommand(const char* c) override { cmds.push_back(c); }
	int64_t nowMs() override { return now; }
};

static ImVec2 g_disp;
static unsigned char* g_atlas; static int g_atw, g_ath;
static const char* g_dump = nullptr;
static void dumpFrame(ImDrawList* dl, const char* tag)
{
	if (!g_dump) return;
	char path[512]; std::snprintf(path, sizeof path, "%s_%s.bin", g_dump, tag);
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
static int frame(Ui& ui, const char* tag = nullptr)
{
	ImGui::NewFrame();
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	ImGuiRenderer r(dl, ImGui::GetFont());
	ui.Draw(&r);
	int bad = 0; unsigned off = 0;
	for (int ci = 0; ci < dl->CmdBuffer.Size; ci++)
	{
		const ImDrawCmd& c = dl->CmdBuffer[ci];
		const bool clipped = c.ClipRect.x > 0.5f || c.ClipRect.y > 0.5f || c.ClipRect.z < g_disp.x - 0.5f || c.ClipRect.w < g_disp.y - 0.5f;
		for (unsigned k = 0; k < c.ElemCount; k++)
		{
			const ImVec2& p = dl->VtxBuffer[dl->IdxBuffer[off + k]].pos;
			if (!clipped && (p.x < -1 || p.y < -1 || p.x > g_disp.x + 1 || p.y > g_disp.y + 1)) bad++;
		}
		off += c.ElemCount;
	}
	const int nv = dl->VtxBuffer.Size;
	if (tag) dumpFrame(dl, tag);
	ImGui::EndFrame();
	CHECK(bad == 0);
	return nv;
}
static void tch(Ui& ui, Fake& f, int type, int n, float x, float y, bool cancel = false) { ui.HandleRawTouch(type, n, (int)x, (int)y, cancel); f.now += 16; }

int main(int argc, char** argv)
{
	float W = argc > 1 ? (float)atof(argv[1]) : 2400, H = argc > 2 ? (float)atof(argv[2]) : 1080;
	g_disp = ImVec2(W, H);
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = g_disp; io.Fonts->AddFontDefault();
	unsigned char* px; int tw, th; io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
	g_atlas = px; g_atw = tw; g_ath = th; g_dump = getenv("MAP_DUMP");
	io.Fonts->TexID = (ImTextureID)1; io.DeltaTime = 1.0f / 60.0f;

	Fake f; f.scr = g_disp;
	Ui ui(&f);

	// fechado: nao desenha nada e nao consome toque
	CHECK(frame(ui) == 0);
	CHECK(!ui.HandleRawTouch(2, 0, 10, 10, false));

	// locais do servidor
	Packet pk; pk.op = 1;
	const char* nomes[] = { "Mercadinho Posto", "Favela", "Banco Central", "Hospital", "Prefeitura", "Aeroporto" };
	for (int i = 0; i < 6; i++) { Location l; l.id = 10 + i; l.icon = i; l.x = -1500 + i * 600; l.y = 300 - i * 200; l.z = 10; l.name = nomes[i]; pk.locs.push_back(l); }
	ui.Apply(pk);
	ui.Open();
	CHECK(ui.IsOpen());
	CHECK(ui.ConsumeFlush());
	CHECK(frame(ui, "aberto") > 100);
	CHECK(ui.locations().size() == 6);
	CHECK(!f.cmds.empty() && f.cmds[0] == "/mapax open");
	// o centro inicial e o jogador
	CHECK(std::fabs(ui.view().cx - f.px) < 1 && std::fabs(ui.view().cy - f.py) < 1);

	// toque agora e consumido
	CHECK(ui.HandleRawTouch(2, 0, 5, 5, false)); ui.HandleRawTouch(1, 0, 5, 5, false);
	frame(ui);

	// arrastar o mapa 1 dedo muda o centro
	const float cx0 = ui.view().cx;
	const float mx = W * 0.35f, my = H * 0.45f;
	tch(ui, f, 2, 0, mx, my); frame(ui);
	for (int i = 1; i <= 8; i++) { tch(ui, f, 3, 0, mx - i * 20.0f, my); }
	frame(ui);
	tch(ui, f, 1, 0, mx - 160, my); frame(ui);
	CHECK(ui.view().cx > cx0 + 1);         // arrastou para a esquerda -> o mundo anda para o leste
	CHECK(!ui.gpsOn());                    // arrastar nao marca GPS
	CHECK(f.cmds.size() == 1);

	// pinca aumenta o zoom
	const float s0 = ui.view().scale;
	tch(ui, f, 2, 0, mx - 40, my); frame(ui);
	tch(ui, f, 2, 1, mx + 40, my); frame(ui);
	for (int i = 1; i <= 6; i++) { tch(ui, f, 3, 0, mx - 40 - i * 10.0f, my); tch(ui, f, 3, 1, mx + 40 + i * 10.0f, my); }
	frame(ui);
	tch(ui, f, 1, 1, mx + 100, my); tch(ui, f, 1, 0, mx - 100, my); frame(ui);
	CHECK(ui.view().scale > s0 * 1.5f);
	CHECK(!ui.gpsOn());

	// toque no mapa vazio = GPS livre
	f.cmds.clear();
	tch(ui, f, 2, 0, mx, my + 40); frame(ui); tch(ui, f, 1, 0, mx, my + 40); frame(ui);
	CHECK(ui.gpsOn() && f.gps);
	CHECK(ui.gpsLoc() == -1);
	CHECK(!f.cmds.empty() && f.cmds.back().rfind("/mapax pos ", 0) == 0);

	// toque na lista = seleciona o local, manda sel, centraliza
	f.cmds.clear();
	{
		const float k = std::max(0.5f, H / 540.0f);
		const float lx = W * 0.96f - 10 * k - 60 * k, ly = H * 0.06f + 10 * k + 30 * k * 2.5f;   // 3a linha (Banco Central)
		tch(ui, f, 2, 0, lx, ly); frame(ui); tch(ui, f, 1, 0, lx, ly); frame(ui);
		CHECK(ui.gpsOn() && ui.gpsLoc() == 12);
		CHECK(!f.cmds.empty() && f.cmds.back() == "/mapax sel 12");
		CHECK(std::fabs(ui.view().cx - (-1500 + 2 * 600)) < 1);
	}
	// rota do servidor desenha mais vertices
	{
		const int a = frame(ui);
		Packet r; r.op = 4; for (int i = 0; i < 40; i++) r.route.push_back({ -300.0f + i * 10, 100.0f + (i % 5) * 4 });
		ui.Apply(r);
		const int b = frame(ui, "rota");
		CHECK(b > a);
		CHECK(ui.routeSize() == 40);
	}
	// limpar GPS
	f.cmds.clear();
	{
		const float k = std::max(0.5f, H / 540.0f);
		const float bx = W * 0.04f + 10 * k + 110 * k + 8 * k + 20 * k, by = H * 0.94f - 10 * k - 17 * k;
		tch(ui, f, 2, 0, bx, by); frame(ui); tch(ui, f, 1, 0, bx, by); frame(ui);
		CHECK(!ui.gpsOn() && !f.gps);
		CHECK(!f.cmds.empty() && f.cmds.back() == "/mapax clear");
	}
	// chegada ao destino (mapa aberto ou nao)
	{
		Packet g; g.op = 3; g.gpsOn = true; g.gx = f.px + 5; g.gy = f.py + 5; g.gz = 0; g.gpsLoc = 11;
		f.cmds.clear();
		ui.Apply(g); frame(ui);
		CHECK(!ui.gpsOn());
		bool arrived = false; for (auto& c : f.cmds) if (c == "/mapax arrived 11") arrived = true;
		CHECK(arrived);
	}
	// sem tiles: nao quebra
	f.withTiles = false; CHECK(frame(ui, "semtiles") > 50); f.withTiles = true;
	// sem jogador: nao quebra
	f.has = false; frame(ui); f.has = true;
	// grade de depuracao
	ui.debugGrid = true; frame(ui, "grade"); ui.debugGrid = false;

	// FECHAR por toque
	f.cmds.clear();
	{
		const float k = std::max(0.5f, H / 540.0f);
		const float bx = W * 0.04f + 10 * k + 40 * k, by = H * 0.94f - 10 * k - 17 * k;
		tch(ui, f, 2, 0, bx, by); frame(ui); tch(ui, f, 1, 0, bx, by); frame(ui);
		CHECK(!ui.IsOpen());
		bool closed = false; for (auto& c : f.cmds) if (c == "/mapax close") closed = true;
		CHECK(closed);
	}
	// fechado de novo: nao consome
	CHECK(!ui.HandleRawTouch(2, 0, 10, 10, false));
	// o dedo que estava segurando ainda e consumido ate soltar
	ui.Open(); frame(ui);
	ui.HandleRawTouch(2, 1, 50, 50, false);
	ui.Close(true);
	CHECK(ui.HandleRawTouch(1, 1, 50, 50, false));
	CHECK(!ui.HandleRawTouch(2, 1, 50, 50, false));
	frame(ui);

	// Back
	ui.Open(); frame(ui);
	f.now += 1000;
	CHECK(ui.OnBack()); CHECK(!ui.IsOpen());
	f.now += 1000;
	CHECK(!ui.OnBack());

	// servidor abre/fecha por RPC
	Packet o; o.op = 2; o.open = true; ui.Apply(o); CHECK(ui.IsOpen());
	o.open = false; ui.Apply(o); CHECK(!ui.IsOpen());

	// Reset limpa tudo
	ui.Open(); ui.Reset(); frame(ui); CHECK(!ui.IsOpen()); CHECK(ui.locations().empty());

	std::printf("%d verificacoes, %d falhas (%gx%g)\n", g_n, g_fail, W, H);
	return g_fail ? 1 : 0;
}
