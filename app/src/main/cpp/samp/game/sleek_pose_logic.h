#pragma once
// =====================================================================================
//  SleekMovement - postura do corpo (lean/pitch, head tracking, head follow), foot IK e camera de ombro.
//  LOGICA PURA (sem GTA): testada no PC em tools/sleek_test.cpp. Convencao de mundo do GTA: x direita, y frente, z cima.
//    +yaw   = gira para a ESQUERDA (anti-horario visto de cima)
//    +pitch = inclina para TRAS / olha para CIMA (giro em torno do eixo direita)
//    +roll  = inclina para a DIREITA (giro em torno do eixo frente)
// =====================================================================================
#include "sleek_logic.h"

namespace Sleek
{
	// --------------------------------------------------------------------------- config
	struct PoseConfig
	{
		bool lean = true, pitch = true, headTracking = true, headFollow = true, footIK = true, footIKCrouched = true;
		// [Lean]
		float maxLean = 10.0f, leanIn = 5.5f, leanOut = 5.0f, inputContribution = 0.30f, turnContribution = 0.40f, angVelContribution = 1.00f;
		float fullTurnAngle = 42.0f, fullTiltTurnSpeed = 120.0f;
		float leanSign = 1.0f;               // extra (cliente): -1 inverte o lado da inclinacao se ficar ao contrario no aparelho
		// [Pitch]
		float envResp = 3.0f, pitchResp = 7.0f, maxAccelPitch = 10.0f, maxBrakePitch = 7.0f;
		float pitchSign = 1.0f;              // extra: -1 inverte frente/tras
		// [HeadTracking]
		float lookDistance = 15.0f, movingScale = 0.7f, maxYaw = 75.0f, maxPitchUp = 30.0f, maxPitchDown = 25.0f;
		float fadeStartYaw = 85.0f, behindYaw = 110.0f;
		float headResp = 9.0f, neckResp = 6.0f, torsoResp = 3.5f, engageResp = 3.0f, disengageResp = 5.0f, maxAngSpeed = 300.0f;
		float torsoYawShare = 0.20f, neckYawShare = 0.30f, torsoPitchShare = 0.10f, neckPitchShare = 0.40f;
		// [HeadFollow]
		float hfLean = 0.35f, hfPitch = 0.30f;
		// [FootIK]
		float probeStart = 0.50f, probeDepth = 0.70f, soleOffset = 0.025f, activation = 0.50f, maxFootOffset = 0.50f;
		float maxStepUp = 0.45f, maxStepDown = 0.28f, deadZone = 0.006f, contactHeight = 0.11f;
		float plantResp = 22.0f, releaseResp = 16.0f, posResp = 18.0f, normalResp = 10.0f, influence = 1.0f;
		float maxHipDeg = 35.0f, maxKneeDeg = 90.0f, maxFootDeg = 28.0f, minNormalZ = 0.68f, fullNormalZ = 0.82f;
		float pedGroundOffset = 1.0f;        // extra: altura do centro do ped sobre o chao (padrao do GTA = 1.0)
	};

