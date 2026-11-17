#include "../main.h"
#include "game.h"
#include "util.h"
#include "sleek.h"
#include "sleek_logic.h"
#include "camstick.h"
#include "pad.h"
#include "Camera.h"
#include "Entity/CPedGTA.h"
#include "Enums/ePedState.h"
#include "Timer.h"
#include "../vendor/inih/cpp/INIReader.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cctype>
#include <vector>

extern PAD_KEYS LocalPlayerKeys;

namespace
{
	Sleek::Config g_cfg;
	bool          g_loaded = false;
	bool          g_alsoCamVectors = false;   // [Client] AlsoRotateCamVectors

	Sleek::FovCtl      g_fov;
	Sleek::IdleShake   g_idle;
	Sleek::ImpactShake g_impact;
	Sleek::Momentum    g_mom;
	float g_fovDelta = 0.0f;

	using Clock = std::chrono::steady_clock;
	Clock::time_point g_lastCam{};
	Clock::time_point g_lastMom{};
	int16_t g_momLR = 0, g_momUD = 0;

	std::string IniPath() { return g_pszStorage ? std::string(g_pszStorage) + "SAMP/sleek.ini" : std::string(); }

	const char* kDefaultIni =
		"; Sleek (recriacao do SleekMovement) - mesmos nomes do .ini original. Edite e use /sleek reload.\n"
		"[Features]\nEnableCameraShakeRewrite=1\nEnableCameraIdleShake=1\nEnableCameraRunFov=1\nEnableCameraPitchFov=1\nEnableMomentum=1\nEnableCrouchLocomotion=1\n"
		"[CameraShake]\nIntensity=1.25\nAmplitude=1.1\nSpeed=17.0\nRotationDegrees=4.0\nRotationSpeed=9.0\n"
		"[CameraIdleShake]\nIntensity=3.0\nSpeed=0.30\nPitchDegrees=0.40\nYawDegrees=0.50\nRollDegrees=0.25\nSprintScale=1.5\n"
		"[CameraFov]\nRunIncrease=15.0\nSprintIncrease=30.0\nRiseResponse=3.0\nFallResponse=3.2\nLookUpIncrease=15.0\nLookUpStartPitch=5.0\nLookUpFullPitch=45.0\nLookDownIncrease=0.0\nPitchResponse=4.5\nMaxTotalChange=45.0\nExitResponse=12.0\n"
		"[Momentum]\nInputDeadZone=0.08\nStopEpsilon=0.015\nWalkAcceleration=1.3\nRunAcceleration=1.6\nSprintAcceleration=1.5\nBraking=4.1\nReversalBraking=3.2\nReversalAngleDegrees=115.0\nReversalSwitchSpeed=0.12\n"
		"LowSpeedTurnRateDegreesPerSecond=540.0\nHighSpeedTurnRateDegreesPerSecond=210.0\nHighSpeedTurnStart=0.60\nEnableWhileAiming=1\nEnableWhileCrouched=1\n"
		"[CrouchLocomotion]\nKeepCrouchedWithSprint=1\n"
		"[Client]\n; 1.0 = valores acima; 0.5 = metade do efeito de FOV\nFovScale=1.0\n; forca do tremor de impacto do motor\nShakeGain=1.0\nShakeDurationSeconds=5.0\n; 1 = tambem gira Front/Up da camera ativa (mira acompanha o tremor)\nAlsoRotateCamVectors=0\n";

