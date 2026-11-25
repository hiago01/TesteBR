// runstyle.cpp - ver runstyle.h
#include "../main.h"
#include "game.h"
#include "util.h"
#include "runstyle.h"
#include "Entity/CPedGTA.h"
#include "Enums/AnimationEnums.h"
#include "Enums/ePedState.h"
#include "../vendor/armhook/patch.h"     // CHook

#include "../net/netgame.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
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

	std::atomic<int> g_wanted{ 0 };   // estilo pedido pelo servidor para o jogador LOCAL (op 25)
	std::atomic<int> g_gen{ 0 };

	// tabela skin -> estilo (op 26, vale para TODOS os jogadores: cada ped usa o estilo da propria skin)
	std::mutex g_tabMtx;
	std::unordered_map<int, int> g_tab;
	std::atomic<int> g_tabGen{ 0 };
	int TabStyle(int skin) { std::lock_guard<std::mutex> l(g_tabMtx); auto it = g_tab.find(skin); return it == g_tab.end() ? 0 : it->second; }

	// estado do Tick por ped: [0..MAX_PLAYERS-1] = jogadores remotos, [MAX_PLAYERS] = local
	struct St { int lastSkin = -1, lastGen = -1, orig = -1, applied = -1; void* ped = nullptr; };
	St g_st[MAX_PLAYERS + 1];

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

void RunStyle::Reset() { g_wanted.store(0); g_gen.fetch_add(1); { std::lock_guard<std::mutex> l(g_tabMtx); g_tab.clear(); } g_tabGen.fetch_add(1); }

void RunStyle::OnTable(const uint16_t* skins, const uint8_t* styles, int n)
{
	{
		std::lock_guard<std::mutex> l(g_tabMtx);
		g_tab.clear();
		for (int i = 0; i < n; i++) if (styles[i] > 0 && styles[i] < kN) g_tab[skins[i]] = styles[i];
	}
	g_tabGen.fetch_add(1);
	FLog("[CORRIDA] tabela de estilos por skin recebida: %d entradas", n);
}

namespace
{
	// aplica o estilo 'w' (0 = nativo) ao ped; st guarda o grupo natural da skin e o que ja foi aplicado
	void ApplyTo(CPedGTA* ped, St& st, int w, int gen)
	{
		int* grp = (int*)((uintptr_t)ped + kGroupOffset);
		const int cur = *grp;
		if (st.ped != (void*)ped) { st = St(); st.ped = (void*)ped; }      // ped novo (reconectou/recriou): zera
		if (!IsWalkGroup(cur)) return;                                      // so mexe num grupo de caminhada conhecido
		if (ped->m_fHealth <= 0.0f || ped->m_nPedState == PEDSTATE_DIE || ped->m_nPedState == PEDSTATE_DEAD) return;
		const int skin = (int)ped->m_nModelIndex;
		if (cur != st.applied) st.orig = cur;                               // grupo "natural" da skin (o jogo o repoe ao trocar de skin)
		if (skin == st.lastSkin && gen == st.lastGen && cur == st.applied) return;
		st.lastSkin = skin; st.lastGen = gen;
		const int want = (w > 0 && w < kN) ? kGroup[w] : -1;
		const int target = (want >= 0) ? want : st.orig;
		if (target < 0 || target == cur) { st.applied = (want >= 0) ? target : -1; return; }
		*grp = target;
		st.applied = (want >= 0) ? target : -1;
		RefreshMoveAnim(ped);
		FLog("[CORRIDA] skin %d: grupo %d -> %d (estilo %d)", skin, cur, target, w);
	}
}

void RunStyle::Tick()
{
	// jogador local: estilo do op 25
	if (CPedGTA* ped = GamePool_FindPlayerPed()) ApplyTo(ped, g_st[MAX_PLAYERS], g_wanted.load(), g_gen.load());

	// jogadores remotos: estilo da tabela pela skin de cada um (so a cada ~3 frames para ficar leve)
	static int frame = 0;
	if ((++frame % 3) != 0) return;
	CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
	if (!pool) return;
	const int gen = g_tabGen.load() + 1000000;                              // espaco de geracoes separado do local
	for (int id = 0; id < MAX_PLAYERS; id++)
	{
		if (id == pool->GetLocalPlayerID() || !pool->GetSlotState(id)) { g_st[id].ped = nullptr; continue; }
		CRemotePlayer* rp = pool->GetAt(id);
		if (!rp || !rp->IsActive()) continue;
		CPlayerPed* w = rp->GetPlayerPed();
		if (!w || !w->m_pPed) continue;
		CPedGTA* ped = (CPedGTA*)w->m_pPed;
		const int skin = (int)ped->m_nModelIndex;
		ApplyTo(ped, g_st[id], TabStyle(skin), gen);
	}
}
