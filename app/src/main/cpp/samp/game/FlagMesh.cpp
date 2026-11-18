#include "FlagMesh.h"

#include <algorithm>
#include <cmath>

namespace MxFlag
{
	namespace
	{
		inline uint32_t Pack(float r, float g, float b)
		{
			auto c = [](float v) { return (uint32_t)std::clamp((int)(v + 0.5f), 0, 255); };
			return (0xFFu << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
		}

		// luz fixa (de cima/lado) + ambiente, dupla face
		inline float Lambert(float nx, float ny, float nz)
		{
			constexpr float lx = 0.40f, ly = 0.30f, lz = 0.86f;
			const float l = std::sqrt(lx * lx + ly * ly + lz * lz);
			return 0.50f + 0.50f * std::fabs((nx * lx + ny * ly + nz * lz) / l);
		}

		void Flush(ISink& sink, int pass, const DrawVertex* verts, std::vector<int>& remap,
				   std::vector<DrawVertex>& lv, std::vector<int>& lg, std::vector<uint16_t>& li)
		{
			if (!li.empty()) sink.Submit(pass, lv.data(), (int)lv.size(), li.data(), (int)li.size());
			for (int g : lg) remap[g] = -1;
			lv.clear(); lg.clear(); li.clear();
			(void)verts;
		}

		// divide uma lista de triangulos (indices globais) em lotes com <= kBatchVerts vertices distintos
		void SubmitBatches(ISink& sink, int pass, const DrawVertex* verts, int nVerts, const std::vector<uint32_t>& tris, std::vector<int>& remap)
		{
			if (tris.empty()) return;
			if ((int)remap.size() < nVerts) remap.assign(nVerts, -1);
			std::vector<DrawVertex> lv; std::vector<int> lg; std::vector<uint16_t> li;
			lv.reserve(kBatchVerts); lg.reserve(kBatchVerts); li.reserve(kBatchVerts * 6);
			for (size_t k = 0; k + 2 < tris.size(); k += 3)
			{
				const uint32_t t[3] = { tris[k], tris[k + 1], tris[k + 2] };
				int fresh = 0;
				for (int q = 0; q < 3; q++)
				{
					if (remap[t[q]] >= 0) continue;
					bool dup = false;
					for (int p = 0; p < q; p++) if (t[p] == t[q]) dup = true;
					if (!dup) fresh++;
				}
				if ((int)lv.size() + fresh > kBatchVerts) Flush(sink, pass, verts, remap, lv, lg, li);
				for (int q = 0; q < 3; q++)
				{
					int& r = remap[t[q]];
					if (r < 0) { r = (int)lv.size(); lv.push_back(verts[t[q]]); lg.push_back((int)t[q]); }
					li.push_back((uint16_t)r);
				}
			}
			Flush(sink, pass, verts, remap, lv, lg, li);
		}
	}

	bool CFlagMesh::Init(const FlagModelData* model)
	{
		m_model = nullptr;
		if (!model || model->parts.size() != 3 || model->clothVerts <= 0) return false;
		m_model = model;
		const int N = model->clothVerts;
		m_cloth.assign((size_t)N * 2, DrawVertex{});
		const MeshPart& cl = model->parts[2];
		if ((int)cl.uv.size() < N * 4) { m_model = nullptr; return false; }       // precisa de UV da frente e do verso
		for (int i = 0; i < N * 2; i++) { m_cloth[i].u = cl.uv[(size_t)i * 2]; m_cloth[i].v = cl.uv[(size_t)i * 2 + 1]; m_cloth[i].color = 0xFFFFFFFFu; }
		// static: mastro (parte 0) + base (parte 1)
		m_staticTris.clear();
		int off = 0;
		m_static.assign((size_t)(model->parts[0].vertexCount() + model->parts[1].vertexCount()), DrawVertex{});
		for (int p = 0; p < 2; p++)
		{
			const MeshPart& mp = model->parts[p];
			for (const MeshTri& t : mp.tris) { m_staticTris.push_back(off + t.a); m_staticTris.push_back(off + t.b); m_staticTris.push_back(off + t.c); }
			off += mp.vertexCount();
		}
		m_remap.assign(std::max((int)m_static.size(), N * 2), -1);
		SetStatic(Vec3{}, 1.0f);
		return true;
	}

