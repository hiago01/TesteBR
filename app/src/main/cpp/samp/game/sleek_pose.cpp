// SleekMovement (recriacao): postura do corpo (lean/pitch, head tracking, head follow) e foot IK.
// Mecanismo: hook em RpHAnimHierarchyUpdateMatrices. So para o esqueleto do jogador LOCAL:
//   1) salva as matrizes locais (modelling) dos ossos que vamos tocar;
//   2) gira os ossos (a mesma tecnica do CPedIK do proprio jogo: edita os RwFrame dos ossos antes do calculo das matrizes);
//   3) chama o original (pose pronta); 4) foot IK: sonda o chao, 2 ossos (coxa/canela) + pelve, chama o original de novo;
//   5) restaura as matrizes locais (idempotente: nunca acumula rotacao entre chamadas/quadros).
#include "../main.h"
#include "game.h"
#include "util.h"
#include "sleek.h"
#include "sleek_priv.h"
#include "pad.h"
#include "Camera.h"
#include "World.h"
#include "Entity/CPedGTA.h"
#include "Enums/ePedBones.h"
#include "RW/RwHelper.h"
#include "RW/rwcore.h"
#include "RW/rphanim.h"
#include "../vendor/armhook/patch.h"

#include <chrono>
#include <cmath>
#include <cstdio>

extern PAD_KEYS LocalPlayerKeys;

namespace
{
	using namespace Sleek;
	RwBool (*g_orig)(RpHAnimHierarchy*) = nullptr;
	bool g_in = false;

	enum { B_PELVIS, B_SPINE, B_SPINE1, B_NECK, B_HEAD, B_LTHIGH, B_LCALF, B_LFOOT, B_RTHIGH, B_RCALF, B_RFOOT, B_COUNT };
	constexpr int kBoneId[B_COUNT] = { BONE_PELVIS, BONE_SPINE, BONE_SPINE1, BONE_NECK, BONE_HEAD, BONE_L_THIGH, BONE_L_CALF, BONE_L_FOOT, BONE_R_THIGH, BONE_R_CALF, BONE_R_FOOT };

	struct BoneCache
	{
		RpHAnimHierarchy* h = nullptr;
		int idx[B_COUNT], par[B_COUNT];
		bool usable = false;
	} g_bc;

	struct Saved { RwFrame* f = nullptr; RwMatrix m; };

	// estado (atualizado ~1x por quadro)
	Lean g_lean; HeadTrack g_head; FootState g_ft[2];
	LeanOut g_leanOut; HeadOut g_headOut;
	float g_pelvisDz = 0.0f;
	using Clock = std::chrono::steady_clock;
	Clock::time_point g_last{};
	V3 g_prevFwd{ 0, 1, 0 }; float g_prevFwdSpeed = 0.0f; bool g_havePrev = false;
	uint32_t g_epoch = 0xFFFFFFFFu;
	bool g_loggedFlags = false;
	int  g_dbgCount = 0;

	inline V3 ToV(const RwV3d& v) { return { v.x, v.y, v.z }; }
	inline void SetV(RwV3d& d, const V3& s) { d.x = s.x; d.y = s.y; d.z = s.z; }

	bool BuildCache(RpHAnimHierarchy* h)
	{
		g_bc = BoneCache(); g_bc.h = h;
		if (!h || !h->pNodeInfo || !h->pMatrixArray || h->numNodes <= 0 || h->numNodes > 128) return false;
		for (int b = 0; b < B_COUNT; b++)
		{
			g_bc.idx[b] = RpHAnimIDGetIndex(h, kBoneId[b]);
			g_bc.par[b] = -1;
			if (g_bc.idx[b] < 0 || g_bc.idx[b] >= h->numNodes) { g_bc.idx[b] = -1; continue; }
			RwFrame* f = h->pNodeInfo[g_bc.idx[b]].pFrame;
			RwFrame* pf = f ? RwFrameGetParent(f) : nullptr;
			if (!pf) { g_bc.idx[b] = -1; continue; }
			for (int n = 0; n < h->numNodes; n++) if (h->pNodeInfo[n].pFrame == pf) { g_bc.par[b] = n; break; }
			if (g_bc.par[b] < 0) g_bc.idx[b] = -1;
		}
		// minimo para funcionar: espinha + cabeca (as pernas sao opcionais: skins sem elas so perdem o foot IK)
		g_bc.usable = g_bc.idx[B_SPINE] >= 0 && g_bc.idx[B_HEAD] >= 0;
		if (!g_loggedFlags)
		{
			g_loggedFlags = true;
			FLog("[SLEEK-POSE] hierarquia: flags=0x%x nos=%d spine=%d head=%d lthigh=%d rthigh=%d usable=%d", h->flags, h->numNodes, g_bc.idx[B_SPINE], g_bc.idx[B_HEAD], g_bc.idx[B_LTHIGH], g_bc.idx[B_RTHIGH], g_bc.usable ? 1 : 0);
		}
		// se o RW reescreve os frames a partir da animacao em cada chamada (flag 0x1000), editar frames nao tem efeito: desliga com aviso
		if (h->flags & rpHANIMHIERARCHYUPDATEMODELLINGMATRICES) { FLog("[SLEEK-POSE] AVISO: hierarquia com UPDATEMODELLINGMATRICES - postura/IK desativados (editar frames nao teria efeito)"); g_bc.usable = false; }
		return g_bc.usable;
	}

