#include "Hose.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "Models/ModelInfo.h"
#include "Models/VehicleModelInfo.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"
#include "../gui/gui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

extern CNetGame* pNetGame;
extern UI* pUI;
extern uintptr_t g_libGTASA;

// =====================================================================================
//  Configuracao (tudo ajustavel em runtime por /mangueira config ...)
// =====================================================================================

namespace
{
	constexpr int   kMaxHoses     = 4;       // mangueiras simultaneas (slots reaproveitados)
	constexpr int   kMinSegments  = 4;
	constexpr int   kMaxSegments  = 16;
	constexpr int   kSides        = 6;       // lados do tubo (6 = hexagono, barato e redondo o bastante)
	constexpr float kPi           = 3.14159265f;
	constexpr float kMaxSagOfDist = 0.35f;   // o sag nunca passa de 35% da distancia A-B

	struct Config
	{
		int   segments     = 10;       // 8..12 recomendado (4..16 aceito)
		float sag          = 0.45f;    // queda maxima no meio (m): 0.3..0.6
		float sagMinFactor = 0.20f;    // sag minimo (fracao) quando A e B estao no alcance maximo
		float maxDist      = 12.0f;    // comprimento da mangueira: alem disso ela solta
		float radius       = 0.022f;   // raio do tubo (m)
		float anchorDZ     = -0.30f;   // /mangueira a: altura do ponto A relativa ao ped (m)
		bool  marker       = true;     // desenha um marcador no ponto A
	} g_cfg;

	// cor da borracha (RGB) e do marcador do ponto A
	constexpr uint8_t kHoseR = 34,  kHoseG = 118, kHoseB = 52;
	constexpr uint8_t kMarkR = 40,  kMarkG = 190, kMarkB = 235;