	void CFlagMesh::SetStatic(const Vec3& base, float brightness)
	{
		if (!m_model) return;
		int off = 0;
		for (int p = 0; p < 2; p++)
		{
			const MeshPart& mp = m_model->parts[p];
			const int nv = mp.vertexCount();
			std::vector<uint8_t> mat(nv, 0);                      // material por vertice (o ultimo triangulo que o usa)
			for (const MeshTri& t : mp.tris) { mat[t.a] = (uint8_t)t.mat; mat[t.b] = (uint8_t)t.mat; mat[t.c] = (uint8_t)t.mat; }
			for (int i = 0; i < nv; i++)
			{
				DrawVertex& d = m_static[(size_t)off + i];
				d.x = base.x + mp.pos[(size_t)i * 3]; d.y = base.y + mp.pos[(size_t)i * 3 + 1]; d.z = base.z + mp.pos[(size_t)i * 3 + 2];
				const float nx = mp.nrm.empty() ? 0 : mp.nrm[(size_t)i * 3], ny = mp.nrm.empty() ? 0 : mp.nrm[(size_t)i * 3 + 1], nz = mp.nrm.empty() ? 1 : mp.nrm[(size_t)i * 3 + 2];
				d.nx = nx; d.ny = ny; d.nz = nz;
				const MeshMaterial& m = mp.mats[std::min<size_t>(mat[i], mp.mats.size() - 1)];
				const float k = brightness * Lambert(nx, ny, nz);
				d.color = Pack(m.r * k, m.g * k, m.b * k);
			}
			off += nv;
		}
	}

	void CFlagMesh::UpdateCloth(const Vec3* sim, const Vec3* simNrm, const Vec3& cam, float brightness)
	{
		if (!m_model) return;
		const int N = m_model->clothVerts;
		const MeshPart& cl = m_model->parts[2];
		for (int v = 0; v < N; v++)
		{
			const SkinVert& s = m_model->skin[v];
			Vec3 p{}, n{};
			for (int q = 0; q < 4; q++)
			{
				const Vec3& sp = sim[s.idx[q]]; const Vec3& sn = simNrm[s.idx[q]]; const float w = s.w[q];
				p.x += sp.x * w; p.y += sp.y * w; p.z += sp.z * w;
				n.x += sn.x * w; n.y += sn.y * w; n.z += sn.z * w;
			}
			const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			if (nl > 1e-6f) { n.x /= nl; n.y /= nl; n.z /= nl; } else { n = Vec3{ 0, -1, 0 }; }
			const uint32_t col = [&] { const float k = 255.0f * brightness * Lambert(n.x, n.y, n.z); return Pack(k, k, k); }();
			DrawVertex& f = m_cloth[v]; DrawVertex& b = m_cloth[(size_t)N + v];
			f.x = b.x = p.x; f.y = b.y = p.y; f.z = b.z = p.z;
			f.nx = n.x; f.ny = n.y; f.nz = n.z; b.nx = -n.x; b.ny = -n.y; b.nz = -n.z;
			f.color = b.color = col;
		}

		// cada triangulo e desenhado pelo lado voltado para a camera
		m_front.clear(); m_back.clear();
		const size_t half = cl.tris.size() / 2;
		for (size_t k = 0; k < half; k++)
		{
			const MeshTri& f = cl.tris[k];
			const DrawVertex &A = m_cloth[f.a], &B = m_cloth[f.b], &C = m_cloth[f.c];
			const float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
			const float fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;   // normal da FRENTE (a,b,c anti-horario visto da frente)
			const float cx = (A.x + B.x + C.x) / 3 - cam.x, cy = (A.y + B.y + C.y) / 3 - cam.y, cz = (A.z + B.z + C.z) / 3 - cam.z;
			if (fx * cx + fy * cy + fz * cz <= 0.0f)   // camera do lado da frente (vetor camera->tri aponta contra a normal)
			{
				m_front.push_back(f.a); m_front.push_back(f.b); m_front.push_back(f.c);
			}
			else
			{
				const MeshTri& b = cl.tris[k + half];
				m_back.push_back(b.a); m_back.push_back(b.b); m_back.push_back(b.c);
			}
		}
	}

	void CFlagMesh::Draw(ISink& sink) const
	{
		if (!m_model) return;
		SubmitBatches(sink, PassStatic, m_static.data(), (int)m_static.size(), m_staticTris, m_remap);
		SubmitBatches(sink, PassFront,  m_cloth.data(),  (int)m_cloth.size(),  m_front,      m_remap);
		SubmitBatches(sink, PassBack,   m_cloth.data(),  (int)m_cloth.size(),  m_back,       m_remap);
	}
}
