// CarBau.cpp - baus da moto 1194 por veiculo. Ver CarBau.h
#include "CarBau.h"

#include "../main.h"
#include "../log.h"
#include "../vendor/armhook/patch.h"
#include "../vendor/raknet/BitStream.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "Entity/CVehicleGTA.h"
#include "Models/ModelInfo.h"
#include "Models/VehicleModelInfo.h"
#include "vehicle.h"
#include "../net/netgame.h"
#include "../net/vehiclepool.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>

extern CNetGame* pNetGame;

namespace CarBau
{
namespace
{
std::mutex                s_mu;
std::unordered_set<int>   s_set;
std::atomic<unsigned>     s_ver{ 1 };
unsigned                  s_applied = 0;
int                       s_frame = 0;
bool                      s_forced = false;
bool                      s_warnedNoExtra = false;

constexpr int8_t kCompNormal = (int8_t)-2;   // 0xFE: "sem pedido" = o jogo sorteia como sempre

int8_t* CompsToUse()
{
    static int8_t* p = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const uintptr_t a = CHook::getSymSoft("_ZN17CVehicleModelInfo13ms_compsToUseE");
        p = reinterpret_cast<int8_t*>(a);
        FLog("[BAU] ms_compsToUse %s", p ? "encontrado" : "NAO encontrado (baus seguem o sorteio do jogo)");
    }
    return p;
}

// geometria do "extra1" do modelo 1194 (os atomicos clonados nos veiculos compartilham esta geometria)
RpGeometry* ExtraGeometry(int model)
{
    CBaseModelInfo* bmi = CModelInfo::GetModelInfo(model);
    if (!bmi) return nullptr;
    CVehicleModelInfo* mi = static_cast<CVehicleModelInfo*>(bmi);
    auto* st = mi->m_pVehicleStruct;
    if (!st || st->m_nNumExtras <= 0) return nullptr;
    RpAtomic* ex = st->m_apExtras[0];
    return ex ? ex->geometry : nullptr;
}

struct Ctx { RpGeometry* g; bool show; int n; };
RpAtomic* AtomicCB(RpAtomic* a, void* d)
{
    Ctx* c = static_cast<Ctx*>(d);
    if (a && a->geometry == c->g) {
        if (c->show) a->object.object.flags |= 0x04; else a->object.object.flags &= (uint8_t)~0x04;   // 0x04 = rpATOMICRENDER
        ++c->n;
    }
    return a;
}

// liga/desliga o bau no veiculo; devolve quantos atomicos do extra existem no clump
int Apply(CVehicleGTA* gv, bool show)
{
    if (!gv || !gv->m_pRwClump || !IsBauModel((int)gv->m_nModelIndex)) return -1;
    RpGeometry* g = ExtraGeometry((int)gv->m_nModelIndex);
    if (!g) return -1;
    Ctx c{ g, show, 0 };
    RpClumpForAllAtomics(gv->m_pRwClump, AtomicCB, &c);
    return c.n;
}

bool Wanted(int vehId)
{
    std::lock_guard<std::mutex> l(s_mu);
    return s_set.count(vehId) != 0;
}

void Sweep()
{
    if (!pNetGame || !pNetGame->GetVehiclePool()) return;
    CVehiclePool* vp = pNetGame->GetVehiclePool();
    for (int id = 0; id < MAX_VEHICLES; ++id) {
        CVehicle* v = vp->GetAt((VEHICLEID)id);
        if (!v || !v->m_pVehicle) continue;
        CVehicleGTA* gv = v->m_pVehicle;
        if (!IsBauModel((int)gv->m_nModelIndex)) continue;
        const int n = Apply(gv, Wanted(id));
        if (n == 0 && !s_warnedNoExtra) { s_warnedNoExtra = true; FLog("[BAU] veiculo id=%d (1194) sem o atomico do extra1: o bau nao pode ser mostrado", id); }
    }
}
} // namespace

void OnPacket(RakNet::BitStream& bs)
{
    uint16_t n = 0;
    if (!bs.Read(n) || n > 400) return;
    std::unordered_set<int> ns;
    for (uint16_t i = 0; i < n; ++i) { uint16_t id = 0; if (!bs.Read(id)) return; ns.insert((int)id); }
    { std::lock_guard<std::mutex> l(s_mu); s_set.swap(ns); }
    s_ver.fetch_add(1);
    FLog("[BAU] lista do servidor: %u moto(s) com bau", (unsigned)n);
}

void Reset()
{
    { std::lock_guard<std::mutex> l(s_mu); s_set.clear(); }
    s_ver.fetch_add(1);
}

void BeforeCreate(int modelId)
{
    if (!IsBauModel(modelId)) return;
    if (modelId == kModel2 && !ExtraGeometry(modelId)) return;   // 1195: so força se o DFF ja carregado tiver extra1 (1194 segue como antes)
    if (int8_t* c = CompsToUse()) { c[0] = 0; c[1] = (int8_t)-1; s_forced = true; }
}

void AfterCreate(int vehId, CVehicleGTA* v)
{
    if (s_forced) {
        s_forced = false;
        if (int8_t* c = CompsToUse()) { c[0] = kCompNormal; c[1] = kCompNormal; }
    }
    if (v) Apply(v, Wanted(vehId));
}

void Tick()
{
    ++s_frame;
    const unsigned ver = s_ver.load();
    if (ver != s_applied) { s_applied = ver; Sweep(); return; }
    if ((s_frame % 120) == 0) Sweep();       // de tempos em tempos (clump recriado, etc.)
}
} // namespace CarBau