	void Load()
	{
		g_loaded = true;
		g_cfg = Sleek::Config();
		const std::string p = IniPath();
		if (p.empty()) return;
		{   // cria o arquivo so se nao existir
			std::ifstream t(p, std::ios::binary);
			if (!t.good()) { std::ofstream o(p, std::ios::binary); if (o.good()) o << kDefaultIni; }
		}
		INIReader r(p);
		if (r.ParseError() < 0) { FLog("[SLEEK] sem leitura de %s - usando padroes", p.c_str()); return; }
		auto F = [&](const char* s, const char* k, float d) { return (float)r.GetReal(s, k, d); };
		auto B = [&](const char* s, const char* k, bool d) { return r.GetBoolean(s, k, d); };
		Sleek::Config& c = g_cfg;
		c.shakeRewrite = B("Features", "EnableCameraShakeRewrite", c.shakeRewrite);
		c.idleShake    = B("Features", "EnableCameraIdleShake", c.idleShake);
		c.runFov       = B("Features", "EnableCameraRunFov", c.runFov);
		c.pitchFov     = B("Features", "EnableCameraPitchFov", c.pitchFov);
		c.momentum     = B("Features", "EnableMomentum", c.momentum);
		c.crouchLoco   = B("Features", "EnableCrouchLocomotion", c.crouchLoco);
		c.shakeIntensity = F("CameraShake", "Intensity", c.shakeIntensity);
		c.shakeAmplitude = F("CameraShake", "Amplitude", c.shakeAmplitude);
		c.shakeSpeed     = F("CameraShake", "Speed", c.shakeSpeed);
		c.shakeRotDeg    = F("CameraShake", "RotationDegrees", c.shakeRotDeg);
		c.shakeRotSpeed  = F("CameraShake", "RotationSpeed", c.shakeRotSpeed);
		c.idleIntensity  = F("CameraIdleShake", "Intensity", c.idleIntensity);
		c.idleSpeed      = F("CameraIdleShake", "Speed", c.idleSpeed);
		c.idlePitchDeg   = F("CameraIdleShake", "PitchDegrees", c.idlePitchDeg);
		c.idleYawDeg     = F("CameraIdleShake", "YawDegrees", c.idleYawDeg);
		c.idleRollDeg    = F("CameraIdleShake", "RollDegrees", c.idleRollDeg);
		c.idleSprintScale= F("CameraIdleShake", "SprintScale", c.idleSprintScale);
		c.runInc     = F("CameraFov", "RunIncrease", c.runInc);
		c.sprintInc  = F("CameraFov", "SprintIncrease", c.sprintInc);
		c.riseResp   = F("CameraFov", "RiseResponse", c.riseResp);
		c.fallResp   = F("CameraFov", "FallResponse", c.fallResp);
		c.lookUpInc  = F("CameraFov", "LookUpIncrease", c.lookUpInc);
		c.lookUpStart= F("CameraFov", "LookUpStartPitch", c.lookUpStart);
		c.lookUpFull = F("CameraFov", "LookUpFullPitch", c.lookUpFull);
		c.lookDownInc= F("CameraFov", "LookDownIncrease", c.lookDownInc);
		c.pitchResp  = F("CameraFov", "PitchResponse", c.pitchResp);
		c.maxTotal   = F("CameraFov", "MaxTotalChange", c.maxTotal);
		c.exitResp   = F("CameraFov", "ExitResponse", c.exitResp);
		c.deadZone   = F("Momentum", "InputDeadZone", c.deadZone);
		c.stopEps    = F("Momentum", "StopEpsilon", c.stopEps);
		c.walkAcc    = F("Momentum", "WalkAcceleration", c.walkAcc);
		c.runAcc     = F("Momentum", "RunAcceleration", c.runAcc);
		c.sprintAcc  = F("Momentum", "SprintAcceleration", c.sprintAcc);
		c.braking    = F("Momentum", "Braking", c.braking);
		c.reversalBraking = F("Momentum", "ReversalBraking", c.reversalBraking);
		c.reversalAngle   = F("Momentum", "ReversalAngleDegrees", c.reversalAngle);
		c.reversalSwitch  = F("Momentum", "ReversalSwitchSpeed", c.reversalSwitch);
		c.lowTurn    = F("Momentum", "LowSpeedTurnRateDegreesPerSecond", c.lowTurn);
		c.highTurn   = F("Momentum", "HighSpeedTurnRateDegreesPerSecond", c.highTurn);
		c.highTurnStart = F("Momentum", "HighSpeedTurnStart", c.highTurnStart);
		c.momentumWhileAiming   = B("Momentum", "EnableWhileAiming", c.momentumWhileAiming);
		c.momentumWhileCrouched = B("Momentum", "EnableWhileCrouched", c.momentumWhileCrouched);
		c.keepCrouchedWithSprint = B("CrouchLocomotion", "KeepCrouchedWithSprint", c.keepCrouchedWithSprint);
		c.fovScale      = F("Client", "FovScale", c.fovScale);
		c.shakeGain     = F("Client", "ShakeGain", c.shakeGain);
		c.shakeDuration = F("Client", "ShakeDurationSeconds", c.shakeDuration);
		g_alsoCamVectors = B("Client", "AlsoRotateCamVectors", false);
		FLog("[SLEEK] config carregada (%s)", p.c_str());
	}
	inline void LoadIfNeeded() { if (!g_loaded) Load(); }

	// jogador local a pe, vivo, sem camera especial (celular)
	CPedGTA* OnFootPed()
	{
		CPedGTA* ped = GamePool_FindPlayerPed();
		if (!ped) return nullptr;
		if (ped->IsInVehicle()) return nullptr;
		if (ped->m_fHealth <= 0.0f || ped->m_nPedState == PEDSTATE_DIE || ped->m_nPedState == PEDSTATE_DEAD) return nullptr;
		if (CamStick::Active()) return nullptr;
		return ped;
	}

	float StepDt(Clock::time_point& last)
	{
		const auto now = Clock::now();
		float dt = 0.016f;
		if (last.time_since_epoch().count() != 0) dt = std::chrono::duration<float>(now - last).count();
		last = now;
		return Sleek::Clamp(dt, 0.0005f, 0.05f);
	}
}