	inline RwFrame* FrameOf(RpHAnimHierarchy* h, int b) { return g_bc.idx[b] >= 0 ? h->pNodeInfo[g_bc.idx[b]].pFrame : nullptr; }

	// matriz de MUNDO de um no (pMatrixArray, ou ltm do frame se a hierarquia for em espaco local)
	inline const RwMatrix* WorldOf(RpHAnimHierarchy* h, int node)
	{
		if (h->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) { RwFrame* f = h->pNodeInfo[node].pFrame; return f ? &f->ltm : &h->pMatrixArray[node]; }
		return &h->pMatrixArray[node];
	}
	inline Basis BasisOf(const RwMatrix* m) { Basis b; b.r = Norm(ToV(m->right), { 1, 0, 0 }); b.u = Norm(ToV(m->up), { 0, 1, 0 }); b.a = Norm(ToV(m->at), { 0, 0, 1 }); return b; }
	inline Basis ParentBasis(RpHAnimHierarchy* h, int b) { return BasisOf(WorldOf(h, g_bc.par[b])); }

	// aplica rotacoes de MUNDO (eixos ped) no osso b em torno do proprio pivo
	void RotateBone(RpHAnimHierarchy* h, int b, const Basis& parent, const V3& axisFwd, const V3& axisRight, float roll, float pitch, float yaw)
	{
		RwFrame* f = FrameOf(h, b); if (!f) return;
		V3 r = ToV(f->modelling.right), u = ToV(f->modelling.up), a = ToV(f->modelling.at);
		if (std::fabs(roll)  > 1e-5f) RotateLocalByWorld(r, u, a, parent, axisFwd, roll);
		if (std::fabs(pitch) > 1e-5f) RotateLocalByWorld(r, u, a, parent, axisRight, pitch);
		if (std::fabs(yaw)   > 1e-5f) RotateLocalByWorld(r, u, a, parent, V3{ 0, 0, 1 }, yaw);
		SetV(f->modelling.right, r); SetV(f->modelling.up, u); SetV(f->modelling.at, a);
		RwMatrixUpdate(&f->modelling);
	}