	struct V3 { float x, y, z; };
	inline V3    operator+(V3 a, V3 b)  { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3    operator-(V3 a, V3 b)  { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3    operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float Dot(V3 a, V3 b)        { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3    Cross(V3 a, V3 b)      { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Len(V3 a)              { return std::sqrt(Dot(a, a)); }
	inline bool  Finite(V3 a)           { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
	inline V3    Norm(V3 a, V3 fallback)
	{
		const float l = Len(a);
		return l > 1e-5f ? a * (1.0f / l) : fallback;
	}

	struct Slot
	{
		bool       used  = false;
		bool       ready = false;      // pontos calculados neste frame
		int        pedId = -1;
		V3         a{};                // ponto A (fixo)
		HoseTarget tgt;
		V3         pts[kMaxSegments + 1];
		int        nPts  = 0;
	};

	Slot       g_slots[kMaxHoses];
	std::mutex g_mtx;

	bool g_hasAnchor = false;          // ponto A criado por /mangueira a
	V3   g_anchor{};

	void Say(const char* fmt, ...)
	{
		char buf[220];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		FLog("%s", buf);
		if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("%s", buf);
	}
}

// =====================================================================================
//  Resolucao segura de ped / veiculo / pontos
// =====================================================================================

namespace
{
	// pedId < 0 ou == ID local -> jogador local; senao, jogador remoto. nullptr se invalido.
	CPlayerPed* ResolvePed(int pedId)
	{
		if (!pNetGame) return nullptr;
		CPlayerPool* pool = pNetGame->GetPlayerPool();
		if (!pool) return nullptr;

		if (pedId < 0 || pedId == (int)pool->GetLocalPlayerID())
		{
			CLocalPlayer* lp = pool->GetLocalPlayer();
			return lp ? lp->GetPlayerPed() : nullptr;
		}

		if (pedId >= 1004) return nullptr;   // MAX_PLAYERS do SA-MP
		CRemotePlayer* rp = pool->GetAt((PLAYERID)pedId);
		return rp ? rp->GetPlayerPed() : nullptr;
	}

	// Ponta B = osso da mao do ped.
	bool HandPos(CPlayerPed* pp, int boneTag, V3& out)
	{
		if (!pp || !pp->m_pPed) return false;
		CVector v;
		pp->GetBonePosition(boneTag, &v);
		out = { v.x, v.y, v.z };
		return Finite(out);
	}

	// Ponta B = tampa do tanque do veiculo (dummy GAS_CAP do modelo, levado para o mundo).
	bool TankPos(int vehicleId, V3& out)
	{
		if (!pNetGame || vehicleId < 0 || vehicleId >= 2000) return false;   // MAX_VEHICLES
		CVehiclePool* vp = pNetGame->GetVehiclePool();
		if (!vp) return false;

		CVehicle* veh = vp->GetAt((VEHICLEID)vehicleId);
		if (!veh || !veh->m_pVehicle) return false;
		CVehicleGTA* gv = veh->m_pVehicle;
		if (!gv->m_matrix) return false;                    // nao cria matriz so para ler

		CVehicleModelInfo* mi = CModelInfo::GetVehicleModelInfo(gv->m_nModelIndex);
		if (!mi || !mi->m_pVehicleStruct) return false;

		const CVector* d = mi->GetModelDummyPosition(DUMMY_GAS_CAP);
		if (!d || (d->x == 0.0f && d->y == 0.0f && d->z == 0.0f)) return false;   // modelo sem tampa

		const CVector w = *gv->m_matrix * *d;
		out = { w.x, w.y, w.z };
		return Finite(out);
	}

	void BuildPoints(Slot& s, const V3& b, float dist)
	{
		const int   S = std::clamp(g_cfg.segments, kMinSegments, kMaxSegments);
		// o sag diminui conforme A e B se afastam (1 perto -> sagMinFactor no alcance maximo)
		const float f   = std::clamp(1.0f - dist / std::max(g_cfg.maxDist, 0.1f), g_cfg.sagMinFactor, 1.0f);
		const float sag = std::min(g_cfg.sag * f, kMaxSagOfDist * dist);

		for (int i = 0; i <= S; i++)
		{
			const float t = (float)i / (float)S;
			V3 p = s.a + (b - s.a) * t;           // P(t) = lerp(A, B, t)
			p.z -= sag * std::sin(kPi * t);       // P.z -= sag * sin(PI * t)
			s.pts[i] = p;
		}
		s.nPts  = S + 1;
		s.ready = true;
	}

	void UpdateSlot(Slot& s)
	{
		const char* why = nullptr;
		V3 b{};

		CPlayerPed* pp = ResolvePed(s.pedId);
		if (!pp || !pp->m_pPed)      why = "jogador invalido";
		else if (pp->IsDead())       why = "o jogador morreu";
		else if (pp->IsInVehicle())  why = "o jogador entrou no veiculo";

		if (!why)
		{
			if (s.tgt.kind == HoseTarget::Hand)
			{
				if (!HandPos(pp, s.tgt.boneTag, b)) why = "mao do ped indisponivel";
			}
			else if (!TankPos(s.tgt.vehicleId, b))
			{
				why = "tanque do veiculo indisponivel";
			}
		}

		float dist = 0.0f;
		if (!why)
		{
			if (!Finite(s.a)) why = "ponto A invalido";
			else
			{
				dist = Len(b - s.a);
				if (!std::isfinite(dist))              why = "posicao invalida";
				else if (dist > g_cfg.maxDist)         why = "o jogador se afastou demais";
			}
		}

		if (why)
		{
			s.used  = false;      // libera o slot (o "ID" volta para o pool)
			s.ready = false;
			Say("[MANGUEIRA] solta: %s", why);
			return;
		}

		BuildPoints(s, b, dist);
	}
}

// =====================================================================================
//  API publica
// =====================================================================================

int Hose_Start(int pedId, const CVector& bombaPos, const HoseTarget& alvo)
{
	std::lock_guard<std::mutex> lk(g_mtx);

	const V3 a{ bombaPos.x, bombaPos.y, bombaPos.z };
	if (!Finite(a)) return 0;

	CPlayerPed* pp = ResolvePed(pedId);
	if (!pp || !pp->m_pPed) return 0;
	if (alvo.kind == HoseTarget::VehicleTank && alvo.vehicleId < 0) return 0;

	// 1 mangueira por ped: reaproveita o slot dele; senao pega o primeiro livre
	int idx = -1;
	for (int i = 0; i < kMaxHoses; i++)
		if (g_slots[i].used && g_slots[i].pedId == pedId) { idx = i; break; }
	if (idx < 0)
		for (int i = 0; i < kMaxHoses; i++)
			if (!g_slots[i].used) { idx = i; break; }
	if (idx < 0)
	{
		Say("[MANGUEIRA] limite de %d mangueiras simultaneas", kMaxHoses);
		return 0;
	}

	Slot& s = g_slots[idx];
	s.used  = true;
	s.ready = false;
	s.pedId = pedId;
	s.a     = a;
	s.tgt   = alvo;
	s.nPts  = 0;
	return idx + 1;
}

void Hose_Stop(int handle)
{
	std::lock_guard<std::mutex> lk(g_mtx);

	if (handle > 0 && handle <= kMaxHoses)
	{
		g_slots[handle - 1].used  = false;
		g_slots[handle - 1].ready = false;
		return;
	}
	for (Slot& s : g_slots) { s.used = false; s.ready = false; }
}

bool Hose_IsActive(int handle)
{
	std::lock_guard<std::mutex> lk(g_mtx);

	if (handle > 0 && handle <= kMaxHoses) return g_slots[handle - 1].used;
	for (const Slot& s : g_slots) if (s.used) return true;
	return false;
}

void Hose_Update()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	for (Slot& s : g_slots)
		if (s.used) UpdateSlot(s);
}

// =====================================================================================
//  Desenho: UM tubo continuo por mangueira (sem emendas) com RwIm3D
// =====================================================================================

namespace
{
	// ATENCAO: nesta libGTASA o indice do Im3D e de 16 bits (RopeIndices = pares uint16, e o
	// CRope::Render passa isso para RwIm3DRenderIndexedPrimitive), mas RwImVertexIndex aparece
	// como uint32 em game/RW/rwplcore.h. Por isso usamos uint16_t aqui e fazemos o cast na chamada.
	constexpr int kMaxVerts = (kMaxSegments + 1) * kSides;
	constexpr int kMaxIdx   = kMaxSegments * kSides * 6 + 2 * (kSides - 2) * 3;

	RxObjSpace3DVertex g_verts[kMaxVerts];
	uint16_t           g_idx[kMaxIdx];

	inline uint32_t PackARGB(int r, int g, int b)
	{
		return (0xFFu << 24) | ((uint32_t)std::clamp(r, 0, 255) << 16) |
			   ((uint32_t)std::clamp(g, 0, 255) << 8) | (uint32_t)std::clamp(b, 0, 255);
	}

	// Sombreado "baked" (o Im3D aqui nao e iluminado): luz de cima/lado + ambiente.
	inline uint32_t Shade(V3 n, uint8_t r, uint8_t g, uint8_t b)
	{
		const V3 L = Norm({ 0.40f, 0.30f, 0.86f }, { 0, 0, 1 });
		const float k = 0.42f + 0.58f * std::max(0.0f, Dot(n, L));
		return PackARGB((int)(r * k), (int)(g * k), (int)(b * k));
	}

	void SetStates()
	{
		// mesmo ponto de partida do CRope::Render (que chama DefinedState() = libGTASA+0x6F4EC4)
		CHook::CallFunction<void>(g_libGTASA + 0x6F4EC4);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(NULL));
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(FALSE));
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
	}

	void Submit(int nVerts, int nIdx)
	{
		if (RwIm3DTransform(g_verts, (RwUInt32)nVerts, nullptr, 0))
		{
			RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST,
										 reinterpret_cast<RwImVertexIndex*>(g_idx), nIdx);
			RwIm3DEnd();
		}
	}

	// Tubo continuo ao longo de p[0..n-1]. Cada anel e compartilhado pelos dois segmentos vizinhos.
	void DrawTube(const V3* p, int n, float radius)
	{
		if (n < 2 || n > kMaxSegments + 1) return;

		// tangentes (diferenca central; pontas usam o segmento vizinho)
		V3 T[kMaxSegments + 1];
		V3 prev = { 0, 0, 1 };
		for (int i = 0; i < n; i++)
		{
			const V3 d = (i == 0) ? (p[1] - p[0]) : (i == n - 1) ? (p[n - 1] - p[n - 2]) : (p[i + 1] - p[i - 1]);
			T[i] = Norm(d, prev);
			prev = T[i];
		}

		// quadro de referencia propagado (sem torcer o tubo)
		const V3 up = (std::fabs(T[0].z) < 0.95f) ? V3{ 0, 0, 1 } : V3{ 1, 0, 0 };
		V3 N = Norm(Cross(up, T[0]), { 1, 0, 0 });

		int vi = 0;
		for (int i = 0; i < n; i++)
		{
			if (i > 0) N = Norm(N - T[i] * Dot(N, T[i]), N);
			const V3 B = Cross(T[i], N);

			for (int k = 0; k < kSides; k++)
			{
				const float th = 2.0f * kPi * (float)k / (float)kSides;
				const V3 dir = N * std::cos(th) + B * std::sin(th);
				const V3 pos = p[i] + dir * radius;

				RxObjSpace3DVertex& v = g_verts[vi++];
				v.objVertex = { pos.x, pos.y, pos.z };
				v.objNormal = { dir.x, dir.y, dir.z };
				v.color     = Shade(dir, kHoseR, kHoseG, kHoseB);
				v.u = 0.0f;
				v.v = 0.0f;
			}
		}

		int ii = 0;
		for (int i = 0; i < n - 1; i++)
		{
			for (int k = 0; k < kSides; k++)
			{
				const int k2 = (k + 1) % kSides;
				const uint16_t a = (uint16_t)(i * kSides + k),       b = (uint16_t)(i * kSides + k2);
				const uint16_t c = (uint16_t)((i + 1) * kSides + k), d = (uint16_t)((i + 1) * kSides + k2);
				g_idx[ii++] = a; g_idx[ii++] = c; g_idx[ii++] = b;
				g_idx[ii++] = b; g_idx[ii++] = c; g_idx[ii++] = d;
			}
		}
		// tampas planas nas duas pontas
		for (int k = 1; k < kSides - 1; k++)
		{
			g_idx[ii++] = 0;
			g_idx[ii++] = (uint16_t)k;
			g_idx[ii++] = (uint16_t)(k + 1);
		}
		const int last = (n - 1) * kSides;
		for (int k = 1; k < kSides - 1; k++)
		{
			g_idx[ii++] = (uint16_t)last;
			g_idx[ii++] = (uint16_t)(last + k + 1);
			g_idx[ii++] = (uint16_t)(last + k);
		}

		Submit(vi, ii);
	}

	// Marcador do ponto A: octaedro pequeno.
	void DrawMarker(const V3& c)
	{
		const float r = 0.06f;
		const V3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		for (int i = 0; i < 6; i++)
		{
			const V3 pos = c + dirs[i] * r;
			RxObjSpace3DVertex& v = g_verts[i];
			v.objVertex = { pos.x, pos.y, pos.z };
			v.objNormal = { dirs[i].x, dirs[i].y, dirs[i].z };
			v.color     = Shade(dirs[i], kMarkR, kMarkG, kMarkB);
			v.u = 0.0f;
			v.v = 0.0f;
		}
		static const uint16_t faces[8][3] = {
			{ 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 },
			{ 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 },
		};
		int ii = 0;
		for (const auto& f : faces) { g_idx[ii++] = f[0]; g_idx[ii++] = f[1]; g_idx[ii++] = f[2]; }
		Submit(6, ii);
	}
}

void Hose_Render()
{
	std::lock_guard<std::mutex> lk(g_mtx);

	bool any = false;
	for (const Slot& s : g_slots) if (s.used && s.ready && s.nPts >= 2) { any = true; break; }
	const bool marker = g_hasAnchor && g_cfg.marker;
	if (!any && !marker) return;

	SetStates();

	if (marker) DrawMarker(g_anchor);

	for (const Slot& s : g_slots)
		if (s.used && s.ready && s.nPts >= 2)
			DrawTube(s.pts, s.nPts, g_cfg.radius);
}

// =====================================================================================
//  Comando do chat: /mangueira
// =====================================================================================

std::string Hose_Command(const std::string& args)
{
	std::vector<std::string> a;
	{
		size_t i = 0;
		while (i < args.size())
		{
			while (i < args.size() && args[i] == ' ') i++;
			size_t j = i;
			while (j < args.size() && args[j] != ' ') j++;
			if (j > i) a.push_back(args.substr(i, j - i));
			i = j;
		}
	}

	char buf[220];

	if (a.empty())
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		int ativas = 0;
		for (const Slot& s : g_slots) if (s.used) ativas++;
		snprintf(buf, sizeof(buf),
				 "[MANGUEIRA] ponto A: %s | ativas: %d | segmentos=%d sag=%.2f alcance=%.1f raio=%.3f altura=%.2f | "
				 "uso: a [x y z] | pegar | soltar | limpar | marcador on|off | config <campo> <n>",
				 g_hasAnchor ? "definido" : "nao definido", ativas,
				 g_cfg.segments, g_cfg.sag, g_cfg.maxDist, g_cfg.radius, g_cfg.anchorDZ);
		return buf;
	}

