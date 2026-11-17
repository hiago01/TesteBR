// SleekMovement (recriacao): postura do corpo (lean/pitch, head tracking, head follow) e foot IK.
// Mecanismo (conferido no libGTASA.so ARM64: _Z30RpHAnimHierarchyUpdateMatricesP16RpHAnimHierarchy @ 0x255C20 converte os
// quaternions dos keyframes em pMatrixArray; editar os RwFrame antes NAO e confiavel). Por isso, so para o esqueleto do jogador LOCAL:
//   1) chama o original (pMatrixArray = pose do quadro, em espaco de MUNDO);
//   2) gira o osso E TODA A SUA SUBARVORE dentro de pMatrixArray, em torno do pivo do osso (equivale a girar o osso);
//   3) foot IK: pelve desce (subarvore inteira), coxa/canela giram, pe acompanha o chao.
// pMatrixArray e recalculado a cada chamada do original: nada acumula, nao ha o que restaurar.
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
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

extern PAD_KEYS LocalPlayerKeys;

namespace
{
	using namespace Sleek;
	RwBool (*g_orig)(RpHAnimHierarchy*) = nullptr;
	bool g_in = false;

	enum { B_PELVIS, B_SPINE, B_SPINE1, B_NECK, B_HEAD, B_LTHIGH, B_LCALF, B_LFOOT, B_RTHIGH, B_RCALF, B_RFOOT, B_RCLAV, B_LCLAV, B_COUNT };
	constexpr int kBoneId[B_COUNT] = { BONE_PELVIS, BONE_SPINE, BONE_SPINE1, BONE_NECK, BONE_HEAD, BONE_L_THIGH, BONE_L_CALF, BONE_L_FOOT, BONE_R_THIGH, BONE_R_CALF, BONE_R_FOOT, BONE_R_CLAVICLE, BONE_L_CLAVICLE };

	struct BoneCache
	{
		RpHAnimHierarchy* h = nullptr;
		int idx[B_COUNT], par[B_COUNT];
		bool usable = false;
	} g_bc;


	// estado (atualizado ~1x por quadro)
	Lean g_lean; HeadTrack g_head; FootState g_ft[2];
	LeanOut g_leanOut; HeadOut g_headOut;
	float g_pelvisDz = 0.0f;
	using Clock = std::chrono::steady_clock;
	Clock::time_point g_last{};
	V3 g_prevFwd{ 0, 1, 0 }; float g_prevFwdSpeed = 0.0f; bool g_havePrev = false;
	uint32_t g_epoch = 0xFFFFFFFFu;
	bool g_loggedFlags = false;
	uint32_t g_cCalls = 0, g_cNoPed = 0, g_cOther = 0, g_cMine = 0, g_cCacheFail = 0, g_cApplied = 0;
	float g_testDeg = 0.0f;   // /sleek pose teste <graus>: inclina o tronco para provar que o hook funciona
	int  g_dbgCount = 0;

	inline V3 ToV(const RwV3d& v) { return { v.x, v.y, v.z }; }
	inline void SetV(RwV3d& d, const V3& s) { d.x = s.x; d.y = s.y; d.z = s.z; }