	RwBool DoPose(RpHAnimHierarchy* h, CPedGTA* ped)
	{
		const PoseConfig& pc = SleekMove::PoseCfg();
		const Config& cc = SleekMove::Cfg();
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		const bool crouched = ped->bIsDucking;
		const bool doPosture = (pc.lean || pc.pitch || pc.headTracking) && !aiming;
		const bool doFoot = pc.footIK && (!crouched || pc.footIKCrouched) && g_bc.idx[B_LTHIGH] >= 0 && g_bc.idx[B_LCALF] >= 0 && g_bc.idx[B_LFOOT] >= 0 &&
		                    g_bc.idx[B_RTHIGH] >= 0 && g_bc.idx[B_RCALF] >= 0 && g_bc.idx[B_RFOOT] >= 0 && g_bc.idx[B_PELVIS] >= 0;
		if (!doPosture && !doFoot) return g_orig(h);

		// ---- estado: ~1x por quadro ----
		const auto now = Clock::now();
		const bool first = g_last.time_since_epoch().count() == 0;
		float since = first ? 1.0f : std::chrono::duration<float>(now - g_last).count();
		const bool gate = since >= 0.003f;
		if (g_epoch != SleekMove::TeleportEpoch()) { g_epoch = SleekMove::TeleportEpoch(); g_lean.Reset(); g_head.Reset(); g_ft[0] = g_ft[1] = FootState(); g_pelvisDz = 0; g_havePrev = false; since = 0.016f; }
		if (since > 0.25f) { g_lean.Reset(); g_head.Reset(); g_havePrev = false; since = 0.016f; }
		const float dt = Clamp(since, 0.0005f, 0.05f);

		// base do corpo no mundo
		V3 fwd = { 0, 1, 0 };
		if (ped->m_matrix) fwd = Norm({ ped->m_matrix->m_forward.x, ped->m_matrix->m_forward.y, 0 }, { 0, 1, 0 });
		const V3 right = Norm(Cross(fwd, V3{ 0, 0, 1 }), { 1, 0, 0 });

		if (gate)
		{
			g_last = now;
			const V3 vel = { ped->m_vecMoveSpeed.x * 50.0f, ped->m_vecMoveSpeed.y * 50.0f, 0 };     // m/s (o jogo guarda por 1/50 s)
			const float fs = Clamp(Dot(vel, fwd) / 6.0f, -1.0f, 1.0f), rs = Clamp(Dot(vel, right) / 6.0f, -1.0f, 1.0f);
			float angVel = 0, accel = 0;
			if (g_havePrev)
			{
				angVel = std::atan2(Cross(g_prevFwd, fwd).z, Clamp(Dot(g_prevFwd, fwd), -1.0f, 1.0f)) / dt;
				accel = Clamp((fs - g_prevFwdSpeed) / dt * 0.5f, -1.0f, 1.0f);
			}
			g_prevFwd = fwd; g_prevFwdSpeed = fs; g_havePrev = true;

			LeanIn li; li.active = doPosture && (pc.lean || pc.pitch);
			li.fwdSpeed = fs; li.rightSpeed = rs; li.angVel = angVel; li.accel = accel;
			li.inputRight = Clamp((float)(int16_t)LocalPlayerKeys.wKeyLR / 128.0f, -1.0f, 1.0f);
			g_leanOut = g_lean.Update(pc, li, dt);
			if (!pc.lean) g_leanOut.roll = 0; if (!pc.pitch) g_leanOut.pitch = 0;

			HeadIn hi; hi.active = doPosture && pc.headTracking;
			hi.moving = (std::fabs(fs) + std::fabs(rs)) > 0.1f;
			const CVector& cf = CCamera::GetActiveCamera().Front;
			const V3 cfH = Norm({ cf.x, cf.y, 0 }, fwd);
			hi.yawRel = std::atan2(Cross(fwd, cfH).z, Clamp(Dot(fwd, cfH), -1.0f, 1.0f));
			hi.pitch = std::asin(Clamp(cf.z, -1.0f, 1.0f));
			g_headOut = g_head.Update(pc, hi, dt);
		}
		(void)cc;

		// ---- salva e aplica postura ----
		Saved sv[B_COUNT];
		auto save = [&](int b) { RwFrame* f = FrameOf(h, b); if (f && !sv[b].f) { sv[b].f = f; sv[b].m = f->modelling; } };
		if (doPosture)
		{
			const LeanOut& L = g_leanOut; const HeadOut& H = g_headOut;
			struct Part { int b; float roll, pitch, yaw; };
			const Part parts[] = {
				{ B_SPINE,  L.roll * 0.4f, L.pitch * 0.4f, H.torsoYaw * 0.5f },
				{ B_SPINE1, L.roll * 0.6f, L.pitch * 0.6f, H.torsoYaw * 0.5f },
				{ B_NECK,   0.0f,          0.0f,           H.neckYaw },
				{ B_HEAD,   pc.headFollow ? -pc.hfLean * L.roll : 0.0f, pc.headFollow ? -pc.hfPitch * L.pitch : 0.0f, H.headYaw },
			};
			// pitch do tronco = lean (+ cabeca/pescoco acrescentam o olhar para cima/baixo)
			float extraPitch[B_COUNT] = {};
			extraPitch[B_SPINE] = H.torsoPitch * 0.5f; extraPitch[B_SPINE1] = H.torsoPitch * 0.5f; extraPitch[B_NECK] = H.neckPitch; extraPitch[B_HEAD] = H.headPitch;
			for (const Part& p : parts)
			{
				if (g_bc.idx[p.b] < 0) continue;
				save(p.b);
				RotateBone(h, p.b, ParentBasis(h, p.b), fwd, right, p.roll, p.pitch + extraPitch[p.b], p.yaw);
			}
		}
		RwBool ret = g_orig(h);

		// ---- foot IK (segunda passada) ----
		if (doFoot)
		{
			const int T[2] = { B_LTHIGH, B_RTHIGH }, K[2] = { B_LCALF, B_RCALF }, Ft[2] = { B_LFOOT, B_RFOOT };
			V3 tp[2], kp[2], fp[2];
			for (int s = 0; s < 2; s++) { tp[s] = ToV(WorldOf(h, g_bc.idx[T[s]])->pos); kp[s] = ToV(WorldOf(h, g_bc.idx[K[s]])->pos); fp[s] = ToV(WorldOf(h, g_bc.idx[Ft[s]])->pos); }
			if (gate)
			{
				const float pedZ = ped->GetPosition().z;
				for (int s = 0; s < 2; s++)
				{
					FootProbe pr;
					const CVector o(fp[s].x, fp[s].y, fp[s].z + pc.probeStart), t(fp[s].x, fp[s].y, fp[s].z + pc.probeStart - pc.probeDepth - pc.probeStart);
					CColPoint col; CEntityGTA* ent = nullptr;
					if (CWorld::ProcessLineOfSight(&o, &t, &col, &ent, true, true, false, true, false, false, false, false))
					{ pr.hit = true; pr.groundZ = col.m_vecPoint.z; pr.normal = { col.m_vecNormal.x, col.m_vecNormal.y, col.m_vecNormal.z }; }
					FootSmooth(pc, g_ft[s], FootRaw(pc, pr, fp[s].z, pedZ), dt);
				}
				g_pelvisDz = Approach(g_pelvisDz, std::min(0.0f, std::min(g_ft[0].dz, g_ft[1].dz)), pc.posResp, dt);
			}
			bool any = std::fabs(g_pelvisDz) > 1e-3f;
			for (int s = 0; s < 2; s++) any = any || std::fabs(g_ft[s].dz) > 1e-3f || (g_ft[s].weight > 0.05f && g_ft[s].nz < 0.999f);
			if (any)
			{
				for (int b : { B_PELVIS, B_LTHIGH, B_LCALF, B_LFOOT, B_RTHIGH, B_RCALF, B_RFOOT }) save(b);
				// pelve desce para o pe mais baixo alcancar o chao
				if (std::fabs(g_pelvisDz) > 1e-4f)
				{
					RwFrame* pf = FrameOf(h, B_PELVIS);
					const V3 d = WorldDeltaToLocal(ParentBasis(h, B_PELVIS), { 0, 0, g_pelvisDz });
					pf->modelling.pos.x += d.x; pf->modelling.pos.y += d.y; pf->modelling.pos.z += d.z;
					RwMatrixUpdate(&pf->modelling);
				}
				for (int s = 0; s < 2; s++)
				{
					const FootState& fs = g_ft[s];
					const V3 off = { 0, 0, g_pelvisDz };
					const V3 T0 = tp[s] + off, K0 = kp[s] + off, F0 = fp[s] + off;
					const V3 target = fp[s] + V3{ 0, 0, fs.dz };
					Basis thighP = ParentBasis(h, T[s]);
					Basis calfP = BasisOf(WorldOf(h, g_bc.idx[T[s]])), footP = BasisOf(WorldOf(h, g_bc.idx[K[s]]));
					if (Len(target - F0) > 1e-3f)
					{
						const V3 Kn = TwoBoneKnee(T0, K0, F0, target);
						V3 ax1; float an1; Arc(K0 - T0, Kn - T0, ax1, an1); an1 = LimitAngle(an1, pc.maxHipDeg);
						{ RwFrame* f = FrameOf(h, T[s]); V3 r = ToV(f->modelling.right), u = ToV(f->modelling.up), a = ToV(f->modelling.at); RotateLocalByWorld(r, u, a, thighP, ax1, an1); SetV(f->modelling.right, r); SetV(f->modelling.up, u); SetV(f->modelling.at, a); RwMatrixUpdate(&f->modelling); }
						// base da canela e do pe depois da rotacao da coxa
						auto rotB = [&](Basis& b) { b.r = Rot(b.r, ax1, an1); b.u = Rot(b.u, ax1, an1); b.a = Rot(b.a, ax1, an1); };
						rotB(calfP); rotB(footP);
						const V3 F1 = T0 + Rot(F0 - T0, ax1, an1);
						V3 ax2; float an2; Arc(F1 - Kn, target - Kn, ax2, an2); an2 = LimitAngle(an2, pc.maxKneeDeg);
						{ RwFrame* f = FrameOf(h, K[s]); V3 r = ToV(f->modelling.right), u = ToV(f->modelling.up), a = ToV(f->modelling.at); RotateLocalByWorld(r, u, a, calfP, ax2, an2); SetV(f->modelling.right, r); SetV(f->modelling.up, u); SetV(f->modelling.at, a); RwMatrixUpdate(&f->modelling); }
						auto rotB2 = [&](Basis& b) { b.r = Rot(b.r, ax2, an2); b.u = Rot(b.u, ax2, an2); b.a = Rot(b.a, ax2, an2); };
						rotB2(footP);
					}
					// pe acompanha a inclinacao do chao
					if (fs.weight > 0.05f)
					{
						V3 axn; float ann; Arc({ 0, 0, 1 }, Norm({ fs.nx, fs.ny, fs.nz }), axn, ann);
						ann = LimitAngle(ann, pc.maxFootDeg) * fs.weight * pc.influence;
						if (std::fabs(ann) > 1e-4f)
						{ RwFrame* f = FrameOf(h, Ft[s]); V3 r = ToV(f->modelling.right), u = ToV(f->modelling.up), a = ToV(f->modelling.at); RotateLocalByWorld(r, u, a, footP, axn, ann); SetV(f->modelling.right, r); SetV(f->modelling.up, u); SetV(f->modelling.at, a); RwMatrixUpdate(&f->modelling); }
					}
				}
				ret = g_orig(h);
			}
		}

		// ---- restaura (idempotente) ----
		for (int b = 0; b < B_COUNT; b++) if (sv[b].f) sv[b].f->modelling = sv[b].m;
		if (g_dbgCount < 3 && doPosture && gate) { g_dbgCount++; FLog("[SLEEK-POSE] ok: lean roll=%.2f pitch=%.2f graus; cabeca yaw=%.2f; pelve dz=%.3f", g_leanOut.roll / kDeg2Rad, g_leanOut.pitch / kDeg2Rad, g_headOut.headYaw / kDeg2Rad, g_pelvisDz); }
		return ret;
	}

