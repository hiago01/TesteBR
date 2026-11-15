// PlayerCarry.cpp - ver PlayerCarry.h
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "net/actorpool.h"
#include "actor.h"
#include "PlayerCarry.h"
#include "PlayerCarryCore.h"
#include "CustomAnim.h"
#include "CarryTuner.h"
#include "CarryArms.h"
#include "IfpLoader.h"
#include <string>
#include "../vendor/raknet/SAMP/SAMPRPC.h"

#include <cmath>
#include <cstring>

extern CNetGame* pNetGame;
extern char* g_pszStorage;

namespace PlayerCarry {

int RPC_Carry = 233;
void ApplyCarrierArms();

namespace {

using namespace carrycore;

State    s_state;                                  // vinculos (RPC e Process rodam na thread de rede/jogo do NetGame)
CPedGTA* s_prepared[kMaxIds];                  // ped do carregado ja preparado (colisao/anim); mudou o ponteiro = ped recriado
uint32_t s_frame = 0;

struct Release { bool used = false; uint16_t id = kNone; float x = 0, y = 0, z = 0, a = 0; };
Release s_release[8];                              // soltaturas pendentes (aplicadas no Process)

CPlayerPed* GetPed(PLAYERID id) {
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool || id >= MAX_PLAYERS) return nullptr;
    CPlayerPed* ped = nullptr;
    if (id == pool->GetLocalPlayerID()) ped = pool->GetLocalPlayer() ? pool->GetLocalPlayer()->GetPlayerPed() : nullptr;
    else if (pool->GetSlotState(id)) { CRemotePlayer* rp = pool->GetAt(id); ped = rp ? rp->GetPlayerPed() : nullptr; }
    if (!ped || !ped->m_pPed || !IsValidGamePed(ped->m_pPed)) return nullptr;
    return ped;
}

// ped GTA de um participante: jogador (local/remoto) ou, para ids >= kActorBase, um Actor do servidor
CPedGTA* GetGtaPed(uint16_t id) {
    if (IsActorId(id)) {
        CActorPool* ap = pNetGame ? pNetGame->GetActorPool() : nullptr;
        CActor* a = ap ? ap->GetAt((PLAYERID)(id - kActorBase)) : nullptr;
        return (a && a->m_pPed && IsValidGamePed(a->m_pPed)) ? a->m_pPed : nullptr;
    }
    CPlayerPed* p = GetPed((PLAYERID)id);
    return p ? p->m_pPed : nullptr;
}

// devolve ao ped o comportamento normal e (se x,y,z validos) coloca-o em pe no ponto seguro
void RestorePed(uint16_t id, bool place, float x, float y, float z, float a) {
    if (id < kMaxIds) s_prepared[id] = nullptr;
    CPedGTA* ped = GetGtaPed(id);
    if (!ped) return;
    CustomAnim::StopAll(ped, 4.0f);                                    // fim do overlay do carregar.ifp
    ped->m_bUsesCollision = true;
    ped->m_vecMoveSpeed = CVector(0.f, 0.f, 0.f);
    if (!place) return;
    const float h = a * 3.14159265f / 180.0f;                          // SA-MP: angulo em graus, frente = (-sin, cos)
    CVector right(std::cos(h), std::sin(h), 0.f), fwd(-std::sin(h), std::cos(h), 0.f), up(0.f, 0.f, 1.f);
    CMatrix m(CVector(x, y, z + 0.3f), right, fwd, up);
    ped->SetMatrix(m);
    ped->UpdateRW();
}

void QueueRelease(uint16_t id, float x, float y, float z, float a) {
    for (auto& r : s_release) if (!r.used) { r.used = true; r.id = id; r.x = x; r.y = y; r.z = z; r.a = a; return; }
    RestorePed(id, true, x, y, z, a);                                  // fila cheia: aplica na hora
}

void ScrCarry(RPCParameters* rpcParams) {
    RakNet::BitStream bs((unsigned char*)rpcParams->input, (rpcParams->numberOfBitsOfData / 8) + 1, false);
    Msg m;
    if (!bs.Read(m.ver) || !bs.Read(m.op) || !bs.Read(m.carrier) || !bs.Read(m.carried) || !bs.Read(m.gen)
        || !bs.Read(m.x) || !bs.Read(m.y) || !bs.Read(m.z) || !bs.Read(m.a)) return;      // pacote incompleto
    if (!Validate(m)) { FLog("[CARRY] RPC %d ignorado (invalido): op=%d c=%d t=%d", RPC_Carry, (int)m.op, (int)m.carrier, (int)m.carried); return; }

    Link stopped;
    switch (s_state.Apply(m, &stopped)) {
        case R_STARTED:   FLog("[CARRY] %d carrega %d (gen %d, op %d)", (int)m.carrier, (int)m.carried, (int)m.gen, (int)m.op); break;
        case R_STOPPED:
            FLog("[CARRY] %d soltou %d", (int)stopped.carrier, (int)stopped.carried);
            QueueRelease(stopped.carried, m.x, m.y, m.z, m.a);
            break;
        default: break;
    }
}

// Bloco customizado SAMP/anims/carregar.ifp (gerado por tools/make_carregar_ifp.py): anims "carregador" e "carregado".
// Se o arquivo nao existir/for invalido, tenta so uma vez e segue com as anims nativas.
bool EnsureIfp() {
    static int s_tried = 0;
    if (CustomIfp::GetAnimNames("carregar")) return true;
    if (s_tried++ > 0 || !g_pszStorage || !g_pszStorage[0]) return false;
    std::string f(g_pszStorage); if (f.back() != '/') f.push_back('/');
    f += "SAMP/anims/carregar.ifp";
    bool ok = CustomAnim::Init(f.c_str(), "carregar");
    FLog("[Carry] %s %s", f.c_str(), ok ? "carregado" : "NAO carregou (usando anims nativas)");
    if (ok) ApplyCarrierArms();
    return ok;
}

// calcula e aplica a matriz do carregado a partir da matriz ATUAL do carregador.
// anchorBone: depois de aplicar, le a posicao REAL do osso da pelve do carregado (GetBonePosition do jogo) e desloca o ped
// para que a pelve fique no ponto do ombro. Isso cancela o deslocamento de raiz da animacao (KO_SHOT_STOM move o osso raiz) e
// qualquer defasagem; sem isso o corpo aparecia a ~1-2 m do ombro, girando.
void PlaceCarried(CPedGTA* carrier, CPedGTA* carried, bool anchorBone) {
    CMatrix& cm = carrier->GetMatrix();
    const float R[3] = { cm.GetRight().x, cm.GetRight().y, cm.GetRight().z };
    const float F[3] = { cm.GetForward().x, cm.GetForward().y, cm.GetForward().z };
    const float U[3] = { cm.GetUp().x, cm.GetUp().y, cm.GetUp().z };
    const float P[3] = { cm.GetPosition().x, cm.GetPosition().y, cm.GetPosition().z };
    for (int i = 0; i < 3; i++) if (!std::isfinite(R[i]) || !std::isfinite(F[i]) || !std::isfinite(U[i]) || !std::isfinite(P[i])) return;
    const Placement p = Place(R, F, U, P);
    CMatrix m(CVector(p.pos[0], p.pos[1], p.pos[2]), CVector(p.right[0], p.right[1], p.right[2]),
              CVector(p.fwd[0], p.fwd[1], p.fwd[2]), CVector(p.up[0], p.up[1], p.up[2]));
    carried->SetMatrix(m);
    carried->m_vecMoveSpeed = CVector(0.f, 0.f, 0.f);
    carried->UpdateRW();
    if (!anchorBone) return;

    carried->UpdateRpHAnim();
    CVector bp(0.f, 0.f, 0.f);
    carried->GetBonePosition(&bp, 1 /*BONE_PELVIS*/, false);
    float dx = p.pos[0] - bp.x, dy = p.pos[1] - bp.y, dz = p.pos[2] - bp.z;
    const bool valid = std::isfinite(bp.x) && std::isfinite(bp.y) && std::isfinite(bp.z) && (std::fabs(bp.x) + std::fabs(bp.y) + std::fabs(bp.z) > 0.01f);
    const float d2 = dx * dx + dy * dy + dz * dz;
    static uint32_t s_log = 0;
    if ((s_log++ % 120) == 0)
        FLog("[CARRY-PLACE] carrier=(%.2f,%.2f,%.2f) F=(%.2f,%.2f,%.2f) U=(%.2f,%.2f,%.2f) alvo=(%.2f,%.2f,%.2f) pelve=(%.2f,%.2f,%.2f)%s corr=%.2f m",
             P[0], P[1], P[2], F[0], F[1], F[2], U[0], U[1], U[2], p.pos[0], p.pos[1], p.pos[2], bp.x, bp.y, bp.z, valid ? "" : " [osso invalido]", std::sqrt(d2));
    if (!valid || d2 > 2.25f * 2.25f || d2 < 1e-6f) return;                     // sem osso valido / correcao absurda: deixa como esta
    CMatrix m2(CVector(p.pos[0] + dx, p.pos[1] + dy, p.pos[2] + dz), CVector(p.right[0], p.right[1], p.right[2]),
               CVector(p.fwd[0], p.fwd[1], p.fwd[2]), CVector(p.up[0], p.up[1], p.up[2]));
    carried->SetMatrix(m2);
    carried->UpdateRW();
}

// UMA fonte de animacao: o IFP novo (carregar.ifp). As anims nativas (CARRY/PED) so entram se o arquivo nao carregou.
// O servidor NAO aplica mais anims no transporte e o cliente ignora as que chegarem (IsInvolved), senao as duas ficam alternando.
// "carregado_<x>": x = m60 m40 m20 0 p20 p40 p60 (dobra da coluna; escolhida no painel /carregarhud)
const char* CarriedAnimName() {
    static const char* n[7] = { "carregado_m60", "carregado_m40", "carregado_m20", "carregado_0", "carregado_p20", "carregado_p40", "carregado_p60" };
    int b = Tune().bend; if (b < -3) b = -3; if (b > 3) b = 3;
    return n[b + 3];
}

void PlayAnims(CPlayerPed* carrier, uint16_t carriedId, CPedGTA* carriedPed, Phase phase) {
    if (EnsureIfp()) {
        const uint16_t fl = CustomAnim::CA_HOLD_LAST | CustomAnim::CA_PARTIAL;
        if (phase == PH_ACTIVE && carrier->m_pPed) CustomAnim::Play(carrier->m_pPed, "carregar", "carregador", fl, 4.0f);
        if (carriedPed) CustomAnim::Play(carriedPed, "carregar", CarriedAnimName(), fl, 4.0f);
        return;
    }
    char nCarrier[] = "CRRY_PRTIAL";
    char nCarried[] = "KO_SHOT_STOM";
    if (phase == PH_ACTIVE && !carrier->IsAnimationPlaying(nCarrier))
        carrier->ApplyAnimation("CRRY_PRTIAL", "CARRY", 4.1f, 0, 0, 0, 1, 1);
    if (IsActorId(carriedId)) {
        CActorPool* ap = pNetGame ? pNetGame->GetActorPool() : nullptr;
        CActor* act = ap ? ap->GetAt((PLAYERID)(carriedId - kActorBase)) : nullptr;
        if (act) act->ApplyAnimation("KO_SHOT_STOM", "PED", 4.1f, 0, 0, 0, 1, 0);
    } else {
        CPlayerPed* cp = GetPed((PLAYERID)carriedId);
        if (cp && !cp->IsAnimationPlaying(nCarried)) cp->ApplyAnimation("KO_SHOT_STOM", "PED", 4.1f, 0, 0, 0, 1, 0);
    }
}

} // namespace

