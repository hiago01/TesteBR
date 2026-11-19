// g++ -std=c++20 -O2 -fsanitize=address,undefined -I app/src/main/cpp/samp/game testes/host_test_haircloth.cpp app/src/main/cpp/samp/game/HairPhysics.cpp -o t && ./t
// Testa a GEOMETRIA do cabelo de tecido (hair6) no host: indices, NaN, cores raiz->ponta, calota fora do cranio, frente aberta, dentes.
#include "HairClothMesh.h"
#include <cstdio>
#include <vector>
using namespace MxHair;
static int fails = 0;
static void chk(bool c, const char* m) { if (!c) { printf("FALHOU: %s\n", m); fails++; } else printf("ok: %s\n", m); }
static Frame Mk(V3 head, float yaw = 0, bool body = true) { Frame f; f.head = head; f.up = {0,0,1}; f.fwd = {-std::sin(yaw), std::cos(yaw), 0}; f.right = {std::cos(yaw), std::sin(yaw), 0}; f.body = body; return f; }
int main()
{
	const Tune& T = GlobalTune();
	Frame f = Mk({100, 200, 31}, 0.6f);
	const V3 skull = f.head + f.up * T.skullUp;
	// ---- calota
	static DrawVertex dv[kDomeVerts]; static uint16_t di[kDomeIdx];
	const uint32_t root = 0xFFE95151u;
	const int nd = BuildDome(f, root, 5, dv, di);
	chk(nd == kDomeIdx, "calota: numero de indices");
	bool inr = true, fin = true; for (int i = 0; i < nd; ++i) if (di[i] >= kDomeVerts) inr = false;
	for (int i = 0; i < kDomeVerts; ++i) if (!std::isfinite(dv[i].x + dv[i].y + dv[i].z + dv[i].nx + dv[i].ny + dv[i].nz)) fin = false;
	chk(inr, "calota: indices dentro do vetor"); chk(fin, "calota: sem NaN");
	float minR = 9, maxR = 0, zmin = 9; for (int i = 0; i < kDomeVerts; ++i) { V3 p{dv[i].x, dv[i].y, dv[i].z}; V3 d = p - skull; float r = std::sqrt(Dot(d, d)); minR = std::min(minR, r); maxR = std::max(maxR, r); zmin = std::min(zmin, d.z); }
	printf("   raio da calota %.4f..%.4f m (cranio %.3f) | borda mais baixa %.1f mm sob o centro do cranio\n", minR, maxR, T.skullR, -zmin * 1000);
	chk(minR > T.skullR + 0.004f, "calota: sempre FORA do cranio (>= 4 mm)");
	// elevacao da borda: frente sobe, lados/atras = rootElev
	chk(std::fabs(DomeLowDeg(0.0f, T.rootElevDeg) - T.rootElevDeg) < 0.5f, "borda atras = elevacao das raizes");
	chk(DomeLowDeg(3.14159265f, T.rootElevDeg) > 48.0f, "borda na testa sobe (~50 graus)");
	// frente: nenhum vertice da calota abaixo de 45 graus de elevacao na direcao do rosto
	{ float minEl = 90; for (int i = 0; i < kDomeVerts; ++i) { V3 d = V3{dv[i].x, dv[i].y, dv[i].z} - skull; float h = std::sqrt(Dot(d,d)); float fw = Dot(d, f.fwd) / h; float el = std::asin(Dot(d, f.up) / h) * 57.2958f; if (fw > 0.9f) minEl = std::min(minEl, el); } chk(minEl > 42.0f, "testa livre (calota nao desce pela frente do rosto)"); }
	// cor: raiz E95151 (shade<=1) -> canal R dominante
	{ int ok = 0; for (int i = 0; i < kDomeVerts; ++i) { uint32_t c = dv[i].color; if ((c >> 24) == 0xFF && ((c >> 16) & 0xFF) > ((c >> 8) & 0xFF)) ok++; } chk(ok == kDomeVerts, "calota: alfa 255 e matiz da cor da raiz"); }
	// ---- folha
	for (int var = 1; var <= 3; ++var)
	{
		Shape sh; sh.cols = kSheetCols; sh.rows = 10; sh.arcDeg = 250; sh.ragged = 0.18f; sh.spikePeriod = kToothPeriod; sh.spikeDepth = 0.5f; sh.length = var == 1 ? 0.30f : var == 2 ? 0.46f : 0.62f;
		Sim s; chk(s.Init(sh, 11), "folha: Init");
		s.SetFrame(f); s.SetWind(0.8f, 0.5f);
		for (int i = 0; i < 400; ++i) { s.SetFrame(f); s.Update(1 / 60.f); }
		std::vector<V3> pos(kMaxP), nrm(kMaxP); s.GetRenderPositions(pos.data()); s.ComputeNormals(pos.data(), nrm.data());
		static DrawVertex sv[kSheetMaxVerts]; static uint16_t si[kSheetMaxIdx]; int nv = 0;
		const int ni = BuildSheet(s, pos.data(), nrm.data(), V3{f.head.x + 2, f.head.y - 2, f.head.z + 1}, 0xFF1E1A1Au, 0xFF2ECC40u, 3, sv, si, nv);
		char m[96];
		std::snprintf(m, sizeof m, "folha v%d: %d vertices / %d indices (esperado %d / %d)", var, nv, ni, kSheetCols * 10, (kSheetCols - 1) * 9 * 6); chk(nv == kSheetCols * 10 && ni == (kSheetCols - 1) * 9 * 6, m);
		bool r = true, ff = true; for (int i = 0; i < ni; ++i) if (si[i] >= nv) r = false;
		for (int i = 0; i < nv; ++i) if (!std::isfinite(sv[i].x + sv[i].y + sv[i].z + sv[i].nx + sv[i].ny + sv[i].nz)) ff = false;
		chk(r && ff, "folha: indices validos e sem NaN");
		// gradiente: linha da raiz ~ cor raiz (escura), linha da ponta ~ verde (G dominante)
		float rootG = 0, tipG = 0, tipR = 0; for (int c = 0; c < kSheetCols; ++c) { rootG += (sv[c].color >> 8) & 0xFF; tipG += (sv[9 * kSheetCols + c].color >> 8) & 0xFF; tipR += (sv[9 * kSheetCols + c].color >> 16) & 0xFF; }
		chk(tipG > 2.0f * rootG, "folha: gradiente raiz(escuro) -> ponta(verde)"); chk(tipG > 1.5f * tipR, "folha: ponta com matiz verde");
		// frente aberta: nenhuma raiz perto da direcao do rosto
		float maxFront = -1; for (int c = 0; c < kSheetCols; ++c) { V3 d = pos[c] - skull; float h = std::sqrt(d.x*d.x + d.y*d.y); if (h > 1e-4f) maxFront = std::max(maxFront, (d.x * f.fwd.x + d.y * f.fwd.y) / h); }
		std::snprintf(m, sizeof m, "folha v%d: raizes nao cobrem o rosto (cos max %.2f < 0.65)", var, maxFront); chk(maxFront < 0.65f, m);
		// dentes: comprimentos alternam
		float mn = 9, mx = 0; for (int c = 0; c < kSheetCols; ++c) { V3 a = pos[9 * kSheetCols + c] - pos[c]; float l = std::sqrt(Dot(a, a)); mn = std::min(mn, l); mx = std::max(mx, l); }
		std::snprintf(m, sizeof m, "folha v%d: dentes (%.2f..%.2f m)", var, mn, mx); chk(mx > 1.3f * mn, m);
		// nenhum vertice da folha dentro do cranio
		float worst = 9; for (int i = 0; i < nv; ++i) { V3 d = V3{sv[i].x, sv[i].y, sv[i].z} - skull; worst = std::min(worst, std::sqrt(Dot(d, d)) - T.skullR); }
		std::snprintf(m, sizeof m, "folha v%d: fora do cranio (folga min %.1f mm)", var, worst * 1000); chk(worst > -0.004f, m);
	}
	printf(fails ? "FALHAS %d\n" : "TUDO OK\n", fails);
	return fails;
}
