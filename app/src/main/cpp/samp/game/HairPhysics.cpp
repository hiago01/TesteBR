#include "HairPhysics.h"
#include <algorithm>
#include <cmath>

namespace MxHair
{
	Tune& GlobalTune() { static Tune t; return t; }

	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
		inline V3 Norm(V3 a, V3 fb) { const float l = Len(a); return l > 1e-6f ? a * (1.0f / l) : fb; }
		inline bool Fin(const V3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
		inline float Hash01(uint32_t s, uint32_t k)
		{
			uint32_t h = s * 747796405u + k * 2891336453u + 12345u;
			h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
			h = (h >> 22u) ^ h;
			return (float)(h & 0xFFFFFFu) / 16777215.0f;
		}
	}

	bool Sim::Init(const Shape& s, uint32_t seed)
	{
		m_cols = 0;
		if (s.cols < 2 || s.cols > kMaxCols || s.rows < 3 || s.rows > kMaxRows) return false;
		if (!(s.length > 0.05f && s.length < 1.2f) || !(s.arcDeg > 30.0f && s.arcDeg <= 340.0f)) return false;
		m_shape = s; m_cols = s.cols; m_rows = s.rows;
		const float rag = std::clamp(s.ragged, 0.0f, 0.3f);
		for (int c = 0; c < m_cols; ++c)
		{
			const float u = (float)c / (float)(m_cols - 1);
			m_ang[c] = (-0.5f + u) * s.arcDeg * kPi / 180.0f;
			// mechas mais longas no meio das costas, pontas irregulares nas laterais
			const float mid = 1.0f - std::fabs(2.0f * u - 1.0f);
			const float k = 1.0f - rag * (0.55f * (1.0f - mid) + 0.45f * Hash01(seed, (uint32_t)c));
			float kk = k;
			if (s.spikePeriod >= 2)    // dentes: onda triangular (ponta no meio do periodo, vale nas bordas) + um pouco de irregularidade
			{
				const float ph = (float)(c % s.spikePeriod) / (float)s.spikePeriod;
				const float tri = 1.0f - std::fabs(2.0f * ph - 1.0f);                       // 0 vale .. 1 ponta
				const float depth = std::clamp(s.spikeDepth, 0.0f, 0.8f);
				kk = (1.0f - depth * (1.0f - tri)) * (1.0f - 0.35f * rag * Hash01(seed ^ 0x51u, (uint32_t)(c / s.spikePeriod)));
			}
			m_seg[c] = s.length * kk / (float)(m_rows - 1);
		}
		for (int i = 0; i < kMaxP; ++i) m_ph[i] = Hash01(seed ^ 0x9E3779B9u, (uint32_t)i) * 6.2831853f;
		m_haveFrame = false; m_latDone = false; m_time = 0; m_acc = 0; stats = Stats{};
		m_windDir = 0; m_windInt = 0.3f;
		return true;
	}

