#include "FlagPhysics.h"

#include <algorithm>
#include <cmath>

namespace MxFlag
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		inline Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		inline bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

		inline float Wrap(float a)
		{
			while (a >  kPi) a -= 2 * kPi;
			while (a < -kPi) a += 2 * kPi;
			return a;
		}

		inline float Lcg01(uint32_t& s) { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFF) / 16777216.0f; }

		enum EdgeKind : uint8_t { kStruct = 0, kShear = 1, kBend = 2 };
	}

	// ---------------------------------------------------------------------------------
	bool CFlagPhysics::Init(const Vec3* rest, int cols, int rows, uint32_t seed)
	{
		m_cols = m_rows = 0;
		if (!rest || cols < 3 || rows < 3 || cols * rows > 4096) return false;
		for (int i = 0; i < cols * rows; i++) if (!Finite(rest[i])) return false;

		m_cols = cols; m_rows = rows;
		const int n = cols * rows;
		m_rest.assign(rest, rest + n);
		m_p.assign(n, Vec3{}); m_prev.assign(n, Vec3{}); m_wind.assign(n, Vec3{});

		// mascara de pinos: coluna 0 fixa; 1 e 2 macias (puxam o tecido junto ao mastro)
		m_pin.assign(n, 0.0f);
		for (int j = 0; j < rows; j++)
		{
			m_pin[j * cols + 0] = 1.0f;
			m_pin[j * cols + 1] = 0.12f;
			if (cols > 3) m_pin[j * cols + 2] = 0.04f;
		}

		// triangulos (mesma ordem do flag_maximo.dff: frente voltada para -Y local)
		m_tris.clear();
		for (int j = 0; j < rows - 1; j++)
			for (int i = 0; i < cols - 1; i++)
			{
				const uint32_t v00 = j * cols + i, v10 = v00 + 1, v01 = v00 + cols, v11 = v01 + 1;
				m_tris.insert(m_tris.end(), { v00, v10, v01, v10, v11, v01 });
			}

		uint32_t s = seed ? seed : 0x9E3779B9u;
		for (float& f : m_seedPh) f = Lcg01(s) * 2 * kPi;

		buildEdges();
		params = PhysicsParams{};
		stats  = Stats{};
		m_time = 0; m_acc = 0; m_yawVel = 0;
		SetTransform(Vec3{}, 0.0f);
		return true;
	}

	void CFlagPhysics::buildEdges()
	{
		m_edges.clear();
		auto add = [&](int a, int b, EdgeKind k)
		{
			const Vec3 d = m_rest[a] - m_rest[b];
			const float l = std::sqrt(Dot(d, d));
			if (l < 1e-6f) return;
			Edge e; e.a = (uint16_t)a; e.b = (uint16_t)b; e.rest = l; e.kind = k;
			m_edges.push_back(e);
		};
		const int C = m_cols, R = m_rows;
		for (int j = 0; j < R; j++)
			for (int i = 0; i < C; i++)
			{
				const int a = j * C + i;
				if (i + 1 < C) add(a, a + 1, kStruct);                    // horizontal
				if (j + 1 < R) add(a, a + C, kStruct);                    // vertical
				if (i + 1 < C && j + 1 < R) { add(a, a + C + 1, kShear); add(a + 1, a + C, kShear); }   // diagonais
			}
		m_nonBend = (int)m_edges.size();
		for (int j = 0; j < R; j++)                                          // flexao (fraca), por ultimo
			for (int i = 0; i < C; i++)
			{
				const int a = j * C + i;
				if (i + 2 < C) add(a, a + 2, kBend);
				if (j + 2 < R) add(a, a + 2 * C, kBend);
			}
	}

	void CFlagPhysics::target(int i, Vec3& out) const
	{
		const float c = std::cos(m_yaw), s = std::sin(m_yaw);
		const Vec3& r = m_rest[i];
		out = { m_base.x + r.x * c - r.y * s, m_base.y + r.x * s + r.y * c, m_base.z + r.z };
	}

	void CFlagPhysics::SetTransform(const Vec3& basePos, float yaw)
	{
		m_base = basePos; m_yaw = Wrap(yaw);
		Reset();
	}

	void CFlagPhysics::SetWind(float dirRad, float intensity)
	{
		if (!std::isfinite(dirRad) || !std::isfinite(intensity)) return;
		m_windDir = Wrap(dirRad);
		m_windInt = std::clamp(intensity, 0.0f, 4.0f);
	}

	void CFlagPhysics::Reset()
	{
		if (!Ready()) return;
		const float nx = -std::sin(m_yaw), ny = std::cos(m_yaw);
		for (int i = 0; i < Count(); i++)
		{
			target(i, m_p[i]);
			if (m_pin[i] < 1.0f)       // ondulacao minima fora do plano: sem ela o tecido plano nunca "flamba" ao pender
			{
				const float x = m_rest[i].x, z = m_rest[i].z;
				const float off = 0.006f * std::sin(7.3f * x + 3.1f * z + m_seedPh[2]) * std::min(x * 2.0f, 1.0f);
				m_p[i].x += nx * off; m_p[i].y += ny * off;
			}
			m_prev[i] = m_p[i];
		}
		m_yawVel = 0; m_acc = 0;
	}

	bool CFlagPhysics::sane() const
	{
		for (const Vec3& p : m_p)
		{
			if (!Finite(p)) return false;
			const Vec3 d = p - m_base;
			if (Dot(d, d) > 8.0f * 8.0f) return false;      // tecido tem ~2.7 m: passou de 8 m = explodiu
		}
		return std::isfinite(m_yaw) && std::isfinite(m_yawVel);
	}

	// ---------------------------------------------------------------------------------
	void CFlagPhysics::sampleWind(Vec3* out) const
	{
		const float t = m_time;
		const float gust = 1.0f + params.gustAmp * (0.6f * std::sin(0.85f * t + m_seedPh[0]) + 0.4f * std::sin(2.10f * t + m_seedPh[1]));
		const float speed = params.baseSpeed * m_windInt * gust;
		const float dx = std::cos(m_windDir), dy = std::sin(m_windDir);
		const float nx = -std::sin(m_yaw), ny = std::cos(m_yaw);        // normal horizontal do tecido (mundo)
		const float W = std::max(m_rest[m_cols - 1].x, 0.5f);
		for (int i = 0; i < Count(); i++)
		{
			const Vec3& r = m_rest[i];
			const float xn = std::clamp(r.x / W, 0.0f, 1.0f);
			const float amp = 0.30f + 0.70f * xn;                        // a ponta livre balanca mais que a base
			// ondas que viajam do mastro para a ponta, com fase diferente por posicao
			const float wLat = std::sin(14.0f * t - 6.0f * r.x + 2.2f * r.z + m_seedPh[2]);
			const float wUp  = std::sin( 9.0f * t - 3.5f * r.x + 1.3f * r.z + m_seedPh[3]);
			const float lat = params.flutterAmp * speed * amp * wLat;
			const float up  = 0.20f * speed * amp * wUp;
			out[i] = { dx * speed + nx * lat, dy * speed + ny * lat, up };
		}
	}

	void CFlagPhysics::integrate(const Vec3* wind)
	{
		const int n = Count();
		static thread_local std::vector<Vec3>  accV;
		static thread_local std::vector<float> accW;
		accV.assign(n, Vec3{}); accW.assign(n, 0.0f);

		const float invDt = 1.0f / kStepDt;
		for (size_t k = 0; k + 2 < m_tris.size(); k += 3)
		{
			const uint32_t a = m_tris[k], b = m_tris[k + 1], c = m_tris[k + 2];
			Vec3 nrm = Cross(m_p[b] - m_p[a], m_p[c] - m_p[a]);
			const float area2 = std::sqrt(Dot(nrm, nrm));              // 2x area
			if (area2 < 1e-9f) continue;
			nrm = nrm * (1.0f / area2);
			const Vec3 vel = ((m_p[a] - m_prev[a]) + (m_p[b] - m_prev[b]) + (m_p[c] - m_prev[c])) * (invDt / 3.0f);
			const Vec3 u   = (wind[a] + wind[b] + wind[c]) * (1.0f / 3.0f) - vel;   // vento relativo
			const float un = Dot(u, nrm);
			const Vec3  ut = u - nrm * un;
			const float utl = std::sqrt(Dot(ut, ut));
			// aceleracao (forca/massa e ambas proporcionais a area): pressao na normal + arrasto tangencial
			const Vec3 acc = nrm * (params.pressureK * std::fabs(un) * un) + ut * (params.tangentK * utl);
			const Vec3 w = acc * area2;
			accV[a] = accV[a] + w; accV[b] = accV[b] + w; accV[c] = accV[c] + w;
			accW[a] += area2; accW[b] += area2; accW[c] += area2;
		}

		const float dt2 = kStepDt * kStepDt;
		constexpr float kMaxMove = 0.30f;                              // m por passo (18 m/s): corta picos numericos
		for (int i = 0; i < n; i++)
		{
			if (m_pin[i] >= 1.0f) { target(i, m_p[i]); m_prev[i] = m_p[i]; continue; }
			Vec3 a = accW[i] > 0 ? accV[i] * (1.0f / accW[i]) : Vec3{};
			a.z -= params.gravity;
			Vec3 mv = (m_p[i] - m_prev[i]) * params.damping + a * dt2;
			const float ml = std::sqrt(Dot(mv, mv));
			if (ml > kMaxMove) mv = mv * (kMaxMove / ml);
			m_prev[i] = m_p[i];
			m_p[i] = m_p[i] + mv;
		}
	}

	void CFlagPhysics::solve()
	{
		const int ne = (int)m_edges.size();
		const int n = Count();
		float worst = 1.0f;

		// tecido real nao resiste a compressao: ao ENCURTAR, a aresta estrutural/cisalhamento so empurra com 20%
		// (permite dobrar/enrugar); ao ESTICAR usa a rigidez cheia. Flexao e sempre fraca.
		auto relax = [&](const Edge& e, float stiff)
		{
			const float wa = m_pin[e.a] >= 1.0f ? 0.0f : 1.0f, wb = m_pin[e.b] >= 1.0f ? 0.0f : 1.0f;
			const float ws = wa + wb; if (ws == 0.0f) return;
			Vec3 d = m_p[e.b] - m_p[e.a];
			const float l = std::sqrt(Dot(d, d)); if (l < 1e-7f) return;
			const float stf = (l < e.rest && e.kind != kBend) ? stiff * 0.20f : stiff;
			const float k = stf * (l - e.rest) / (l * ws);
			m_p[e.a] = m_p[e.a] + d * (k * wa);
			m_p[e.b] = m_p[e.b] - d * (k * wb);
		};
		static const float kStiff[3] = { 1.0f, 0.55f, 0.12f };

		// as arestas de flexao ficam no FIM do vetor (ver buildEdges): LOD distante so percorre as estruturais/cisalhamento
		const int nUse = params.bending ? ne : m_nonBend;
		for (int it = 0; it < params.iterations; it++)
		{
			if (it & 1) for (int k = nUse - 1; k >= 0; k--) relax(m_edges[k], kStiff[m_edges[k].kind]);
			else        for (int k = 0;        k <  nUse; k++) relax(m_edges[k], kStiff[m_edges[k].kind]);
		}

		// pinos macios (1x por passo): puxam para a posicao de repouso e acompanham o giro do mastro
		for (int i = 0; i < n; i++)
		{
			const float w = m_pin[i];
			if (w <= 0.0f || w >= 1.0f) continue;
			Vec3 t; target(i, t);
			m_p[i] = m_p[i] + (t - m_p[i]) * w;
		}

		// limite de estiramento (so aresta estrutural e cisalhamento): 2 passes duros
		for (int pass = 0; pass < 4; pass++)
			for (int k = 0; k < m_nonBend; k++)
			{
				const Edge& e = m_edges[k];
				const float wa = m_pin[e.a] >= 1.0f ? 0.0f : 1.0f, wb = m_pin[e.b] >= 1.0f ? 0.0f : 1.0f;
				const float ws = wa + wb; if (ws == 0.0f) continue;
				Vec3 d = m_p[e.b] - m_p[e.a];
				const float l = std::sqrt(Dot(d, d));
				const float lim = e.rest * params.maxStretch;
				if (l > lim)
				{
					const float kk = (l - lim) / (l * ws);
					m_p[e.a] = m_p[e.a] + d * (kk * wa);
					m_p[e.b] = m_p[e.b] - d * (kk * wb);
				}
			}

		// colisao: cilindro do mastro (eixo vertical no pe da base) e piso da base
		const float r2 = params.mastRadius * params.mastRadius;
		for (int i = 0; i < n; i++)
		{
			if (m_pin[i] >= 1.0f) continue;
			Vec3& p = m_p[i];
			const float dx = p.x - m_base.x, dy = p.y - m_base.y;
			const float d2 = dx * dx + dy * dy;
			if (d2 < r2)
			{
				if (d2 > 1e-10f) { const float s = params.mastRadius / std::sqrt(d2); p.x = m_base.x + dx * s; p.y = m_base.y + dy * s; }
				else             { p.x = m_base.x + params.mastRadius; }
			}
			const float zmin = m_base.z + params.floorZ;
			if (p.z < zmin) p.z = zmin;
		}

		for (int k = 0; k < m_nonBend; k++)
		{
			const Edge& e = m_edges[k];
			if (e.kind != kStruct) continue;
			const Vec3 d = m_p[e.b] - m_p[e.a];
			worst = std::max(worst, std::sqrt(Dot(d, d)) / e.rest);
		}
		stats.maxStretchSeen = worst;
	}

	void CFlagPhysics::Step()
	{
		if (!Ready()) return;

		// cata-vento: o quadro gira em torno do mastro ate o tecido apontar a favor do vento (mola amortecida)
		const float swing = 0.14f * std::min(m_windInt, 1.5f) * std::sin(0.65f * m_time + m_seedPh[1]);
		const float err = Wrap(m_windDir + swing - m_yaw);
		m_yawVel += (params.yawStiffness * err - params.yawDamping * m_yawVel) * kStepDt;
		m_yaw = Wrap(m_yaw + m_yawVel * kStepDt);
		m_time += kStepDt;

		sampleWind(m_wind.data());
		integrate(m_wind.data());
		solve();

		if (!sane()) { Reset(); stats.resets++; }
	}

	void CFlagPhysics::Update(float dtReal)
	{
		if (!Ready() || !m_enabled) { stats.lastSubsteps = 0; return; }
		if (!(dtReal > 0.0f)) { stats.lastSubsteps = 0; return; }       // dt <= 0 ou NaN
		m_acc += std::min(dtReal, kMaxFrameDt);
		int n = 0;
		while (m_acc >= kStepDt && n < kMaxSubsteps) { Step(); m_acc -= kStepDt; n++; }
		if (m_acc >= kStepDt) m_acc = 0.0f;                              // estourou o limite: descarta o resto (sem espiral)
		stats.lastSubsteps = n;
	}

	void CFlagPhysics::GetRenderPositions(Vec3* out) const
	{
		const float a = std::clamp(m_acc / kStepDt, 0.0f, 1.0f);        // extrapolacao suave entre passos fixos
		for (int i = 0; i < Count(); i++) out[i] = m_p[i] + (m_p[i] - m_prev[i]) * a;
	}

	void CFlagPhysics::ComputeNormals(const Vec3* pos, Vec3* out) const
	{
		const int n = Count();
		for (int i = 0; i < n; i++) out[i] = Vec3{};
		for (size_t k = 0; k + 2 < m_tris.size(); k += 3)
		{
			const uint32_t a = m_tris[k], b = m_tris[k + 1], c = m_tris[k + 2];
			const Vec3 f = Cross(pos[b] - pos[a], pos[c] - pos[a]);   // proporcional a area: pesa a media
			out[a] = out[a] + f; out[b] = out[b] + f; out[c] = out[c] + f;
		}
		const Vec3 fb = { std::sin(m_yaw), -std::cos(m_yaw), 0.0f };   // frente = -Y local
		for (int i = 0; i < n; i++)
		{
			const float l = std::sqrt(Dot(out[i], out[i]));
			out[i] = l > 1e-8f ? out[i] * (1.0f / l) : fb;
		}
	}
}
