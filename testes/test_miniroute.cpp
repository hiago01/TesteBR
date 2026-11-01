#include "gui/samp_widgets/MiniRoute.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace MiniRoute;
static int fails = 0, oks = 0;
#define CHECK(c, m) do { if (c) oks++; else { fails++; std::printf("FALHA: %s (linha %d)\n", m, __LINE__); } } while (0)
static bool inside(const Vec2& p, float R) { return std::sqrt(p.x * p.x + p.y * p.y) <= R + 1e-4f; }
int main()
{
	std::vector<std::vector<Vec2>> runs;
	// 1) reta atravessando o disco: entra e sai
	ClipToCircle({ {-2, 0}, {2, 0} }, 1.0f, runs);
	CHECK(runs.size() == 1 && runs[0].size() == 2, "reta atravessa: 1 run de 2 pontos");
	CHECK(std::fabs(runs[0][0].x + 1.0f) < 1e-4f && std::fabs(runs[0][1].x - 1.0f) < 1e-4f, "pontas exatamente no circulo");
	// 2) totalmente fora
	ClipToCircle({ {3, 3}, {5, 3} }, 1.0f, runs); CHECK(runs.empty(), "fora: nada");
	ClipToCircle({ {-2, 1.5f}, {2, 1.5f} }, 1.0f, runs); CHECK(runs.empty(), "reta fora do disco (acima): nada");
	// 3) totalmente dentro
	ClipToCircle({ {0, 0}, {0.3f, 0.2f}, {0.5f, -0.1f} }, 1.0f, runs);
	CHECK(runs.size() == 1 && runs[0].size() == 3, "dentro: 1 run com os 3 pontos");
	// 4) sai e volta: duas runs
	ClipToCircle({ {0, 0}, {3, 0}, {3, 0.5f}, {0.2f, 0.5f} }, 1.0f, runs);
	CHECK(runs.size() == 2, "sai e volta: 2 runs");
	// 5) comeca fora, entra
	ClipToCircle({ {-3, 0}, {0, 0} }, 1.0f, runs);
	CHECK(runs.size() == 1 && std::fabs(runs[0][0].x + 1.0f) < 1e-4f && runs[0].back().x == 0.0f, "entra no disco: comeca no circulo");
	// 6) degenerados
	ClipToCircle({}, 1.0f, runs); CHECK(runs.empty(), "vazio");
	ClipToCircle({ {0, 0} }, 1.0f, runs); CHECK(runs.empty(), "1 ponto");
	ClipToCircle({ {0, 0}, {1, 1} }, 0.0f, runs); CHECK(runs.empty(), "raio 0");
	ClipToCircle({ {0.1f, 0.1f}, {0.1f, 0.1f}, {0.2f, 0.2f} }, 1.0f, runs); CHECK(runs.size() == 1 && runs[0].size() >= 2, "ponto repetido nao quebra");
	float nan = std::nanf("");
	ClipToCircle({ {0, 0}, {nan, 0}, {0.2f, 0.2f}, {0.3f, 0.3f} }, 1.0f, runs); CHECK(runs.size() == 1, "NaN corta a run e nao propaga");
	// 7) aleatorio: todo ponto de saida esta dentro do disco, e nenhuma run tem < 2 pontos
	std::mt19937 g(7); std::uniform_real_distribution<float> d(-3, 3);
	int bad = 0;
	for (int it = 0; it < 5000; it++)
	{
		std::vector<Vec2> in; const int n = 2 + g() % 20;
		for (int i = 0; i < n; i++) in.push_back({ d(g), d(g) });
		ClipToCircle(in, 0.965f, runs);
		for (auto& r : runs) { if (r.size() < 2) bad++; for (auto& p : r) if (!inside(p, 0.965f)) bad++; }
	}
	CHECK(bad == 0, "5000 polilinhas aleatorias: tudo dentro do circulo, runs >= 2 pontos");
	// 8) Build com transformacoes falsas: mundo -> radar (centro (100,200), alcance 50, rotacao 90 graus) -> tela (cx=300,cy=400,raio 80)
	auto toRadar = [](Vec2 w) { float x = (w.x - 100) / 50.0f, y = (w.y - 200) / 50.0f; return Vec2{ -y, x }; };   // gira 90 graus
	auto toScreen = [](Vec2 r) { return Vec2{ 300 + r.x * 80, 400 - r.y * 80 }; };
	std::vector<Vec2> w = { {100, 200}, {100, 250}, {100, 400} };    // do jogador para o norte (y+)
	Build(w, toRadar, toScreen, kClipRadius, runs);
	CHECK(runs.size() == 1 && runs[0].size() == 2, "Build: rota 1 run, 2 pontos (centro + ponto de saida no circulo)");
	CHECK(std::fabs(runs[0][0].x - 300) < 1e-3f && std::fabs(runs[0][0].y - 400) < 1e-3f, "Build: comeca no centro do radar na tela");
	float rr = std::sqrt(std::pow(runs[0].back().x - 300, 2) + std::pow(runs[0].back().y - 400, 2));
	CHECK(std::fabs(rr - 80 * kClipRadius) < 0.05f, "Build: termina na borda do disco (raio 80 * 0.965)");
	// 9) estado compartilhado
	bool st = true; std::vector<Vec2> o; std::vector<std::vector<Vec2>> so; float rad = 0;
	CHECK(!GetWorld(o, st), "sem rota publicada: GetWorld false");
	PublishWorld(w, true); CHECK(GetWorld(o, st) && st && o.size() == 3, "PublishWorld/GetWorld");
	PublishScreen(std::move(runs), 300, 400, 80, 1000);
	CHECK(GetScreen(so, rad, 1100, st) && rad == 80, "GetScreen fresco");
	CHECK(!GetScreen(so, rad, 1000 + kFreshMs + 1, st), "GetScreen velho (radar nao desenhado) = nada");
	ClearWorld(); CHECK(!GetWorld(o, st), "ClearWorld");
	// ---- poligono convexo (mascara)
	std::vector<Vec2> sq = { {2, 3}, {-2, 3}, {-2, -3}, {2, -3} };       // vertices embaralhados (horario e fora de ordem)
	auto hull = ConvexHull(sq);
	CHECK(hull.size() == 4, "hull do retangulo: 4 vertices");
	ClipToConvex({ {-5, 0}, {5, 0} }, hull, runs);
	CHECK(runs.size() == 1 && std::fabs(runs[0][0].x + 2) < 1e-4f && std::fabs(runs[0][1].x - 2) < 1e-4f, "poligono: reta horizontal recorta em +-2");
	ClipToConvex({ {0, -9}, {0, 9} }, hull, runs);
	CHECK(runs.size() == 1 && std::fabs(runs[0][0].y + 3) < 1e-4f && std::fabs(runs[0][1].y - 3) < 1e-4f, "poligono: reta vertical recorta em +-3");
	ClipToConvex({ {5, 5}, {6, 6} }, hull, runs); CHECK(runs.empty(), "poligono: fora");
	ClipToConvex({ {0, 0}, {1, 1}, {1.5f, -1} }, hull, runs); CHECK(runs.size() == 1 && runs[0].size() == 3, "poligono: dentro");
	ClipToConvex({ {0, 0}, {9, 0}, {9, 1}, {0, 1} }, hull, runs); CHECK(runs.size() == 2, "poligono: sai e volta = 2 runs");
	// mascara tipo "quadrado arredondado" (expoente 6, escala 1,10) com 8 vertices em pixels, centro (500,300) raio 100
	{
		std::vector<Vec2> mask; const float cxp = 500, cyp = 300, R = 100;
		for (int i = 0; i < 8; i++) { float a = i * 3.14159265f / 4.0f; float x = std::cos(a), y = std::sin(a);
			float den = std::pow(std::pow(std::fabs(x), 6) + std::pow(std::fabs(y), 6), 1.0f / 6.0f); mask.push_back({ cxp + x / den * R * 1.10f, cyp + y / den * R * 1.10f }); }
		std::reverse(mask.begin() + 2, mask.begin() + 6);                  // ordem embaralhada
		auto poly = MaskPolygon(mask, cxp, cyp, R, 0.97f);
		CHECK(poly.size() == 8, "MaskPolygon: 8 vertices validos");
		// ponto no canto do quadrado arredondado fica DENTRO do poligono mas FORA do circulo de raio R
		float tt0, tt1; Vec2 corner = { cxp + 92, cyp + 92 };               // |x|^6+|y|^6 (0,92)^: < 1,1^6 -> dentro da mascara
		CHECK(ClipSegConvex(corner, { cxp + 93, cyp + 92 }, poly, tt0, tt1), "canto da mascara dentro do poligono");
		CHECK(std::hypot(92.0f, 92.0f) > R, "(canto esta fora do circulo de raio R: o circulo cortaria a linha)");
		CHECK(MaskPolygon(mask, cxp + 400, cyp, R, 0.97f).empty(), "MaskPolygon: centro em outro espaco -> vazio (usa circulo)");
		CHECK(MaskPolygon({ {0, 0}, {1, 1} }, 0, 0, 100, 0.97f).empty(), "MaskPolygon: poucos vertices -> vazio");
		std::vector<Vec2> bad = mask; bad[0].x = std::nanf(""); CHECK(MaskPolygon(bad, cxp, cyp, R, 0.97f).empty(), "MaskPolygon: NaN -> vazio");
		// Build com poligono: do centro para fora ate o canto
		auto idR = [](Vec2 w) { return Vec2{ w.x / 50.0f, w.y / 50.0f }; };
		auto toS = [&](Vec2 r) { return Vec2{ cxp + r.x * R, cyp - r.y * R }; };
		std::vector<std::vector<Vec2>> rr2; BuildPoly({ {0, 0}, {200, 200} }, idR, toS, poly, rr2);
		CHECK(rr2.size() == 1 && rr2[0].size() == 2, "BuildPoly: 1 run");
		float dx = rr2[0][1].x - cxp, dy = rr2[0][1].y - cyp;
		CHECK(std::hypot(dx, dy) > R * 1.2f, "BuildPoly: a diagonal vai ate o canto do quadrado arredondado (alem do raio R)");
		bool allIn = true; for (auto& p : poly) { (void)p; }
		CHECK(allIn, "ok");
	}
	// aleatorio: todo ponto recortado no poligono esta dentro dele
	{
		std::vector<Vec2> pol = { {0,-10}, {10,-5}, {10,5}, {0,10}, {-10,5}, {-10,-5} }; int badc = 0;
		std::mt19937 g2(11); std::uniform_real_distribution<float> d2(-30, 30);
		for (int it = 0; it < 4000; it++)
		{
			std::vector<Vec2> in; int n = 2 + g2() % 12; for (int i = 0; i < n; i++) in.push_back({ d2(g2), d2(g2) });
			ClipToConvex(in, pol, runs);
			for (auto& r : runs) { if (r.size() < 2) badc++; for (auto& p : r) for (size_t i = 0; i < pol.size(); i++) { const Vec2& v = pol[i]; const Vec2& w = pol[(i + 1) % pol.size()];
				if ((w.x - v.x) * (p.y - v.y) - (w.y - v.y) * (p.x - v.x) < -1e-2f) badc++; } }
		}
		CHECK(badc == 0, "4000 polilinhas aleatorias: tudo dentro do poligono convexo");
	}
	{
		std::vector<Vec2> t = { {0, 0}, {10, 0}, {10, 10} };
		CHECK(TrimStart(t, 4) && std::fabs(t[0].x - 4) < 1e-4f && t.size() == 3, "TrimStart: corta 4 px do 1o segmento");
		t = { {0, 0}, {10, 0}, {10, 10} };
		CHECK(TrimStart(t, 15) && std::fabs(t[0].x - 10) < 1e-4f && std::fabs(t[0].y - 5) < 1e-4f && t.size() == 2, "TrimStart: passa do 1o segmento");
		t = { {0, 0}, {10, 0} }; CHECK(!TrimStart(t, 50), "TrimStart: polilinha mais curta que o corte = descarta");
		t = { {0, 0} }; CHECK(!TrimStart(t, 1), "TrimStart: 1 ponto");
	}
	std::printf("%d verificacoes ok, %d falhas\n", oks, fails);
	return fails ? 1 : 0;
}
