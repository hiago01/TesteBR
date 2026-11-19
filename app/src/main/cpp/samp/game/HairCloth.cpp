#include "HairCloth.h"

#include "HairPhysics.h"
#include "HairClothMesh.h"
#include "SkinSlots.h"
#include "CorpTex.h"
#include "CustomSkin.h"
#include "ProvadorModel.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "Camera.h"
#include "RW/RenderWare.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// =====================================================================================
//  Cabelo de tecido caido (hair6): ciclo de vida por jogador, simulacao e desenho (RwIm3D). Ver HairCloth.h.
//  A fisica (HairPhysics) e a geometria (HairClothMesh) sao modulos PUROS testados no PC; aqui fica so a cola com o jogo.
// =====================================================================================

static_assert(sizeof(MxHair::DrawVertex) == sizeof(RxObjSpace3DVertex), "layout Im3D");
static_assert(offsetof(MxHair::DrawVertex, nx) == offsetof(RxObjSpace3DVertex, objNormal), "layout Im3D");
static_assert(offsetof(MxHair::DrawVertex, color) == offsetof(RxObjSpace3DVertex, color), "layout Im3D");
static_assert(offsetof(MxHair::DrawVertex, u) == offsetof(RxObjSpace3DVertex, u), "layout Im3D");

namespace
{
	using HcClock = std::chrono::steady_clock;

	constexpr int   kBone       = 8;        // osso da cabeca (igual ao IdCard3D / CPlayerTags::Render)
	constexpr int   kMaxActive  = 14;       // no maximo N cabelos simulados/desenhados por quadro (o do ator local tem prioridade)
	constexpr float kMaxDist    = 40.0f;    // m: alem disso nem simula
	constexpr float kIdleSec    = 2.0f;     // sem uso por tanto tempo -> libera a simulacao

	struct HcCfg
	{
		bool  enabled   = true;
		float windDirRad = 0.8f;            // PARA ONDE o vento sopra
		float windInt    = 0.35f;
		bool  drift      = true;            // direcao do vento muda devagar
		int   testVar    = 0;               // /cabelo6 teste: forca no ator local (0 = desligado)
		uint32_t testC1  = 0, testC2 = 0;   // cores do teste (0 = padrao do slot)
	} g_hcCfg;

	struct HcEntry
	{
		MxHair::Sim sim;
		int      var = 0;                   // variante simulada (formato da folha depende dela)
		uint32_t c1 = 0, c2 = 0;            // cores efetivas (RAIZ, PONTA)
		uint32_t seed = 1;
		bool     visible = false;           // foi atualizado NESTE quadro (Render so desenha estes)
		int64_t  lastUse = 0;
		MxHair::Frame frame{};
	};
	std::unique_ptr<HcEntry> g_hc[MAX_PLAYERS];
	std::mutex             g_hcMtx;
	int64_t                g_hcLastMs = 0;
	uint32_t               g_hcFrame = 0;
	double                 g_hcWindT = 0.0;
	// diagnostico
	int      g_hcNActive = 0;
	float    g_hcCostMs = 0.0f;
	uint32_t g_hcDrawn = 0;