	float g_accelF = 0.0f;
	int  g_parent[128];
	std::vector<int> g_sub[B_COUNT];
	bool BuildTree(RpHAnimHierarchy* h);
	bool BuildCache(RpHAnimHierarchy* h)
	{
		g_bc = BoneCache(); g_bc.h = h;
		for (int b = 0; b < B_COUNT; b++) { g_bc.idx[b] = -1; g_bc.par[b] = -1; }
		if (!h || !h->pNodeInfo || !h->pMatrixArray || h->numNodes <= 0 || h->numNodes > 128) { FLog("[SLEEK-POSE] hierarquia invalida (nos=%d)", h ? h->numNodes : -1); return false; }
		const int n = h->numNodes;
		// log unico: IDs reais dos nos (para conferir o mapeamento dos ossos)
		if (!g_loggedFlags)
		{
			std::string ids;
			for (int i = 0; i < n; i++) { char t[24]; snprintf(t, sizeof(t), "%d%s ", h->pNodeInfo[i].nodeID, (h->pNodeInfo[i].flags & 2) ? "^" : ((h->pNodeInfo[i].flags & 1) ? "v" : "")); ids += t; }
			FLog("[SLEEK-POSE] hierarquia: flags=0x%x nos=%d nodeIDs(^=push v=pop)= %s", h->flags, n, ids.c_str());
		}
		// ossos: procura pelo nodeID direto no vetor (sem depender de funcoes do RW nem de RwFrame)
		for (int b = 0; b < B_COUNT; b++)
			for (int i = 0; i < n; i++) if (h->pNodeInfo[i].nodeID == kBoneId[b]) { g_bc.idx[b] = i; break; }
		// pais pelas flags push/pop do proprio HAnim (0x2 = push, 0x1 = pop)
		{
			std::vector<int> st; int cur = -1;
			for (int i = 0; i < n; i++)
			{
				g_parent[i] = cur;
				const int fl = h->pNodeInfo[i].flags;
				if (fl & rpHANIMPUSHPARENTMATRIX) st.push_back(i);
				if (fl & rpHANIMPOPPARENTMATRIX) { cur = st.empty() ? -1 : st.back(); if (!st.empty()) st.pop_back(); }
				else cur = i;
			}
		}
		g_bc.usable = g_bc.idx[B_SPINE] >= 0 && g_bc.idx[B_HEAD] >= 0;
		// sanidade: a cabeca precisa descender da espinha
		if (g_bc.usable)
		{
			bool ok = false; int a = g_bc.idx[B_HEAD];
			for (int guard = 0; a >= 0 && guard < 64; guard++) { if (a == g_bc.idx[B_SPINE]) { ok = true; break; } a = g_parent[a]; }
			if (!ok) { FLog("[SLEEK-POSE] AVISO: a cabeca nao descende da espinha (push/pop inesperado) - desativado"); g_bc.usable = false; }
		}
		if (h->flags & rpHANIMHIERARCHYLOCALSPACEMATRICES) { FLog("[SLEEK-POSE] AVISO: espaco local (flags=0x%x) - desativado", h->flags); g_bc.usable = false; }
		if (g_bc.usable) BuildTree(h);
		if (!g_loggedFlags)
		{
			g_loggedFlags = true;
			FLog("[SLEEK-POSE] ossos: pelve=%d spine=%d spine1=%d neck=%d head=%d lthigh=%d lcalf=%d lfoot=%d rthigh=%d rcalf=%d rfoot=%d | usable=%d", g_bc.idx[B_PELVIS], g_bc.idx[B_SPINE], g_bc.idx[B_SPINE1], g_bc.idx[B_NECK], g_bc.idx[B_HEAD],
				g_bc.idx[B_LTHIGH], g_bc.idx[B_LCALF], g_bc.idx[B_LFOOT], g_bc.idx[B_RTHIGH], g_bc.idx[B_RCALF], g_bc.idx[B_RFOOT], g_bc.usable ? 1 : 0);
		}
		return g_bc.usable;
	}

	// ---- subarvores (pelo pai de cada no, via RwFrame) ----

	bool BuildTree(RpHAnimHierarchy* h)
	{
		const int n = h->numNodes;
		for (int b = 0; b < B_COUNT; b++)
		{
			g_sub[b].clear();
			if (g_bc.idx[b] < 0) continue;
			for (int i = g_bc.idx[b]; i < n; i++)
			{
				int a = i; bool in = false;
				for (int guard = 0; a >= 0 && guard < 64; guard++) { if (a == g_bc.idx[b]) { in = true; break; } a = g_parent[a]; }
				if (in) g_sub[b].push_back(i);
			}
		}
		return true;
	}

