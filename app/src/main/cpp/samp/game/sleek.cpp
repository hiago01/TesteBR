#include "../main.h"
#include "game.h"
#include "util.h"
#include "sleek.h"
#include "sleek_logic.h"
#include "sleek_priv.h"
#include "../vendor/armhook/patch.h"
#include "camstick.h"
#include "pad.h"
#include "Camera.h"
#include "World.h"
#include "Collision/ColPoint.h"
#include "Enums/eCamMode.h"
#include "Entity/CPedGTA.h"
#include "Enums/ePedState.h"
#include "Tasks/TaskManager.h"
#include "Enums/eTaskType.h"
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
	Sleek::Config      g_cfg;
	Sleek::PoseConfig  g_pose;
	Sleek::CamConfig   g_camc;
	bool               g_loaded = false;
	bool               g_alsoCamVectors = false;

	Sleek::FovCtl      g_fov;
	Sleek::IdleShake   g_idle;
	Sleek::ImpactShake g_impact;
	Sleek::Momentum    g_mom;
	Sleek::CamState    g_cams;
	float g_fovDelta = 0.0f;
	float g_blockedT = 0.0f;
	uint32_t g_epoch = 0; bool g_havePos = false; Sleek::V3 g_lastPos;

	using Clock = std::chrono::steady_clock;
	Clock::time_point g_lastCam{}, g_lastMom{};
	int16_t g_momLR = 0, g_momUD = 0;

	std::string IniPath() { return g_pszStorage ? std::string(g_pszStorage) + "SAMP/sleek.ini" : std::string(); }

	// Arquivo criado SO se nao existir (nunca sobrescrito). Chaves ausentes num arquivo antigo usam os padroes do codigo.
	const char* kDefaultIni =
		"; Sleek (recriacao do SleekMovement) - mesmos nomes do .ini original. Edite e use /sleek reload.\n"
		"; Postura (Lean/Pitch/HeadTracking/FootIK) vem ligada por padrao; para desligar crie o arquivo vazio SAMP/sleek_pose.off.\n[Features]\nEnableLean=1\nEnablePitch=1\nEnableHeadFollow=1\nEnableFootIK=1\nEnableFootIKWhileCrouched=1\nEnableModernCamera=1\nEnableCameraShakeRewrite=1\nEnableCameraIdleShake=0\nEnableCameraRunFov=1\nEnableCameraPitchFov=1\nEnableHeadTracking=1\nEnableMomentum=1\nEnableCrouchLocomotion=1\n"
		"[CameraShake]\nIntensity=1.25\nAmplitude=1.1\nSpeed=17.0\nRotationDegrees=4.0\nRotationSpeed=9.0\n"
		"[CameraIdleShake]\nIntensity=3.0\nSpeed=0.30\nPitchDegrees=0.40\nYawDegrees=0.50\nRollDegrees=0.25\nSprintScale=1.5\n"
		"[CameraFov]\nRunIncrease=15.0\nSprintIncrease=30.0\nRiseResponse=3.0\nFallResponse=3.2\nLookUpIncrease=15.0\nLookUpStartPitch=5.0\nLookUpFullPitch=45.0\nLookDownIncrease=0.0\nPitchResponse=4.5\nMaxTotalChange=45.0\nExitResponse=12.0\n"
		"[Momentum]\nInputDeadZone=0.08\nStopEpsilon=0.015\nWalkAcceleration=1.3\nRunAcceleration=1.6\nSprintAcceleration=1.5\nBraking=4.1\nReversalBraking=3.2\nReversalAngleDegrees=115.0\nReversalSwitchSpeed=0.12\n"
		"LowSpeedTurnRateDegreesPerSecond=540.0\nHighSpeedTurnRateDegreesPerSecond=210.0\nHighSpeedTurnStart=0.60\nLateralGrip=0.72\nBlockedSpeedDecay=10.0\nTeleportResetDistance=3.0\nEnableWhileAiming=1\nEnableWhileCrouched=1\n"
		"[CrouchLocomotion]\nKeepCrouchedWithSprint=1\n"
		"[HeadTracking]\nLookDistance=15.0\nMovingScale=0.7\nMaxYawDegrees=75.0\nMaxPitchUpDegrees=30.0\nMaxPitchDownDegrees=25.0\nFadeStartYawDegrees=85.0\nBehindYawDegrees=110.0\nHeadResponse=9.0\nNeckResponse=6.0\nTorsoResponse=3.5\nEngageResponse=3.0\nDisengageResponse=5.0\nMaxAngularSpeed=300.0\nTorsoYawShare=0.20\nNeckYawShare=0.30\nTorsoPitchShare=0.10\nNeckPitchShare=0.40\n"
		"[HeadFollow]\nLeanInfluence=0.35\nPitchInfluence=0.30\n"
		"[Lean]\nMaxLeanDegrees=10.0\nLeanInResponse=5.5\nLeanOutResponse=5.0\nInputContribution=0.30\nTurnContribution=0.40\nAngularVelocityContribution=1.00\nFullTurnAngleDegrees=42.0\nFullTiltTurnSpeedDegreesPerSecond=120.0\n"
		"[Pitch]\nMovementEnvelopeResponse=3.0\nPitchResponse=7.0\nMaxAccelerationPitchDegrees=10.0\nMaxBrakingPitchDegrees=7.0\n"
		"[FootIK]\nProbeStartHeight=0.50\nProbeDepth=0.70\nSoleOffset=0.025\nActivationDistance=0.50\nMaxFootOffset=0.50\nMaxStepUpHeight=0.45\nMaxStepDownHeight=0.28\nCorrectionDeadZone=0.006\nContactHeight=0.11\nPlantResponse=22.0\nReleaseResponse=16.0\nPositionResponse=18.0\nNormalResponse=10.0\nInfluence=1.0\nMaxHipCorrectionDegrees=35.0\nMaxKneeCorrectionDegrees=90.0\nMaxFootCorrectionDegrees=28.0\nMinimumGroundNormalZ=0.68\nFullInfluenceGroundNormalZ=0.82\n"
		"[ModernCamera]\nOffsetX=0.45\nOffsetY=0.15\nShoulderSwapResponse=8.0\nAnchorHeightOffset=0.26\nZoomNearDistance=1.50\nZoomMiddleDistance=2.30\nZoomFarDistance=3.40\nZoomResponse=8.0\nLateralFollowResponse=31.5\nDepthFollowResponse=6.0\nApproachFollowResponse=14.0\nVerticalFollowResponse=14.0\nMaxLateralLag=0.22\nMaxApproachLag=0.12\nMaxRecedeLag=0.45\nMaxVerticalLag=0.35\nTransitionResponse=5.5\nExitTransitionResponse=9.0\nEnableCollision=1\nCollisionRadius=0.18\nCollisionPadding=0.12\nCollisionRecoveryResponse=6.0\nTeleportResetDistance=8.0\n"
		"[Client]\n; IniRev=2: tremor ocioso (idle) desligado por padrao. Para ligar: EnableCameraIdleShake=1 (ou /sleek ocioso on)\nIniRev=2\n; 0..1 = quanto da inclinacao do tronco e desfeita nos bracos (1 = bracos nao acompanham o tronco)\nArmCounter=0.0\nTorsoPitchMaxDegrees=6.0\n; FOV: 1.0 = valores acima; 0.5 = metade do efeito\nFovScale=1.0\nShakeGain=1.0\nShakeDurationSeconds=5.0\n; 1 = tambem gira Front/Up da camera ativa (a mira acompanha o tremor)\nAlsoRotateCamVectors=0\n"
		"; -1 inverte o lado da inclinacao do corpo / frente-tras, se ficar ao contrario no aparelho\nLeanSign=1.0\nPitchSign=1.0\n; altura do centro do ped sobre o chao (foot IK)\nPedGroundOffset=1.0\n; altura-base da ancora da camera de ombro (somada a AnchorHeightOffset)\nShoulderAnchorBase=0.55\n";

	float g_anchorBase = 0.55f;

	// Postura/IK agora vem LIGADA por padrao (o hook do esqueleto ficou estavel). Para desligar: crie o arquivo vazio SAMP/sleek_pose.off
	bool PoseMarker()
	{
		const std::string mk = g_pszStorage ? std::string(g_pszStorage) + "SAMP/sleek_pose.off" : std::string();
		FILE* f = mk.empty() ? nullptr : fopen(mk.c_str(), "rb");
		if (f) { fclose(f); return false; }
		return true;
	}

	void Load()
	{
		g_loaded = true;
		g_cfg = Sleek::Config(); g_pose = Sleek::PoseConfig(); g_camc = Sleek::CamConfig(); g_alsoCamVectors = false; g_anchorBase = 0.55f;
		const std::string p = IniPath();
		if (p.empty()) return;
		{
			std::ifstream t(p, std::ios::binary);
			if (!t.good()) { std::ofstream o(p, std::ios::binary); if (o.good()) o << kDefaultIni; }
		}
		INIReader r(p);
		if (r.ParseError() < 0) { FLog("[SLEEK] sem leitura de %s - usando padroes", p.c_str()); return; }
		auto F = [&](const char* s, const char* k, float d) { return (float)r.GetReal(s, k, d); };
		auto B = [&](const char* s, const char* k, bool d) { return r.GetBoolean(s, k, d); };
		Sleek::Config& c = g_cfg; Sleek::PoseConfig& q = g_pose; Sleek::CamConfig& m = g_camc;
		c.shakeRewrite = B("Features", "EnableCameraShakeRewrite", c.shakeRewrite);
		c.idleShake    = B("Features", "EnableCameraIdleShake", c.idleShake);
		// INI antigo (sem IniRev>=2) foi gerado com o tremor ocioso ligado: o padrao agora e DESLIGADO
		if (F("Client", "IniRev", 1.0f) < 2.0f) c.idleShake = false;
		c.runFov       = B("Features", "EnableCameraRunFov", c.runFov);
		c.pitchFov     = B("Features", "EnableCameraPitchFov", c.pitchFov);
		c.momentum     = B("Features", "EnableMomentum", c.momentum);
		c.crouchLoco   = B("Features", "EnableCrouchLocomotion", c.crouchLoco);
		const bool pm = PoseMarker();   // sem SAMP/sleek_pose.off a postura/IK nasce ligada (a chave do INI, se existir, manda)
		q.lean = B("Features", "EnableLean", pm); q.pitch = B("Features", "EnablePitch", pm);
		q.headTracking = B("Features", "EnableHeadTracking", pm); q.headFollow = B("Features", "EnableHeadFollow", q.headFollow);
		q.footIK = B("Features", "EnableFootIK", pm); q.footIKCrouched = B("Features", "EnableFootIKWhileCrouched", q.footIKCrouched);
		m.enabled = B("Features", "EnableModernCamera", true);
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
		c.lateralGrip   = F("Momentum", "LateralGrip", c.lateralGrip);
		c.blockedDecay  = F("Momentum", "BlockedSpeedDecay", c.blockedDecay);
		c.teleportDist  = F("Momentum", "TeleportResetDistance", c.teleportDist);
		c.momentumWhileAiming   = B("Momentum", "EnableWhileAiming", c.momentumWhileAiming);
		c.momentumWhileCrouched = B("Momentum", "EnableWhileCrouched", c.momentumWhileCrouched);
		c.keepCrouchedWithSprint = B("CrouchLocomotion", "KeepCrouchedWithSprint", c.keepCrouchedWithSprint);
		// [Lean] [Pitch]
		q.maxLean = F("Lean", "MaxLeanDegrees", q.maxLean); q.leanIn = F("Lean", "LeanInResponse", q.leanIn); q.leanOut = F("Lean", "LeanOutResponse", q.leanOut);
		q.inputContribution = F("Lean", "InputContribution", q.inputContribution); q.turnContribution = F("Lean", "TurnContribution", q.turnContribution);
		q.angVelContribution = F("Lean", "AngularVelocityContribution", q.angVelContribution); q.fullTurnAngle = F("Lean", "FullTurnAngleDegrees", q.fullTurnAngle);
		q.fullTiltTurnSpeed = F("Lean", "FullTiltTurnSpeedDegreesPerSecond", q.fullTiltTurnSpeed);
		q.envResp = F("Pitch", "MovementEnvelopeResponse", q.envResp); q.pitchResp = F("Pitch", "PitchResponse", q.pitchResp);
		q.maxAccelPitch = F("Pitch", "MaxAccelerationPitchDegrees", q.maxAccelPitch); q.maxBrakePitch = F("Pitch", "MaxBrakingPitchDegrees", q.maxBrakePitch);
		// [HeadTracking] [HeadFollow]
		q.lookDistance = F("HeadTracking", "LookDistance", q.lookDistance); q.movingScale = F("HeadTracking", "MovingScale", q.movingScale);
		q.maxYaw = F("HeadTracking", "MaxYawDegrees", q.maxYaw); q.maxPitchUp = F("HeadTracking", "MaxPitchUpDegrees", q.maxPitchUp); q.maxPitchDown = F("HeadTracking", "MaxPitchDownDegrees", q.maxPitchDown);
		q.fadeStartYaw = F("HeadTracking", "FadeStartYawDegrees", q.fadeStartYaw); q.behindYaw = F("HeadTracking", "BehindYawDegrees", q.behindYaw);
		q.headResp = F("HeadTracking", "HeadResponse", q.headResp); q.neckResp = F("HeadTracking", "NeckResponse", q.neckResp); q.torsoResp = F("HeadTracking", "TorsoResponse", q.torsoResp);
		q.engageResp = F("HeadTracking", "EngageResponse", q.engageResp); q.disengageResp = F("HeadTracking", "DisengageResponse", q.disengageResp); q.maxAngSpeed = F("HeadTracking", "MaxAngularSpeed", q.maxAngSpeed);
		q.torsoYawShare = F("HeadTracking", "TorsoYawShare", q.torsoYawShare); q.neckYawShare = F("HeadTracking", "NeckYawShare", q.neckYawShare);
		q.torsoPitchShare = F("HeadTracking", "TorsoPitchShare", q.torsoPitchShare); q.neckPitchShare = F("HeadTracking", "NeckPitchShare", q.neckPitchShare);
		q.hfLean = F("HeadFollow", "LeanInfluence", q.hfLean); q.hfPitch = F("HeadFollow", "PitchInfluence", q.hfPitch);
		// [FootIK]
		q.probeStart = F("FootIK", "ProbeStartHeight", q.probeStart); q.probeDepth = F("FootIK", "ProbeDepth", q.probeDepth); q.soleOffset = F("FootIK", "SoleOffset", q.soleOffset);
		q.activation = F("FootIK", "ActivationDistance", q.activation); q.maxFootOffset = F("FootIK", "MaxFootOffset", q.maxFootOffset);
		q.maxStepUp = F("FootIK", "MaxStepUpHeight", q.maxStepUp); q.maxStepDown = F("FootIK", "MaxStepDownHeight", q.maxStepDown); q.deadZone = F("FootIK", "CorrectionDeadZone", q.deadZone);
		q.contactHeight = F("FootIK", "ContactHeight", q.contactHeight); q.plantResp = F("FootIK", "PlantResponse", q.plantResp); q.releaseResp = F("FootIK", "ReleaseResponse", q.releaseResp);
		q.posResp = F("FootIK", "PositionResponse", q.posResp); q.normalResp = F("FootIK", "NormalResponse", q.normalResp); q.influence = F("FootIK", "Influence", q.influence);
		q.maxHipDeg = F("FootIK", "MaxHipCorrectionDegrees", q.maxHipDeg); q.maxKneeDeg = F("FootIK", "MaxKneeCorrectionDegrees", q.maxKneeDeg); q.maxFootDeg = F("FootIK", "MaxFootCorrectionDegrees", q.maxFootDeg);
		q.minNormalZ = F("FootIK", "MinimumGroundNormalZ", q.minNormalZ); q.fullNormalZ = F("FootIK", "FullInfluenceGroundNormalZ", q.fullNormalZ);
		// [ModernCamera]
		m.offsetX = F("ModernCamera", "OffsetX", m.offsetX); m.offsetY = F("ModernCamera", "OffsetY", m.offsetY); m.swapResp = F("ModernCamera", "ShoulderSwapResponse", m.swapResp);
		m.anchorHeight = F("ModernCamera", "AnchorHeightOffset", m.anchorHeight);
		m.zoomNear = F("ModernCamera", "ZoomNearDistance", m.zoomNear); m.zoomMid = F("ModernCamera", "ZoomMiddleDistance", m.zoomMid); m.zoomFar = F("ModernCamera", "ZoomFarDistance", m.zoomFar);
		m.zoomResp = F("ModernCamera", "ZoomResponse", m.zoomResp);
		m.lateralResp = F("ModernCamera", "LateralFollowResponse", m.lateralResp); m.depthResp = F("ModernCamera", "DepthFollowResponse", m.depthResp);
		m.approachResp = F("ModernCamera", "ApproachFollowResponse", m.approachResp); m.verticalResp = F("ModernCamera", "VerticalFollowResponse", m.verticalResp);
		m.maxLatLag = F("ModernCamera", "MaxLateralLag", m.maxLatLag); m.maxApproachLag = F("ModernCamera", "MaxApproachLag", m.maxApproachLag);
		m.maxRecedeLag = F("ModernCamera", "MaxRecedeLag", m.maxRecedeLag); m.maxVertLag = F("ModernCamera", "MaxVerticalLag", m.maxVertLag);
		m.transResp = F("ModernCamera", "TransitionResponse", m.transResp); m.exitResp = F("ModernCamera", "ExitTransitionResponse", m.exitResp);
		m.collision = B("ModernCamera", "EnableCollision", m.collision); m.colRadius = F("ModernCamera", "CollisionRadius", m.colRadius);
		m.colPadding = F("ModernCamera", "CollisionPadding", m.colPadding); m.colRecovery = F("ModernCamera", "CollisionRecoveryResponse", m.colRecovery);
		m.teleportDist = F("ModernCamera", "TeleportResetDistance", m.teleportDist);
		// [Client]
		c.fovScale = F("Client", "FovScale", c.fovScale); c.shakeGain = F("Client", "ShakeGain", c.shakeGain); c.shakeDuration = F("Client", "ShakeDurationSeconds", c.shakeDuration);
		g_alsoCamVectors = B("Client", "AlsoRotateCamVectors", false);
		q.leanSign = F("Client", "LeanSign", q.leanSign); q.pitchSign = F("Client", "PitchSign", q.pitchSign); q.pedGroundOffset = F("Client", "PedGroundOffset", q.pedGroundOffset);
		q.armCounter = Sleek::Clamp(F("Client", "ArmCounter", q.armCounter), 0.0f, 1.0f); q.torsoPitchMax = Sleek::Clamp(F("Client", "TorsoPitchMaxDegrees", q.torsoPitchMax), 0.0f, 20.0f);
		g_anchorBase = F("Client", "ShoulderAnchorBase", g_anchorBase);
		FLog("[SLEEK] config carregada (%s)", p.c_str());
	}
	inline void LoadIfNeeded() { if (!g_loaded) Load(); }

	CPedGTA* BasicPed()
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

	// teleporte: o jogador "salta" mais que o limite entre dois quadros de camera => zera tudo que acumula estado
	void CheckTeleport(CPedGTA* ped)
	{
		if (!ped) { g_havePos = false; return; }
		const CVector& p = ped->GetPosition();
		const Sleek::V3 now{ p.x, p.y, p.z };
		if (g_havePos)
		{
			const float d = Sleek::Len(now - g_lastPos);
			if (d > g_cfg.teleportDist) { g_epoch++; g_mom.Reset(); g_fov.Reset(); g_cams.haveAnchor = false; g_cams.colDist = 99.0f; FLog("[SLEEK] teleporte detectado (%.1f m): estados zerados", d); }
		}
		g_lastPos = now; g_havePos = true;
	}
}