	int64_t HcNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(HcClock::now().time_since_epoch()).count(); }

	int HcHair6Id()
	{
		static int id = 0;
		if (!id) { const SkinSlots::Slot* sl = SkinSlots::Find("hair6"); if (sl) id = (int)(sl - SkinSlots::kSlots) + 1; }
		return id;
	}

	// formato da folha por variante (1 ombro / 2 meio das costas / 3 cintura)
	MxHair::Shape HcShapeFor(int var)
	{
		MxHair::Shape s;
		s.cols = MxHair::kSheetCols; s.rows = 10; s.arcDeg = 250.0f; s.ragged = 0.18f;
		s.spikePeriod = MxHair::kToothPeriod; s.spikeDepth = 0.50f;
		s.length = var <= 1 ? 0.30f : var == 2 ? 0.46f : 0.62f;
		return s;
	}

	// O que mostrar para o jogador `id`? false = sem cabelo de tecido.
	bool HcResolve(PLAYERID id, bool isLocal, int& var, uint32_t& c1, uint32_t& c2)
	{
		const int sid = HcHair6Id();
		if (!sid) return false;
		const SkinSlots::Slot& sl = SkinSlots::kSlots[sid - 1];
		if (isLocal && g_hcCfg.testVar > 0)
		{
			var = g_hcCfg.testVar;
			c1 = g_hcCfg.testC1 ? g_hcCfg.testC1 : sl.tint; c2 = g_hcCfg.testC2 ? g_hcCfg.testC2 : sl.tint2;
			return true;
		}
		if (isLocal && CorpTex::LocalPreview())          // /provador aberto: a previa vive no molde (CustomSkin), nao no snapshot do servidor
		{
			CCustomSkin::UiState st;
			if (CCustomSkin::UiGetState(st) && (size_t)(sid - 1) < st.slots.size())
			{
				const CCustomSkin::UiSlot& u = st.slots[(size_t)(sid - 1)];
				if (u.state == ProvadorModel::kEquipped && u.variant >= 1)
				{
					var = u.variant;
					c1 = u.argb ? u.argb : sl.tint; c2 = u.argb2 ? u.argb2 : sl.tint2;
					return true;
				}
			}
			return false;
		}
		CorpTex::SlotView sv;
		if (!CorpTex::PlayerSlot(id, sid, sv) || sv.est != 2 || sv.var < 1) return false;
		var = (int)sv.var;
		c1 = sv.argb ? SkinSlots::CleanArgb(sv.argb) : sl.tint;
		c2 = sv.argb2 ? SkinSlots::CleanArgb(sv.argb2) : sl.tint2;
		return true;
	}

	void HcSetStates()
	{
		CHook::CallFunction<void>(g_libGTASA + 0x6F4EC4);   // DefinedState(), igual ao Hose.cpp / Flag.cpp / IdCard3D.cpp

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(NULL));
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(FALSE));
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
		RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(TRUE));
	}

	// indice do Im3D e de 16 bits nesta libGTASA (ver nota no Hose.cpp)
	void HcSubmit(const MxHair::DrawVertex* v, int nv, const uint16_t* idx, int ni)
	{
		if (nv <= 0 || ni <= 0) return;
		if (RwIm3DTransform(reinterpret_cast<RwIm3DVertex*>(const_cast<MxHair::DrawVertex*>(v)), (RwUInt32)nv, nullptr, 0))
		{
			RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, reinterpret_cast<RwImVertexIndex*>(const_cast<uint16_t*>(idx)), ni);
			RwIm3DEnd();
		}
	}

	MxHair::DrawVertex g_hcSheetV[MxHair::kSheetMaxVerts];
	uint16_t           g_hcSheetI[MxHair::kSheetMaxIdx];
	MxHair::DrawVertex g_hcDomeV[MxHair::kDomeVerts];
	uint16_t           g_hcDomeI[MxHair::kDomeIdx];

	std::vector<std::string> HcSplit(const std::string& s)
	{
		std::vector<std::string> a; size_t i = 0;
		while (i < s.size())
		{
			while (i < s.size() && s[i] == ' ') i++;
			size_t j = i; while (j < s.size() && s[j] != ' ') j++;
			if (j > i) a.push_back(s.substr(i, j - i));
			i = j;
		}
		return a;
	}
	bool HcNum(const std::string& s, float& v) { char* e = nullptr; v = std::strtof(s.c_str(), &e); return e && *e == 0 && !s.empty() && std::isfinite(v); }
	bool HcHex(const std::string& s0, uint32_t& argb)   // "RRGGBB" ou "#RRGGBB" ou "AARRGGBB"
	{
		std::string s = s0; if (!s.empty() && s[0] == '#') s.erase(0, 1);
		if (s.size() != 6 && s.size() != 8) return false;
		char* e = nullptr; const unsigned long v = std::strtoul(s.c_str(), &e, 16);
		if (!e || *e) return false;
		argb = s.size() == 6 ? (0xFF000000u | (uint32_t)v) : (uint32_t)v;
		return true;
	}
	std::string HcHx(uint32_t a) { char b[16]; std::snprintf(b, sizeof(b), "%06X", (unsigned)(a & 0xFFFFFF)); return b; }
}