	// gira o osso b (e tudo abaixo dele) em torno do pivo (posicao do osso), eixo de MUNDO
	void RotateSub(RpHAnimHierarchy* h, int b, const V3& axis, float ang)
	{
		if (g_bc.idx[b] < 0 || std::fabs(ang) < 1e-5f) return;
		const V3 P = ToV(h->pMatrixArray[g_bc.idx[b]].pos);
		for (int n : g_sub[b])
		{
			RwMatrix& m = h->pMatrixArray[n];
			SetV(m.pos, P + Rot(ToV(m.pos) - P, axis, ang));
			SetV(m.right, Rot(ToV(m.right), axis, ang));
			SetV(m.up,    Rot(ToV(m.up),    axis, ang));
			SetV(m.at,    Rot(ToV(m.at),    axis, ang));
		}
	}
	void RotateBone3(RpHAnimHierarchy* h, int b, const V3& fwd, const V3& right, float roll, float pitch, float yaw)
	{
		RotateSub(h, b, fwd, roll); RotateSub(h, b, right, pitch); RotateSub(h, b, V3{ 0, 0, 1 }, yaw);
	}
	inline V3 BonePos(RpHAnimHierarchy* h, int b) { return ToV(h->pMatrixArray[g_bc.idx[b]].pos); }

	RwBool DoPose(RpHAnimHierarchy* h, CPedGTA* ped)
	{
		const PoseConfig& pc = SleekMove::PoseCfg();
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		const bool crouched = ped->bIsDucking;
		const bool doPosture = ((pc.lean || pc.pitch || pc.headTracking) && !aiming) || g_testDeg != 0.0f;
		const bool doFoot = pc.footIK && (!crouched || pc.footIKCrouched) && g_bc.idx[B_LTHIGH] >= 0 && g_bc.idx[B_LCALF] >= 0 && g_bc.idx[B_LFOOT] >= 0 &&
		                    g_bc.idx[B_RTHIGH] >= 0 && g_bc.idx[B_RCALF] >= 0 && g_bc.idx[B_RFOOT] >= 0 && g_bc.idx[B_PELVIS] >= 0;
		RwBool ret = g_orig(h);                       // pose do quadro
		if (!doPosture && !doFoot) return ret;

		// ---- estado: ~1x por quadro ----
		const auto now = Clock::now();
		const bool first = g_last.time_since_epoch().count() == 0;
		float since = first ? 1.0f : std::chrono::duration<float>(now - g_last).count();
		const bool gate = since >= 0.003f;
		if (g_epoch != SleekMove::TeleportEpoch()) { g_epoch = SleekMove::TeleportEpoch(); g_lean.Reset(); g_head.Reset(); g_ft[0] = g_ft[1] = FootState(); g_pelvisDz = 0; g_havePrev = false; since = 0.016f; }
		if (since > 0.25f) { g_lean.Reset(); g_head.Reset(); g_havePrev = false; since = 0.016f; }
		const float dt = Clamp(since, 0.0005f, 0.05f);

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
				g_accelF = Approach(g_accelF, Clamp((fs - g_prevFwdSpeed) / dt * 0.5f, -1.0f, 1.0f), 6.0f, dt);   // filtra o ruido da velocidade
				accel = g_accelF;
			}
			g_prevFwd = fwd; g_prevFwdSpeed = fs; g_havePrev = true;

			LeanIn li; li.active = doPosture && (pc.lean || pc.pitch);
			li.fwdSpeed = fs; li.rightSpeed = rs; li.angVel = angVel; li.accel = accel;
			li.inputRight = Clamp((float)(int16_t)LocalPlayerKeys.wKeyLR / 128.0f, -1.0f, 1.0f);
			g_leanOut = g_lean.Update(pc, li, dt);
			if (!pc.lean) g_leanOut.roll = 0;
			if (!pc.pitch) g_leanOut.pitch = 0;

