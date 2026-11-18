#include <cstdlib>
#include "FlagMesh.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
using namespace MxFlag;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("  FALHOU: " #c " -- "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
struct Mock : ISink
{
	int calls[3] = {0,0,0}, tris[3] = {0,0,0}, maxV = 0, bad = 0, degenerate = 0;
	std::set<float> uvMin;
	void Submit(int pass, const DrawVertex* v, int nv, const uint16_t* idx, int ni) override
	{
		calls[pass]++; tris[pass] += ni / 3; maxV = std::max(maxV, nv);
		if (ni % 3) bad++;
		for (int i = 0; i < ni; i++) if (idx[i] >= nv) bad++;
		for (int i = 0; i + 2 < ni; i += 3) { if (idx[i] == idx[i+1] || idx[i+1] == idx[i+2] || idx[i] == idx[i+2]) degenerate++; }
		for (int i = 0; i < nv; i++) if (!std::isfinite(v[i].x + v[i].y + v[i].z + v[i].u + v[i].v)) bad++;
	}
};
int main()
{
	std::ifstream in(getenv("FLAG_DFF") ? getenv("FLAG_DFF") : "SAMP/models/flag_maximo.dff", std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {});
	FlagModelData m; std::string e; CHECK(LoadFlagModel(b.data(), b.size(), m, e), "%s", e.c_str());
	CFlagMesh mesh; CHECK(mesh.Init(&m), "init");
	std::vector<Vec3> rest(m.cols * m.rows); for (size_t i = 0; i < rest.size(); i++) rest[i] = { m.simRest[i*3], 0, m.simRest[i*3+2] };
	CFlagPhysics f; f.Init(rest.data(), m.cols, m.rows, 5); f.SetTransform({10, 20, 5}, 0); f.SetWind(0, 1);
	for (int i = 0; i < 300; i++) f.Update(1/60.f);
	std::vector<Vec3> rp(f.Count()), nr(f.Count());
	f.GetRenderPositions(rp.data()); f.ComputeNormals(rp.data(), nr.data());
	mesh.SetStatic({10, 20, 5}, 1.0f);
	const int half = (int)m.parts[2].tris.size() / 2;
	struct Cam { const char* nome; Vec3 c; } cams[] = { {"frente (-Y)", {13, 10, 6}}, {"verso (+Y)", {13, 30, 6}}, {"de cima", {13, 20, 50}}, {"lateral +X", {40, 20, 6}} };
	for (auto& c : cams)
	{
		mesh.UpdateCloth(rp.data(), nr.data(), c.c, 1.0f);
		Mock s; mesh.Draw(s);
		std::printf("  camera %-12s: estatico %d tri (%d lotes) | frente %d tri (%d) | verso %d tri (%d) | maior lote %d vert\n", c.nome, s.tris[0], s.calls[0], s.tris[1], s.calls[1], s.tris[2], s.calls[2], s.maxV);
		CHECK(s.tris[1] + s.tris[2] == half, "cada triangulo do tecido deve ser desenhado exatamente 1x (%d+%d != %d)", s.tris[1], s.tris[2], half);
		CHECK(s.tris[0] == (int)(m.parts[0].tris.size() + m.parts[1].tris.size()), "estatico");
		CHECK(s.maxV <= kBatchVerts, "lote grande %d", s.maxV); CHECK(s.bad == 0, "indices/valores invalidos"); CHECK(s.degenerate == 0, "triangulo degenerado no lote");
	}
	// bandeira em repouso e camera bem na frente: tudo frente; camera atras: tudo verso
	{ CFlagPhysics r; r.Init(rest.data(), m.cols, m.rows, 5); r.SetTransform({0,0,0}, 0); r.SetWind(0, 0);
	  std::vector<Vec3> p(r.Count()), n(r.Count()); r.GetRenderPositions(p.data()); r.ComputeNormals(p.data(), n.data());
	  mesh.UpdateCloth(p.data(), n.data(), {1, -10, 2}, 1); CHECK(mesh.frontTris() == half && mesh.backTris() == 0, "repouso/frente: %d/%d", mesh.frontTris(), mesh.backTris());
	  mesh.UpdateCloth(p.data(), n.data(), {1, +10, 2}, 1); CHECK(mesh.frontTris() == 0 && mesh.backTris() == half, "repouso/verso: %d/%d", mesh.frontTris(), mesh.backTris());
	  std::printf("  repouso: camera -Y => %d frente / 0 verso; camera +Y => 0 frente / %d verso\n", half, half); }
	std::printf("%s (%d falhas)\n", fails ? "REPROVADO" : "APROVADO", fails); return fails ? 1 : 0;
}