// =====================================================================================
//  Atualizacao: escolhe quem tem cabelo, posiciona a raiz na cabeca (osso 8) e simula
// =====================================================================================
void HairCloth_Update()
{
	std::lock_guard<std::mutex> lk(g_hcMtx);
	++g_hcFrame;
	for (auto& e : g_hc) if (e) e->visible = false;
	g_hcNActive = 0;
	if (!g_hcCfg.enabled || !pNetGame || !pGame) return;
	CPlayerPool* pool = pNetGame->GetPlayerPool();
	if (!pool) return;
	if (!HcHair6Id()) return;

	const int64_t now = HcNowMs();
	float dt = g_hcLastMs ? (float)(now - g_hcLastMs) * 0.001f : 1.0f / 60.0f;
	g_hcLastMs = now;
	dt = std::clamp(dt, 0.0f, MxHair::kMaxFrameDt);
	g_hcWindT += dt;
	const float wdir = g_hcCfg.windDirRad + (g_hcCfg.drift ? 0.55f * (float)std::sin(g_hcWindT * 0.07) : 0.0f);
	const float wint = g_hcCfg.windInt * (g_hcCfg.drift ? (1.0f + 0.25f * (float)std::sin(g_hcWindT * 0.11 + 1.3)) : 1.0f);

	const auto t0 = HcClock::now();
	const PLAYERID localId = pool->GetLocalPlayerID();
	int active = 0;

	// ordem: o ator local primeiro (sempre cabe no limite), depois os demais por id
	for (int pass = 0; pass < 2 && active < kMaxActive; ++pass)
	{
		for (int id = 0; id < MAX_PLAYERS && active < kMaxActive; ++id)
		{
			const bool isLocal = (id == localId);
			if ((pass == 0) != isLocal) continue;

			CPlayerPed* ped = nullptr;
			if (isLocal)
			{
				CLocalPlayer* lp = pool->GetLocalPlayer();
				ped = lp ? lp->GetPlayerPed() : nullptr;
			}
			else
			{
				if (!pool->GetSlotState((PLAYERID)id)) continue;
				CRemotePlayer* rp = pool->GetAt((PLAYERID)id);
				if (!rp || !rp->IsActive() || rp->IsNPC()) continue;
				ped = rp->GetPlayerPed();
			}
			if (!ped || !ped->m_pPed || !ped->m_pPed->IsAdded() || !ped->m_pPed->m_matrix) continue;

			int var = 0; uint32_t c1 = 0, c2 = 0;
			if (!HcResolve((PLAYERID)id, isLocal, var, c1, c2)) continue;
			if (ped->m_pPed->GetDistanceFromCamera() > kMaxDist) continue;

			std::unique_ptr<HcEntry>& slot = g_hc[id];
			if (!slot) { slot = std::make_unique<HcEntry>(); slot->seed = 0x9E3779B1u * (uint32_t)(id + 1); }
			HcEntry& e = *slot;
			if (e.var != var || !e.sim.Ready())           // variante nova: refaz a folha (comprimento muda)
			{
				if (!e.sim.Init(HcShapeFor(var), e.seed)) { slot.reset(); continue; }
				e.var = var;
			}
			e.c1 = c1; e.c2 = c2;

			CVector head(0.0f, 0.0f, 0.0f);
			ped->GetBonePosition(kBone, &head);
			const CVector rgt = ped->m_pPed->m_matrix->GetRight(), fwd = ped->m_pPed->m_matrix->GetForward(), upv = ped->m_pPed->m_matrix->GetUp();
			MxHair::Frame f;
			f.head = { head.x, head.y, head.z };
			f.right = { rgt.x, rgt.y, rgt.z }; f.fwd = { fwd.x, fwd.y, fwd.z }; f.up = { upv.x, upv.y, upv.z };
			f.body = !ped->m_pPed->IsInVehicle();         // no carro o tronco/banco nao e o cilindro do corpo: sem colisao
			e.frame = f;
			e.sim.SetFrame(f);
			e.sim.SetWind(wdir, wint);
			e.sim.Update(dt);
			e.visible = true; e.lastUse = now;
			++active;
		}
	}
	g_hcNActive = active;

	for (auto& e : g_hc)                                    // libera quem sumiu faz tempo (mem. e custo)
		if (e && !e->visible && now - e->lastUse > (int64_t)(kIdleSec * 1000.0f)) e.reset();

	g_hcCostMs = std::chrono::duration<float, std::milli>(HcClock::now() - t0).count();
}