void Register(RakClientInterface* rak)   { rak->RegisterAsRemoteProcedureCall(&RPC_Carry, ScrCarry); }
void Unregister(RakClientInterface* rak) { rak->UnregisterAsRemoteProcedureCall(&RPC_Carry); ClearAll(); }

void ClearAll() {
    for (int i = 0; i < kMaxIds; i++) {
        if (s_prepared[i]) RestorePed((uint16_t)i, false, 0, 0, 0, 0);
        s_prepared[i] = nullptr;
    }
    for (auto& r : s_release) r = Release();
    s_state.Clear();
}

bool LocalIsCarrying() {
    if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
    const PLAYERID me = pNetGame->GetPlayerPool()->GetLocalPlayerID();
    return me < kMaxPlayers && s_state.link[me].used;
}
bool IsInvolved(PLAYERID id) { return id < kMaxPlayers && s_state.Find(id) != nullptr; }
bool IsActorInvolved(PLAYERID actorId) { return actorId < kMaxActors && s_state.carriedBy[kActorBase + actorId] != kNone; }

void OnPlayerLeft(PLAYERID id) {
    if (id >= MAX_PLAYERS) return;
    const Link* l = s_state.Find(id);
    if (l) {
        uint16_t carrier = l->carrier, carried = l->carried;
        s_state.Remove(carrier);
        if (carried != id) RestorePed(carried, false, 0, 0, 0, 0);     // o outro participante volta ao normal (o servidor tambem manda o op 3)
    }
    s_prepared[id] = nullptr;
}

