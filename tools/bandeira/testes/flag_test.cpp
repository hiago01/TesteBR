#include "FlagPhysics.h"
#include "FlagModel.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace MxFlag;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("  FALHOU: " #c " -- "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static FlagModelData g_m;
static std::vector<Vec3> RestFront()
{
	std::vector<Vec3> r(g_m.cols * g_m.rows);
	for (int i = 0; i < g_m.cols * g_m.rows; i++) r[i] = { g_m.simRest[i*3], 0.0f, g_m.simRest[i*3+2] };
	return r;
}
struct Res { float tipExt, tipZ, maxStr, minMast; int resets; bool nan; float yaw; };
static Res Run(float fps, float seconds, float windDir, float wind, float yaw0 = 0, Vec3 base = {100, 200, 30}, float chgAt = -1, float dir2 = 0, float* yawAfter = nullptr)
{
	CFlagPhysics f; auto rest = RestFront();
	f.Init(rest.data(), g_m.cols, g_m.rows, 1234u);
	f.SetTransform(base, yaw0); f.SetWind(windDir, wind);
	Res r{0, 0, 1, 1e9f, 0, false, 0};
	const int frames = (int)(seconds * fps); const float dt = 1.0f / fps;
	for (int k = 0; k < frames; k++)
	{
		if (chgAt >= 0 && k * dt >= chgAt) { f.SetWind(dir2, wind); }
		f.Update(dt);
		CHECK(f.stats.lastSubsteps <= kMaxSubsteps, "substeps %d", f.stats.lastSubsteps);
		if (k > frames / 2)
		{
			r.maxStr = std::max(r.maxStr, f.stats.maxStretchSeen);
			for (int i = 0; i < f.Count(); i++)
			{
				const Vec3& p = f.Positions()[i];
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) r.nan = true;
				if (f.PinWeight(i) < 1.0f) { float d = std::hypot(p.x - base.x, p.y - base.y); r.minMast = std::min(r.minMast, d); }
			}
		}
	}
	// ponta livre: ultima coluna, linha do meio, e media da ultima coluna
	double ext = 0, tz = 0; int n = 0;
	for (int j = 0; j < f.Rows(); j++) { const Vec3& p = f.Positions()[j * f.Cols() + f.Cols() - 1]; ext += std::hypot(p.x - base.x, p.y - base.y); tz += p.z - base.z; n++; }
	r.tipExt = (float)(ext / n); r.tipZ = (float)(tz / n); r.resets = (int)f.stats.resets; r.yaw = f.Yaw();
	if (yawAfter) *yawAfter = f.Yaw();
	// pinos exatos
	for (int j = 0; j < f.Rows(); j++)
	{
		const Vec3& p = f.Positions()[j * f.Cols()], &q = rest[j * f.Cols()];
		const float c = std::cos(f.Yaw()), s = std::sin(f.Yaw());
		const float ex = base.x + q.x * c - q.y * s, ey = base.y + q.x * s + q.y * c, ez = base.z + q.z;
		CHECK(std::fabs(p.x - ex) < 1e-4f && std::fabs(p.y - ey) < 1e-4f && std::fabs(p.z - ez) < 1e-4f, "coluna 0 solta da pinagem (linha %d)", j);
	}
	return r;
}
int main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "SAMP/models/flag_maximo.dff";
	std::ifstream in(path, std::ios::binary); std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
	std::string err;
	std::printf("[1] leitor do DFF (%s, %zu bytes)\n", path, buf.size());
	CHECK(LoadFlagModel(buf.data(), buf.size(), g_m, err), "%s", err.c_str());
	if (fails) return 1;
	std::printf("  partes=%zu verts: mastro=%d base=%d tecido=%d  grade %dx%d N=%d  tris tecido=%zu mats=%zu (%s, %s)\n", g_m.parts.size(),
		g_m.parts[0].vertexCount(), g_m.parts[1].vertexCount(), g_m.parts[2].vertexCount(), g_m.cols, g_m.rows, g_m.clothVerts,
		g_m.parts[2].tris.size(), g_m.parts[2].mats.size(), g_m.parts[2].mats[0].texture.c_str(), g_m.parts[2].mats[1].texture.c_str());
	CHECK(g_m.cols == 13 && g_m.rows == 41 && g_m.clothVerts == 533, "grade inesperada");
	CHECK(g_m.parts[2].mats[0].texture == "flag_maximo" && g_m.parts[2].mats[1].texture == "flag_maximo_verso", "texturas");
	// DFF invalido nao pode travar
	{ FlagModelData t; std::string e; CHECK(!LoadFlagModel(buf.data(), 40, t, e), "truncado aceito"); CHECK(!LoadFlagModel(buf.data(), 0, t, e), "vazio aceito");
	  std::vector<uint8_t> bad = buf; for (size_t i = 100; i < bad.size(); i += 7) bad[i] ^= 0xA5; LoadFlagModel(bad.data(), bad.size(), t, e); std::printf("  DFF corrompido: sem crash (%s)\n", e.empty() ? "ok" : e.c_str()); }

	std::printf("[2] estabilidade x FPS (20 s, vento 1.0 para +X)\n");
	float ref = 0;
	for (float fps : { 10.f, 20.f, 30.f, 60.f, 90.f, 144.f })
	{
		Res r = Run(fps, 20, 0.0f, 1.0f);
		std::printf("  %5.0f fps: extensao=%.3f m  altura=%+.3f m  maxEstiramento=%.3f  menorDistMastro=%.4f  resets=%d nan=%d yaw=%.2f\n", fps, r.tipExt, r.tipZ, r.maxStr, r.minMast, r.resets, r.nan, r.yaw);
		CHECK(!r.nan && r.resets == 0, "instavel a %.0f fps", fps);
		CHECK(r.maxStr < 1.12f, "estiramento %.3f", r.maxStr);
		CHECK(r.minMast >= 0.0339f, "penetrou no mastro: %.4f", r.minMast);
		if (fps == 60) ref = r.tipExt;
	}
	std::printf("[3] vento x forma\n");
	for (float w : { 0.0f, 0.3f, 0.6f, 1.0f, 1.5f, 2.5f, 4.0f })
	{
		Res r = Run(60, 20, 0.0f, w);
		std::printf("  vento %.1f: extensao=%.3f m (de %.2f)  altura media da ponta=%+.3f m  maxEst=%.3f resets=%d\n", w, r.tipExt, 1.235f, r.tipZ, r.maxStr, r.resets);
		CHECK(!r.nan && r.resets == 0, "instavel vento %.1f", w);
		if (w == 0.0f) CHECK(r.tipExt < 0.45f, "sem vento deveria pender ao longo do mastro (ext %.3f)", r.tipExt);
		if (w == 1.0f) CHECK(r.tipExt > 0.9f, "vento normal deveria esticar o tecido (ext %.3f)", r.tipExt);
	}
	std::printf("[4] mudanca de direcao do vento (60 fps): 0 -> 90 graus aos 5 s\n");
	{ float y; Run(60, 14, 0.0f, 1.0f, 0.0f, {0,0,0}, 5.0f, 1.5707963f, &y); std::printf("  yaw final = %.2f rad (alvo 1.57)\n", y); CHECK(std::fabs(y - 1.5708f) < 0.35f, "cata-vento nao seguiu"); }
	{ float y; Run(60, 14, 0.0f, 1.0f, 0.0f, {0,0,0}, 5.0f, -3.0f, &y); std::printf("  yaw final = %.2f rad (alvo -3.00, giro de ~172 graus pelo caminho curto)\n", y); CHECK(std::fabs(std::remainder(y + 3.0f, 6.2831853f)) < 0.35f, "cata-vento 180"); }
	std::printf("[5] entradas absurdas\n");
	{ CFlagPhysics f; auto rest = RestFront(); f.Init(rest.data(), g_m.cols, g_m.rows, 1); f.SetTransform({1,2,3}, 0.5f);
	  f.SetWind(NAN, 1.0f); f.SetWind(0, INFINITY); f.Update(0); f.Update(-1); f.Update(NAN); f.Update(5.0f); CHECK(f.stats.lastSubsteps <= kMaxSubsteps, "5 s de dt");
	  f.SetWind(0, 50.0f); for (int i = 0; i < 600; i++) f.Update(1/60.f);
	  bool ok = true; for (int i = 0; i < f.Count(); i++) ok &= std::isfinite(f.Positions()[i].x + f.Positions()[i].y + f.Positions()[i].z);
	  CHECK(ok, "NaN com vento 50 (clamp em 4)"); std::printf("  dt 0/-1/NaN/5s e vento extremo: ok, resets=%u\n", f.stats.resets);
	  CFlagPhysics g; CHECK(!g.Init(nullptr, 13, 41, 1), "init nulo"); CHECK(!g.Init(rest.data(), 2, 41, 1), "grade pequena"); g.Update(0.016f); g.Reset(); }
	std::printf("[6] guarda anti-explosao\n");
	{ CFlagPhysics f; auto rest = RestFront(); f.Init(rest.data(), g_m.cols, g_m.rows, 7); f.SetTransform({0,0,0}, 0); f.SetWind(0, 1);
	  for (int i = 0; i < 120; i++) f.Update(1/60.f);
	  // injeta NaN na simulacao por parametro invalido
	  f.params.gravity = NAN; f.Update(0.02f); f.params.gravity = 9.81f; for (int i = 0; i < 60; i++) f.Update(1/60.f);
	  bool ok = true; for (int i = 0; i < f.Count(); i++) ok &= std::isfinite(f.Positions()[i].x); CHECK(ok, "nao recuperou"); CHECK(f.stats.resets >= 1, "guarda nao disparou");
	  std::printf("  NaN injetado -> reset automatico (%u) e recuperacao ok\n", f.stats.resets); }
	std::printf("[7] custo (1 flag, 4 substeps = pior caso por quadro)\n");
	{ CFlagPhysics f; auto rest = RestFront(); f.Init(rest.data(), g_m.cols, g_m.rows, 7); f.SetWind(0, 1);
	  for (int i = 0; i < 120; i++) f.Update(1/60.f);
	  auto t0 = std::chrono::steady_clock::now(); const int N = 2000; for (int i = 0; i < N; i++) f.Step();
	  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / N;
	  std::printf("  %.4f ms por passo (host x86, -O2: ARM64 de celular ~3-6x mais lento) -> pior caso 4 passos = %.3f ms\n", ms, ms * 4); }
	std::printf("\n%s (%d falhas)\n", fails ? "REPROVADO" : "APROVADO", fails);
	return fails ? 1 : 0;
}