// =====================================================================================
//  Desenho (RwIm3D): calota + folha, por jogador
// =====================================================================================
void HairCloth_Render()
{
	std::lock_guard<std::mutex> lk(g_hcMtx);
	if (!g_hcCfg.enabled || g_hcNActive <= 0) return;

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector camC = TheCamera.GetPosition();
	const MxHair::V3 cam = { camC.x, camC.y, camC.z };

	bool states = false;
	MxHair::V3 pos[MxHair::kMaxP], nrm[MxHair::kMaxP];
	for (int id = 0; id < MAX_PLAYERS; ++id)
	{
		HcEntry* e = g_hc[id].get();
		if (!e || !e->visible || !e->sim.Ready()) continue;

		if (!states) { HcSetStates(); states = true; }

		const int nd = MxHair::BuildDome(e->frame, e->c1, e->seed, g_hcDomeV, g_hcDomeI);
		HcSubmit(g_hcDomeV, MxHair::kDomeVerts, g_hcDomeI, nd);

		e->sim.GetRenderPositions(pos);
		e->sim.ComputeNormals(pos, nrm);
		int nv = 0;
		const int ni = MxHair::BuildSheet(e->sim, pos, nrm, cam, e->c1, e->c2, e->seed, g_hcSheetV, g_hcSheetI, nv);
		HcSubmit(g_hcSheetV, nv, g_hcSheetI, ni);
		++g_hcDrawn;
	}
}

void HairCloth_Reset()
{
	std::lock_guard<std::mutex> lk(g_hcMtx);
	for (auto& e : g_hc) e.reset();
	g_hcNActive = 0; g_hcLastMs = 0;
}

