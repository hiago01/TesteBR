#include "Weapskin.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "../vendor/bass/bass.h"
#include "game.h"
#include "util.h"
#include "../net/netgame.h"
#include "../net/playerpool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

extern CGame*    pGame;
extern CNetGame* pNetGame;
extern uintptr_t g_libGTASA;

namespace
{
	std::atomic<uint16_t> g_model[MAX_PLAYERS];     // modelo novo (0 = sem skin)
	std::atomic<uint8_t>  g_weapon[MAX_PLAYERS];    // tipo de arma que recebe a skin
	std::atomic<uint16_t> g_applied[MAX_PLAYERS];   // modelo que esta na mao agora por causa da skin (0 = nenhum)
	std::atomic<bool>     g_dirty[MAX_PLAYERS];

	void (*g_origAddWeaponModel)(CPedGTA*, int) = nullptr;
	void (*g_origWeaponFire)(void*, int, void*, int) = nullptr;

	HSAMPLE g_sample = 0;
	uint32_t g_nextTry = 0;                         // ms: nao tenta carregar o wav toda hora se o BASS ainda nao iniciou

	int PlayerOfPed(CPedGTA* ped)
	{
		if (!ped || !pNetGame) return -1;
		CPlayerPool* pp = pNetGame->GetPlayerPool();
		if (!pp) return -1;
		if (ped == GamePool_FindPlayerPed()) return pp->GetLocalPlayerID();
		const PLAYERID id = pp->FindRemotePlayerIDFromGtaPtr(ped);
		return (id == INVALID_PLAYER_ID || id >= MAX_PLAYERS) ? -1 : (int)id;
	}

	CPedGTA* PedOfPlayer(int pid)
	{
		if (!pNetGame || pid < 0 || pid >= MAX_PLAYERS) return nullptr;
		CPlayerPool* pp = pNetGame->GetPlayerPool();
		if (!pp) return nullptr;
		if (pid == pp->GetLocalPlayerID()) return GamePool_FindPlayerPed();
		CRemotePlayer* rp = pp->GetAt((PLAYERID)pid);
		if (!rp || !rp->GetPlayerPed()) return nullptr;
		return rp->GetPlayerPed()->m_pPed;
	}

	bool EnsureLoaded(int model)
	{
		if (!pGame) return false;
		if (pGame->IsModelLoaded(model)) return true;
		pGame->RequestModel((uint16_t)model);
		pGame->LoadRequestedModels();
		return pGame->IsModelLoaded(model);
	}

	uint32_t NowMs()
	{
		timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
	}

	bool EnsureSample()
	{
		if (g_sample) return true;
		const uint32_t now = NowMs();
		if (now < g_nextTry) return false;
		g_nextTry = now + 3000;
		if (BASS_GetDevice() == (DWORD)-1) return false;   // BASS ainda nao iniciado (nao inicia aqui: o voice usa flags proprias)
		if (!g_pszStorage || !g_pszStorage[0]) return false;
		const std::string path = std::string(g_pszStorage) + "SAMP/sons/fnfal.wav";
		g_sample = BASS_SampleLoad(FALSE, path.c_str(), 0, 0, 8, BASS_SAMPLE_OVER_POS);
		FLog("[WEAPSKIN] som %s: %s", path.c_str(), g_sample ? "carregado" : "NAO carregado");
		return g_sample != 0;
	}

