// runstyle.cpp - ver runstyle.h
#include "../main.h"
#include "game.h"
#include "util.h"
#include "runstyle.h"
#include "Entity/CPedGTA.h"
#include "Enums/AnimationEnums.h"
#include "Enums/ePedState.h"
#include "../vendor/armhook/patch.h"     // CHook

#include <atomic>
#include <cstdint>
#include <dlfcn.h>

namespace
{
	// indice enviado pelo servidor -> grupo de caminhada que JA existe no jogo (ids do enum do projeto)
	const int kGroup[] = {
		-1,                    // 0 nativo
		ANIM_GROUP_MAN,        // 1 civil
		ANIM_GROUP_PLAYER,     // 2 cj
		ANIM_GROUP_MUSCULAR,   // 3 musculoso
		ANIM_GROUP_GANG1,      // 4 gangue
		ANIM_GROUP_GANG2,      // 5 gangue2
		ANIM_GROUP_JOGGER,     // 6 corredor
		ANIM_GROUP_FATMAN,     // 7 gordo
		ANIM_GROUP_OLDMAN,     // 8 idoso
		ANIM_GROUP_SHUFFLE,    // 9 arrastado
		ANIM_GROUP_DRUNKMAN,   // 10 bebado
		ANIM_GROUP_SWAT,       // 11 swat
		ANIM_GROUP_WOMAN,      // 12 mulher
		ANIM_GROUP_SEXYWOMAN,  // 13 sexy
		ANIM_GROUP_BUSYWOMAN,  // 14 apressada
		ANIM_GROUP_JOGWOMAN,   // 15 corredora
	};
	constexpr int kN = (int)(sizeof(kGroup) / sizeof(kGroup[0]));
	constexpr uintptr_t kGroupOffset = 0x630;     // o mesmo offset que /reanim ativas le como "grupoAnd"

	bool IsWalkGroup(int g) { return g == ANIM_GROUP_PLAYER || g == ANIM_GROUP_MUSCULAR || g == ANIM_GROUP_PLAYERROCKET || (g >= ANIM_GROUP_MAN && g <= ANIM_GROUP_SKATE); }

	std::atomic<int> g_wanted{ 0 };   // estilo pedido pelo servidor
	std::atomic<int> g_gen{ 0 };

	// estado do Tick (so thread do jogo)
	int g_lastSkin = -1, g_lastGen = -1, g_orig = -1, g_applied = -1;

	void RefreshMoveAnim(CPedGTA* ped)
	{
		// CPed::SetMoveAnim() reaplica a locomocao do grupo atual (se o simbolo existir; senao vale no proximo passo)
		static void (*fn)(void*) = (void(*)(void*))(CHook::lib ? dlsym(CHook::lib, "_ZN4CPed11SetMoveAnimEv") : nullptr);
		if (fn) fn(ped);
	}
}

void RunStyle::OnServer(int style)
{
	if (style < 0 || style >= kN) style = 0;
	g_wanted.store(style);
	g_gen.fetch_add(1);
	FLog("[CORRIDA] servidor definiu o estilo %d", style);
}

void RunStyle::Reset() { g_wanted.store(0); g_gen.fetch_add(1); }

void RunStyle::Tick()
{
	CPedGTA* ped = GamePool_FindPlayerPed();
	if (!ped) return;
	int* grp = (int*)((uintptr_t)ped + kGroupOffset);
	const int cur = *grp;
	if (!IsWalkGroup(cur)) return;                          // so mexe quando o ped esta num grupo de caminhada conhecido
	if (ped->m_fHealth <= 0.0f || ped->m_nPedState == PEDSTATE_DIE || ped->m_nPedState == PEDSTATE_DEAD) return;

	const int skin = (int)ped->m_nModelIndex;
	if (cur != g_applied) g_orig = cur;                     // grupo "natural" da skin (o jogo o repoe ao trocar de skin)
	const int gen = g_gen.load();
	if (skin == g_lastSkin && gen == g_lastGen && cur == g_applied) return;   // nada a fazer
	g_lastSkin = skin; g_lastGen = gen;

	const int w = g_wanted.load();
	const int want = (w > 0 && w < kN) ? kGroup[w] : -1;
	const int target = (want >= 0) ? want : g_orig;
	if (target < 0 || target == cur) { g_applied = (want >= 0) ? target : -1; return; }
	*grp = target;
	g_applied = (want >= 0) ? target : -1;
	RefreshMoveAnim(ped);
	FLog("[CORRIDA] skin %d: grupo %d -> %d (estilo %d)", skin, cur, target, w);
}
