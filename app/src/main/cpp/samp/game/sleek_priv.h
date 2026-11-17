#pragma once
// interno do modulo Sleek (compartilhado entre sleek.cpp e sleek_pose.cpp)
#include "sleek_pose_logic.h"
struct CPedGTA;
namespace SleekMove
{
	Sleek::Config&     Cfg();
	Sleek::PoseConfig& PoseCfg();
	Sleek::CamConfig&  CamCfg();
	void     EnsureLoaded();
	uint32_t TeleportEpoch();            // incrementa quando o jogador local "salta" de lugar (teleporte/respawn)
	CPedGTA* PoseEligiblePed();          // jogador local a pe em locomocao normal (sem nado/escalada/queda/ar) ou nullptr
	void     SetPoseFlag(const char* name, bool v, bool& handled);
	std::string PoseStatus();
}