	void Sim::SetWind(float dirRad, float intensity)
	{
		m_windDir = std::isfinite(dirRad) ? dirRad : 0.0f;
		m_windInt = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 4.0f) : 0.0f;
	}

	void Sim::layout()
	{
		const Tune& T = GlobalTune();
		const V3 skull = m_f.head + m_f.up * T.skullUp;
		const float e = T.rootElevDeg * kPi / 180.0f;
		const float ce = std::cos(e), se = std::sin(e);
		for (int c = 0; c < m_cols; ++c)
		{
			const V3 d = m_f.right * std::sin(m_ang[c]) - m_f.fwd * std::cos(m_ang[c]);
			const V3 root = skull + d * (T.rootRadius * ce) + m_f.up * (T.rootRadius * se);
			for (int r = 0; r < m_rows; ++r)
			{
				const float flare = 0.016f * (float)std::min(r, 3);
				m_t[r * m_cols + c] = root + d * flare - m_f.up * ((float)r * m_seg[c]);
			}
		}
		if (!m_latDone)
		{
			for (int r = 0; r < m_rows; ++r)
				for (int c = 0; c + 1 < m_cols; ++c)
				{
					const V3 dd = m_t[r * m_cols + c + 1] - m_t[r * m_cols + c];
					const float dv = dd.z, dh = std::sqrt(dd.x * dd.x + dd.y * dd.y);   // dentes: colunas vizinhas tem alturas diferentes na mesma linha
					const float fan = 1.0f + 0.9f * (float)r / (float)(m_rows - 1);     // a folha abre em leque para baixo (so na horizontal)
					const float d0 = Len(m_t[c + 1] - m_t[c]) * fan;                    // formula original (validada) para folhas sem dentes
					m_lat[r][c] = (m_shape.spikePeriod >= 2) ? std::sqrt(dh * dh * fan * fan + dv * dv) : d0;
				}
			m_latDone = true;
		}
	}

	void Sim::SetFrame(const Frame& f)
	{
		if (!m_cols) return;
		if (!Fin(f.head) || !Fin(f.right) || !Fin(f.fwd) || !Fin(f.up)) return;
		m_f = f;
		m_f.right = Norm(f.right, V3{ 1, 0, 0 }); m_f.fwd = Norm(f.fwd, V3{ 0, 1, 0 }); m_f.up = Norm(f.up, V3{ 0, 0, 1 });
		const bool jump = m_haveFrame && Len(f.head - m_lastHead) > 1.5f;     // teleporte / spawn: recomeca do repouso
		layout();
		m_lastHead = f.head;
		if (!m_haveFrame || jump) { m_haveFrame = true; Reset(); return; }
		for (int c = 0; c < m_cols; ++c) { m_p[c] = m_t[c]; m_prev[c] = m_t[c]; }       // raiz presa a cabeca
	}

	void Sim::Reset()
	{
		if (!m_haveFrame) return;
		layout();
		for (int i = 0; i < Count(); ++i) { m_p[i] = m_t[i]; m_prev[i] = m_t[i]; }
		m_acc = 0;
	}

	V3 Sim::windAt(int c, int r) const
	{
		const Tune& T = GlobalTune();
		if (m_windInt <= 0.0f) return V3{};
		const int i = r * m_cols + c;
		const float sp = T.baseSpeed * m_windInt;
		const float tt = m_time;
		const float travel = 0.55f * (float)r - 0.35f * (float)c;       // ondas que viajam raiz -> ponta
		const float g = 1.0f + T.gust * std::sin(2.0f * kPi * 0.37f * tt - travel + 0.6f * m_ph[i]);
		const V3 dir{ std::cos(m_windDir), std::sin(m_windDir), 0.0f };
		const V3 lat{ -dir.y, dir.x, 0.0f };
		V3 w = dir * (sp * g);
		w = w + lat * (sp * T.flutter * std::sin(2.0f * kPi * 0.9f * tt - 1.3f * travel + m_ph[i]));
		w.z += sp * T.flutter * 0.25f * std::sin(2.0f * kPi * 0.6f * tt - travel + 1.7f * m_ph[i]);
		return w;
	}

	void Sim::ComputeNormals(const V3* pos, V3* out) const
	{
		for (int r = 0; r < m_rows; ++r)
			for (int c = 0; c < m_cols; ++c)
			{
				const int c0 = std::max(c - 1, 0), c1 = std::min(c + 1, m_cols - 1);
				const int r0 = std::max(r - 1, 0), r1 = std::min(r + 1, m_rows - 1);
				const V3 tc = pos[r * m_cols + c1] - pos[r * m_cols + c0];
				const V3 tr = pos[r1 * m_cols + c] - pos[r0 * m_cols + c];
				out[r * m_cols + c] = Norm(Cross(tc, tr), V3{ 0, 0, 1 });
			}
	}

	void Sim::integrate()
	{
		const Tune& T = GlobalTune();
		const float dt = kStepDt;
		V3 nrm[kMaxP];
		ComputeNormals(m_p, nrm);
		for (int r = 1; r < m_rows; ++r)
		{
			const float tw = 0.35f + 0.65f * (float)r / (float)(m_rows - 1);     // a ponta e mais leve
			for (int c = 0; c < m_cols; ++c)
			{
				const int i = r * m_cols + c;
				const V3 v = (m_p[i] - m_prev[i]) * T.damping;
				V3 a{ 0, 0, -T.gravity };
				if (m_windInt > 0.0f)
				{
					const V3 vrel = windAt(c, r) - v * (1.0f / dt);
					const float vn = Dot(nrm[i], vrel);
					const V3 vt = vrel - nrm[i] * vn;
					a = a + nrm[i] * (T.pressureK * vn * tw) + vt * (T.tangentK * tw);
				}
				m_prev[i] = m_p[i];
				m_p[i] = m_p[i] + v + a * (dt * dt);
			}
		}
		m_time += dt;
	}

	void Sim::collide()
	{
		const Tune& T = GlobalTune();
		const V3 S = m_f.head + m_f.up * T.skullUp;
		const float R = T.skullR + T.skin;
		const V3 A0 = m_f.head - m_f.fwd * T.bodyBack;
		const float ea = T.bodyA + T.skin, eb = T.bodyB + T.skin;
		for (int r = 1; r < m_rows; ++r)
			for (int c = 0; c < m_cols; ++c)
			{
				V3& p = m_p[r * m_cols + c];
				const V3 d = p - S; const float l2 = Dot(d, d);
				if (l2 < R * R)
				{
					const float l = std::sqrt(l2);
					const V3 n = l > 1e-5f ? d * (1.0f / l) : (m_f.fwd * -1.0f);
					p = S + n * R;
				}
				if (m_f.body)
				{
					const V3 dv = p - A0;
					const float hz = Dot(dv, m_f.up);
					if (hz < -T.bodyTop && hz > -T.bodyBottom)
					{
						const float x = Dot(dv, m_f.right), y = Dot(dv, m_f.fwd);
						const float q = (x * x) / (ea * ea) + (y * y) / (eb * eb);
						if (q < 1.0f)
						{
							float k;
							V3 pxy = m_f.right * x + m_f.fwd * y;
							if (q > 1e-6f) { k = 1.0f / std::sqrt(q); p = p + pxy * (k - 1.0f); }
							else           { p = p + m_f.fwd * (-eb - y); }          // exatamente no eixo: empurra para tras
						}
					}
				}
			}
	}

	void Sim::solve()
	{
		const Tune& T = GlobalTune();
		// linhas 1 e 2 "macias": voltam ao repouso (volume junto ao couro cabeludo)
		for (int c = 0; c < m_cols; ++c)
		{
			if (m_rows > 1) { V3& p = m_p[m_cols + c]; p = p + (m_t[m_cols + c] - p) * T.softPin1; }
			if (m_rows > 2) { V3& p = m_p[2 * m_cols + c]; p = p + (m_t[2 * m_cols + c] - p) * T.softPin2; }
		}
		for (int it = 0; it < std::max(1, T.iterations); ++it)
		{
			// comprimento da mecha (linha 0 fixa)
			for (int c = 0; c < m_cols; ++c)
				for (int r = 0; r + 1 < m_rows; ++r)
				{
					V3& a = m_p[r * m_cols + c]; V3& b = m_p[(r + 1) * m_cols + c];
					const V3 d = b - a; const float l = Len(d);
					if (l < 1e-6f) continue;
					const float diff = (l - m_seg[c]) / l;
					const float wa = (r == 0) ? 0.0f : 0.5f, wb = (r == 0) ? 1.0f : 0.5f;
					a = a + d * (diff * wa); b = b - d * (diff * wb);
				}
			// laterais (folha): so puxa se esticar demais; empurra de leve se amassar demais
			for (int r = 1; r < m_rows; ++r)
				for (int c = 0; c + 1 < m_cols; ++c)
				{
					V3& a = m_p[r * m_cols + c]; V3& b = m_p[r * m_cols + c + 1];
					const V3 d = b - a; const float l = Len(d);
					if (l < 1e-6f) continue;
					const float rest = m_lat[r][c];
					float diff = 0;
					if (l > rest * 1.15f) diff = (l - rest * 1.15f) / l * 0.5f;
					else if (l < rest * 0.45f) diff = (l - rest * 0.45f) / l * 0.2f;
					if (diff != 0) { a = a + d * (diff * 0.5f); b = b - d * (diff * 0.5f); }
				}
			// flexao minima: nao dobra a mecha sobre si
			for (int c = 0; c < m_cols; ++c)
				for (int r = 0; r + 2 < m_rows; ++r)
				{
					V3& a = m_p[r * m_cols + c]; V3& b = m_p[(r + 2) * m_cols + c];
					const V3 d = b - a; const float l = Len(d);
					const float rest = 2.0f * m_seg[c] * 0.9f;
					if (l < 1e-6f || l >= rest) continue;
					const float diff = (l - rest) / l * 0.3f;
					const float wa = (r == 0) ? 0.0f : 0.5f, wb = (r == 0) ? 1.0f : 0.5f;
					a = a + d * (diff * wa); b = b - d * (diff * wb);
				}
			collide();
		}
		// limite de estiramento (passe duro, de cima para baixo), depois colisao de novo (a colisao tem prioridade sobre o estiramento)
		for (int pass = 0; pass < 3; ++pass)
		{
			for (int c = 0; c < m_cols; ++c)
				for (int r = 0; r + 1 < m_rows; ++r)
				{
					V3& a = m_p[r * m_cols + c]; V3& b = m_p[(r + 1) * m_cols + c];
					const V3 d = b - a; const float l = Len(d);
					const float lim = m_seg[c] * T.stretchMax;
					if (l > lim && l > 1e-6f) b = a + d * (lim / l);
				}
			collide();
		}
		float worst = 1.0f;
		for (int c = 0; c < m_cols; ++c)
			for (int r = 0; r + 1 < m_rows; ++r)
				worst = std::max(worst, std::min(Len(m_p[(r + 1) * m_cols + c] - m_p[r * m_cols + c]), 10.0f) / std::max(m_seg[c], 1e-6f));
		stats.maxStretchSeen = worst;
		for (int c = 0; c < m_cols; ++c) { m_p[c] = m_t[c]; }
	}

	bool Sim::sane() const
	{
		for (int i = 0; i < Count(); ++i)
		{
			if (!Fin(m_p[i])) return false;
			if (Len(m_p[i] - m_f.head) > 1.6f) return false;
		}
		return true;
	}

	void Sim::Step()
	{
		if (!m_haveFrame) return;
		integrate();
		solve();
		if (!sane()) { ++stats.resets; Reset(); }
	}

	void Sim::Update(float dtReal)
	{
		stats.lastSubsteps = 0;
		if (!m_haveFrame) return;
		float dt = std::isfinite(dtReal) ? dtReal : 0.0f;
		dt = std::clamp(dt, 0.0f, kMaxFrameDt);
		m_acc += dt;
		int n = 0;
		while (m_acc >= kStepDt && n < kMaxSubsteps) { Step(); m_acc -= kStepDt; ++n; }
		if (n == kMaxSubsteps) m_acc = 0;                      // tempo que sobrou e descartado
		stats.lastSubsteps = n;
	}

	void Sim::GetRenderPositions(V3* out) const
	{
		const float f = std::clamp(m_acc / kStepDt, 0.0f, 1.0f);
		for (int i = 0; i < Count(); ++i) out[i] = (i < m_cols) ? m_p[i] : m_p[i] + (m_p[i] - m_prev[i]) * f;
	}
}