	if (a[0] == "a")
	{
		V3 p{};
		if (a.size() >= 4)
		{
			p = { (float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str()) };
		}
		else
		{
			CPlayerPed* pp = ResolvePed(-1);
			if (!pp || !pp->m_pPed) return "[MANGUEIRA] jogador local indisponivel";

			const CVector pos = pp->m_pPed->GetPosition();
			float dz;
			{ std::lock_guard<std::mutex> lk(g_mtx); dz = g_cfg.anchorDZ; }
			p = { pos.x, pos.y, pos.z + dz };
		}

		if (!Finite(p)) return "[MANGUEIRA] coordenadas invalidas";

		std::lock_guard<std::mutex> lk(g_mtx);
		g_anchor    = p;
		g_hasAnchor = true;
		snprintf(buf, sizeof(buf), "[MANGUEIRA] ponto A criado em %.2f, %.2f, %.2f", p.x, p.y, p.z);
		return buf;
	}

	if (a[0] == "pegar")
	{
		V3 p;
		float maxDist;
		{
			std::lock_guard<std::mutex> lk(g_mtx);
			if (!g_hasAnchor) return "[MANGUEIRA] crie o ponto A antes: /mangueira a";
			p = g_anchor;
			maxDist = g_cfg.maxDist;
		}

		CPlayerPed* pp = ResolvePed(-1);
		if (!pp || !pp->m_pPed)  return "[MANGUEIRA] jogador local indisponivel";
		if (pp->IsDead())        return "[MANGUEIRA] voce esta morto";
		if (pp->IsInVehicle())   return "[MANGUEIRA] saia do veiculo primeiro";

		const CVector pos = pp->m_pPed->GetPosition();
		if (Len(V3{ pos.x, pos.y, pos.z } - p) > maxDist)
		{
			snprintf(buf, sizeof(buf), "[MANGUEIRA] chegue mais perto do ponto A (alcance %.1f m)", maxDist);
			return buf;
		}

		const int h = Hose_Start(-1, CVector(p.x, p.y, p.z), HoseTarget());
		return h > 0 ? "[MANGUEIRA] mangueira na mao (/mangueira soltar)" : "[MANGUEIRA] nao foi possivel pegar";
	}