			HeadIn hi; hi.active = doPosture && pc.headTracking;
			hi.moving = (std::fabs(fs) + std::fabs(rs)) > 0.1f;
			const CVector& cf = CCamera::GetActiveCamera().Front;
			const V3 cfH = Norm({ cf.x, cf.y, 0 }, fwd);
			hi.yawRel = std::atan2(Cross(fwd, cfH).z, Clamp(Dot(fwd, cfH), -1.0f, 1.0f));
			hi.pitch = std::asin(Clamp(cf.z, -1.0f, 1.0f));
			g_headOut = g_head.Update(pc, hi, dt);
		}

		// ---- postura: raiz -> folhas (cada giro ja carrega os ossos abaixo) ----
		if (doPosture)
		{
			const LeanOut& L = g_leanOut; const HeadOut& H = g_headOut;
			if (g_testDeg != 0.0f) { RotateBone3(h, B_SPINE, fwd, right, g_testDeg * kDeg2Rad * 0.5f, 0, 0); RotateBone3(h, B_SPINE1, fwd, right, g_testDeg * kDeg2Rad * 0.5f, 0, 0); }
			// pitch total do tronco limitado (acel/freio + olhar) para nao jogar tronco/bracos para tras
			const float pMax = pc.torsoPitchMax * kDeg2Rad;
			const float tp = Clamp(L.pitch + H.torsoPitch, -pMax, pMax);
			const float tr = L.roll, ty = H.torsoYaw;
			RotateBone3(h, B_SPINE,  fwd, right, tr * 0.4f, tp * 0.4f, ty * 0.5f);
			RotateBone3(h, B_SPINE1, fwd, right, tr * 0.6f, tp * 0.6f, ty * 0.5f);
			// bracos: a clavicula e filha da SPINE1, entao o braco (e a arma na mao) acompanhou o tronco; desfaz parte disso em torno do ombro
			if (pc.armCounter > 0.001f)
			{
				const float k = -pc.armCounter;   // roll/pitch somam 0.4+0.6 = 1.0; yaw 0.5+0.5 = 1.0
				for (int cb : { (int)B_RCLAV, (int)B_LCLAV }) RotateBone3(h, cb, fwd, right, tr * k, tp * k, ty * k);
			}
			RotateBone3(h, B_NECK,   fwd, right, 0.0f, H.neckPitch, H.neckYaw);
			RotateBone3(h, B_HEAD,   fwd, right, pc.headFollow ? -pc.hfLean * L.roll : 0.0f, H.headPitch + (pc.headFollow ? -pc.hfPitch * L.pitch : 0.0f), H.headYaw);
		}

		// ---- foot IK ----
		if (doFoot)
		{
			const int T[2] = { B_LTHIGH, B_RTHIGH }, K[2] = { B_LCALF, B_RCALF }, Ft[2] = { B_LFOOT, B_RFOOT };
			if (gate)
			{
				const float pedZ = ped->GetPosition().z;
				for (int sd = 0; sd < 2; sd++)
				{
					const V3 fp = BonePos(h, Ft[sd]);
					FootProbe pr;
					const CVector o(fp.x, fp.y, fp.z + pc.probeStart), t(fp.x, fp.y, fp.z - pc.probeDepth);
					CColPoint col; CEntityGTA* ent = nullptr;
					if (CWorld::ProcessLineOfSight(&o, &t, &col, &ent, true, true, false, true, false, false, false, false))
					{ pr.hit = true; pr.groundZ = col.m_vecPoint.z; pr.normal = { col.m_vecNormal.x, col.m_vecNormal.y, col.m_vecNormal.z }; }
					FootSmooth(pc, g_ft[sd], FootRaw(pc, pr, fp.z, pedZ), dt);
				}
				g_pelvisDz = Approach(g_pelvisDz, std::min(0.0f, std::min(g_ft[0].dz, g_ft[1].dz)), pc.posResp, dt);
			}
			// pelve (e tudo abaixo: pernas incluidas) desce para o pe mais baixo alcancar o chao
			if (std::fabs(g_pelvisDz) > 1e-4f)
				for (int n : g_sub[B_PELVIS]) h->pMatrixArray[n].pos.z += g_pelvisDz;
			for (int sd = 0; sd < 2; sd++)
			{
				const FootState& fs = g_ft[sd];
				const V3 Tp = BonePos(h, T[sd]), Kp = BonePos(h, K[sd]), Fp = BonePos(h, Ft[sd]);
				const V3 footWorldTarget = Fp + V3{ 0, 0, fs.dz - g_pelvisDz };      // o pe ja desceu junto com a pelve
				if (Len(footWorldTarget - Fp) > 1e-3f)
				{
					const V3 Kn = TwoBoneKnee(Tp, Kp, Fp, footWorldTarget);
					V3 ax1; float an1; Arc(Kp - Tp, Kn - Tp, ax1, an1);
					RotateSub(h, T[sd], ax1, LimitAngle(an1, pc.maxHipDeg));               // coxa (carrega canela e pe)
					const V3 K2 = BonePos(h, K[sd]), F2 = BonePos(h, Ft[sd]);
					V3 ax2; float an2; Arc(F2 - K2, footWorldTarget - K2, ax2, an2);
					RotateSub(h, K[sd], ax2, LimitAngle(an2, pc.maxKneeDeg));              // canela (carrega o pe)
				}
				if (fs.weight > 0.05f)
				{
					V3 axn; float ann; Arc({ 0, 0, 1 }, Norm({ fs.nx, fs.ny, fs.nz }), axn, ann);
					RotateSub(h, Ft[sd], axn, LimitAngle(ann, pc.maxFootDeg) * fs.weight * pc.influence);
				}
			}
		}
		g_cApplied++;
		if (g_dbgCount < 3 && gate) { g_dbgCount++; FLog("[SLEEK-POSE] ok: lean roll=%.2f pitch=%.2f graus; cabeca yaw=%.2f; pelve dz=%.3f", g_leanOut.roll / kDeg2Rad, g_leanOut.pitch / kDeg2Rad, g_headOut.headYaw / kDeg2Rad, g_pelvisDz); }
		return ret;
	}

	RwBool Upd_hook(RpHAnimHierarchy* h)
	{
		g_cCalls++;
		if (g_in || !h) return g_orig(h);
		CPedGTA* ped = SleekMove::PoseEligiblePed();
		if (!ped || !ped->m_pRwClump) { g_cNoPed++; return g_orig(h); }
		RpHAnimHierarchy* mine = GetAnimHierarchyFromSkinClump(ped->m_pRwClump);
		if (mine != h) { g_cOther++; return g_orig(h); }
		g_cMine++;
		if (g_bc.h != h && !BuildCache(h)) { g_cCacheFail++; return g_orig(h); }
		if (!g_bc.usable) { g_cCacheFail++; return g_orig(h); }
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
		// OPT-IN: sem o arquivo vazio SAMP/sleek_pose.on o hook NAO e instalado.
		{
			const std::string mk = g_pszStorage ? std::string(g_pszStorage) + "SAMP/sleek_pose.on" : std::string();
			FILE* f = mk.empty() ? nullptr : fopen(mk.c_str(), "rb");
			if (!f) { FLog("[SLEEK-POSE] desativado (crie SAMP/sleek_pose.on para instalar o hook de postura/IK)"); return; }
			fclose(f);
		}
		// libGTASA.so e ARM64: a funcao esta exportada com o nome C++ (conferido: 0x255C20). NUNCA usar o overload InlineHook(lib, addr)
		// (soma +1 = bit thumb do ARM32 -> hook em endereco desalinhado -> crash no CPlayerPed::CPlayerPed) nem InlineHook("nome") (exit(0) se faltar).
		const uintptr_t sym = CHook::getSymSoft("_Z30RpHAnimHierarchyUpdateMatricesP16RpHAnimHierarchy");
		void* target = sym ? (void*)sym : (void*)(g_libGTASA + 0x255C20);
		CHook::InlineHook(target, &Upd_hook, &g_orig);
		FLog("[SLEEK-POSE] hook RpHAnimHierarchyUpdateMatrices @%p via %s: %s", target, sym ? "simbolo" : "endereco 0x255C20", g_orig ? "instalado" : "FALHOU - postura/IK inativos");
	}
	void SetPoseTest(float deg) { g_testDeg = deg; }
	std::string PoseStatus()
	{
		char b[300]; snprintf(b, sizeof(b), "hook=%s chamadas=%u semPed/inelegivel=%u outros=%u meu=%u cacheFalhou=%u aplicado=%u esqueleto=%s",
			g_orig ? "ok" : "AUSENTE", g_cCalls, g_cNoPed, g_cOther, g_cMine, g_cCacheFail, g_cApplied, g_bc.usable ? "ok" : "n/d");
		return b;
	}
}