namespace SleekMove
{
	Sleek::Config&     Cfg()     { return g_cfg; }
	Sleek::PoseConfig& PoseCfg() { return g_pose; }
	Sleek::CamConfig&  CamCfg()  { return g_camc; }
	void     EnsureLoaded()      { LoadIfNeeded(); }
	uint32_t TeleportEpoch()     { return g_epoch; }

	CPedGTA* PoseEligiblePed()
	{
		LoadIfNeeded();
		CPedGTA* ped = BasicPed();
		if (!ped) return nullptr;
		if (ped->bIsInTheAir || !ped->m_pIntelligence) return nullptr;
		CTaskManager& tm = ped->GetTaskManager();
		if (tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_SWIM) || tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_CLIMB) ||
		    tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_FALL) || tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_IN_AIR) ||
		    tm.CTaskManager::FindActiveTaskByType(TASK_SIMPLE_JUMP)) return nullptr;
		return ped;
	}

	float FovDelta() { return g_fovDelta; }

	// ---------------------------------------------------------------------------------------------------
	// Camera: a CCamera::Process do jogo monta m_mCameraMatrix e DEPOIS chama CalculateDerivedValues (matriz de visao/frustum) e
	// CopyCameraMatrixToRWCam (envia ao render) - conferido no libGTASA.so ARM64. Editar a matriz depois do Process nao chega ao render.
	// Por isso: o Process calcula tudo ANTES (estado, FOV, tremores) e a aplicacao na matriz acontece DENTRO do hook de
	// CalculateDerivedValues (1x por Process), logo antes de a matriz de visao ser derivada.
	// ---------------------------------------------------------------------------------------------------
	struct Pend { bool valid = false, applied = false, shoulder = false; float dt = 0.016f, yaw = 0, pitch = 0, roll = 0, offX = 0, offZ = 0; CPedGTA* ped = nullptr; } g_pend;
	bool g_zoomOverride = false;
	struct CamDbg { unsigned process = 0, derived = 0, applied = 0, shoulderFrames = 0; int mode = -1; bool ped = false, onFoot = false; } g_dbg;
	void (*g_origDerived)(CCamera*, bool, bool) = nullptr;

	void ApplyToCameraMatrix(CCamera* cam)
	{
		using namespace Sleek;
		const CamConfig& mc = g_camc;
		CMatrix& m = cam->m_mCameraMatrix;
		V3 r{ m.m_right.x, m.m_right.y, m.m_right.z }, f{ m.m_forward.x, m.m_forward.y, m.m_forward.z }, u{ m.m_up.x, m.m_up.y, m.m_up.z };
		V3 pos{ m.m_pos.x, m.m_pos.y, m.m_pos.z };

		// ---- camera de ombro: desloca a camera para o lado (e um pouco para cima); o personagem sai do centro da tela ----
		if (g_pend.shoulder && g_pend.ped && g_cams.blend > 0.003f)
		{
			const V3 right = Norm(r, { 1, 0, 0 }), fwd = Norm(f, { 0, 1, 0 }), up = Norm(u, { 0, 0, 1 });
			const CVector& pp = g_pend.ped->GetPosition();
			const V3 anchor = { pp.x, pp.y, pp.z + g_anchorBase + mc.anchorHeight };
			g_cams.sideS = Approach(g_cams.sideS, g_cams.side, mc.swapResp, g_pend.dt);
			V3 want = pos + right * (mc.offsetX * g_cams.sideS) + up * mc.offsetY;
			if (g_zoomOverride)   // distancia fixa (perto/medio/longe) so se pedida por /sleek zoom; senao respeita o zoom do projeto
			{
				g_cams.dist = Approach(g_cams.dist, g_cams.ZoomTarget(mc), mc.zoomResp, g_pend.dt);
				want = anchor - fwd * g_cams.dist + right * (mc.offsetX * g_cams.sideS) + up * mc.offsetY;
			}
			if (mc.collision)
			{
				const V3 dir = want - anchor; const float len = Len(dir);
				float allowed = len;
				if (len > 0.05f)
				{
					const float pad = mc.colRadius + mc.colPadding;
					const CVector o(anchor.x, anchor.y, anchor.z), tt(want.x + dir.x / len * pad, want.y + dir.y / len * pad, want.z + dir.z / len * pad);
					CColPoint col; CEntityGTA* ent = nullptr;
					if (CWorld::ProcessLineOfSight(&o, &tt, &col, &ent, true, true, false, true, false, false, true, false))
					{
						const V3 hit = { col.m_vecPoint.x, col.m_vecPoint.y, col.m_vecPoint.z };
						allowed = Clamp(Len(hit - anchor) - pad, 0.35f, len);
					}
				}
				g_cams.colDist = allowed < g_cams.colDist ? allowed : Approach(g_cams.colDist, allowed, mc.colRecovery, g_pend.dt);
				if (g_cams.colDist < len) want = anchor + dir * (g_cams.colDist / len);
			}
			pos = pos + (want - pos) * g_cams.blend;
		}

		// ---- tremores ----
		if (std::fabs(g_pend.yaw) + std::fabs(g_pend.pitch) + std::fabs(g_pend.roll) > 1e-6f)
			RotateBasis(r, f, u, g_pend.yaw, g_pend.pitch, g_pend.roll);
		pos = pos + r * g_pend.offX + u * g_pend.offZ;

		m.m_right = CVector(r.x, r.y, r.z); m.m_forward = CVector(f.x, f.y, f.z); m.m_up = CVector(u.x, u.y, u.z);
		m.m_pos = CVector(pos.x, pos.y, pos.z);
	}

	void Derived_hook(CCamera* cam, bool a, bool b)
	{
		g_dbg.derived++;
		if (g_pend.valid && !g_pend.applied && cam) { g_pend.applied = true; g_dbg.applied++; ApplyToCameraMatrix(cam); }
		g_origDerived(cam, a, b);
	}

	void (*g_origProcess)(uintptr_t) = nullptr;
	void Process_hook(uintptr_t thiz);   // definida depois de CameraProcess

	void InstallCameraHook()
	{
		LoadIfNeeded();
		// ARM64: simbolo exportado confirmado; fallback = endereco exato SEM +1 (nunca usar o overload lib+addr, que soma o bit thumb).
		const uintptr_t sym = CHook::getSymSoft("_ZN7CCamera22CalculateDerivedValuesEbb");
		void* target = sym ? (void*)sym : (void*)(g_libGTASA + 0x4BC93C);
		CHook::InlineHook(target, &Derived_hook, &g_origDerived);
		FLog("[SLEEK] hook CCamera::CalculateDerivedValues @%p via %s: %s", target, sym ? "simbolo" : "endereco 0x4BC93C", g_origDerived ? "instalado" : "FALHOU - camera de ombro/tremor inativos");
		// CCamera::Process: o hook por PLT do projeto (offset 0x6717BC, de 32 bits) NAO existe no libGTASA ARM64 (o slot real e 0x8429D8; 0x6717BC cai em .rodata),
		// por isso o Process nunca passava pelo Sleek (log: process=0). Hook inline direto na funcao, igual ao CalculateDerivedValues.
		const uintptr_t psym = CHook::getSymSoft("_ZN7CCamera7ProcessEv");
		void* ptarget = psym ? (void*)psym : (void*)(g_libGTASA + 0x4BAB78);
		CHook::InlineHook(ptarget, &Process_hook, &g_origProcess);
		FLog("[SLEEK] hook CCamera::Process @%p via %s: %s", ptarget, psym ? "simbolo" : "endereco 0x4BAB78", g_origProcess ? "instalado" : "FALHOU");
	}

	std::string CamStatus()
	{
		char b[320];
		snprintf(b, sizeof(b), "hookDerived=%s process=%u derived=%u aplicado=%u quadrosOmbro=%u modo=%d ped=%d aPe=%d blend=%.2f ligada=%d offX=%.2f lado=%.0f zoomFixo=%d",
			g_origDerived ? "ok" : "FALHOU", g_dbg.process, g_dbg.derived, g_dbg.applied, g_dbg.shoulderFrames, g_dbg.mode, g_dbg.ped, g_dbg.onFoot, g_cams.blend, g_camc.enabled, g_camc.offsetX, g_cams.side, g_zoomOverride);
		return b;
	}

	void CameraProcess(void (*orig)(uintptr_t), uintptr_t thiz)
	{
		LoadIfNeeded();
		const Sleek::Config& c = g_cfg;
		const Sleek::CamConfig& mc = g_camc;
		if (!g_origDerived || (!c.shakeRewrite && !c.idleShake && !c.runFov && !c.pitchFov && !mc.enabled))
		{
			// sem o hook de CalculateDerivedValues nao ha como aplicar camera/tremor; so o FOV (hook do SetFOV) continua
			float dummy = 0; (void)dummy;
			if (!g_origDerived && (c.runFov || c.pitchFov))
			{
				CCamera* cam0 = reinterpret_cast<CCamera*>(thiz); (void)cam0;
				orig(thiz);
				const float dt0 = StepDt(g_lastCam); CPedGTA* ped0 = BasicPed(); CheckTeleport(ped0);
				Sleek::FovIn in; in.active = ped0 != nullptr && LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] == 0;
				const float lr = (float)(int16_t)LocalPlayerKeys.wKeyLR / 128.0f, ud = (float)(int16_t)LocalPlayerKeys.wKeyUD / 128.0f;
				in.stick = Sleek::Clamp(std::sqrt(lr * lr + ud * ud), 0.0f, 1.0f); in.sprint = LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] != 0;
				in.pitchDeg = std::asin(Sleek::Clamp(CCamera::GetActiveCamera().Front.z, -1.0f, 1.0f)) / Sleek::kDeg2Rad;
				g_fovDelta = g_fov.Update(c, in, dt0);
				return;
			}
			g_fovDelta = 0.0f; orig(thiz); return;
		}

		g_dbg.process++;
		if (g_dbg.process % 600 == 1) FLog("[SLEEK-CAM] %s", CamStatus().c_str());
		CCamera* cam = reinterpret_cast<CCamera*>(thiz);
		float savedForce = cam->m_fCamShakeForce;
		const uint32_t shakeStart = cam->m_uiCamShakeStart;

		const float dt = StepDt(g_lastCam);
		CPedGTA* ped = BasicPed();
		CheckTeleport(ped);
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		const bool sprint = LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] != 0;

		// ---- FOV ----
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

		// ---- estado da camera de ombro (modo da camera do quadro anterior) ----
		{
			CCam& ac = CCamera::GetActiveCamera();
			// a camera de ombro vale a pe e sem mirar, em qualquer modo de camera de seguir (o modo e so registrado para diagnostico)
			const bool onFoot = ped && !ped->IsInVehicle();
			const bool follow = onFoot && !aiming && mc.enabled;
			g_dbg.mode = (int)ac.m_nMode; g_dbg.ped = ped != nullptr; g_dbg.onFoot = onFoot; if (follow) g_dbg.shoulderFrames++;
			g_cams.blend = Sleek::Approach(g_cams.blend, follow ? 1.0f : 0.0f, follow ? mc.transResp : mc.exitResp, dt);
			g_pend.shoulder = follow;
		}

		// ---- tremores (calculados agora, aplicados dentro de CalculateDerivedValues) ----
		const float strength = c.shakeRewrite ? Sleek::ShakeStrength(c, savedForce, shakeStart, CTimer::m_snTimeInMilliseconds) : 0.0f;
		const Sleek::ShakeOut sh = g_impact.Update(c, strength, dt);
		const Sleek::IdleOut id = g_idle.Update(c, ped != nullptr && !aiming, sprint, dt);
		g_pend.yaw = sh.yaw + id.yaw; g_pend.pitch = sh.pitch + id.pitch; g_pend.roll = sh.roll + id.roll; g_pend.offX = sh.offX; g_pend.offZ = sh.offZ;
		g_pend.dt = dt; g_pend.ped = ped; g_pend.applied = false; g_pend.valid = true;

		if (c.shakeRewrite) cam->m_fCamShakeForce = 0.0f;       // o motor nao aplica o tremor dele; o nosso entra na CalculateDerivedValues
		orig(thiz);
		if (c.shakeRewrite) cam->m_fCamShakeForce = savedForce;
		g_pend.valid = false;
	}

	void Process_hook(uintptr_t thiz) { if (g_origProcess) CameraProcess(g_origProcess, thiz); }

	void ApplyStick(int16_t& lr, int16_t& ud)
	{
		LoadIfNeeded();
		const Sleek::Config& c = g_cfg;
		if (!c.momentum) return;
		const auto now = Clock::now();
		const bool first = g_lastMom.time_since_epoch().count() == 0;
		const float since = first ? 1.0f : std::chrono::duration<float>(now - g_lastMom).count();
		if (since < 0.003f) { lr = g_momLR; ud = g_momUD; return; }

		CPedGTA* ped = BasicPed();
		const bool aiming = LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] != 0;
		if (!ped || ped->bIsInTheAir || (aiming && !c.momentumWhileAiming) || (ped->bIsDucking && !c.momentumWhileCrouched))
		{
			g_mom.Reset(); g_blockedT = 0; g_lastMom = now; g_momLR = lr; g_momUD = ud; return;
		}
		if (since > 0.25f) { g_mom.Reset(); g_blockedT = 0; }
		const float dt = Sleek::Clamp(since, 0.0005f, 0.05f);
		g_lastMom = now;

		const bool sprint = LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] != 0;
		g_mom.Update(c, (float)lr / 128.0f, -(float)ud / 128.0f, sprint, dt);

		// BlockedSpeedDecay: stick empurrando mas o personagem quase parado (parede/obstaculo) por >0,25 s => a velocidade interna some
		{
			const float real = std::sqrt(ped->m_vecMoveSpeed.x * ped->m_vecMoveSpeed.x + ped->m_vecMoveSpeed.y * ped->m_vecMoveSpeed.y) * 50.0f;   // m/s
			const float expected = g_mom.Speed() * (sprint ? 7.0f : 4.5f);
			if (expected > 1.2f && real < 0.2f * expected) g_blockedT += dt; else g_blockedT = 0.0f;
			if (g_blockedT > 0.25f) g_mom.DecayBlocked(c, dt);
		}

		int ol = (int)std::lround(g_mom.vx * 128.0f), ou = (int)std::lround(-g_mom.vy * 128.0f);
		ol = (int)Sleek::Clamp((float)ol, -128.0f, 128.0f); ou = (int)Sleek::Clamp((float)ou, -128.0f, 128.0f);
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
		Sleek::Config& c = g_cfg; Sleek::PoseConfig& q = g_pose; Sleek::CamConfig& m = g_camc;
		auto onoff = [](bool b) { return b ? "ON" : "OFF"; };
		char b[420];
		auto resetAll = [&]() { g_fov.Reset(); g_mom.Reset(); g_cams.haveAnchor = false; g_epoch++; };
		if (t.empty() || t[0] == "status")
		{
			snprintf(b, sizeof(b), "[SLEEK] tremor=%s ocioso=%s corrida=%s pitchfov=%s inercia=%s agachado=%s | lean=%s inclina=%s cabeca=%s pe=%s ombro=%s(%s,%s) | FOV=+%.1f | %s",
				onoff(c.shakeRewrite), onoff(c.idleShake), onoff(c.runFov), onoff(c.pitchFov), onoff(c.momentum), onoff(c.crouchLoco),
				onoff(q.lean), onoff(q.pitch), onoff(q.headTracking), onoff(q.footIK), onoff(m.enabled), g_cams.side > 0 ? "dir" : "esq", g_cams.zoomLevel == 0 ? "perto" : (g_cams.zoomLevel == 2 ? "longe" : "medio"), g_fovDelta, PoseStatus().c_str());
			return b;
		}
		if (t[0] == "reload") { Load(); resetAll(); return "[SLEEK] config recarregada de SAMP/sleek.ini"; }
		if (t[0] == "reset") { resetAll(); g_idle = Sleek::IdleShake(); g_impact = Sleek::ImpactShake(); return "[SLEEK] estados zerados"; }
		if (t[0] == "off" || t[0] == "on")
		{
			const bool v = t[0] == "on";
			c.shakeRewrite = c.idleShake = c.runFov = c.pitchFov = c.momentum = c.crouchLoco = v;
			q.lean = q.pitch = q.headTracking = q.footIK = v; m.enabled = v;
			if (!v) { resetAll(); g_fovDelta = 0; }
			return v ? "[SLEEK] tudo LIGADO" : "[SLEEK] tudo DESLIGADO";
		}
		if (t[0] == "fov" && t.size() >= 2) { c.fovScale = Sleek::Clamp((float)atof(t[1].c_str()), 0.0f, 2.0f); snprintf(b, sizeof(b), "[SLEEK] escala do FOV = %.2f", c.fovScale); return b; }
		if (t[0] == "braco" && t.size() >= 2)
		{
			Sleek::PoseConfig& pq = PoseCfg(); pq.armCounter = Sleek::Clamp((float)atof(t[1].c_str()), 0.0f, 1.0f);
			snprintf(b, sizeof(b), "[SLEEK] bracos: compensacao do tronco = %.2f (0 = acompanham o tronco, 1 = ficam como na animacao)", pq.armCounter); return b;
		}
		if (t[0] == "pose")
		{
			if (t.size() >= 3 && t[1] == "teste") { SetPoseTest((float)atof(t[2].c_str())); return "[SLEEK] teste de pose: tronco inclinado (0 = desliga). Se nada mexer, o hook nao esta pegando seu personagem: use /sleek pose"; }
			return "[SLEEK-POSE] " + PoseStatus();
		}
		if (t[0] == "camera") return "[SLEEK-CAM] " + CamStatus();
		if (t[0] == "ombro" && t.size() == 1) { g_cams.side = -g_cams.side; return g_cams.side > 0 ? "[SLEEK] ombro DIREITO" : "[SLEEK] ombro ESQUERDO"; }
		if (t[0] == "zoom" && t.size() >= 2)
		{
			if (t[1] == "perto") g_cams.zoomLevel = 0; else if (t[1] == "medio") g_cams.zoomLevel = 1; else if (t[1] == "longe") g_cams.zoomLevel = 2;
			else if (t[1] == "auto") { g_zoomOverride = false; return "[SLEEK] zoom: volta a respeitar o zoom do projeto"; }
			else return "[SLEEK] uso: /sleek zoom perto|medio|longe|auto";
			g_zoomOverride = true; return "[SLEEK] zoom da camera de ombro fixado (use /sleek zoom auto para voltar)";
		}
		if (t.size() >= 2 && (t[1] == "on" || t[1] == "off"))
		{
			const bool v = t[1] == "on"; bool* f = nullptr;
			if (t[0] == "tremor") f = &c.shakeRewrite; else if (t[0] == "ocioso" || t[0] == "idle") f = &c.idleShake;
			else if (t[0] == "corrida" || t[0] == "fovcorrida") f = &c.runFov; else if (t[0] == "pitch" || t[0] == "fovpitch") f = &c.pitchFov;
			else if (t[0] == "inercia" || t[0] == "momentum") f = &c.momentum; else if (t[0] == "agachado") f = &c.crouchLoco;
			else if (t[0] == "lean") f = &q.lean; else if (t[0] == "inclina" || t[0] == "inclinacao") f = &q.pitch;
			else if (t[0] == "cabeca" || t[0] == "head") f = &q.headTracking; else if (t[0] == "pe" || t[0] == "footik" || t[0] == "ik") f = &q.footIK;
			else if (t[0] == "ombro" || t[0] == "camera") f = &m.enabled;
			if (f) { *f = v; if (!v) resetAll(); snprintf(b, sizeof(b), "[SLEEK] %s = %s", t[0].c_str(), onoff(v)); return b; }
		}
		return "[SLEEK] modulos: tremor ocioso corrida pitch inercia agachado lean inclina cabeca pe ombro (on|off) | ombro (troca lado) | zoom perto|medio|longe | fov <esc> | reload | reset";
	}
}
