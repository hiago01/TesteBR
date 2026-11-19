#pragma once
// =====================================================================================
//  HairClothMesh - geometria de desenho do cabelo de tecido (hair6). Modulo PURO (sem RenderWare): compila no PC para teste.
//  Gera os vertices/indices (formato do RxObjSpace3DVertex, 0x24 bytes) da CALOTA (couro cabeludo) e da FOLHA DE TECIDO.
//  Cor por VERTICE (0xAARRGGBB, alfa 255) com sombreamento "baked" (o Im3D nao e iluminado): nao depende da cor do material do ped.
// =====================================================================================
#include "HairPhysics.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MxHair
{
	struct DrawVertex { float x, y, z; float nx, ny, nz; uint32_t color; float u, v; };
	static_assert(sizeof(DrawVertex) == 0x24, "DrawVertex deve ter o layout de RxObjSpace3DVertex");

	constexpr int kToothPeriod = 4;      // colunas por dente (a folha usa onda triangular: vale, meio, PONTA, meio)
	constexpr int kSheetCols   = 32;     // 8 dentes em volta da cabeca
	constexpr int kDomeSegs  = 24;
	constexpr int kDomeRings = 5;
	constexpr int kDomeVerts = kDomeSegs * (kDomeRings + 1);
	constexpr int kDomeIdx   = kDomeSegs * kDomeRings * 6;
	constexpr int kSheetMaxVerts = kMaxP;
	constexpr int kSheetMaxIdx   = (kMaxCols - 1) * (kMaxRows - 1) * 6;

	namespace mesh_detail
	{
		constexpr float kPi = 3.14159265358979f;
		inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
		inline float Smooth(float a, float b, float x) { const float t = Clamp01((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
		inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
		inline V3 Nrm(V3 a, V3 fb) { const float l = Len(a); return l > 1e-6f ? a * (1.0f / l) : fb; }
		inline uint32_t Hash(uint32_t s) { s ^= s >> 16; s *= 0x7feb352dU; s ^= s >> 15; s *= 0x846ca68bU; s ^= s >> 16; return s; }
		inline float H01(uint32_t s) { return (float)(Hash(s) & 0xFFFFFFu) / 16777215.0f; }
		struct C3 { float r, g, b; };
		inline C3 Unpack(uint32_t argb) { return { (float)((argb >> 16) & 0xFF), (float)((argb >> 8) & 0xFF), (float)(argb & 0xFF) }; }
		inline C3 Mix(C3 a, C3 b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
		inline uint32_t Pack(C3 c, float k)
		{
			auto q = [](float x) { return (uint32_t)(x < 0.0f ? 0.0f : (x > 255.0f ? 255.0f : x + 0.5f)); };
			return 0xFF000000u | (q(c.r * k) << 16) | (q(c.g * k) << 8) | q(c.b * k);
		}
		// luz de cima/lado + ambiente (igual a mangueira)
		inline float Light(V3 n)
		{
			const V3 L = Nrm({ 0.40f, 0.30f, 0.86f }, { 0, 0, 1 });
			return 0.46f + 0.54f * std::max(0.0f, Dot(n, L));
		}
	}

	// Elevacao (graus) da borda inferior da calota em funcao do azimute (a = 0 atras, + direita do ped). Na frente (testa) a borda sobe
	// (linha do cabelo ~50 graus); nos lados/atras ela encontra as raizes da folha (rootElevDeg).
	inline float DomeLowDeg(float aRad, float rootElevDeg)
	{
		using namespace mesh_detail;
		float a = std::fabs(std::fmod(aRad, 2.0f * kPi)); if (a > kPi) a = 2.0f * kPi - a;       // 0 atras .. pi frente
		const float phiFront = 180.0f - a * 180.0f / kPi;                                       // 0 = bem na frente
		return 50.0f + (rootElevDeg - 50.0f) * Smooth(38.0f, 82.0f, phiFront);
	}

	// Calota: kDomeVerts vertices / kDomeIdx indices. Retorna o numero de indices.
	inline int BuildDome(const Frame& f, uint32_t rootArgb, uint32_t seed, DrawVertex* v, uint16_t* idx)
	{
		using namespace mesh_detail;
		const Tune& T = GlobalTune();
		const V3 skull = f.head + f.up * T.skullUp;
		const float R = T.rootRadius + 0.0035f;
		const C3 col = Unpack(rootArgb);
		for (int i = 0; i < kDomeSegs; ++i)
		{
			const float a = 2.0f * kPi * (float)i / (float)kDomeSegs;
			const V3 d = f.right * std::sin(a) - f.fwd * std::cos(a);
			const float low = DomeLowDeg(a, T.rootElevDeg) * kPi / 180.0f;
			for (int j = 0; j <= kDomeRings; ++j)
			{
				const float t = (float)j / (float)kDomeRings;
				const float el = (kPi * 0.5f) + (low - kPi * 0.5f) * t;                          // 90 graus (coroa) -> borda
				const V3 n = d * std::cos(el) + f.up * std::sin(el);
				const V3 p = skull + n * R;
				DrawVertex& o = v[i * (kDomeRings + 1) + j];
				o.x = p.x; o.y = p.y; o.z = p.z; o.nx = n.x; o.ny = n.y; o.nz = n.z;
				const float grain = 0.93f + 0.07f * H01(seed * 7919u + (uint32_t)(i * 31 + j));
				o.color = Pack(col, Light(n) * grain * (0.82f + 0.18f * (1.0f - t)));          // um pouco mais escuro perto da borda
				o.u = (float)i / (float)kDomeSegs; o.v = t;
			}
		}
		int ni = 0;
		for (int i = 0; i < kDomeSegs; ++i)
		{
			const int i1 = (i + 1) % kDomeSegs;
			for (int j = 0; j < kDomeRings; ++j)
			{
				const uint16_t a = (uint16_t)(i * (kDomeRings + 1) + j), b = (uint16_t)(i1 * (kDomeRings + 1) + j);
				const uint16_t c = (uint16_t)(i * (kDomeRings + 1) + j + 1), d = (uint16_t)(i1 * (kDomeRings + 1) + j + 1);
				idx[ni++] = a; idx[ni++] = b; idx[ni++] = c;
				idx[ni++] = b; idx[ni++] = d; idx[ni++] = c;
			}
		}
		return ni;
	}

	// Folha de tecido: cols*rows vertices (pos/nrm vindos da simulacao), (cols-1)*(rows-1)*2 triangulos. Duas faces (cull desligado):
	// a normal usada na luz e virada para a camera. Gradiente RAIZ -> PONTA por linha + pontas dos dentes um pouco mais claras.
	// Retorna o numero de indices; nv recebe o numero de vertices.
	inline int BuildSheet(const Sim& sim, const V3* pos, const V3* nrm, V3 cam, uint32_t rootArgb, uint32_t tipArgb, uint32_t seed,
						  DrawVertex* v, uint16_t* idx, int& nv)
	{
		using namespace mesh_detail;
		const int C = sim.Cols(), Rw = sim.Rows();
		nv = C * Rw;
		const C3 cr = Unpack(rootArgb), ct = Unpack(tipArgb);
		for (int r = 0; r < Rw; ++r)
		{
			const float t = (float)r / (float)(Rw - 1);
			const float mix = Smooth(0.12f, 1.0f, t);
			const float ao  = 0.72f + 0.28f * Smooth(0.0f, 0.30f, t);                       // sombra perto do couro cabeludo
			for (int c = 0; c < C; ++c)
			{
				const int i = r * C + c;
				V3 n = nrm[i];
				if (Dot(n, pos[i] - cam) > 0.0f) n = n * -1.0f;                             // face voltada para a camera
				const float tooth = (c % kToothPeriod == kToothPeriod / 2) ? 1.05f : 0.97f;   // leve brilho na PONTA do dente
				const float grain = 0.95f + 0.05f * H01(seed * 104729u + (uint32_t)i);
				DrawVertex& o = v[i];
				o.x = pos[i].x; o.y = pos[i].y; o.z = pos[i].z; o.nx = n.x; o.ny = n.y; o.nz = n.z;
				o.color = Pack(Mix(cr, ct, mix), Light(n) * ao * grain * (1.0f + (tooth - 1.0f) * t));
				o.u = (float)c / (float)(C - 1); o.v = t;
			}
		}
		int ni = 0;
		for (int r = 0; r + 1 < Rw; ++r)
			for (int c = 0; c + 1 < C; ++c)
			{
				const uint16_t a = (uint16_t)(r * C + c), b = (uint16_t)(r * C + c + 1), cc = (uint16_t)((r + 1) * C + c), d = (uint16_t)((r + 1) * C + c + 1);
				idx[ni++] = a; idx[ni++] = b; idx[ni++] = cc;
				idx[ni++] = b; idx[ni++] = d; idx[ni++] = cc;
			}
		return ni;
	}
}
