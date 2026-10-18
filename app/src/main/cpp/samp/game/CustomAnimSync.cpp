// CustomAnimSync.cpp
// Formato do RPC (servidor -> cliente), BitStream, ordem exata:
//   uint16 playerid | uint8 blockLen | char block[blockLen] | uint8 animLen | char anim[animLen]
//   | uint16 flags (CustomAnim::Flags) | float blendDelta
//   blockLen == 0  => limpar as anims customizadas desse player.
// Estado e enviado so na MUDANCA (nao e por tick). Quem entra no stream recebe o estado atual (servidor reenvia em
// OnPlayerStreamIn; o cliente tambem reaplica o cache ao criar o ped).
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "CustomAnimSync.h"
#include "CustomAnim.h"
#include "../vendor/raknet/SAMP/SAMPRPC.h"

#include <cstring>

extern CNetGame* pNetGame;

namespace CustomAnimSync {

int RPC_CustomAnim = 230;   // TODO validar (ver .h)

namespace {

struct State { bool active = false; char block[24] = {}; char anim[24] = {}; uint16_t flags = 0; float delta = 4.0f; };
State s_state[MAX_PLAYERS];

CPlayerPed* GetPed(PLAYERID id) {
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool) return nullptr;
    if (id == pool->GetLocalPlayerID()) return pool->GetLocalPlayer() ? pool->GetLocalPlayer()->GetPlayerPed() : nullptr;
    if (!pool->GetSlotState(id)) return nullptr;
    CRemotePlayer* rp = pool->GetAt(id);
    return rp ? rp->GetPlayerPed() : nullptr;
}

void Apply(PLAYERID id) {
    const State& s = s_state[id];
    CPlayerPed* ped = GetPed(id);
    if (!ped || !ped->m_pPed) return;
    if (s.active) CustomAnim::Play(ped->m_pPed, s.block, s.anim, s.flags, s.delta);
    else          CustomAnim::StopAll(ped->m_pPed);
}

void ScrCustomAnim(RPCParameters* rpcParams) {
    RakNet::BitStream bs((unsigned char*)rpcParams->input, (rpcParams->numberOfBitsOfData / 8) + 1, false);
    PLAYERID id; uint8_t bl = 0, al = 0; char block[24] = {}, anim[24] = {}; uint16_t flags = 0; float delta = 4.0f;

    if (!bs.Read(id) || id >= MAX_PLAYERS) return;
    if (!bs.Read(bl) || bl >= sizeof(block)) return;
    if (bl && !bs.Read(block, bl)) return;
    if (!bs.Read(al) || al >= sizeof(anim)) return;
    if (al && !bs.Read(anim, al)) return;
    if (!bs.Read(flags) || !bs.Read(delta)) return;
    block[bl] = 0; anim[al] = 0;

    State& s = s_state[id];
    if (bl == 0) {
        s = State{};                                           // limpar
    } else {
        if (!CustomAnim::IsCustomBlockName(block)) return;     // servidor nao pode disparar bloco stock por aqui
        s.active = true; s.flags = flags; s.delta = (delta > 0.f && delta < 100.f) ? delta : 4.0f;
        strncpy(s.block, block, sizeof(s.block) - 1); strncpy(s.anim, anim, sizeof(s.anim) - 1);
    }
    Apply(id);     // se o ped ainda nao existe, fica em cache e OnPlayerStreamIn reaplica
}

} // namespace

void Register(RakClientInterface* rak)   { rak->RegisterAsRemoteProcedureCall(&RPC_CustomAnim, ScrCustomAnim); }
void Unregister(RakClientInterface* rak) { rak->UnregisterAsRemoteProcedureCall(&RPC_CustomAnim); }

void OnPlayerStreamIn(PLAYERID id) { if (id < MAX_PLAYERS && s_state[id].active) Apply(id); }

void OnPlayerStreamOut(PLAYERID id) {
    if (id >= MAX_PLAYERS) return;
    if (CPlayerPed* ped = GetPed(id)) if (ped->m_pPed) CustomAnim::StopAll(ped->m_pPed, 100.0f);   // chamar ANTES de destruir o ped
    s_state[id] = State{};
}

void OnPlayerStateChange(PLAYERID id, uint8_t newState) {
    if (id < MAX_PLAYERS && s_state[id].active && newState != PLAYER_STATE_ONFOOT) OnPlayerStreamOut(id);
}

bool DebugSet(PLAYERID id, const char* block, const char* anim, uint16_t flags, float delta) {
    if (id >= MAX_PLAYERS) return false;
    State& s = s_state[id];
    if (!block || !*block) { s = State{}; Apply(id); return true; }
    if (!anim || strlen(block) >= sizeof(s.block) || strlen(anim) >= sizeof(s.anim)) return false;
    if (!CustomAnim::IsCustomBlockName(block)) return false;
    s.active = true; s.flags = flags; s.delta = (delta > 0.f && delta < 100.f) ? delta : 4.0f;
    strncpy(s.block, block, sizeof(s.block) - 1); strncpy(s.anim, anim, sizeof(s.anim) - 1);
    Apply(id);
    return true;
}

void ClearAll() { for (auto& s : s_state) s = State{}; }

} // namespace CustomAnimSync
