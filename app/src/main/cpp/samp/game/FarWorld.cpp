#include "FarWorld.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include "../main.h"
#include "../log.h"
#include "../vendor/armhook/patch.h"
#include "Renderer.h"
#include <set>
#include <vector>
#include <cmath>
#include <ctime>
#include <algorithm>
#include "Pools.h"
#include "Streaming.h"
#include "game/Models/ModelInfo.h"
#include "game/Collision/ColModel.h"
#include "game/Entity/Building.h"

namespace
{
    // --- Patch 1: CEntity::Add(const CRect&) @0x4CD5E0 (16 instr.) e CEntity::Remove() @0x4CD8B8 (13 instr.) -----------------
    // Original: xlo=max(esq,-3000) xhi=min(dir,2999)  ylo=max([12],-3000) yhi=min([4],2999)
    // Novo:     os 4 valores passam por clamp(-3000, 2999)  (entidade dentro do mapa: resultado IDENTICO)
#include "FarWorld_words.inc"

    constexpr uintptr_t kAddPatch = 0x4CD5E0, kRemPatch = 0x4CD8B8;

    static bool PatchBlock(const char* nome, uintptr_t off, const uint32_t* orig, const uint32_t* novo, size_t n)
    {
        const uint32_t* cur = reinterpret_cast<const uint32_t*>(g_libGTASA + off);
        bool jaNovo = true, ehOrig = true;
        for (size_t i = 0; i < n; i++) { if (cur[i] != orig[i]) ehOrig = false; if (cur[i] != novo[i]) jaNovo = false; }
        if (jaNovo) { FLog("[FARWORLD] %s ja estava aplicado", nome); return true; }
        if (!ehOrig)
        {
            FLog("[FARWORLD] %s NAO aplicado: bytes em libGTASA+0x%lx diferem do esperado (outra versao do libGTASA?) cur[0]=%08x cur[%zu]=%08x",
                 nome, (unsigned long)off, cur[0], n - 1, cur[n - 1]);
            return false;
        }
        CHook::WriteMemory(g_libGTASA + off, (uintptr_t)novo, n * 4);
        const uint32_t* chk = reinterpret_cast<const uint32_t*>(g_libGTASA + off);
        for (size_t i = 0; i < n; i++) if (chk[i] != novo[i]) { FLog("[FARWORLD] %s: escrita nao confirmou (word %zu)", nome, i); return false; }
        FLog("[FARWORLD] %s aplicado (%zu instrucoes em libGTASA+0x%lx)", nome, n, (unsigned long)off);
        return true;
    }

    // --- Patch 2: CRenderer::ScanSectorList(int x, int y) ----------------------------------------------------------------
    // CWorldScan::ScanWorld chama o callback para QUALQUER (x,y) do frustum (inclusive negativo/>119). Para fora de 0..119 o
    // nativo so varre as listas "repeat" (objetos moveis) e ignora a lista principal (predios/dummies). Aqui, alem do original,
    // varremos o setor de borda correspondente, no maximo 1x por varredura do mundo (ms_nCurrentScanCode).
    using ScanFn = void (*)(int, int);
    static ScanFn o_ScanSectorList = nullptr;
    static const volatile uint16_t* s_scanCode = nullptr;
    static uint16_t s_lastCode = 0xFFFF;
    static int      s_done[96];
    static int      s_nDone = 0;
    static unsigned s_logTick = 0;


    // --- Sonda de diagnostico: a cada ~6 s, com a camera fora de +-3000, loga o estado dos predios proximos ----------------
    static const float* s_camPos = nullptr;   // CRenderer::ms_vecCameraPosition (nativo)

    static std::string Ids(const std::set<int>& st)
    {
        std::string r; int n = 0;
        for (int id : st) { if (n++ >= 10) { r += "..."; break; } char b[40]; CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
            snprintf(b, sizeof b, "%d(%s) ", id, mi ? mi->m_modelName : "?"); r += b; }
        return r;
    }

