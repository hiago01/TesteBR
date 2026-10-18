#pragma once

#include <cmath>
#include <cstdint>

// =====================================================================================
//  Logica pura da roda de armas (sem ImGui, sem GTA): categorias, geometria angular,
//  hit-test, toque curto x longo e "proxima arma". Fica separada do desenho para poder
//  ser testada no PC (g++ comum).
//
//  Os "slots" sao os indices reais de CPedGTA::m_aWeapons[13] (convencao do GTA SA):
//    0 punhos/soqueira | 1 armas brancas | 2 pistolas | 3 escopetas | 4 submetralhadoras
//    5 rifles de assalto | 6 rifles/sniper | 7 pesadas (RPG, lanca-chamas, minigun)
//    8 arremessaveis (granada, molotov, satchel) | 9 spray/extintor/camera
//    10 presentes (flores, bengala, dildos) | 11 oculos/paraquedas | 12 detonador
// =====================================================================================

namespace WeaponWheel
{
	constexpr int   kNumSlots = 13;
	constexpr float kPi       = 3.14159265358979f;
	constexpr float kTwoPi    = 6.28318530717959f;

	// ---------------------------------------------------------------- categorias
	struct Category
	{
		const char* name;
		int         count;
		int         slots[3];
	};

	// Ordem no sentido horario a partir do topo: com as 5 armas da imagem de referencia
	// (pistola, assalto, rifle, escopeta, SMG) o desenho coincide com a roda do print.
	constexpr int kNumCats = 8;
	constexpr Category kCats[kNumCats] = {
		{ "PISTOLAS",              1, { 2, -1, -1 } },
		{ "ASSALTO",               1, { 5, -1, -1 } },
		{ "RIFLES",                1, { 6, -1, -1 } },
		{ "ESCOPETAS",             1, { 3, -1, -1 } },
		{ "SUBMETRALHADORAS",      1, { 4, -1, -1 } },
		{ "PESADAS E EXPLOSIVOS",  3, { 7, 8, 12 } },
		{ "BRANCAS",               1, { 1, -1, -1 } },
		{ "OUTROS",                3, { 9, 10, 11 } },
	};
	// O slot 0 (punhos) fica no centro da roda.

	inline bool Owned(uint32_t mask, int slot) { return slot >= 0 && slot < kNumSlots && ((mask >> slot) & 1u) != 0; }

	// Slots possuidos de uma categoria (em ordem). Retorna quantos.
	inline int OwnedInCat(uint32_t mask, int cat, int* out)
	{
		int n = 0;
		for (int i = 0; i < kCats[cat].count; i++)
			if (Owned(mask, kCats[cat].slots[i])) out[n++] = kCats[cat].slots[i];
		return n;
	}

	// Segmentos da roda = categorias que tem pelo menos uma arma possuida.
	struct Ring
	{
		int cat[kNumCats];
		int count;
	};

	inline Ring BuildRing(uint32_t mask)
	{
		Ring r;
		r.count = 0;
		int tmp[3];
		for (int c = 0; c < kNumCats; c++)
			if (OwnedInCat(mask, c, tmp) > 0) r.cat[r.count++] = c;
		return r;
	}

	// Arma "padrao" do segmento: a ativa, se for da categoria; senao a primeira possuida.
	inline int DefaultSlotInCat(uint32_t mask, int cat, int activeSlot)
	{
		int list[3];
		const int n = OwnedInCat(mask, cat, list);
		if (n == 0) return -1;
		for (int i = 0; i < n; i++) if (list[i] == activeSlot) return activeSlot;
		return list[0];
	}

	// ---------------------------------------------------------------- angulos
	// Angulo em radianos medido a partir do topo, no sentido horario, em [0, 2pi).
	inline float AngleFromTop(float dx, float dy)
	{
		float a = std::atan2(dx, -dy);
		if (a < 0.0f) a += kTwoPi;
		return a;
	}

	// O segmento i fica CENTRADO em i * (2pi/n): o primeiro fica no topo.
	inline float SegmentCenter(int i, int n) { return (float)i * (kTwoPi / (float)n); }

	inline int SegmentAt(float angle, int n)
	{
		const float s = kTwoPi / (float)n;
		int i = (int)std::floor((angle + s * 0.5f) / s);
		return ((i % n) + n) % n;
	}

	// Sub-celulas (varias armas na mesma categoria): leque centrado no segmento.
	constexpr float kSubMaxStep = 0.42f;   // ~24 graus entre sub-celulas

	inline float SubStep(int n, int m)
	{
		const float s = kTwoPi / (float)n;
		const float st = s / (float)m;
		return st < kSubMaxStep ? st : kSubMaxStep;
	}

	inline float SubCenter(int seg, int n, int k, int m)
	{
		return SegmentCenter(seg, n) + ((float)k - (float)(m - 1) * 0.5f) * SubStep(n, m);
	}

	inline int SubAt(float angle, int seg, int n, int m)
	{
		int best = 0;
		float bestD = 1e9f;
		for (int k = 0; k < m; k++)
		{
			float d = std::fabs(angle - SubCenter(seg, n, k, m));
			while (d > kPi) d = std::fabs(d - kTwoPi);
			if (d < bestD) { bestD = d; best = k; }
		}
		return best;
	}

	// ---------------------------------------------------------------- hit-test
	enum class Zone { Cancel, Center, Ring, Sub };

	struct Params { float rIn, rOut, rCancel; };

	struct Pick
	{
		Zone zone;
		int  seg;   // indice do segmento (Ring/Sub)
		int  sub;   // indice da sub-celula (Sub)
	};

	// dx,dy = posicao do dedo relativa ao centro da roda.
	// subCount[i] = quantas armas possuidas ha na categoria do segmento i.
	inline Pick PickAt(float dx, float dy, const Params& p, int n, const int* subCount)
	{
		Pick pk = { Zone::Cancel, -1, -1 };
		const float r = std::sqrt(dx * dx + dy * dy);

		if (r < p.rIn) { pk.zone = Zone::Center; return pk; }
		if (r > p.rCancel || n <= 0) return pk;

		const float a = AngleFromTop(dx, dy);
		pk.seg = SegmentAt(a, n);

		if (r > p.rOut && subCount && subCount[pk.seg] > 1)
		{
			pk.zone = Zone::Sub;
			pk.sub  = SubAt(a, pk.seg, n, subCount[pk.seg]);
			return pk;
		}

		pk.zone = Zone::Ring;   // inclusive o "estouro" para fora da borda em categorias de 1 arma
		return pk;
	}

	// ---------------------------------------------------------------- toque
	enum class Gesture { Tap, Hold };

	inline Gesture Classify(int64_t elapsedMs, int64_t holdMs)
	{
		return elapsedMs >= holdMs ? Gesture::Hold : Gesture::Tap;
	}

	// Proxima/anterior arma possuida (slot 0 incluido), dando a volta. -1 se nao ha nenhuma.
	inline int NextSlot(uint32_t mask, int current, int dir)
	{
		int list[kNumSlots], n = 0, idx = -1;
		for (int s = 0; s < kNumSlots; s++)
		{
			if (!Owned(mask, s)) continue;
			if (s == current) idx = n;
			list[n++] = s;
		}
		if (n == 0) return -1;
		if (idx < 0) return dir > 0 ? list[0] : list[n - 1];
		return list[((idx + dir) % n + n) % n];
	}
}
