#include "FlagModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MxFlag
{
	namespace
	{
		constexpr uint32_t kStruct = 1, kString = 2, kTexture = 6, kMaterial = 7, kMaterialList = 8,
						   kGeometry = 15, kClump = 16, kGeometryList = 26;

		struct Rd
		{
			const uint8_t* d; size_t n; size_t p = 0; bool ok = true;
			Rd(const uint8_t* dd, size_t nn) : d(dd), n(nn) {}
			bool can(size_t k) const { return ok && p + k <= n; }
			template <class T> T get() { T v{}; if (!can(sizeof(T))) { ok = false; return v; } std::memcpy(&v, d + p, sizeof(T)); p += sizeof(T); return v; }
		};

		struct Hdr { uint32_t type, size, ver; };

		bool ReadHdr(Rd& r, Hdr& h)
		{
			if (!r.can(12)) return false;
			h.type = r.get<uint32_t>(); h.size = r.get<uint32_t>(); h.ver = r.get<uint32_t>();
			return r.ok && (size_t)h.size <= r.n - r.p;
		}

		bool ParseTexture(const uint8_t* d, size_t n, std::string& name)
		{
			Rd r(d, n);
			while (r.p + 12 <= n)
			{
				Hdr h; if (!ReadHdr(r, h)) return false;
				if (h.type == kString && name.empty())
				{
					const char* s = reinterpret_cast<const char*>(d + r.p);
					name.assign(s, strnlen(s, h.size));
				}
				r.p += h.size;
			}
			return true;
		}

		bool ParseMaterial(const uint8_t* d, size_t n, MeshMaterial& m)
		{
			Rd r(d, n);
			bool gotStruct = false;
			while (r.p + 12 <= n)
			{
				Hdr h; if (!ReadHdr(r, h)) return false;
				const size_t end = r.p + h.size;
				if (h.type == kStruct && !gotStruct)
				{
					if (h.size < 16) return false;
					r.get<uint32_t>();                                           // flags
					m.r = r.get<uint8_t>(); m.g = r.get<uint8_t>(); m.b = r.get<uint8_t>(); m.a = r.get<uint8_t>();
					gotStruct = true;
				}
				else if (h.type == kTexture) { if (!ParseTexture(d + r.p, h.size, m.texture)) return false; }
				r.p = end;
			}
			return gotStruct;
		}

		bool ParseMaterialList(const uint8_t* d, size_t n, std::vector<MeshMaterial>& mats)
		{
			Rd r(d, n);
			while (r.p + 12 <= n)
			{
				Hdr h; if (!ReadHdr(r, h)) return false;
				const size_t end = r.p + h.size;
				if (h.type == kMaterial)
				{
					MeshMaterial m; if (!ParseMaterial(d + r.p, h.size, m)) return false;
					mats.push_back(m);
				}
				r.p = end;
			}
			return true;
		}

		bool ParseGeometryStruct(const uint8_t* d, size_t n, uint32_t ver, MeshPart& g, std::string& err)
		{
			Rd r(d, n);
			const uint32_t flags = r.get<uint32_t>(), nt = r.get<uint32_t>(), nv = r.get<uint32_t>(), nmorph = r.get<uint32_t>();
			if (!r.ok) { err = "geometria truncada"; return false; }
			if (nv == 0 || nv > 60000 || nt == 0 || nt > 100000 || nmorph != 1) { err = "geometria com contagens invalidas"; return false; }
			if (ver < 0x34000) { r.p += 12; }                                    // ambient/diffuse/specular (so em RW antigo)
			const uint32_t uvSets = (flags >> 16) & 0xFF ? (flags >> 16) & 0xFF : ((flags & 0x4) ? 1 : (flags & 0x80) ? 2 : 0);
			if (flags & 0x8)  r.p += (size_t)nv * 4;                              // pre-lit (cores por vertice)
			if (!r.can(0)) { err = "geometria truncada"; return false; }
			if (uvSets)
			{
				if (!r.can((size_t)nv * 8 * uvSets)) { err = "UV truncado"; return false; }
				g.uv.resize((size_t)nv * 2);
				std::memcpy(g.uv.data(), d + r.p, (size_t)nv * 8);                // so o 1o conjunto
				r.p += (size_t)nv * 8 * uvSets;
			}
			if (!r.can((size_t)nt * 8)) { err = "triangulos truncados"; return false; }
			g.tris.resize(nt);
			for (uint32_t i = 0; i < nt; i++)
			{
				const uint16_t v2 = r.get<uint16_t>(), v1 = r.get<uint16_t>(), mat = r.get<uint16_t>(), v3 = r.get<uint16_t>();
				if (v1 >= nv || v2 >= nv || v3 >= nv) { err = "indice de vertice fora da faixa"; return false; }
				g.tris[i] = { v1, v2, v3, mat };
			}
			r.p += 16;                                                            // bounding sphere
			const uint32_t hasV = r.get<uint32_t>(), hasN = r.get<uint32_t>();
			if (!r.ok || !hasV) { err = "geometria sem vertices"; return false; }
			if (!r.can((size_t)nv * 12)) { err = "vertices truncados"; return false; }
			g.pos.resize((size_t)nv * 3);
			std::memcpy(g.pos.data(), d + r.p, (size_t)nv * 12); r.p += (size_t)nv * 12;
			if (hasN)
			{
				if (!r.can((size_t)nv * 12)) { err = "normais truncadas"; return false; }
				g.nrm.resize((size_t)nv * 3);
				std::memcpy(g.nrm.data(), d + r.p, (size_t)nv * 12); r.p += (size_t)nv * 12;
			}
			for (float f : g.pos) if (!std::isfinite(f) || std::fabs(f) > 100.0f) { err = "vertice invalido (NaN/Inf ou fora de +-100 m)"; return false; }
			for (float f : g.uv) if (!std::isfinite(f)) { err = "UV nao finito"; return false; }
			return true;
		}

		bool ParseGeometry(const uint8_t* d, size_t n, MeshPart& g, std::string& err)
		{
			Rd r(d, n);
			bool gotStruct = false;
			while (r.p + 12 <= n)
			{
				Hdr h; if (!ReadHdr(r, h)) { err = "chunk invalido na geometria"; return false; }
				const size_t end = r.p + h.size;
				if (h.type == kStruct && !gotStruct) { if (!ParseGeometryStruct(d + r.p, h.size, h.ver, g, err)) return false; gotStruct = true; }
				else if (h.type == kMaterialList) { if (!ParseMaterialList(d + r.p, h.size, g.mats)) { err = "lista de materiais invalida"; return false; } }
				r.p = end;
			}
			if (!gotStruct) { err = "geometria sem struct"; return false; }
			for (const MeshTri& t : g.tris) if (t.mat >= g.mats.size()) { err = "triangulo com material inexistente"; return false; }
			return true;
		}
	}

	bool LoadFlagModel(const uint8_t* data, size_t size, FlagModelData& out, std::string& err)
	{
		out = FlagModelData{};
		Rd r(data, size);
		Hdr clump;
		if (!ReadHdr(r, clump) || clump.type != kClump) { err = "nao e um DFF (Clump ausente)"; return false; }
		const size_t clumpEnd = r.p + clump.size;
		while (r.p + 12 <= clumpEnd)
		{
			Hdr h; if (!ReadHdr(r, h)) { err = "chunk invalido no Clump"; return false; }
			const size_t end = r.p + h.size;
			if (h.type == kGeometryList)
			{
				Rd g(data + r.p, h.size);
				uint32_t num = 0; bool gotNum = false;
				while (g.p + 12 <= h.size)
				{
					Hdr gh; if (!ReadHdr(g, gh)) { err = "chunk invalido na lista de geometrias"; return false; }
					const size_t gend = g.p + gh.size;
					if (gh.type == kStruct && !gotNum) { if (gh.size < 4) { err = "struct da lista de geometrias truncado"; return false; } std::memcpy(&num, data + r.p + g.p, 4); gotNum = true; }
					else if (gh.type == kGeometry)
					{
						MeshPart part;
						if (!ParseGeometry(data + r.p + g.p, gh.size, part, err)) return false;
						out.parts.push_back(std::move(part));
					}
					g.p = gend;
				}
				if (!gotNum || num != out.parts.size()) { err = "contagem de geometrias diverge"; return false; }
			}
			r.p = end;
		}
		if (out.parts.size() != 3) { err = "esperava 3 geometrias (mastro, base, tecido)"; return false; }

		// tecido = parte 2: vertices 0..N-1 = frente, N..2N-1 = verso; grade derivada da malha (1a linha = mesmo Z)
		const MeshPart& cl = out.parts[2];
		const int nv = cl.vertexCount();
		if (nv < 18 || (nv & 1)) { err = "tecido com numero de vertices invalido"; return false; }
		const int N = nv / 2;
		int cols = 0;
		while (cols < N && cl.pos[(size_t)cols * 3 + 2] == cl.pos[2]) cols++;
		if (cols < 3 || N % cols) { err = "nao foi possivel derivar a grade do tecido"; return false; }
		out.cols = cols; out.rows = N / cols; out.clothVerts = N;
		if (out.rows < 3) { err = "tecido com poucas linhas"; return false; }
		if (cl.mats.size() < 2) { err = "tecido precisa de 2 materiais (frente e verso)"; return false; }

		// a 2a metade dos triangulos e o VERSO: o k-esimo triangulo do verso usa os mesmos vertices do da frente (+N) e material 1
		if (cl.tris.size() % 2) { err = "tecido com numero impar de triangulos"; return false; }
		const size_t half = cl.tris.size() / 2;
		for (size_t k = 0; k < half; k++)
		{
			const MeshTri& f = cl.tris[k]; const MeshTri& b = cl.tris[k + half];
			if (f.a >= N || f.b >= N || f.c >= N || f.mat != 0 || b.mat != 1) { err = "tecido: frente/verso fora do padrao (material/indices)"; return false; }
			const int s0 = (int)b.a - N, s1 = (int)b.b - N, s2 = (int)b.c - N;
			const bool same = (s0 == f.a && s1 == f.c && s2 == f.b) || (s0 == f.a && s1 == f.b && s2 == f.c) ||
							  ((s0 == f.b || s0 == f.c) && (s1 == f.a || s1 == f.b || s1 == f.c) && (s2 == f.a || s2 == f.b || s2 == f.c));
			if (!same) { err = "tecido: triangulo do verso nao espelha o da frente"; return false; }
		}

		// grade de simulacao retangular = caixa do tecido
		float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
		for (int i = 0; i < N; i++)
		{
			const float x = cl.pos[(size_t)i * 3], z = cl.pos[(size_t)i * 3 + 2];
			mnx = std::min(mnx, x); mxx = std::max(mxx, x); mnz = std::min(mnz, z); mxz = std::max(mxz, z);
		}
		if (!(mxx - mnx > 0.2f) || !(mxz - mnz > 0.2f)) { err = "tecido com caixa degenerada"; return false; }
		const int C = out.cols - 1, R = out.rows - 1;
		out.simRest.resize((size_t)out.cols * out.rows * 3);
		for (int j = 0; j < out.rows; j++)
			for (int i = 0; i < out.cols; i++)
			{
				float* q = &out.simRest[((size_t)j * out.cols + i) * 3];
				q[0] = mnx + (mxx - mnx) * i / C; q[1] = 0.0f; q[2] = mnz + (mxz - mnz) * j / R;
			}
		out.skin.resize(N);
		for (int v = 0; v < N; v++)
		{
			const float fx = (cl.pos[(size_t)v * 3] - mnx) / (mxx - mnx) * C, fz = (cl.pos[(size_t)v * 3 + 2] - mnz) / (mxz - mnz) * R;
			int ci = std::clamp((int)std::floor(fx), 0, C - 1), cj = std::clamp((int)std::floor(fz), 0, R - 1);
			const float tx = std::clamp(fx - ci, 0.0f, 1.0f), tz = std::clamp(fz - cj, 0.0f, 1.0f);
			SkinVert& s = out.skin[v];
			s.idx[0] = (uint16_t)(cj * out.cols + ci);       s.w[0] = (1 - tx) * (1 - tz);
			s.idx[1] = (uint16_t)(cj * out.cols + ci + 1);   s.w[1] = tx * (1 - tz);
			s.idx[2] = (uint16_t)((cj + 1) * out.cols + ci); s.w[2] = (1 - tx) * tz;
			s.idx[3] = (uint16_t)((cj + 1) * out.cols + ci + 1); s.w[3] = tx * tz;
		}
		return true;
	}
}