	// --------------------------------------------------------------------------- vetores
	inline V3 operator+(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 operator-(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 operator*(const V3& a, float k) { return { a.x * k, a.y * k, a.z * k }; }
	inline float Len(const V3& a) { return std::sqrt(Dot(a, a)); }
	inline V3 Norm(const V3& a, const V3& fallback = { 0, 0, 1 }) { const float l = Len(a); return l > 1e-6f ? a * (1.0f / l) : fallback; }
	inline float WrapPi(float a) { while (a > kPi) a -= 2.0f * kPi; while (a < -kPi) a += 2.0f * kPi; return a; }

	// base de um osso-pai no MUNDO (linhas right/up/at da matriz do pai)
	struct Basis { V3 r{ 1, 0, 0 }, u{ 0, 1, 0 }, a{ 0, 0, 1 }; };

	// Gira os eixos LOCAIS (right/up/at do modelling do osso, em espaco do pai) por uma rotacao do MUNDO em torno do pivo do osso.
	inline void RotateLocalByWorld(V3& cr, V3& cu, V3& ca, const Basis& P, const V3& axis, float ang)
	{
		auto one = [&](V3& c)
		{
			const V3 w = P.r * c.x + P.u * c.y + P.a * c.z;       // local -> mundo (convencao de linhas do RenderWare)
			const V3 w2 = Rot(w, axis, ang);
			c = { Dot(w2, P.r), Dot(w2, P.u), Dot(w2, P.a) };      // mundo -> local
		};
		one(cr); one(cu); one(ca);
	}
	// desloca um ponto do osso (pos local) por um vetor do MUNDO
	inline V3 WorldDeltaToLocal(const Basis& P, const V3& d) { return { Dot(d, P.r), Dot(d, P.u), Dot(d, P.a) }; }

	// arco mais curto from->to (eixo unitario + angulo)
	inline void Arc(const V3& from, const V3& to, V3& axis, float& ang)
	{
		const V3 f = Norm(from), t = Norm(to);
		axis = Cross(f, t);
		const float s = Len(axis), c = Clamp(Dot(f, t), -1.0f, 1.0f);
		if (s < 1e-6f) { axis = { 0, 0, 1 }; ang = 0.0f; return; }
		axis = axis * (1.0f / s);
		ang = std::atan2(s, c);
	}

	// --------------------------------------------------------------------------- lean / pitch do corpo
	struct LeanIn
	{
		bool active = false;
		float fwdSpeed = 0, rightSpeed = 0;   // velocidade local normalizada (-1..1 ~ corrida)
		float angVel = 0;                      // rad/s (+ = esquerda)
		float inputRight = 0;                  // stick lateral -1..1
		float accel = 0;                       // variacao da velocidade para frente (1/s, normalizada)
	};
	struct LeanOut { float roll = 0, pitch = 0; };   // radianos (roll + = direita; pitch + = tras)
	struct Lean
	{
		float roll = 0, pitch = 0, env = 0;
		LeanOut Update(const PoseConfig& c, const LeanIn& in, float dt)
		{
			float tRoll = 0, tPitch = 0;
			if (in.active)
			{
				const float speedEnv = Sat(std::fabs(in.fwdSpeed) + std::fabs(in.rightSpeed));
				env = Approach(env, speedEnv, c.envResp, dt);
				const float turn = Clamp(-in.angVel / (c.fullTiltTurnSpeed * kDeg2Rad), -1.0f, 1.0f);   // virar a esquerda => inclina a esquerda
				const float raw = c.inputContribution * in.inputRight + (c.turnContribution + c.angVelContribution * 0.5f) * turn;
				tRoll = Clamp(raw, -1.0f, 1.0f) * env * c.maxLean * kDeg2Rad * c.leanSign;
				const float a = Clamp(in.accel, -1.0f, 1.0f);
				tPitch = (a >= 0 ? -a * c.maxAccelPitch : -a * c.maxBrakePitch) * kDeg2Rad * c.pitchSign;    // acelera: inclina para frente (pitch -)
			}
			else env = Approach(env, 0, c.envResp, dt);
			roll  = Approach(roll,  tRoll,  std::fabs(tRoll)  > std::fabs(roll)  ? c.leanIn : c.leanOut, dt);
			pitch = Approach(pitch, tPitch, c.pitchResp, dt);
			return { roll, pitch };
		}
		void Reset() { roll = pitch = env = 0; }
	};

	// --------------------------------------------------------------------------- head tracking
	struct HeadIn { bool active = false; bool moving = false; float yawRel = 0; float pitch = 0; };   // yawRel (rad, + esquerda) = camera em relacao ao corpo
	struct HeadOut { float torsoYaw = 0, neckYaw = 0, headYaw = 0, torsoPitch = 0, neckPitch = 0, headPitch = 0; };   // rad
	struct HeadTrack
	{
		float weight = 0, yaw = 0, pitch = 0, tYaw = 0, nYaw = 0, hYaw = 0, tPit = 0, nPit = 0, hPit = 0;
		static float Slew(float cur, float target, float resp, float maxSpeed, float dt)
		{
			const float want = Approach(cur, target, resp, dt);
			const float lim = maxSpeed * dt;
			return cur + Clamp(want - cur, -lim, lim);
		}
		HeadOut Update(const PoseConfig& c, const HeadIn& in, float dt)
		{
			const float ay = std::fabs(in.yawRel / kDeg2Rad);
			// olhar para tras: some o efeito (nao torce o pescoco)
			const float behindFade = 1.0f - SmoothStep(c.fadeStartYaw, c.behindYaw, ay);
			const float tw = (in.active ? 1.0f : 0.0f) * behindFade;
			weight = Approach(weight, tw, tw > weight ? c.engageResp : c.disengageResp, dt);
			const float sc = in.moving ? c.movingScale : 1.0f;
			const float ty = Clamp(in.yawRel, -c.maxYaw * kDeg2Rad, c.maxYaw * kDeg2Rad) * sc * weight;
			const float tp = Clamp(in.pitch, -c.maxPitchDown * kDeg2Rad, c.maxPitchUp * kDeg2Rad) * sc * weight;
			const float ms = c.maxAngSpeed * kDeg2Rad;
			tYaw = Slew(tYaw, ty * c.torsoYawShare, c.torsoResp, ms, dt);
			nYaw = Slew(nYaw, ty * c.neckYawShare, c.neckResp, ms, dt);
			hYaw = Slew(hYaw, ty * (1.0f - c.torsoYawShare - c.neckYawShare), c.headResp, ms, dt);
			tPit = Slew(tPit, tp * c.torsoPitchShare, c.torsoResp, ms, dt);
			nPit = Slew(nPit, tp * c.neckPitchShare, c.neckResp, ms, dt);
			hPit = Slew(hPit, tp * (1.0f - c.torsoPitchShare - c.neckPitchShare), c.headResp, ms, dt);
			return { tYaw, nYaw, hYaw, tPit, nPit, hPit };
		}
		void Reset() { *this = HeadTrack(); }
	};

	// --------------------------------------------------------------------------- foot IK
	// IK de 2 ossos: devolve o novo joelho para o pe atingir 'target'. 'pole' = joelho atual (mantem o plano da dobra).
	inline V3 TwoBoneKnee(const V3& T, const V3& K, const V3& F, const V3& target)
	{
		const float l1 = Len(K - T), l2 = Len(F - K);
		V3 axis = target - T; float d = Len(axis);
		axis = Norm(axis, { 0, 0, -1 });
		d = Clamp(d, std::fabs(l1 - l2) + 1e-3f, l1 + l2 - 1e-3f);
		const float x = (l1 * l1 - l2 * l2 + d * d) / (2.0f * d);
		const float h = std::sqrt(std::max(0.0f, l1 * l1 - x * x));
		V3 pole = (K - T) - axis * Dot(K - T, axis);
		pole = Norm(pole, { 0, 1, 0 });
		return T + axis * x + pole * h;
	}
	inline float LimitAngle(float a, float maxDeg) { const float m = maxDeg * kDeg2Rad; return Clamp(a, -m, m); }

	struct FootState
	{
		float dz = 0;       // correcao vertical suavizada (m)
		float weight = 0;   // 0..1 pe plantado
		float nx = 0, ny = 0, nz = 1;   // normal suavizada
	};
	struct FootProbe { bool hit = false; float groundZ = 0; V3 normal{ 0, 0, 1 }; };
	struct FootTarget { float dz = 0, w = 0; V3 n{ 0, 0, 1 }; };

	// alvo bruto de um pe (antes da suavizacao). footZ = altura do tornozelo animado; pedZ = centro do ped.
	inline FootTarget FootRaw(const PoseConfig& c, const FootProbe& pr, float footZ, float pedZ)
	{
		FootTarget t;
		if (!pr.hit || pr.normal.z < c.minNormalZ) return t;
		const float assumed = pedZ - c.pedGroundOffset;                  // chao que a animacao assume
		const float hA = footZ - assumed;                                 // altura do tornozelo sobre esse chao
		const float w = 1.0f - SmoothStep(c.contactHeight, c.contactHeight + 0.15f, hA);   // so corrige pe plantado
		float dz = (pr.groundZ - assumed) * w;
		dz = Clamp(dz, -c.maxStepDown, c.maxStepUp);
		if (std::fabs(dz) < c.deadZone) dz = 0.0f;
		t.dz = Clamp(dz, -c.maxFootOffset, c.maxFootOffset) * c.influence;
		t.w = w * SmoothStep(c.minNormalZ, c.fullNormalZ, pr.normal.z);
		t.n = pr.normal;
		return t;
	}
	inline void FootSmooth(const PoseConfig& c, FootState& s, const FootTarget& t, float dt)
	{
		s.weight = Approach(s.weight, t.w, t.w > s.weight ? c.plantResp : c.releaseResp, dt);
		s.dz = Approach(s.dz, t.dz, c.posResp, dt);
		s.nx = Approach(s.nx, t.n.x, c.normalResp, dt); s.ny = Approach(s.ny, t.n.y, c.normalResp, dt); s.nz = Approach(s.nz, t.n.z, c.normalResp, dt);
	}

	// --------------------------------------------------------------------------- camera de ombro
	struct CamConfig
	{
		bool enabled = true;
		float offsetX = 0.45f, offsetY = 0.15f, swapResp = 8.0f, anchorHeight = 0.26f;
		float zoomNear = 1.50f, zoomMid = 2.30f, zoomFar = 3.40f, zoomResp = 8.0f;
		float lateralResp = 31.5f, depthResp = 6.0f, approachResp = 14.0f, verticalResp = 14.0f;
		float maxLatLag = 0.22f, maxApproachLag = 0.12f, maxRecedeLag = 0.45f, maxVertLag = 0.35f;
		float transResp = 5.5f, exitResp = 9.0f;
		bool collision = true; float colRadius = 0.18f, colPadding = 0.12f, colRecovery = 6.0f;
		float teleportDist = 8.0f;
	};
	struct CamState
	{
		float side = 1.0f;          // +1 ombro direito, -1 esquerdo (valor alvo)
		float sideS = 1.0f;         // suavizado
		int   zoomLevel = 1;        // 0 perto, 1 medio, 2 longe
		float dist = 2.3f;          // distancia suavizada
		float blend = 0.0f;         // 0..1 (entrada/saida do modo)
		float colDist = 99.0f;      // limite por colisao (suavizado)
		bool  haveAnchor = false; V3 anchor;   // ancora suavizada (segue o ped com atraso)
		float ZoomTarget(const CamConfig& c) const { return zoomLevel == 0 ? c.zoomNear : (zoomLevel == 2 ? c.zoomFar : c.zoomMid); }
	};
	// segue 'target' no referencial da camera (right/fwd/up) com respostas e atrasos maximos por eixo
	inline V3 FollowAnchor(const CamConfig& c, CamState& s, const V3& target, const V3& right, const V3& fwd, float dt)
	{
		if (!s.haveAnchor) { s.anchor = target; s.haveAnchor = true; return target; }
		V3 d = target - s.anchor;
		float dl = Dot(d, right), df = Dot(d, fwd), dv = d.z;
		const V3 horizFwd = Norm({ fwd.x, fwd.y, 0 }, { 0, 1, 0 });
		df = Dot(d, horizFwd);
		// o personagem se afasta (df>0 = anda para longe da camera): atraso de "approach"; se aproxima: "recede"
		const float kl = 1.0f - std::exp(-c.lateralResp * dt), kf = 1.0f - std::exp(-(df >= 0 ? c.approachResp : c.depthResp) * dt), kv = 1.0f - std::exp(-c.verticalResp * dt);
		float nl = dl * (1 - kl), nf = df * (1 - kf), nv = dv * (1 - kv);                // resto do atraso depois do passo
		nl = Clamp(nl, -c.maxLatLag, c.maxLatLag);
		nf = Clamp(nf, -c.maxRecedeLag, c.maxApproachLag);
		nv = Clamp(nv, -c.maxVertLag, c.maxVertLag);
		const V3 horizRight = Norm({ right.x, right.y, 0 }, { 1, 0, 0 });
		s.anchor = target - horizRight * nl - horizFwd * nf - V3{ 0, 0, nv };
		return s.anchor;
	}
}