void Process() {
    CarryTuner::LoadSaved();                      // valores salvos pelo painel /carregarhud (1x)
    CarryTuner::ApplyPending();
    // soltaturas pendentes
    for (auto& r : s_release) if (r.used) { RestorePed(r.id, true, r.x, r.y, r.z, r.a); r = Release(); }
    if (s_state.count <= 0) return;
    s_frame++;

    int seen = 0;
    for (int c = 0; c < MAX_PLAYERS && seen < s_state.count; c++) {
        const Link& l = s_state.link[c];
        if (!l.used) continue;
        seen++;

        CPlayerPed* carrier = GetPed(l.carrier);
        CPedGTA* carried = GetGtaPed(l.carried);
        if (!carrier || !carried) { if (l.carried < kMaxIds) s_prepared[l.carried] = nullptr; continue; }   // fora do stream: o vinculo continua guardado
        if (carrier->IsInVehicle()) continue;                                      // o servidor encerra; aqui so nao posiciona

        if (s_prepared[l.carried] != carried) {                                   // 1a vez (ou ped recriado): reaplica
            s_prepared[l.carried] = carried;
            carried->m_bUsesCollision = false;                                    // sem empurrao entre os dois corpos
            CustomAnim::StopAll(carried, 2.0f);                                   // troca de variante (painel): tira a anterior
            PlayAnims(carrier, l.carried, carried, l.phase);
        }
        if (l.phase != PH_ACTIVE) continue;                                       // na preparacao o carregado ainda fica onde esta

        PlaceCarried(carrier->m_pPed, carried, false);

        if ((s_frame % 90) == 0) PlayAnims(carrier, l.carried, carried, l.phase);            // a cada ~1,5 s garante que as animacoes seguem
    }
}

