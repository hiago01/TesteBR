// ScooterRider.cpp - ver ScooterRider.h
// Animacoes: bike.ifp do recurso MTA "scooter" (borsuk / SparroW MTA), com as anims renomeadas BIKEv_* -> SCOOT_* para nao
// colidir por nome com as de moto do jogo. O IFP e carregado como bloco customizado (CustomAnim/IfpLoader) e tocado por cima:
// CAnimManager::BlendAnimation (nao parcial) faz as outras animacoes completas (a de moto) sairem em fade-out.
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "game/Entity/CPedGTA.h"
#include "game/Entity/CVehicleGTA.h"

#include "ScooterRider.h"
#include "CustomAnim.h"
#include "CustomAnimSync.h"
#include "IfpLoader.h"

#include <chrono>
#include <cmath>
#include <string>

extern CNetGame* pNetGame;
extern char* g_pszStorage;

namespace ScooterRider {
namespace {

constexpr int         kModel     = 662;        // IDE: mx_scooter
constexpr const char* kBlock     = "scooter";
constexpr const char* kStill     = "SCOOT_Still";
constexpr const char* kRide      = "SCOOT_Ride";
constexpr float       kBlendIn   = 6.0f;
constexpr int64_t     kReassertMs = 500;       // a tarefa de veiculo pode voltar a misturar a anim de moto: reafirma de tempos em tempos

struct St { bool on = false; int anim = -1; int64_t t = 0; };
St s_st[MAX_PLAYERS];
bool s_tried = false, s_ok = false;
int64_t s_last = 0;

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool EnsureLoaded() {
    if (CustomIfp::GetAnimNames(kBlock)) return true;
    if (s_tried) return false;                                   // so tenta uma vez por sessao (falha ja foi logada)
    s_tried = true;
    if (!g_pszStorage || !g_pszStorage[0]) return false;
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    p += "SAMP/anims/scooter.ifp";
    s_ok = CustomAnim::Init(p.c_str(), kBlock);
    FLog("[SCOOTER] %s: %s", p.c_str(), s_ok ? "bloco carregado" : "NAO carregado (animacoes de moto continuam)");
    return s_ok;
}

CPedGTA* PedOf(CPlayerPool* pool, PLAYERID id) {
    CPlayerPed* pp = nullptr;
    if (id == pool->GetLocalPlayerID()) { if (pool->GetLocalPlayer()) pp = pool->GetLocalPlayer()->GetPlayerPed(); }
    else if (pool->GetSlotState(id)) { CRemotePlayer* rp = pool->GetAt(id); if (rp) pp = rp->GetPlayerPed(); }
    return pp ? pp->m_pPed : nullptr;
}

// true se o ped esta DIRIGINDO um patinete (modelo 662)
bool Riding(CPedGTA* ped) {
    if (!ped || !ped->m_pRwClump || !ped->IsInVehicle()) return false;
    CVehicleGTA* v = ped->pVehicle;
    return v->m_nModelIndex == kModel && v->pDriver == ped;
}

} // namespace

void Update() {
    const int64_t now = NowMs();
    if (now - s_last < 100) return;
    s_last = now;
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool) return;

    for (PLAYERID id = 0; id < MAX_PLAYERS; id++) {
        St& s = s_st[id];
        CPedGTA* ped = PedOf(pool, id);
        if (!Riding(ped)) {
            if (s.on) {                                          // desceu (ou saiu do stream): devolve as animacoes normais
                if (ped && ped->m_pRwClump) { CustomAnim::Stop(ped, kBlock, kStill, 6.0f); CustomAnim::Stop(ped, kBlock, kRide, 6.0f); }
                s = St{};
            }
            continue;
        }
        if (!s.on && !EnsureLoaded()) return;                    // sem o IFP nada a fazer (nao tenta de novo)
        const CVector& sp = ped->pVehicle->m_vecMoveSpeed;
        const float spd = std::sqrt(sp.x * sp.x + sp.y * sp.y);
        // histerese: parado abaixo de 0.01, andando acima de 0.02 (nao fica trocando de animacao no limite)
        const int want = (s.anim == 1) ? (spd < 0.01f ? 0 : 1) : (spd > 0.02f ? 1 : 0);
        if (!s.on || want != s.anim || now - s.t >= kReassertMs) {
            if (CustomAnim::Play(ped, kBlock, want ? kRide : kStill, CustomAnim::CA_LOOP, kBlendIn)) {
                if (!s.on || want != s.anim) FLog("[SCOOTER] jogador %d: %s", (int)id, want ? kRide : kStill);
                s.on = true; s.anim = want; s.t = now;
            } else { s.on = true; s.anim = want; s.t = now + 5000; }   // falhou (anim ausente?): nao martela a cada 100 ms
        }
    }
}

} // namespace ScooterRider