    static void Probe()
    {
        static unsigned logged = 0; static time_t last = 0;
        if (!s_camPos || logged >= 30) return;
        const time_t now = time(nullptr);
        if (now - last < 6) return;
        last = now; ++logged;
        const float cx = s_camPos[0], cy = s_camPos[1], cz = s_camPos[2];
        auto* pool = CPools::ms_pBuildingPool;
        if (!pool) return;
        int total = 0, far_ = 0, perto = 0, semRw = 0;
        std::set<int> semCol, naoCarregado, comRwNulo;
        struct Near { float d; CBuilding* b; };
        std::vector<Near> nearest;
        const int n = pool->GetSize();
        for (int i = 0; i < n; i++)
        {
            if (pool->IsFreeSlotAtIndex(i)) continue;
            CBuilding* b = pool->GetAt(i);
            if (!b) continue;
            ++total;
            const CVector& p = b->GetPosition();
            if (std::fabs(p.x) > 3000.0f || std::fabs(p.y) > 3000.0f) ++far_;
            const float d = std::hypot(p.x - cx, p.y - cy);
            if (d > 400.0f) continue;
            ++perto;
            const int id = b->m_nModelIndex;
            CBaseModelInfo* mi = (id >= 0 && id < CModelInfo::NUM_MODEL_INFOS) ? CModelInfo::ms_modelInfoPtrs[id] : nullptr;
            if (!mi) continue;
            if (!mi->m_pColModel) semCol.insert(id);
            if (!CStreaming::IsModelLoaded(id)) naoCarregado.insert(id);
            else if (!b->m_pRwObject) { ++semRw; comRwNulo.insert(id); }
            nearest.push_back({d, b});
        }
        std::sort(nearest.begin(), nearest.end(), [](const Near& a, const Near& b2) { return a.d < b2.d; });
        FLog("[FARWORLD] sonda #%u camera=(%.0f,%.0f,%.0f) predios no pool=%d (fora de +-3000=%d) | a <400 m=%d | modelos SEM colisao: %zu [%s] | NAO carregados: %zu [%s] | carregado mas sem RW: %d [%s]",
             logged, cx, cy, cz, total, far_, perto, semCol.size(), Ids(semCol).c_str(), naoCarregado.size(), Ids(naoCarregado).c_str(), semRw, Ids(comRwNulo).c_str());
        for (size_t k = 0; k < nearest.size() && k < 8; k++)
        {
            CBuilding* b = nearest[k].b; const int id = b->m_nModelIndex; CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
            FLog("[FARWORLD]   perto #%zu: id=%d '%s' dist=%.0f draw=%.0f stream=%d(1=ok) rw=%p col=%p tris=%u usaColisao=%d visivel=%d lod=%d",
                 k, id, mi->m_modelName, nearest[k].d, mi->m_fDrawDistance, (int)CStreaming::GetInfo(id).m_nLoadState, (void*)b->m_pRwObject,
                 (void*)mi->m_pColModel, mi->m_pColModel ? (unsigned)mi->m_pColModel->GetTriCount() : 0u,
                 (int)b->m_bUsesCollision, (int)b->m_bIsVisible, (int)b->m_nLodIndex);
        }
    }

    static void h_ScanSectorList(int x, int y)
    {
        o_ScanSectorList(x, y);

        if (static_cast<unsigned>(x) < 120u && static_cast<unsigned>(y) < 120u) return;   // dentro da grade: nativo ja cuidou de tudo

        const int cx = x < 0 ? 0 : (x > 119 ? 119 : x);
        const int cy = y < 0 ? 0 : (y > 119 ? 119 : y);
        const int key = cy * 120 + cx;

        const uint16_t code = s_scanCode ? *s_scanCode : 0;
        if (code != s_lastCode) { s_lastCode = code; s_nDone = 0; }
        for (int i = 0; i < s_nDone; i++) if (s_done[i] == key) return;
        if (s_nDone < 96) s_done[s_nDone++] = key; else return;

        o_ScanSectorList(cx, cy);
        if (s_nDone == 1) Probe();   // 1x por varredura

        if ((++s_logTick % 600u) == 1u)
            FLog("[FARWORLD] setor fora da grade (%d,%d) -> borda (%d,%d); setores de borda varridos neste quadro=%d, visiveis=%d",
                 x, y, cx, cy, s_nDone, (int)CRenderer::ms_nNoOfVisibleEntities);
    }

    static bool Desligado()
    {
        std::string p = (g_pszStorage && g_pszStorage[0]) ? std::string(g_pszStorage) : std::string("/storage/emulated/0/Android/data/com.maximo.roleplay/files/");
        if (p.back() != '/') p.push_back('/');
        p += "SAMP/farworld.off";
        FILE* f = std::fopen(p.c_str(), "r");
        if (!f) return false;
        std::fclose(f);
        return true;
    }
}

void FarWorld::Install()
{
    if (Desligado()) { FLog("[FARWORLD] desligado por SAMP/farworld.off - nada aplicado"); return; }

    const bool a = PatchBlock("CEntity::Add(CRect)", kAddPatch, kAddOrig, kAddNew, 16);
    const bool r = PatchBlock("CEntity::Remove", kRemPatch, kRemOrig, kRemNew, 13);
    // Add e Remove TEM que andar juntos: predio registrado na borda e nao removido deixaria ponteiro solto nas listas.
    if (a != r)
    {
        FLog("[FARWORLD] AVISO: so um dos patches (Add=%d, Remove=%d) foi aplicado - revertendo o outro", (int)a, (int)r);
        if (a) CHook::WriteMemory(g_libGTASA + kAddPatch, (uintptr_t)kAddOrig, 16 * 4);
        if (r) CHook::WriteMemory(g_libGTASA + kRemPatch, (uintptr_t)kRemOrig, 13 * 4);
        return;
    }
    if (!a) return;

    s_camPos = reinterpret_cast<const float*>(CHook::getSymSoft("_ZN9CRenderer20ms_vecCameraPositionE"));
    s_scanCode = reinterpret_cast<const volatile uint16_t*>(g_libGTASA + 0xC18350);   // CWorld::ms_nCurrentScanCode
    const uintptr_t p = CHook::getSymSoft("_ZN9CRenderer14ScanSectorListEii");
    if (!p) { FLog("[FARWORLD] simbolo ScanSectorList nao encontrado: predios ficam registrados mas o renderer NAO varre a borda"); return; }
    CHook::InlineHook((void*)p, (void*)h_ScanSectorList, (void**)&o_ScanSectorList);
    FLog("[FARWORLD] hook ScanSectorList @%p: %s", (void*)p, o_ScanSectorList ? "instalado" : "FALHOU");
}
