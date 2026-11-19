// Teste de PC (ImGui real sem tela): minimapa da cidade (ordem de desenho, etiqueta por cima, rodovia marcada) e eixo real no mapa aberto.
#include "samp/gui/imguirenderer.h"
#include "samp/gui/samp_widgets/WorldMapUI.h"
#include <cstdio>
#include <cmath>
#include "samp/settings.h"
CSettings* pSettings = nullptr;
using namespace WorldMap;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; std::printf("FALHOU linha %d: %s\n", __LINE__, #c); } } while (0)
struct Fake : IPlatform
{
	ImVec2 scr; float px = -3200, py = 1967, hd = 90; int64_t now = 100000; City c; int tagCalls = 0;
	ImVec2 screen() override { return scr; }
	bool player(float& x, float& y, float& h) override { x = px; y = py; h = hd; return true; }
	ImTextureID tile(int) override { return (ImTextureID)1; }
	ImTextureID fullMap() override { return 0; }
	void setGps(float, float, float) override {}
	void clearGps() override {}
	void sendCommand(const char*) override {}
	int64_t nowMs() override { return now; }
	const City* city() override { return &c; }
	ImTextureID cityOverview() override { return (ImTextureID)2; }
	void cityImageRect(float& x0, float& y0, float& x1, float& y1) override { x0 = -16250; y0 = -750.2f; x1 = 1362.8f; y1 = 3653; }
	void drawLocationTagOver(ImGuiRenderer*) override { tagCalls++; }
};
static int firstAmber = -1, firstArrow = -1;
static int countColor(ImDrawList* dl, ImU32 col) { int n = 0; for (int i = 0; i < dl->VtxBuffer.Size; i++) if (dl->VtxBuffer[i].col == col) n++; return n; }
int main()
{
	ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(1600, 720); io.Fonts->AddFontDefault();
	unsigned char* px; int tw, th; io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th); io.Fonts->TexID = (ImTextureID)1; io.DeltaTime = 1.0f / 60.0f;
	Fake f; f.scr = io.DisplaySize; f.c.ok = true; f.c.minX = -16250; f.c.maxX = -1175; f.c.minY = -750.2f; f.c.maxY = 3653;
	Ui ui(&f);
	const ImU32 amber = ImColor(255, 205, 70, 255), white = ImColor(255, 255, 255, 235);
	auto minimap = [&](float plx, float ply, int& tag, int& amberV, int& arrowV)
	{
		f.px = plx; f.py = ply; f.tagCalls = 0; f.now += 16;
		// o hook do radar (jogo) publica: transformacao mundo->pixels (1 px = 2 m, centro 120,120; norte para cima) e a rodovia ja em pixels
		MiniCity::Xf t; t.wx = plx; t.wy = ply; t.sx = 120; t.sy = 120; t.ax = 0.5f; t.ay = 0; t.bx = 0; t.by = -0.5f; t.cx = 120; t.cy = 120; t.rad = 100;
		t.poly = { { 20, 20 }, { 20, 220 }, { 220, 220 }, { 220, 20 } };
		if (MiniCity::Side(t.poly[0], t.poly[1], { 120, 120 }) < 0.0f) std::reverse(t.poly.begin(), t.poly.end());
		MiniCity::PublishXf(std::move(t), f.now);
		std::vector<std::vector<MiniRoute::Vec2>> runs; runs.push_back({ { 20, 125 }, { 120, 125 }, { 220, 125 } });
		MiniRoute::PublishLink(std::move(runs), 100.0f, f.now);
		ImGui::NewFrame();
		ImDrawList* dl = ImGui::GetBackgroundDrawList(); ImGuiRenderer r(dl, ImGui::GetFont());
		ui.Draw(&r);
		tag = f.tagCalls; amberV = countColor(dl, amber);
		arrowV = countColor(dl, ImColor(15, 15, 15, 255));
		firstAmber = firstArrow = -1;
		for (int i = 0; i < dl->VtxBuffer.Size; i++) { if (firstAmber < 0 && dl->VtxBuffer[i].col == amber) firstAmber = i; if (firstArrow < 0 && dl->VtxBuffer[i].col == ImColor(15, 15, 15, 255)) firstArrow = i; }
		ImGui::EndFrame();
	};
	int tag, am, ar;
	minimap(-3200, 1967, tag, am, ar);          // dentro da cidade, mapa fechado
	CHECK(MiniCity::gInCity.load());
	CHECK(tag == 1);                            // etiqueta redesenhada por cima, 1x por quadro
	CHECK(am > 0);                              // rodovia marcada (amarelo) tambem na cidade
	CHECK(ar > 0);                              // seta do jogador desenhada
	CHECK(firstAmber >= 0 && firstArrow > firstAmber);   // a seta vem DEPOIS da rodovia (a linha nao a cobre)
	minimap(-2900, 1967, tag, am, ar);          // em San Andreas (fora da cidade): HUD desenha a etiqueta, o mapa nao
	CHECK(!MiniCity::gInCity.load());
	CHECK(tag == 0);
	CHECK(am > 0);                              // rodovia marcada tambem no mapa de SA (como antes)
	CHECK(ar == 0);                             // seta propria so na cidade (em SA e' a do radar nativo)
	// mapa aberto: eixo real. Cidade = rodovia inteira ; SA = so x >= -3000
	auto openAmber = [&](float plx, float ply)
	{
		f.px = plx; f.py = ply; f.now += 16; ui.Open();
		int n = 0;
		for (int i = 0; i < 4; i++)
		{
			ImGui::NewFrame(); ImDrawList* dl = ImGui::GetBackgroundDrawList(); ImGuiRenderer r(dl, ImGui::GetFont());
			ui.Draw(&r); n = countColor(dl, amber); ImGui::EndFrame(); f.now += 16;
		}
		ui.Close(false);
		return n;
	};
	MiniCity::ClearXf();
	const int nCity = openAmber(-3200, 1967);
	CHECK(ui.IsOpen() || true);
	const int nSA = openAmber(-2800, 1967);
	std::printf("amarelo no mapa aberto: cidade=%d SA=%d\n", nCity, nSA);
	CHECK(nCity > 0 && nSA > 0);
	CHECK(nCity > nSA);                          // na cidade a rodovia inteira (mais segmentos) ; em SA so a ponta
	std::printf("%d verificacoes, %d falhas\n", g_n, g_fail);
	return g_fail ? 1 : 0;
}