// Roda DEPOIS do PreRender de todos os peds. O ProcessControl do jogo reconstroi a matriz do ped a partir do heading (perde o
// giro de 180 graus) e o carregador pode andar entre o Process de rede e o desenho; por isso o posicionamento que vale e este.
void ApplyCarrierArms() {
    if (!CustomIfp::GetAnimNames("carregar")) return;
    for (int i = 0; i < carryarms::kBones; i++) {
        float q[4]; carryarms::Compute(i, q);
        CustomAnim::SetBoneRotation("carregar", "carregador", carryarms::kBoneId[i], q[0], q[1], q[2], q[3]);
    }
}

void ReapplyAnims() {            // painel: trocou a dobra -> reaplica nos carregados ja ativos
    for (auto& p : s_prepared) p = nullptr;
}

void ProcessAfterPreRender() {
    if (s_state.count <= 0) return;
    int seen = 0;
    for (int c = 0; c < MAX_PLAYERS && seen < s_state.count; c++) {
        const Link& l = s_state.link[c];
        if (!l.used) continue;
        seen++;
        if (l.phase != PH_ACTIVE) continue;
        CPlayerPed* carrier = GetPed(l.carrier);
        CPedGTA* carried = GetGtaPed(l.carried);
        if (!carrier || !carried || carrier->IsInVehicle() || s_prepared[l.carried] != carried) continue;
        PlaceCarried(carrier->m_pPed, carried, true);              // ancora a pelve no ombro e recalcula os ossos
        carried->UpdateRpHAnim();
    }
}

} // namespace PlayerCarry