	RwBool Upd_hook(RpHAnimHierarchy* h)
	{
		if (g_in || !h) return g_orig(h);
		CPedGTA* ped = SleekMove::PoseEligiblePed();
		if (!ped || !ped->m_pRwClump) return g_orig(h);
		RpHAnimHierarchy* mine = GetAnimHierarchyFromSkinClump(ped->m_pRwClump);
		if (mine != h) return g_orig(h);
		if (g_bc.h != h && !BuildCache(h)) return g_orig(h);
		if (!g_bc.usable) return g_orig(h);
		g_in = true;
		RwBool r = DoPose(h, ped);
		g_in = false;
		return r;
	}
}

namespace SleekMove
{
	void InstallPoseHook()
	{
		// OPT-IN: sem o arquivo vazio SAMP/sleek_pose.on o hook NAO e instalado (o jogo roda exatamente como antes).
		{
			const std::string mk = g_pszStorage ? std::string(g_pszStorage) + "SAMP/sleek_pose.on" : std::string();
			FILE* f = mk.empty() ? nullptr : fopen(mk.c_str(), "rb");
			if (!f) { FLog("[SLEEK-POSE] desativado (crie SAMP/sleek_pose.on para instalar o hook de postura/IK)"); return; }
			fclose(f);
		}
		// NUNCA usar InlineHook("simbolo"): ele chama exit(0) se o simbolo nao existe (foi o crash "Error find RpHAnimHierarchyUpdateMatrices").
		// 1) tenta o simbolo de forma "suave"; 2) cai no endereco que o proprio projeto ja usa para esta funcao (RW/rphanim.cpp).
		const uintptr_t sym = CHook::getSymSoft("RpHAnimHierarchyUpdateMatrices");
		if (sym)
			CHook::InlineHook((void*)sym, &Upd_hook, &g_orig);
		else
			CHook::InlineHook(g_libGTASA, 0x255C20, &Upd_hook, &g_orig);
		FLog("[SLEEK-POSE] hook RpHAnimHierarchyUpdateMatrices via %s: %s", sym ? "simbolo" : "endereco 0x255C20", g_orig ? "instalado" : "FALHOU - postura/IK inativos");
	}
	std::string PoseStatus()
	{
		char b[200]; snprintf(b, sizeof(b), "hook=%s esqueleto=%s lean=%.1f/%.1f cabeca=%.1f pelve=%.3f", g_orig ? "ok" : "AUSENTE", g_bc.usable ? "ok" : "n/d",
			g_leanOut.roll / kDeg2Rad, g_leanOut.pitch / kDeg2Rad, g_headOut.headYaw / kDeg2Rad, g_pelvisDz);
		return b;
	}
}