	if (a[0] == "soltar") { Hose_Stop(-1); return "[MANGUEIRA] solta"; }

	if (a[0] == "limpar")
	{
		Hose_Stop(-1);
		std::lock_guard<std::mutex> lk(g_mtx);
		g_hasAnchor = false;
		return "[MANGUEIRA] ponto A removido";
	}

	if (a[0] == "marcador" && a.size() >= 2)
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_cfg.marker = (a[1] != "off");
		return g_cfg.marker ? "[MANGUEIRA] marcador ligado" : "[MANGUEIRA] marcador desligado";
	}

	if (a[0] == "config" && a.size() >= 3)
	{
		const float v = (float)atof(a[2].c_str());
		std::lock_guard<std::mutex> lk(g_mtx);
		if (a[1] == "segmentos") { g_cfg.segments = std::clamp((int)v, kMinSegments, kMaxSegments); return "[MANGUEIRA] segmentos ok"; }
		if (a[1] == "sag")       { g_cfg.sag      = std::clamp(v, 0.0f, 2.0f);                      return "[MANGUEIRA] sag ok"; }
		if (a[1] == "alcance")   { g_cfg.maxDist  = std::clamp(v, 1.0f, 50.0f);                     return "[MANGUEIRA] alcance ok"; }
		if (a[1] == "raio")      { g_cfg.radius   = std::clamp(v, 0.005f, 0.2f);                    return "[MANGUEIRA] raio ok"; }
		if (a[1] == "altura")    { g_cfg.anchorDZ = std::clamp(v, -2.0f, 2.0f);                     return "[MANGUEIRA] altura ok"; }
	}

	return "[MANGUEIRA] uso: /mangueira | a [x y z] | pegar | soltar | limpar | marcador on|off | "
		   "config segmentos|sag|alcance|raio|altura <n>";
}
