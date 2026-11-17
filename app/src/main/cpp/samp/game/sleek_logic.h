#pragma once
// =====================================================================================
//  SleekMovement (recriacao em C++ para o cliente Android) - LOGICA PURA
//  Sem nenhuma dependencia do GTA/SA-MP: compila e e testada no PC (tools/sleek_test.cpp).
//  Valores padrao = SleekMovement.ini do mod original (PC, x86).
//  Modulos aqui: FOV de corrida/sprint/pitch, tremor "de mao" (idle), tremor de impacto
//  reescrito (suave, rotacional), inercia de movimento (momentum).
// =====================================================================================
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Sleek
{
	constexpr float kPi = 3.14159265358979f;
	constexpr float kDeg2Rad = kPi / 180.0f;

	inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline float Sat(float v) { return Clamp(v, 0.0f, 1.0f); }
	inline float SmoothStep(float a, float b, float x)
	{
		if (b <= a) return x >= b ? 1.0f : 0.0f;
		float t = Sat((x - a) / (b - a));
		return t * t * (3.0f - 2.0f * t);
	}
	// aproximacao exponencial independente de FPS: cur -> target com "response" (1/s)
	inline float Approach(float cur, float target, float response, float dt)
	{
		if (response <= 0.0f) return target;
		return cur + (target - cur) * (1.0f - std::exp(-response * dt));
	}

	// --------------------------------------------------------------------------- config
	struct Config
	{
		// [Features]
		bool modernCamera = true;            // (nao portado: camera de ombro - ver README)
		bool shakeRewrite = true;
		bool idleShake    = true;
		bool runFov       = true;
		bool pitchFov     = true;
		bool momentum     = true;
		bool crouchLoco   = true;

		// [CameraShake]
		float shakeIntensity = 1.25f, shakeAmplitude = 1.1f, shakeSpeed = 17.0f;
		float shakeRotDeg = 4.0f, shakeRotSpeed = 9.0f;
		float shakeGain = 1.0f;              // extra (cliente): escala a "forca" do motor
		float shakeDuration = 5.0f;          // extra (cliente): segundos ate zerar a forca do motor

		// [CameraIdleShake]
		float idleIntensity = 3.0f, idleSpeed = 0.30f;
		float idlePitchDeg = 0.40f, idleYawDeg = 0.50f, idleRollDeg = 0.25f;
		float idleSprintScale = 1.5f;

		// [CameraFov]
		float runInc = 15.0f, sprintInc = 30.0f, riseResp = 3.0f, fallResp = 3.2f;
		float lookUpInc = 15.0f, lookUpStart = 5.0f, lookUpFull = 45.0f, lookDownInc = 0.0f;
		float pitchResp = 4.5f, maxTotal = 45.0f, exitResp = 12.0f;
		float fovScale = 1.0f;               // extra (cliente): 1.0 = valores do INI; 0.5 = metade

		// [Momentum]
		float deadZone = 0.08f, stopEps = 0.015f;
		float walkAcc = 1.3f, runAcc = 1.6f, sprintAcc = 1.5f;
		float braking = 4.1f, reversalBraking = 3.2f, reversalAngle = 115.0f, reversalSwitch = 0.12f;
		float lowTurn = 540.0f, highTurn = 210.0f, highTurnStart = 0.60f;
		bool  momentumWhileAiming = true, momentumWhileCrouched = true;
		float lateralGrip = 0.72f;           // 1 = sem perda ao curvar; menor = perde velocidade ao mudar de direcao
		float blockedDecay = 10.0f;          // 1/s: velocidade interna some rapido se o personagem esta bloqueado (parede)
		float teleportDist = 3.0f;           // metros num quadro => zera a inercia

		// [CrouchLocomotion]
		bool keepCrouchedWithSprint = true;
	};

	// --------------------------------------------------------------------------- FOV
	struct FovIn
	{
		bool  active = false;   // a pe, local, camera normal
		float stick = 0.0f;     // 0..1 (modulo do joystick)
		bool  sprint = false;
		float pitchDeg = 0.0f;  // + = olhando para cima
	};
	struct FovCtl
	{
		float speedFov = 0.0f, pitchFov = 0.0f;
		// devolve o delta de FOV (graus) a somar ao FOV do jogo
		float Update(const Config& c, const FovIn& in, float dt)
		{
			float tSpeed = 0.0f, tPitch = 0.0f;
			if (in.active)
			{
				if (c.runFov)
				{
					if (in.sprint && in.stick > 0.2f) tSpeed = c.sprintInc;
					else tSpeed = c.runInc * SmoothStep(0.30f, 0.90f, in.stick);   // andar devagar nao abre o FOV
				}
				if (c.pitchFov)
				{
					tPitch = c.lookUpInc * SmoothStep(c.lookUpStart, c.lookUpFull, in.pitchDeg)
					       + c.lookDownInc * SmoothStep(c.lookUpStart, c.lookUpFull, -in.pitchDeg);
				}
			}
			const float rS = !in.active ? c.exitResp : (tSpeed > speedFov ? c.riseResp : c.fallResp);
			const float rP = !in.active ? c.exitResp : c.pitchResp;
			speedFov = Approach(speedFov, tSpeed, rS, dt);
			pitchFov = Approach(pitchFov, tPitch, rP, dt);
			float tot = (speedFov + pitchFov) * c.fovScale;
			return Clamp(tot, -c.maxTotal, c.maxTotal);
		}
		void Reset() { speedFov = pitchFov = 0.0f; }
	};

	// --------------------------------------------------------------------------- rotacao de matriz de camera
	struct V3 { float x = 0, y = 0, z = 0; };
	inline V3 Cross(const V3& a, const V3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3 Rot(const V3& v, const V3& axis, float ang)   // Rodrigues (axis unitario)
	{
		const float c = std::cos(ang), s = std::sin(ang);
		const V3 k = Cross(axis, v);
		const float d = Dot(axis, v) * (1.0f - c);
		return { v.x * c + k.x * s + axis.x * d, v.y * c + k.y * s + axis.y * d, v.z * c + k.z * s + axis.z * d };
	}
	// Gira a base da camera (right, forward, up) nos eixos LOCAIS: yaw (em torno de up), pitch (em torno de right), roll (em torno de forward). Radianos.
	inline void RotateBasis(V3& right, V3& fwd, V3& up, float yaw, float pitch, float roll)
	{
		V3 r = right, f = fwd, u = up;
		f = Rot(f, u, yaw);   r = Rot(r, u, yaw);
		f = Rot(f, r, pitch); u = Rot(u, r, pitch);
		r = Rot(r, f, roll);  u = Rot(u, f, roll);
		right = r; fwd = f; up = u;
	}

	// --------------------------------------------------------------------------- tremor ocioso ("camera na mao")
	struct IdleOut { float yaw = 0, pitch = 0, roll = 0; };   // radianos
	struct IdleShake
	{
		float t = 0.0f, weight = 0.0f;
		IdleOut Update(const Config& c, bool active, bool sprint, float dt)
		{
			t += dt;
			weight = Approach(weight, (active && c.idleShake) ? 1.0f : 0.0f, active ? 3.0f : 8.0f, dt);
			IdleOut o;
			if (weight < 0.001f) return o;
			const float k = c.idleIntensity * weight * (sprint ? c.idleSprintScale : 1.0f);
			const float w = 2.0f * kPi * c.idleSpeed;
			// somas de senos incomensuraveis: movimento organico, sem repeticao perceptivel
			auto n = [&](float f, float p) { return 0.62f * std::sin(w * f * t + p) + 0.38f * std::sin(w * f * 2.31f * t + p * 1.7f + 1.3f); };
			o.pitch = c.idlePitchDeg * k * n(1.00f, 0.0f) * kDeg2Rad;
			o.yaw   = c.idleYawDeg   * k * n(0.83f, 2.1f) * kDeg2Rad;
			o.roll  = c.idleRollDeg  * k * n(1.17f, 4.2f) * kDeg2Rad;
			return o;
		}
	};

	// --------------------------------------------------------------------------- tremor de impacto reescrito
	// O motor guarda m_fCamShakeForce + m_uiCamShakeStart (ms). A forca decai linearmente em 'shakeDuration' s.
	inline float ShakeStrength(const Config& c, float engineForce, uint32_t startMs, uint32_t nowMs)
	{
		if (engineForce <= 0.0f || nowMs < startMs) return 0.0f;
		const float el = (float)(nowMs - startMs) * 0.001f;
		const float f = 1.0f - el / std::max(0.1f, c.shakeDuration);
		return f > 0.0f ? Clamp(engineForce * c.shakeGain, 0.0f, 3.0f) * f : 0.0f;
	}
	struct ShakeOut { float yaw = 0, pitch = 0, roll = 0; float offX = 0, offY = 0, offZ = 0; };  // rad / metros (locais)
	struct ImpactShake
	{
		float t = 0.0f, smooth = 0.0f;
		ShakeOut Update(const Config& c, float strength, float dt)
		{
			t += dt;
			smooth = Approach(smooth, strength, strength > smooth ? 30.0f : 6.0f, dt);   // ataque rapido, saida suave
			ShakeOut o;
			if (smooth < 0.002f) return o;
			const float a = smooth * c.shakeIntensity;
			const float p = 2.0f * kPi;
			const float rs = c.shakeRotSpeed, ps = c.shakeSpeed;
			o.roll  = c.shakeRotDeg * a * 0.5f * std::sin(p * rs * 0.11f * t) * kDeg2Rad;
			o.pitch = c.shakeRotDeg * a * 0.35f * std::sin(p * rs * 0.13f * t + 1.1f) * kDeg2Rad;
			o.yaw   = c.shakeRotDeg * a * 0.35f * std::sin(p * rs * 0.09f * t + 2.3f) * kDeg2Rad;
			const float amp = 0.012f * c.shakeAmplitude * a;       // ~1,2 cm por unidade de forca
			o.offX = amp * std::sin(p * ps * 0.10f * t);
			o.offZ = amp * std::sin(p * ps * 0.12f * t + 0.7f);
			return o;
		}
	};

	// --------------------------------------------------------------------------- inercia (momentum)
	// Trabalha no espaco do STICK (x = direita, y = frente), modulo 0..1. O resultado volta ao jogo como stick (+-128).
	struct Momentum
	{
		float vx = 0.0f, vy = 0.0f;     // velocidade "suavizada"
		bool  reversing = false;

		void Reset() { vx = vy = 0.0f; reversing = false; }
		// BlockedSpeedDecay: chamado quando o personagem esta encostado em algo (o motor nao anda mas o stick insiste)
		void DecayBlocked(const Config& c, float dt)
		{
			const float k = std::exp(-c.blockedDecay * dt);
			vx *= k; vy *= k;
			if (Speed() < c.stopEps) Reset();
		}
		float Speed() const { return std::sqrt(vx * vx + vy * vy); }

		void Update(const Config& c, float ix, float iy, bool sprint, float dt)
		{
			float im = std::sqrt(ix * ix + iy * iy);
			if (im > 1.0f) { ix /= im; iy /= im; im = 1.0f; }
			const bool hasIn = im > c.deadZone;
			float sp = Speed();

			if (!hasIn)
			{
				reversing = false;
				sp = std::max(0.0f, sp - c.braking * dt);
				scaleTo(sp);
				if (sp < c.stopEps) Reset();
				return;
			}

			const float accel = sprint ? c.sprintAcc : (im > 0.6f ? c.runAcc : c.walkAcc);
			float dirx = ix / im, diry = iy / im;

			if (sp > c.stopEps)
			{
				const float cx = vx / sp, cy = vy / sp;
				const float ang = std::acos(Clamp(cx * dirx + cy * diry, -1.0f, 1.0f)) / kDeg2Rad;
				if (ang > c.reversalAngle && sp > c.reversalSwitch) reversing = true;
				if (reversing)
				{
					// inversao brusca: freia primeiro, so redireciona abaixo da velocidade de troca
					sp = std::max(0.0f, sp - c.reversalBraking * dt);
					scaleTo(sp);
					if (sp <= c.reversalSwitch) { reversing = false; vx = dirx * sp; vy = diry * sp; }
					return;
				}
				// giro limitado: rapido devagar, lento em alta velocidade
				const float rate = (c.lowTurn + (c.highTurn - c.lowTurn) * SmoothStep(c.highTurnStart * 0.5f, 1.0f, sp)) * kDeg2Rad;
				const float cur = std::atan2(cy, cx), want = std::atan2(diry, dirx);
				float d = want - cur;
				while (d > kPi) d -= 2.0f * kPi;
				while (d < -kPi) d += 2.0f * kPi;
				const float step = Clamp(d, -rate * dt, rate * dt);
				const float na = cur + step;
				const float tgt = im;
				float nsp = sp < tgt ? std::min(tgt, sp + accel * dt) : std::max(tgt, sp - c.braking * dt);
				// LateralGrip: curvar perde velocidade proporcional ao giro (grip 1 = nada; 0,72 = perde um pouco por curva)
				nsp *= 1.0f - (1.0f - Clamp(c.lateralGrip, 0.0f, 1.0f)) * std::fabs(std::sin(step));
				vx = std::cos(na) * nsp; vy = std::sin(na) * nsp;
			}
			else
			{
				// parado: arranca na direcao do input
				const float nsp = std::min(im, accel * dt + c.stopEps);
				vx = dirx * nsp; vy = diry * nsp;
			}
		}
	private:
		void scaleTo(float sp)
		{
			const float cur = Speed();
			if (cur < 1e-6f) { vx = vy = 0.0f; return; }
			const float k = sp / cur; vx *= k; vy *= k;
		}
	};
}