// =====================================================================================
//  Comando: /cabelo6 ...
// =====================================================================================
std::string HairCloth_Command(const std::string& args)
{
	std::lock_guard<std::mutex> lk(g_hcMtx);
	const std::vector<std::string> a = HcSplit(args);
	char buf[256];
	if (a.empty() || a[0] == "ajuda" || a[0] == "help")
		return "[CABELO6] /cabelo6 teste <1-3|off> | cor <RAIZ RRGGBB> [PONTA RRGGBB] | vento <graus> <0-4> | deriva on|off | on|off | info | "
			   "tune <nome> <valor> | tune lista | tune padrao";
	const std::string& c = a[0];
	if (c == "on" || c == "off") { g_hcCfg.enabled = (c == "on"); return std::string("[CABELO6] ") + (g_hcCfg.enabled ? "ligado" : "desligado"); }
	if (c == "teste")
	{
		if (a.size() < 2) return "[CABELO6] uso: /cabelo6 teste <1|2|3|off>  (forca o cabelo de tecido no SEU ator, sem servidor)";
		if (a[1] == "off") { g_hcCfg.testVar = 0; return "[CABELO6] teste desligado (volta a valer o servidor/provador)"; }
		const int v = std::atoi(a[1].c_str());
		if (v < 1 || v > 3) return "[CABELO6] variante 1..3 (1 ombro, 2 meio das costas, 3 cintura)";
		g_hcCfg.testVar = v;
		return "[CABELO6] teste: variante " + std::to_string(v) + " no ator local (cores " + (g_hcCfg.testC1 ? HcHx(g_hcCfg.testC1) : "padrao") + "/" + (g_hcCfg.testC2 ? HcHx(g_hcCfg.testC2) : "padrao") + ")";
	}
	if (c == "cor")
	{
		uint32_t x = 0, y = 0;
		if (a.size() < 2 || !HcHex(a[1], x)) return "[CABELO6] uso: /cabelo6 cor <RAIZ RRGGBB> [PONTA RRGGBB]   (ex.: /cabelo6 cor E95151 2ECC40)";
		if (a.size() >= 3 && !HcHex(a[2], y)) return "[CABELO6] cor da ponta invalida (RRGGBB)";
		g_hcCfg.testC1 = x; g_hcCfg.testC2 = y ? y : 0;
		return "[CABELO6] cores do TESTE: raiz " + HcHx(x) + (y ? " ponta " + HcHx(y) : std::string(" ponta = padrao"));
	}
	if (c == "vento")
	{
		float d = 0, in = 0;
		if (a.size() < 3 || !HcNum(a[1], d) || !HcNum(a[2], in)) return "[CABELO6] uso: /cabelo6 vento <graus PARA ONDE sopra> <0..4>";
		g_hcCfg.windDirRad = d * 3.14159265f / 180.0f; g_hcCfg.windInt = std::clamp(in, 0.0f, 4.0f);
		return "[CABELO6] vento " + std::to_string((int)d) + " graus, intensidade " + std::to_string(g_hcCfg.windInt).substr(0, 4);
	}
	if (c == "deriva") { g_hcCfg.drift = !(a.size() > 1 && a[1] == "off"); return std::string("[CABELO6] deriva do vento ") + (g_hcCfg.drift ? "ligada" : "desligada"); }
	if (c == "info")
	{
		uint32_t resets = 0; float stretch = 1.0f; int sub = 0;
		for (auto& e : g_hc) if (e && e->sim.Ready()) { resets += e->sim.stats.resets; stretch = std::max(stretch, e->sim.stats.maxStretchSeen); sub = std::max(sub, e->sim.stats.lastSubsteps); }
		std::snprintf(buf, sizeof(buf), "[CABELO6] %s | ativos %d | custo %.2f ms/quadro | desenhos %u | resets %u | estiramento max %.3f | substeps %d | slot id %d",
					  g_hcCfg.enabled ? "ligado" : "desligado", g_hcNActive, g_hcCostMs, (unsigned)g_hcDrawn, (unsigned)resets, stretch, sub, HcHair6Id());
		return buf;
	}
	if (c == "tune")
	{
		MxHair::Tune& T = MxHair::GlobalTune();
		struct P { const char* n; float* p; };
		const P ps[] = { { "skullUp", &T.skullUp }, { "skullR", &T.skullR }, { "rootElev", &T.rootElevDeg }, { "rootRadius", &T.rootRadius },
						 { "skin", &T.skin }, { "gravity", &T.gravity }, { "damping", &T.damping }, { "baseSpeed", &T.baseSpeed }, { "gust", &T.gust },
						 { "flutter", &T.flutter }, { "pressure", &T.pressureK }, { "tangent", &T.tangentK }, { "stretch", &T.stretchMax } };
		if (a.size() >= 2 && a[1] == "padrao") { T = MxHair::Tune{}; return "[CABELO6] tune: valores padrao restaurados"; }
		if (a.size() < 3)
		{
			std::string s = "[CABELO6] tune:";
			for (const P& q : ps) { std::snprintf(buf, sizeof(buf), " %s=%.3f", q.n, *q.p); s += buf; }
			return s;
		}
		float v = 0; if (!HcNum(a[2], v)) return "[CABELO6] valor invalido";
		for (const P& q : ps) if (a[1] == q.n) { *q.p = v; std::snprintf(buf, sizeof(buf), "[CABELO6] %s = %.4f", q.n, v); return buf; }
		return "[CABELO6] parametro desconhecido (/cabelo6 tune lista)";
	}
	return "[CABELO6] comando desconhecido (/cabelo6 ajuda)";
}