	bool PlayShot(CPedGTA* ped)
	{
		if (!EnsureSample()) return false;
		const CVector p = ped->GetPosition();
		float dist = 0.0f, pan = 0.0f;
		if (pGame && pGame->FindPlayerPed() && pGame->FindPlayerPed()->m_pPed)
		{
			CPedGTA* me = pGame->FindPlayerPed()->m_pPed;
			const CVector m = me->GetPosition();
			const float dx = p.x - m.x, dy = p.y - m.y, dz = p.z - m.z;
			dist = sqrtf(dx * dx + dy * dy + dz * dz);
			if (me != ped && dist > 0.5f && me->m_matrix)
			{
				const CVector r = me->m_matrix->GetRight();
				pan = std::max(-0.8f, std::min(0.8f, (dx * r.x + dy * r.y) / dist));
			}
		}
		if (dist > 140.0f) return true;                     // longe demais: silencio (e nao toca o original)
		const float t = 1.0f - dist / 140.0f;
		const float vol = std::min(1.0f, 0.95f * t * t + 0.05f);
		const DWORD ch = BASS_SampleGetChannel(g_sample, FALSE);
		if (!ch) return true;
		BASS_ChannelSetAttribute(ch, BASS_ATTRIB_VOL, vol);
		BASS_ChannelSetAttribute(ch, BASS_ATTRIB_PAN, pan);
		BASS_ChannelPlay(ch, TRUE);
		return true;
	}

	void Hook_AddWeaponModel(CPedGTA* ped, int model)
	{
		const int pid = PlayerOfPed(ped);
		if (pid >= 0)
		{
			const uint16_t sm = g_model[pid].load();
			const uint8_t  w  = g_weapon[pid].load();
			if (sm && w && model == GameGetWeaponModelIDFromWeaponID(w) && EnsureLoaded(sm)) { model = sm; g_applied[pid] = sm; }
			else g_applied[pid] = 0;
		}
		g_origAddWeaponModel(ped, model);
	}

	void Hook_WeaponFire(void* thiz, int weaponType, void* entity, int audioEvent)
	{
		const int pid = PlayerOfPed((CPedGTA*)entity);
		if (pid >= 0 && g_model[pid].load() && g_weapon[pid].load() == weaponType && PlayShot((CPedGTA*)entity))
			return;                                          // som novo tocado: pula o som original
		g_origWeaponFire(thiz, weaponType, entity, audioEvent);
	}
}

void Weapskin_InstallHooks()
{
	CHook::InlineHook("_ZN4CPed14AddWeaponModelEi", &Hook_AddWeaponModel, &g_origAddWeaponModel);
	CHook::InlineHook("_ZN20CAEWeaponAudioEntity10WeaponFireE11eWeaponTypeP9CPhysicali", &Hook_WeaponFire, &g_origWeaponFire);
}

void Weapskin_Reset()
{
	for (int i = 0; i < MAX_PLAYERS; i++) { g_model[i] = 0; g_weapon[i] = 0; g_applied[i] = 0; g_dirty[i] = false; }
}

void Weapskin_OnServerPacket(uint16_t playerId, uint8_t weapon, uint16_t model)
{
	if (playerId >= MAX_PLAYERS) return;
	g_weapon[playerId] = weapon;
	g_model[playerId]  = model;
	g_dirty[playerId]  = true;
}

void Weapskin_Process()
{
	if (!pNetGame) return;
	for (int pid = 0; pid < MAX_PLAYERS; pid++)
	{
		if (!g_dirty[pid].load()) continue;
		CPedGTA* ped = PedOfPlayer(pid);
		g_dirty[pid] = false;
		if (!ped || !g_origAddWeaponModel) continue;       // sem ped (fora do stream): o hook aplica quando a arma for equipada
		const int slot = ped->m_nActiveWeaponSlot;
		if (slot < 0 || slot >= 13) continue;
		const int w = (int)ped->m_aWeapons[slot].dwType;
		const int cur = ped->m_nWeaponModelId;
		if (w <= 0 || cur <= 0) continue;
		const int base = GameGetWeaponModelIDFromWeaponID(w);
		const bool holdingBase = (cur == base);
		const bool holdingSkin = (g_applied[pid].load() != 0 && cur == (int)g_applied[pid].load());
		if (!holdingBase && !holdingSkin) continue;
		const uint16_t sm = g_model[pid].load();
		const bool wantSkin = sm && g_weapon[pid].load() == w;
		if (wantSkin == holdingSkin) continue;             // ja esta certo
		CHook::CallFunction<void>("_ZN4CPed17RemoveWeaponModelEi", ped, cur);
		Hook_AddWeaponModel(ped, base);                     // escolhe skin ou original conforme o estado
	}
}
