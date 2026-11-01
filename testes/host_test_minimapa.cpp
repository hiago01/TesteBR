// Teste no PC: Ui (WorldMapUI.h) com o ImGui REAL, plataforma falsa e grafo SINTETICO. Roda o GPS local (A*), desenha rota e
// camada de ruas e rasteriza o ImDrawList em um PPM (sem GPU) para conferir o resultado a olho. Nao testa o jogo.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "gui/samp_widgets/MiniRoute.h"
#include "../app/src/main/cpp/samp/vendor/imgui/imgui.h"
#include "../app/src/main/cpp/samp/gui/imguirenderer.h"
#include "../app/src/main/cpp/samp/gui/uisettings.h"
#include "../app/src/main/cpp/samp/gui/samp_widgets/WorldMapUI.h"
#include <chrono>
#include <fstream>
#include <functional>
using namespace WorldMap;
// UISettings real depende do jogo; so o que o ImGuiRenderer usa:
float UISettings::m_outlineSize = 1.0f; float UISettings::m_fontSize = 16.0f;

// ---- mesmo mundo sintetico do test_roadgraph (resumido): grade de 50 un., parede em x=0 com abertura em y~2025
static void put(std::vector<uint8_t>& b, const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
struct SN { float x, y; uint8_t w; std::vector<std::pair<int,int>> links; };
static std::vector<uint8_t> BuildFile(int area, const std::vector<SN>& veh)
{
	std::vector<uint8_t> f; std::vector<uint32_t> links; std::vector<uint16_t> base;
	uint32_t nodes = (uint32_t)veh.size(), vv = nodes, pp = 0, navi = 0;
	for (auto& n : veh) { base.push_back((uint16_t)links.size()); for (auto& l : n.links) links.push_back((uint32_t)l.first | ((uint32_t)l.second << 16)); }
	uint32_t nl = (uint32_t)links.size();
	put(f, &nodes, 4); put(f, &vv, 4); put(f, &pp, 4); put(f, &navi, 4); put(f, &nl, 4);
	for (uint32_t i = 0; i < nodes; i++)
	{
		uint8_t r[28] = {};
		int16_t x = (int16_t)std::lround(veh[i].x * 8), y = (int16_t)std::lround(veh[i].y * 8), z = 80;
		std::memcpy(r + 8, &x, 2); std::memcpy(r + 10, &y, 2); std::memcpy(r + 12, &z, 2);
		uint16_t un = 0x7FFE, bl = base[i], ar = (uint16_t)area, nid = (uint16_t)i;
		std::memcpy(r + 14, &un, 2); std::memcpy(r + 16, &bl, 2); std::memcpy(r + 18, &ar, 2); std::memcpy(r + 20, &nid, 2);
		r[22] = veh[i].w; uint32_t fl = (uint32_t)veh[i].links.size() & 0xF; std::memcpy(r + 24, &fl, 4);
		put(f, r, 28);
	}
	for (uint32_t l : links) { uint16_t a = (uint16_t)(l & 0xFFFF), n = (uint16_t)(l >> 16); put(f, &a, 2); put(f, &n, 2); }
	std::vector<uint8_t> z(0x300 + 2ull * nl + 2ull * (nl + 192), 0); put(f, z.data(), z.size());
	return f;
}
static void MakeWorld(const std::string& root)
{
	const int nx = 120, st = 50;
	std::vector<std::vector<SN>> area(64); std::vector<int> aOf(nx * nx), nOf(nx * nx);
	for (int gy = 0; gy < nx; gy++) for (int gx = 0; gx < nx; gx++)
	{
		// ruas "curvas": desloca o y com uma senoide para nao ser so grade reta
		const float x = -3000 + gx * st + 25.0f, y = -3000 + gy * st + 25.0f + 14.0f * std::sin(gx * 0.18f);
		const int a = (int)((-3000 + gy * st + 25 + 3000) / 750) * 8 + (int)((x + 3000) / 750);
		aOf[gy * nx + gx] = a; nOf[gy * nx + gx] = (int)area[a].size();
		area[a].push_back({ x, y, (uint8_t)((gx % 10 == 0 || gy % 10 == 0) ? ((gx % 30 == 0 || gy % 30 == 0) ? 22 : 12) : 6), {} });
	}
	for (int gy = 0; gy < nx; gy++) for (int gx = 0; gx < nx; gx++)
	{
		const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++)
		{
			const int hx = gx + dx[k], hy = gy + dy[k];
			if (hx < 0 || hy < 0 || hx >= nx || hy >= nx) continue;
			const bool wall = ((gx == 59 && hx == 60) || (gx == 60 && hx == 59)) && gy != 100;
			if (wall) continue;
			area[aOf[gy * nx + gx]][nOf[gy * nx + gx]].links.push_back({ aOf[hy * nx + hx], nOf[hy * nx + hx] });
		}
	}
	mkdir(root.c_str(), 0755); mkdir((root + "SAMP").c_str(), 0755); mkdir((root + "SAMP/paths").c_str(), 0755);
	for (int a = 0; a < 64; a++) { auto f = BuildFile(a, area[a]); std::ofstream o(root + "SAMP/paths/nodes" + std::to_string(a) + ".dat", std::ios::binary); o.write((const char*)f.data(), f.size()); }
}