namespace SleekMove
{
	float FovDelta() { return g_fovDelta; }

	void CameraProcess(void (*orig)(uintptr_t), uintptr_t thiz)
	{
		LoadIfNeeded();
		const Sleek::Config& c = g_cfg;
		if (!c.shakeRewrite && !c.idleShake && !c.runFov && !c.pitchFov) { g_fovDelta = 0.0f; orig(thiz); return; }

		CCamera* cam = reinterpret_cast<CCamera*>(thiz);
		float savedForce = cam->m_fCamShakeForce;
		const uint32_t shakeStart = cam->m_uiCamShakeStart;
		if (c.shakeRewrite) cam->m_fCamShakeForce = 0.0f;       // o motor nao aplica o proprio tremor; o nosso entra depois
		orig(thiz);
		if (c.shakeRewrite) cam->m_fCamShakeForce = savedForce;  // estado do motor intacto (decaimento/outros leitores)

		const float dt = StepDt(g_lastCam);
		CPedGTA* ped = OnFootPed();
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		const bool sprint = LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] != 0;

		// ---- FOV (corrida / sprint / olhar para cima) ----
		{
			Sleek::FovIn in;
			in.active = (ped != nullptr) && !aiming;
			const float lr = (float)(int16_t)LocalPlayerKeys.wKeyLR / 128.0f, ud = (float)(int16_t)LocalPlayerKeys.wKeyUD / 128.0f;
			in.stick = Sleek::Clamp(std::sqrt(lr * lr + ud * ud), 0.0f, 1.0f);
			in.sprint = sprint;
			const float fz = Sleek::Clamp(CCamera::GetActiveCamera().Front.z, -1.0f, 1.0f);
			in.pitchDeg = std::asin(fz) / Sleek::kDeg2Rad;
			g_fovDelta = g_fov.Update(c, in, dt);
		}

		// ---- tremores (rotacao + pequeno deslocamento) ----
		const float strength = c.shakeRewrite ? Sleek::ShakeStrength(c, savedForce, shakeStart, CTimer::m_snTimeInMilliseconds) : 0.0f;
		Sleek::ShakeOut sh = g_impact.Update(c, strength, dt);
		Sleek::IdleOut id = g_idle.Update(c, ped != nullptr && !aiming, sprint, dt);
		const float yaw = sh.yaw + id.yaw, pitch = sh.pitch + id.pitch, roll = sh.roll + id.roll;
		if (std::fabs(yaw) + std::fabs(pitch) + std::fabs(roll) + std::fabs(sh.offX) + std::fabs(sh.offZ) < 1e-6f) return;

		CMatrix& m = cam->m_mCameraMatrix;
		Sleek::V3 r{ m.m_right.x, m.m_right.y, m.m_right.z }, f{ m.m_forward.x, m.m_forward.y, m.m_forward.z }, u{ m.m_up.x, m.m_up.y, m.m_up.z };
		Sleek::RotateBasis(r, f, u, yaw, pitch, roll);
		m.m_right = CVector(r.x, r.y, r.z); m.m_forward = CVector(f.x, f.y, f.z); m.m_up = CVector(u.x, u.y, u.z);
		m.m_pos.x += r.x * sh.offX + u.x * sh.offZ; m.m_pos.y += r.y * sh.offX + u.y * sh.offZ; m.m_pos.z += r.z * sh.offX + u.z * sh.offZ;
		m.UpdateRW();
		if (cam->m_pRwCamera) RwFrameUpdateObjects(RwCameraGetFrame(cam->m_pRwCamera));

