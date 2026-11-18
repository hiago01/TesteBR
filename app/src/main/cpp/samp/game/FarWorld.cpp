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
    static uint16_t s_lastScan2 = 0xFFFF;


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
            FLog("[FARWORLD]   perto #%zu: id=%d '%s' dist=%.0f draw=%.0f stream=%d(1=ok) rw=%p col=%p tris=%u caixas=%u esf=%u slot=%d usaColisao=%d visivel=%d lod=%d",
                 k, id, mi->m_modelName, nearest[k].d, mi->m_fDrawDistance, (int)CStreaming::GetInfo(id).m_nLoadState, (void*)b->m_pRwObject,
                 (void*)mi->m_pColModel, mi->m_pColModel ? (unsigned)mi->m_pColModel->GetTriCount() : 0u,
                 (mi->m_pColModel && mi->m_pColModel->m_pColData) ? (unsigned)mi->m_pColModel->m_pColData->m_nNumBoxes : 0u,
                 (mi->m_pColModel && mi->m_pColModel->m_pColData) ? (unsigned)mi->m_pColModel->m_pColData->m_nNumSpheres : 0u,
                 mi->m_pColModel ? (int)mi->m_pColModel->m_nColSlot : -1,
                 (int)b->m_bUsesCollision, (int)b->m_bIsVisible, (int)b->m_nLodIndex);
        }
    }


    // --- Zona distante: distancia de desenho + pre-carga de modelos ----------------------------------------------------------
    // Os objetos reais do SA (postes, casas, cercas...) tem draw 50..150 m porque, no mapa original, cada um tem um LOD (modelo
    // "LODxxx") que aparece de longe. A cidade nova nao tem LODs (lod=-1), entao o objeto so aparecia a poucos metros. Enquanto a
    // camera esta FORA de +-2900 (zona da cidade), a distancia de desenho dos modelos usados la e' aumentada (e restaurada ao
    // voltar ao mapa original, para nao mexer em LS/SF/LV):
    //    draw <= 60 m  -> 150 m | 60 < draw < 300 m -> 300 m | estradas MRoad (18789/18795) -> 450 m | >= 300 m (terreno 599): igual
    // Alem disso, os predios a menos de ~ draw da camera cujo modelo nao esta carregado sao PEDIDOS ao streaming (ate 6 por varredura,
    // no maximo 12 pendentes), porque o pedido nativo so acontece depois que a entidade passa no frustum e some de novo ao girar a
    // camera (efeito "so aparece quando chego muito perto"). Desliga: SAMP/farworld_draw.off e SAMP/farworld_prefetch.off.
    static bool s_zona = false, s_zonaPronta = false;
    static std::vector<std::pair<int, float>> s_drawOrig;     // (id, draw original) dos modelos ajustados
    static std::vector<int> s_pend;                            // ids pedidos por nos e ainda nao carregados
    static int s_cursor = 0;
    static unsigned s_req = 0, s_zonaLog = 0;
    static int s_marc = -1;   // -1 = nao lido

    static bool Marcador(const char* nome)
    {
        std::string p = (g_pszStorage && g_pszStorage[0]) ? std::string(g_pszStorage) : std::string("/storage/emulated/0/Android/data/com.maximo.roleplay/files/");
        if (p.back() != '/') p.push_back('/');
        p += "SAMP/"; p += nome;
        FILE* f = std::fopen(p.c_str(), "r");
        if (!f) return false;
        std::fclose(f); return true;
    }

    static float NovoDraw(int id, float d)
    {
        if (id == 18789 || id == 18795) return d < 450.0f ? 450.0f : d;
        if (d >= 300.0f || d <= 0.0f) return d;
        if (d <= 60.0f) return 150.0f;
        return 300.0f;
    }

    static void ZonaLigar()
    {
        auto* pool = CPools::ms_pBuildingPool;
        if (!pool) return;
        if (!s_zonaPronta)
        {
            s_zonaPronta = true;
            std::set<int> ids;
            const int n = pool->GetSize();
            for (int i = 0; i < n; i++)
            {
                if (pool->IsFreeSlotAtIndex(i)) continue;
                CBuilding* b = pool->GetAt(i);
                if (!b) continue;
                const CVector& p = b->GetPosition();
                if (std::fabs(p.x) > 3000.0f || std::fabs(p.y) > 3000.0f) ids.insert(b->m_nModelIndex);
            }
            for (int id : ids)
            {
                if (id < 0 || id >= CModelInfo::NUM_MODEL_INFOS) continue;
                CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
                if (mi) s_drawOrig.emplace_back(id, mi->m_fDrawDistance);
            }
            FLog("[FARWORLD] zona distante: %zu modelos distintos fora de +-3000", s_drawOrig.size());
        }
        int mudou = 0;
        for (auto& pr : s_drawOrig)
        {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[pr.first];
            if (!mi) continue;
            const float nd = NovoDraw(pr.first, pr.second);
            if (nd != mi->m_fDrawDistance) { mi->m_fDrawDistance = nd; ++mudou; }
        }
        FLog("[FARWORLD] zona distante LIGADA: distancia de desenho ajustada em %d modelos", mudou);
    }

    static void ZonaDesligar()
    {
        for (auto& pr : s_drawOrig)
        {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[pr.first];
            if (mi) mi->m_fDrawDistance = pr.second;
        }
        s_pend.clear();
        FLog("[FARWORLD] zona distante DESLIGADA: distancias de desenho restauradas");
    }

    static void Prefetch(float cx, float cy)
    {
        auto* pool = CPools::ms_pBuildingPool;
        if (!pool) return;
        // pendentes que ja carregaram saem da lista
        for (size_t i = 0; i < s_pend.size();)
        {
            const int st = (int)CStreaming::GetInfo(s_pend[i]).m_nLoadState;
            if (st == (int)eStreamingLoadState::LOADSTATE_LOADED || st == (int)eStreamingLoadState::LOADSTATE_NOT_LOADED) { s_pend[i] = s_pend.back(); s_pend.pop_back(); }
            else ++i;
        }
        if (s_pend.size() >= 12) return;
        const int n = pool->GetSize();
        if (n <= 0) return;
        struct Cand { float d2; int id; };
        std::vector<Cand> cand;
        int fim = s_cursor + 3000; if (fim > n) fim = n;
        for (int i = s_cursor; i < fim; i++)
        {
            if (pool->IsFreeSlotAtIndex(i)) continue;
            CBuilding* b = pool->GetAt(i);
            if (!b) continue;
            const int id = b->m_nModelIndex;
            if (id < 0 || id >= CModelInfo::NUM_MODEL_INFOS) continue;
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
            if (!mi) continue;
            if (CStreaming::GetInfo(id).m_nLoadState != eStreamingLoadState::LOADSTATE_NOT_LOADED) continue;
            const CVector& p = b->GetPosition();
            const float dx = p.x - cx, dy = p.y - cy;
            float lim = mi->m_fDrawDistance; if (lim > 420.0f) lim = 420.0f;
            if (dx * dx + dy * dy > lim * lim) continue;
            cand.push_back({dx * dx + dy * dy, id});
        }
        s_cursor = (fim >= n) ? 0 : fim;
        if (cand.empty()) return;
        std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b2) { return a.d2 < b2.d2; });
        int feitos = 0;
        for (const Cand& c : cand)
        {
            if (feitos >= 6 || s_pend.size() >= 12) break;
            if (std::find(s_pend.begin(), s_pend.end(), c.id) != s_pend.end()) continue;
            CStreaming::RequestModel(c.id, 0);
            s_pend.push_back(c.id); ++feitos; ++s_req;
        }
        if (feitos && (s_req <= 30 || (s_req % 500) < 6))
            FLog("[FARWORLD] pre-carga: +%d pedidos (total %u, pendentes %zu); mais proximo: id=%d a %.0f m", feitos, s_req, s_pend.size(), cand[0].id, std::sqrt(cand[0].d2));
    }

    // 1x por varredura do mundo (primeira chamada de ScanSectorList depois que ms_nCurrentScanCode muda)
    static void PorVarredura()
    {
        if (!s_camPos) return;
        if (s_marc < 0) s_marc = (Marcador("farworld_draw.off") ? 1 : 0) | (Marcador("farworld_prefetch.off") ? 2 : 0);
        const float cx = s_camPos[0], cy = s_camPos[1];
        const float ax = std::fabs(cx), ay = std::fabs(cy);
        const bool entra = ax > 2900.0f || ay > 2900.0f;
        const bool sai = ax < 2600.0f && ay < 2600.0f;
        if (!s_zona && entra) { s_zona = true; if (!(s_marc & 1)) ZonaLigar(); }
        else if (s_zona && sai) { s_zona = false; if (!(s_marc & 1)) ZonaDesligar(); }
        if (s_zona)
        {
            if (!(s_marc & 2)) Prefetch(cx, cy);
            if (ax > 2900.0f || ay > 2900.0f) Probe();
        }
    }

    static void h_ScanSectorList(int x, int y)
    {
        o_ScanSectorList(x, y);

        { const uint16_t c0 = s_scanCode ? *s_scanCode : 0; if (c0 != s_lastScan2) { s_lastScan2 = c0; PorVarredura(); } }

        if (static_cast<unsigned>(x) < 120u && static_cast<unsigned>(y) < 120u) return;   // dentro da grade: nativo ja cuidou de tudo

        const int cx = x < 0 ? 0 : (x > 119 ? 119 : x);
        const int cy = y < 0 ? 0 : (y > 119 ? 119 : y);
        const int key = cy * 120 + cx;

        const uint16_t code = s_scanCode ? *s_scanCode : 0;
        if (code != s_lastCode) { s_lastCode = code; s_nDone = 0; }
        for (int i = 0; i < s_nDone; i++) if (s_done[i] == key) return;
        if (s_nDone < 96) s_done[s_nDone++] = key; else return;

        o_ScanSectorList(cx, cy);

        if ((++s_logTick % 600u) == 1u)
            FLog("[FARWORLD] setor fora da grade (%d,%d) -> borda (%d,%d); setores de borda varridos neste quadro=%d, visiveis=%d",
                 x, y, cx, cy, s_nDone, (int)CRenderer::ms_nNoOfVisibleEntities);
    }


    // --- Patch 3: CEntity::SetupBigBuilding() ------------------------------------------------------------------------------
    // LoadScene marca como "big building" toda instancia sem LOD-filho cujo modelo tem distancia de desenho > 300 (os terrenos/
    // estradas do SA-MP tem 599). Big building NAO entra nas listas de setor (so numa grade 30x30 de 200 m que o renderer varre
    // apenas dentro de +-3000, e sem colisao): por isso o terreno nunca era pedido ao streaming (stream=0 nas sondas) e nao tinha
    // colisao. Para as instancias do mapa novo (fora de +-3000, ou modelos do bloco SA-MP 18631..19999) a marca e' pulada: elas
    // viram predios comuns (setor + colisao + streaming normal). LODs de verdade (refcount != 0) continuam como antes.
    using SetupBigFn = void (*)(CEntityGTA*);
    static SetupBigFn o_SetupBig = nullptr;
    static int s_bigSkipped = 0;
    static void h_SetupBig(CEntityGTA* e)
    {
        if (e)
        {
            const uint8_t lodRefs = *reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(e) + 0x58);
            if (lodRefs == 0)
            {
                const int id = e->m_nModelIndex;
                const CVector& p = e->GetPosition();
                const bool longe = std::fabs(p.x) > 3000.0f || std::fabs(p.y) > 3000.0f;
                if (longe || (id >= 18631 && id < 20000))
                {
                    if (++s_bigSkipped <= 6 || (s_bigSkipped % 1000) == 0)
                        FLog("[FARWORLD] big building ignorado #%d: id=%d pos=(%.0f,%.0f,%.0f) -> vira predio comum (colisao + streaming)", s_bigSkipped, id, p.x, p.y, p.z);
                    return;
                }
            }
        }
        o_SetupBig(e);
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

    const uintptr_t pb = CHook::getSymSoft("_ZN7CEntity16SetupBigBuildingEv");
    if (pb) { CHook::InlineHook((void*)pb, (void*)h_SetupBig, (void**)&o_SetupBig); FLog("[FARWORLD] hook SetupBigBuilding @%p: %s", (void*)pb, o_SetupBig ? "instalado" : "FALHOU"); }
    else FLog("[FARWORLD] simbolo SetupBigBuilding nao encontrado: terrenos (draw>300) seguem como big building");
    s_camPos = reinterpret_cast<const float*>(CHook::getSymSoft("_ZN9CRenderer20ms_vecCameraPositionE"));
    s_scanCode = reinterpret_cast<const volatile uint16_t*>(g_libGTASA + 0xC18350);   // CWorld::ms_nCurrentScanCode
    const uintptr_t p = CHook::getSymSoft("_ZN9CRenderer14ScanSectorListEii");
    if (!p) { FLog("[FARWORLD] simbolo ScanSectorList nao encontrado: predios ficam registrados mas o renderer NAO varre a borda"); return; }
    CHook::InlineHook((void*)p, (void*)h_ScanSectorList, (void**)&o_ScanSectorList);
    FLog("[FARWORLD] hook ScanSectorList @%p: %s", (void*)p, o_ScanSectorList ? "instalado" : "FALHOU");
}