struct FakePlat : IPlatform
{
	Roads::Graph g; Lines::Styles st = Lines::Defaults(); float px = -500, py = 25; bool haveGraph = true;
	ImVec2 screen() override { return ImVec2(1600, 720); }
	bool player(float& x, float& y, float& h) override { x = px; y = py; h = 90; return true; }
	ImTextureID tile(int) override { return 0; }
	ImTextureID fullMap() override { return 0; }
	void setGps(float, float, float) override {}
	void clearGps() override {}
	void sendCommand(const char*) override {}
	int64_t nowMs() override { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	const Roads::Graph* roads() override { return haveGraph ? &g : nullptr; }
	const Lines::Styles& lineStyles() override { return st; }
};

// ---- rasterizador minimo do ImDrawList (triangulos com cor de vertice e alfa da fonte), so para conferir visualmente
struct Img { int w, h; std::vector<float> c; Img(int W, int H) : w(W), h(H), c((size_t)W * H * 3, 0.0f) {} };
static void Raster(const ImDrawList* dl, Img& im, const unsigned char* atlas, int aw, int ah)
{
	const ImDrawIdx* idx = dl->IdxBuffer.Data; size_t ib = 0;
	for (int ci = 0; ci < dl->CmdBuffer.Size; ci++)
	{
		const ImDrawCmd& cmd = dl->CmdBuffer[ci];
		for (unsigned e = 0; e + 2 < cmd.ElemCount; e += 3, ib += 3)
		{
			const ImDrawVert& A = dl->VtxBuffer[idx[ib]]; const ImDrawVert& B = dl->VtxBuffer[idx[ib + 1]]; const ImDrawVert& C = dl->VtxBuffer[idx[ib + 2]];
			const float minx = std::min({ A.pos.x, B.pos.x, C.pos.x }), maxx = std::max({ A.pos.x, B.pos.x, C.pos.x });
			const float miny = std::min({ A.pos.y, B.pos.y, C.pos.y }), maxy = std::max({ A.pos.y, B.pos.y, C.pos.y });
			const float den = (B.pos.y - C.pos.y) * (A.pos.x - C.pos.x) + (C.pos.x - B.pos.x) * (A.pos.y - C.pos.y);
			if (std::fabs(den) < 1e-6f) continue;
			for (int y = std::max(0, (int)std::floor(miny)); y <= std::min(im.h - 1, (int)std::ceil(maxy)); y++)
				for (int x = std::max(0, (int)std::floor(minx)); x <= std::min(im.w - 1, (int)std::ceil(maxx)); x++)
				{
					if (x < cmd.ClipRect.x || x >= cmd.ClipRect.z || y < cmd.ClipRect.y || y >= cmd.ClipRect.w) continue;
					const float px = x + 0.5f, py = y + 0.5f;
					const float l1 = ((B.pos.y - C.pos.y) * (px - C.pos.x) + (C.pos.x - B.pos.x) * (py - C.pos.y)) / den;
					const float l2 = ((C.pos.y - A.pos.y) * (px - C.pos.x) + (A.pos.x - C.pos.x) * (py - C.pos.y)) / den;
					const float l3 = 1.0f - l1 - l2;
					if (l1 < 0 || l2 < 0 || l3 < 0) continue;
					auto ch = [&](int sh) { return (l1 * ((A.col >> sh) & 255) + l2 * ((B.col >> sh) & 255) + l3 * ((C.col >> sh) & 255)) / 255.0f; };
					float r = ch(0), g = ch(8), b = ch(16), a = ch(24);
					const float u = l1 * A.uv.x + l2 * B.uv.x + l3 * C.uv.x, v = l1 * A.uv.y + l2 * B.uv.y + l3 * C.uv.y;
					a *= atlas[std::min(ah - 1, std::max(0, (int)(v * ah))) * aw + std::min(aw - 1, std::max(0, (int)(u * aw)))] / 255.0f;
					float* d = &im.c[((size_t)y * im.w + x) * 3];
					d[0] = d[0] * (1 - a) + r * a; d[1] = d[1] * (1 - a) + g * a; d[2] = d[2] * (1 - a) + b * a;
				}
		}
	}
}
static void SavePPM(const Img& im, const char* path)
{
	std::ofstream o(path, std::ios::binary); o << "P6\n" << im.w << " " << im.h << "\n255\n";
	for (size_t i = 0; i < im.c.size(); i++) o.put((char)std::min(255, std::max(0, (int)(im.c[i] * 255.0f + 0.5f))));
}

int main(int argc, char** argv)
{
	const std::string root = "/tmp/rg_render_root/";
	mkdir("/tmp/rg_render_root", 0755);
	MakeWorld(root);
	FakePlat plat;
	std::vector<std::string> log;
	if (!Roads::LoadAll(root, plat.g, log)) { std::printf("grafo nao carregou\n"); return 1; }
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(1600, 720);
	ImFont* font = io.Fonts->AddFontDefault();
	unsigned char* atlas; int aw, ah; io.Fonts->GetTexDataAsAlpha8(&atlas, &aw, &ah);
	io.Fonts->TexID = (ImTextureID)1;
	Ui ui(&plat);
	int fails = 0, oks = 0;
#define CHECK(c, m) do { if (c) oks++; else { fails++; std::printf("FALHA: %s\n", m); } } while (0)

	// "radar do jogo" falso: centro (220, 520) px, raio 110 px (espaco do radar = disco de raio 1), alcance 180 unidades do mundo,
	// SEM rotacao (norte para cima). Mascara = quadrado arredondado (expoente 6, escala 1,10) como no hooks.cpp.
	const float cx = 220, cy = 520, R = 110, range = 180;
	std::vector<WorldMap::Vec2> mask;
	for (int i = 0; i < 8; i++) { float a = i * 3.14159265f / 4.0f, x = std::cos(a), y = std::sin(a);
		float den = std::pow(std::pow(std::fabs(x), 6) + std::pow(std::fabs(y), 6), 1.0f / 6.0f); mask.push_back({ cx + x / den * R * 1.10f, cy + y / den * R * 1.10f }); }
	std::vector<float> mv; for (auto& m : mask) { mv.push_back(m.x); mv.push_back(m.y); }
	MiniRoute::SetMask(mv.data(), 8);

	// o que o hook faz depois de CHud::DrawRadar (com transformacoes falsas no lugar das do jogo)
	auto hookStep = [&](float plx, float ply) {
		std::vector<WorldMap::Vec2> world; bool straight = false;
		if (!MiniRoute::GetWorld(world, straight)) { MiniRoute::ClearScreen(); return; }
		auto toRadar = [&](WorldMap::Vec2 w) { return WorldMap::Vec2{ (w.x - plx) / range, (w.y - ply) / range }; };
		auto toScreen = [&](WorldMap::Vec2 r) { return WorldMap::Vec2{ cx + r.x * R, cy - r.y * R }; };
		std::vector<std::vector<WorldMap::Vec2>> runs;
		auto poly = MiniRoute::MaskPolygon(MiniRoute::GetMask(), cx, cy, R, 0.97f);
		if (!poly.empty()) MiniRoute::BuildPoly(world, toRadar, toScreen, poly, runs); else MiniRoute::Build(world, toRadar, toScreen, MiniRoute::kClipRadius, runs);
		MiniRoute::PublishScreen(std::move(runs), cx, cy, R, plat.nowMs());
	};
	auto frame = [&](bool save, const char* ppm, int* verts = nullptr) {
		ImGui::NewFrame();
		ImDrawList* dl = ImGui::GetOverlayDrawList();
		ImGuiRenderer r(dl, font);
		ui.Draw(&r);
		ImGui::EndFrame();
		if (save)
		{
			Img im(1600, 720);
			for (int y = 0; y < 720; y++) for (int x = 0; x < 1600; x++) { float* d = &im.c[((size_t)y * 1600 + x) * 3]; d[0] = 0.10f; d[1] = 0.12f; d[2] = 0.14f; }
			// fundo: a forma da mascara do radar em cinza, para ver a linha dentro dela
			auto poly = MiniRoute::MaskPolygon(mask, cx, cy, R, 1.0f);
			for (int y = 0; y < 720; y++) for (int x = 0; x < 1600; x++) { bool in = true; for (size_t i = 0; i < poly.size() && in; i++) { auto& v = poly[i]; auto& w = poly[(i + 1) % poly.size()]; if ((w.x - v.x) * (y - v.y) - (w.y - v.y) * (x - v.x) < 0) in = false; }
				if (in) { float* d = &im.c[((size_t)y * 1600 + x) * 3]; d[0] = 0.35f; d[1] = 0.45f; d[2] = 0.35f; } }
			Raster(dl, im, atlas, aw, ah); SavePPM(im, ppm);
		}
		if (verts) *verts = dl->VtxBuffer.Size;
		ImGui::Render();
	};
	auto packetGps = [&](float x, float y) { Packet p; p.op = 3; p.gpsOn = true; p.gx = x; p.gy = y; p.gz = 0; p.gpsLoc = -1; ui.Apply(p); };

	// 1) sem GPS: nada no minimapa
	int v0 = 0; frame(false, "", &v0); hookStep(plat.px, plat.py); frame(false, "", &v0);
	std::vector<std::vector<WorldMap::Vec2>> so; float rad; bool st;
	CHECK(!MiniRoute::GetScreen(so, rad, plat.nowMs(), st), "sem GPS: nada publicado para o minimapa");

	// 2) GPS ligado, mapa FECHADO: a rota local (A*) e publicada, o hook converte, o ImGui desenha
	plat.px = -500; plat.py = 25;
	packetGps(500, 25);
	for (int i = 0; i < 3; i++) { frame(false, ""); hookStep(plat.px, plat.py); }
	std::vector<WorldMap::Vec2> w; bool straight = true;
	CHECK(MiniRoute::GetWorld(w, straight), "GPS ligado + mapa fechado: rota publicada (mundo)");
	CHECK(w.size() >= 2 && !straight, "rota pelas ruas (nao a reta)");
	CHECK(std::fabs(w[0].x - plat.px) < 1e-3f && std::fabs(w[0].y - plat.py) < 1e-3f, "rota comeca na posicao do jogador");
	CHECK(MiniRoute::GetScreen(so, rad, plat.nowMs(), st) && !so.empty(), "hook publicou polilinhas em pixels");
	bool allInside = true; auto poly = MiniRoute::MaskPolygon(mask, cx, cy, R, 1.0f);
	for (auto& run : so) for (auto& p : run) for (size_t i = 0; i < poly.size(); i++) { auto& v = poly[i]; auto& q = poly[(i + 1) % poly.size()]; if ((q.x - v.x) * (p.y - v.y) - (q.y - v.y) * (p.x - v.x) < -0.5f) allInside = false; }
	CHECK(allInside, "todos os pontos da rota no minimapa estao dentro da mascara");
	CHECK(!so.empty() && std::hypot(so[0][0].x - cx, so[0][0].y - cy) < 1.0f, "a linha parte do centro (jogador)");

	int v1 = 0; frame(true, "/tmp/minimapa_A.ppm", &v1);
	CHECK(v1 > v0 + 20, "ImGui desenhou a linha (mais vertices que sem rota)");
	std::printf("  rota (mundo): %zu pontos; minimapa: %zu trecho(s); vertices %d -> %d\n", w.size(), so.size(), v0, v1);

	// 3) o jogador anda: a parte percorrida some (a rota sempre comeca no jogador)
	plat.px = -250; plat.py = 25; frame(false, ""); hookStep(plat.px, plat.py); frame(false, "");
	MiniRoute::GetWorld(w, straight);
	CHECK(std::fabs(w[0].x - plat.px) < 1e-3f, "depois de andar: rota recomeca na nova posicao");

	// 4) radar nao desenhado (stamp velho): nada e desenhado
	CHECK(!MiniRoute::GetScreen(so, rad, plat.nowMs() + 5000, st), "radar parado/oculto: rota nao e desenhada");

	// 5) abrir o mapa grande limpa a rota do minimapa
	ui.Open(); frame(false, ""); frame(false, "");
	CHECK(!MiniRoute::GetWorld(w, straight), "mapa aberto: sem rota no minimapa");
	ui.Close(false); frame(false, ""); hookStep(plat.px, plat.py);
	CHECK(MiniRoute::GetWorld(w, straight), "mapa fechado de novo: a rota volta");

	// 6) sem grafo de ruas: reta ate o destino (sinalizada), sem travar
	plat.haveGraph = false; packetGps(-100, 400);
	for (int i = 0; i < 2; i++) { frame(false, ""); hookStep(plat.px, plat.py); }
	CHECK(MiniRoute::GetWorld(w, straight) && straight && w.size() == 2, "sem grafo: reta jogador->destino");
	frame(true, "/tmp/minimapa_B.ppm");
	plat.haveGraph = true;

	// 7) /mapa minimapa off
	MiniRoute::gEnabled.store(false); frame(false, ""); hookStep(plat.px, plat.py);
	CHECK(!MiniRoute::GetWorld(w, straight), "desligado: nada publicado");
	MiniRoute::gEnabled.store(true);

	// 8) limpar o GPS apaga a rota
	Packet pc; pc.op = 3; pc.gpsOn = false; ui.Apply(pc); frame(false, ""); frame(false, "");
	CHECK(!MiniRoute::GetWorld(w, straight), "GPS desligado: rota do minimapa apagada");

	ImGui::DestroyContext();
	std::printf("%d verificacoes ok, %d falhas\n", oks, fails);
	return fails;
}