		if (g_alsoCamVectors)
		{
			CCam& cc = CCamera::GetActiveCamera();
			Sleek::V3 cf{ cc.Front.x, cc.Front.y, cc.Front.z }, cu{ cc.Up.x, cc.Up.y, cc.Up.z };
			Sleek::V3 cr = Sleek::Cross(cf, cu);
			const float l = std::sqrt(Sleek::Dot(cr, cr));
			if (l > 1e-4f) { cr.x /= l; cr.y /= l; cr.z /= l; Sleek::RotateBasis(cr, cf, cu, yaw, pitch, roll); cc.Front = CVector(cf.x, cf.y, cf.z); cc.Up = CVector(cu.x, cu.y, cu.z); }
		}
	}

	void ApplyStick(int16_t& lr, int16_t& ud)
	{
		LoadIfNeeded();
		const Sleek::Config& c = g_cfg;
		if (!c.momentum) return;
		const auto now = Clock::now();
		const bool first = g_lastMom.time_since_epoch().count() == 0;
		const float since = first ? 1.0f : std::chrono::duration<float>(now - g_lastMom).count();
		if (since < 0.003f) { lr = g_momLR; ud = g_momUD; return; }       // 2a chamada do mesmo quadro: mesmo resultado

		CPedGTA* ped = OnFootPed();
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		if (!ped || ped->bIsInTheAir || (aiming && !c.momentumWhileAiming) || (ped->bIsDucking && !c.momentumWhileCrouched))
		{
			g_mom.Reset(); g_lastMom = now; g_momLR = lr; g_momUD = ud; return;   // situacao especial: sem interferir
		}
		if (since > 0.25f) g_mom.Reset();                                          // pausa / troca de cena: nao arrastar velocidade antiga
		const float dt = Sleek::Clamp(since, 0.0005f, 0.05f);
		g_lastMom = now;

		g_mom.Update(c, (float)lr / 128.0f, -(float)ud / 128.0f, LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] != 0, dt);
		int ol = (int)std::lround(g_mom.vx * 128.0f), ou = (int)std::lround(-g_mom.vy * 128.0f);
		ol = Sleek::Clamp((float)ol, -128.0f, 128.0f); ou = Sleek::Clamp((float)ou, -128.0f, 128.0f);
		lr = g_momLR = (int16_t)ol; ud = g_momUD = (int16_t)ou;
	}

	uint32_t FilterSprint(uint32_t sprint)
	{
		LoadIfNeeded();
		if (!sprint || !g_cfg.crouchLoco || !g_cfg.keepCrouchedWithSprint) return sprint;
		CPedGTA* ped = GamePool_FindPlayerPed();
		if (ped && !ped->IsInVehicle() && ped->bIsDucking) return 0;
		return sprint;
	}

	std::string Command(const std::string& argsIn)
	{
		LoadIfNeeded();
		std::vector<std::string> t; { std::istringstream ss(argsIn); std::string w; while (ss >> w) { for (auto& ch : w) ch = (char)tolower((unsigned char)ch); t.push_back(w); } }
		Sleek::Config& c = g_cfg;
		auto onoff = [](bool b) { return b ? "ON" : "OFF"; };
		char b[300];
		if (t.empty() || t[0] == "status")
		{
			snprintf(b, sizeof(b), "[SLEEK] tremor=%s ocioso=%s fovCorrida=%s fovPitch=%s inercia=%s agachado=%s | FOV agora=+%.1f | uso: /sleek <modulo> on|off | fov <0-2> | reload | reset",
				onoff(c.shakeRewrite), onoff(c.idleShake), onoff(c.runFov), onoff(c.pitchFov), onoff(c.momentum), onoff(c.crouchLoco), g_fovDelta);
			return b;
		}
		if (t[0] == "reload") { Load(); g_fov.Reset(); g_mom.Reset(); return "[SLEEK] config recarregada de SAMP/sleek.ini"; }
		if (t[0] == "reset") { g_fov.Reset(); g_mom.Reset(); g_idle = Sleek::IdleShake(); g_impact = Sleek::ImpactShake(); return "[SLEEK] estados zerados"; }
		if (t[0] == "off" || t[0] == "on")
		{
			const bool v = t[0] == "on";
			c.shakeRewrite = c.idleShake = c.runFov = c.pitchFov = c.momentum = c.crouchLoco = v;
			if (!v) { g_fov.Reset(); g_mom.Reset(); g_fovDelta = 0; }
			return v ? "[SLEEK] tudo LIGADO" : "[SLEEK] tudo DESLIGADO";
		}
		if (t[0] == "fov" && t.size() >= 2) { c.fovScale = Sleek::Clamp((float)atof(t[1].c_str()), 0.0f, 2.0f); snprintf(b, sizeof(b), "[SLEEK] escala do FOV = %.2f", c.fovScale); return b; }
		if (t.size() >= 2 && (t[1] == "on" || t[1] == "off"))
		{
			const bool v = t[1] == "on"; bool* f = nullptr;
			if (t[0] == "tremor") f = &c.shakeRewrite; else if (t[0] == "ocioso" || t[0] == "idle") f = &c.idleShake;
			else if (t[0] == "corrida" || t[0] == "fovcorrida") f = &c.runFov; else if (t[0] == "pitch" || t[0] == "fovpitch") f = &c.pitchFov;
			else if (t[0] == "inercia" || t[0] == "momentum") f = &c.momentum; else if (t[0] == "agachado") f = &c.crouchLoco;
			if (f) { *f = v; if (!v) { g_fov.Reset(); g_mom.Reset(); } snprintf(b, sizeof(b), "[SLEEK] %s = %s", t[0].c_str(), onoff(v)); return b; }
		}
		return "[SLEEK] modulos: tremor | ocioso | corrida | pitch | inercia | agachado  (ex.: /sleek inercia off)";
	}
}
