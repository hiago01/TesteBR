#pragma once
#include "../game/game.h"
struct CPed_ { CVector p{5,6,7}; CVector GetPosition() { return p; } };
struct CPlayerPed { CPed_* m_pPed; };
struct CLocalPlayer { CPlayerPed* GetPlayerPed(); };
struct CPlayerPool { CLocalPlayer* GetLocalPlayer(); };
struct CNetGame { CPlayerPool* GetPlayerPool(); };
