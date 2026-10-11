#include "ClothesSync.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "../main.h"
#include "game.h"
#include "../net/netgame.h"
#include "Entity/PlayerPedGta.h"
#include "PlayerPedData.h"
#include "PedClothesDesc.h"
#include "playerped.h"

extern CNetGame* pNetGame;

// ---------------------------------------------------------------------------
// NOTAS DE ENGENHARIA (confirmadas lendo a libGTASA.so 2.10 arm64 do projeto)
//
// 0) O tick por frame vem de CGame::Process() (game.cpp). NÃO usar InstallPLT em
//    g_libGTASA+0x66FE58: esse endereço está dentro de CAutomobile::HydraulicControl()
//    (.text), não na GOT. A GOT real de CGame::Process é 0x840198.
//
// 1) CClothes::RebuildPlayer(ped, ignoreFatMuscle):
//      extrai as animações do clump, aborta a task secundária 5 (IK),
//      deleta o RwObject do ped, (se !ignoreFatMuscle) copia CStats[21]/[23]
//      (gordura/músculo DO JOGADOR LOCAL) para o descritor, chama
//      ConstructPedModel(modelo, desc, &gOldClothesState, false), recria o
//      RwObject, devolve as animações e copia desc -> gOldClothesState.
//    => aqui sempre usamos ignoreFatMuscle = true.
//
// 2) ConstructPedModel reconstrói o clump e o TXD do MODELO 0 (CJ) no
//    CModelInfo, não do ped. Textura do corpo (torso/pernas/sapatos/tatuagens)
//    é uma só para todos os peds com modelo 0. Por isso a aplicação em
//    jogadores remotos fica DESLIGADA por padrão (SetRemoteEnabled).
//
// 3) Nomes inválidos podem travar o CreateSkinnedClump. Antes de aplicar,
//    validamos: textura contra a tabela clothesIDs da própria lib e modelo
//    contra o diretório TEXDB\PLAYER.IMG (CDirectory::FindItem).
// ---------------------------------------------------------------------------

namespace
{
    // Offsets da libGTASA.so 2.10 arm64 (conferidos com a tabela de símbolos)
    constexpr uintptr_t OFF_REBUILD_PLAYER    = 0x540CDC; // _ZN8CClothes13RebuildPlayerEP10CPlayerPedb
    constexpr uintptr_t OFF_GET_UPPERCASE_KEY = 0x4D7280; // _ZN7CKeyGen15GetUppercaseKeyEPKc
    constexpr uintptr_t OFF_DIR_FIND_ITEM     = 0x4CA3C8; // _ZN10CDirectory8FindItemEjRjS0_
    constexpr uintptr_t OFF_PLAYER_IMAGE_DIR  = 0xC3EC50; // _ZL15gPlayerImageDir (TEXDB\PLAYER.IMG)
    constexpr uintptr_t OFF_CLOTHES_IDS       = 0x889F38; // clothesIDs[391] = { const char* nome; uint64 id; }
    constexpr int       CLOTHES_IDS_COUNT     = 391;

    constexpr uint64_t  kMinIntervalMs        = 120;      // no máx. 1 rebuild a cada 120 ms
    constexpr uint64_t  kBlockLogEveryMs      = 3000;     // log de "aguardando" no máx. a cada 3 s

    struct Entry
    {
        bool        dirty = false;
        bool        present[CLOTHES_NUM_COMPONENTS] = {};
        ClothesItem items[CLOTHES_NUM_COMPONENTS] = {};
        const void* lastPed   = nullptr;   // ped para o qual aplicamos pela última vez
        const void* lastClump = nullptr;   // clump resultante (muda => precisa reaplicar)
    };

    std::mutex                               s_mutex;
    std::unordered_map<uint16_t, Entry>      s_entries;
    bool                                     s_remoteEnabled = false;
    uint64_t                                 s_nextAllowedMs = 0;

    // diagnóstico (/cj status)
    uint64_t                                 s_processCalls  = 0;
    uint64_t                                 s_applyCount    = 0;
    uint64_t                                 s_lastBlockLogMs = 0;
    std::string                              s_lastBlock     = "-";
    std::string                              s_lastResult    = "-";
    bool                                     s_warnedNoImg   = false;

    std::unordered_set<std::string>          s_knownTextures;   // minúsculas
    bool                                     s_texturesLoaded = false;
    std::unordered_map<std::string, bool>    s_dffCache;

    uint64_t NowMs()
    {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::string Lower(const char* s)
    {
        std::string r(s ? s : "");
        for (auto& c : r) c = (char)std::tolower((unsigned char)c);
        return r;
    }

    // vazio ou só [A-Za-z0-9_]
    bool CleanName(const char* s)
    {
        for (; *s; ++s)
        {
            const unsigned char c = (unsigned char)*s;
            if (!(std::isalnum(c) || c == '_')) return false;
        }
        return true;
    }

    // ---- validação ---------------------------------------------------------
    void LoadKnownTextures()
    {
        s_texturesLoaded = true;
        const uint8_t* tab = reinterpret_cast<const uint8_t*>(g_libGTASA + OFF_CLOTHES_IDS);
        for (int i = 0; i < CLOTHES_IDS_COUNT; ++i)
        {
            const char* name = *reinterpret_cast<const char* const*>(tab + 16 * i);
            if (name && name[0]) s_knownTextures.insert(Lower(name));
        }
        FLog("[CJ] %d nomes de textura carregados de clothesIDs", (int)s_knownTextures.size());
    }

    bool IsKnownTexture(const char* texture)
    {
        if (!s_texturesLoaded) LoadKnownTextures();
        if (s_knownTextures.empty()) return true;           // não conseguiu carregar: não bloqueia
        return s_knownTextures.count(Lower(texture)) != 0;
    }

    bool PlayerImgReady()
    {
        // CDirectory: contagem de itens em +0xC (ver CDirectory::FindItem)
        return *reinterpret_cast<const int32_t*>(g_libGTASA + OFF_PLAYER_IMAGE_DIR + 0xC) > 0;
    }

    bool HasPlayerImgDff(const char* model)
    {
        const std::string key = Lower(model);
        auto it = s_dffCache.find(key);
        if (it != s_dffCache.end()) return it->second;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s.DFF", model);

        using FnKey  = uint32_t (*)(const char*);
        using FnFind = bool (*)(void*, uint32_t, uint32_t&, uint32_t&);
        auto getKey = reinterpret_cast<FnKey>(g_libGTASA + OFF_GET_UPPERCASE_KEY);
        auto find   = reinterpret_cast<FnFind>(g_libGTASA + OFF_DIR_FIND_ITEM);

        uint32_t offset = 0, size = 0;
        const bool ok = find(reinterpret_cast<void*>(g_libGTASA + OFF_PLAYER_IMAGE_DIR),
                             getKey(buf), offset, size);
        s_dffCache[key] = ok;
        return ok;
    }

    // ---- ped ---------------------------------------------------------------
    CPlayerPedGta* ResolvePed(CPlayerPool* pool, uint16_t id, bool isLocal)
    {
        CPlayerPed* wrapper = nullptr;
        if (isLocal)
        {
            CLocalPlayer* lp = pool->GetLocalPlayer();
            if (!lp) return nullptr;
            wrapper = lp->GetPlayerPed();
        }
        else
        {
            CRemotePlayer* rp = pool->GetAt(id);
            if (!rp || !rp->IsActive()) return nullptr;
            wrapper = rp->GetPlayerPed();
        }
        if (!wrapper || !wrapper->m_pPed) return nullptr;
        return static_cast<CPlayerPedGta*>(wrapper->m_pPed);
    }

    bool CanRebuild(CPlayerPedGta* ped, std::string& why)
    {
        if (ped->m_nModelIndex != 0)
        {
            why = "skin=" + std::to_string((int)ped->m_nModelIndex) + " (precisa ser 0)";
            return false;
        }
        if (!ped->m_pRwClump)                       { why = "ped sem clump (RwObject)"; return false; }
        if (!ped->m_pPlayerData)                    { why = "ped sem playerData"; return false; }
        if (!ped->m_pPlayerData->m_pPedClothesDesc) { why = "playerData sem clothesDesc"; return false; }
        if (ped->IsInVehicle())                     { why = "dentro de veiculo"; return false; }
        return true;
    }

    void NoteBlocked(uint16_t id, const std::string& why, uint64_t now)
    {
        s_lastBlock = why;
        if (now - s_lastBlockLogMs >= kBlockLogEveryMs)
        {
            s_lastBlockLogMs = now;
            FLog("[CJ] id=%u aguardando: %s", (unsigned)id, why.c_str());
        }
    }

    // Aplica as peças no descritor e reconstrói o ped. Retorna true se tratou o pedido.
    bool ApplyAndRebuild(CPlayerPedGta* ped, Entry& e, uint16_t id)
    {
        // Diretório do PLAYER.IMG vazio: não bloqueia, só desliga a validação do .dff
        const bool imgReady = PlayerImgReady();
        if (!imgReady && !s_warnedNoImg)
        {
            s_warnedNoImg = true;
            FLog("[CJ] aviso: gPlayerImageDir vazio; validacao de modelo (.dff) desligada");
        }

        CPedClothesDesc* desc = ped->m_pPlayerData->m_pPedClothesDesc;
        int applied = 0;

        for (int c = 0; c < CLOTHES_NUM_COMPONENTS; ++c)
        {
            if (!e.present[c]) continue;

            const ClothesItem& it = e.items[c];
            const char* tex = it.texture[0] ? it.texture : nullptr;
            const char* mdl = it.model[0]   ? it.model   : nullptr;
            const int   slot = CPedClothesDesc::ModelSlotForComponent(c);

            if (!tex && c < CLOTHES_COMPONENT_TATTOO_FIRST)
            {
                FLog("[CJ] id=%u comp=%d: roupa base não pode ser removida; ignorado", (unsigned)id, c);
                continue;
            }
            if (tex && !IsKnownTexture(tex))
            {
                FLog("[CJ] id=%u comp=%d: textura '%s' não existe em clothesIDs; ignorado", (unsigned)id, c, tex);
                continue;
            }
            if (slot >= 0 && tex && !mdl)
            {
                FLog("[CJ] id=%u comp=%d: textura '%s' exige modelo; ignorado", (unsigned)id, c, tex);
                continue;
            }
            if (slot >= 0 && mdl && imgReady && !HasPlayerImgDff(mdl))
            {
                FLog("[CJ] id=%u comp=%d: modelo '%s.dff' não existe em PLAYER.IMG; ignorado", (unsigned)id, c, mdl);
                continue;
            }
            if (slot < 0) mdl = nullptr;                                 // tatuagem: nunca passa modelo

            desc->SetTextureAndModel(tex, mdl, c);
            ++applied;
        }

        e.dirty     = false;
        e.lastPed   = ped;
        e.lastClump = ped->m_pRwClump;

        if (applied == 0)
        {
            s_lastResult = "nenhuma peça válida (veja [CJ] no log)";
            FLog("[CJ] id=%u: nenhuma peça válida; RebuildPlayer não chamado", (unsigned)id);
            return true;
        }

        FLog("[CJ] id=%u: %d peça(s) aplicada(s); RebuildPlayer(ped=%p, ignoreFatMuscle=1)",
             (unsigned)id, applied, (void*)ped);

        using RebuildFn = void (*)(void*, bool);
        reinterpret_cast<RebuildFn>(g_libGTASA + OFF_REBUILD_PLAYER)(static_cast<void*>(ped), true);

        e.lastClump = ped->m_pRwClump;                                   // clump novo, criado pelo rebuild
        ++s_applyCount;
        s_lastResult = "ok: " + std::to_string(applied) + " peça(s)";
        FLog("[CJ] id=%u: RebuildPlayer retornou (clump=%p)", (unsigned)id, (void*)ped->m_pRwClump);
        return true;
    }

    bool TryProcess(CPlayerPool* pool, uint16_t id, bool isLocal, Entry& e, uint64_t now)
    {
        CPlayerPedGta* ped = ResolvePed(pool, id, isLocal);
        if (!ped)
        {
            NoteBlocked(id, "ped nao encontrado", now);
            return false;
        }

        const bool needs = e.dirty || e.lastPed != ped || e.lastClump != ped->m_pRwClump;
        if (!needs) return false;

        std::string why;
        if (!CanRebuild(ped, why))
        {
            NoteBlocked(id, why, now);
            return false;
        }

        if (!ApplyAndRebuild(ped, e, id)) return false;
        s_nextAllowedMs = now + kMinIntervalMs;
        s_lastBlock = "-";
        return true;
    }
}

int CClothesSync::Receive(uint16_t playerId, bool replaceAll, const ClothesItem* items, int count)
{
    if (playerId >= MAX_PLAYERS || count < 0) return 0;
    if (count > 0 && !items) return 0;
    if (count == 0 && !replaceAll) return 0;      // replaceAll com 0 itens = "limpar acessórios/tatuagens"

    std::lock_guard<std::mutex> lock(s_mutex);
    Entry& e = s_entries[playerId];

    if (replaceAll)
    {
        for (int c = 0; c < CLOTHES_NUM_COMPONENTS; ++c)
        {
            e.items[c] = ClothesItem{};
            e.items[c].component = (uint8_t)c;
            // 4..17 (tatuagens/acessórios): ausente na lista => remover
            e.present[c] = (c >= CLOTHES_COMPONENT_TATTOO_FIRST);
        }
    }

    int accepted = 0;
    for (int i = 0; i < count; ++i)
    {
        const ClothesItem& in = items[i];
        if (in.component >= CLOTHES_NUM_COMPONENTS) continue;

        ClothesItem clean = in;
        clean.texture[sizeof(clean.texture) - 1] = '\0';
        clean.model[sizeof(clean.model) - 1]     = '\0';
        if (!CleanName(clean.texture) || !CleanName(clean.model))
        {
            FLog("[CJ] id=%u comp=%d: nome com caractere inválido; ignorado", (unsigned)playerId, (int)in.component);
            continue;
        }
        e.items[in.component]   = clean;
        e.present[in.component] = true;
        ++accepted;
    }

    e.dirty = true;
    FLog("[CJ] recebido id=%u itens=%d aceitos=%d replaceAll=%d", (unsigned)playerId, count, accepted, replaceAll ? 1 : 0);
    return accepted;
}

void CClothesSync::ClearPlayer(uint16_t playerId)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_entries.erase(playerId);
}

void CClothesSync::Reset()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_entries.clear();
    s_nextAllowedMs = 0;
    s_lastBlock = "-";
}

void CClothesSync::SetRemoteEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_remoteEnabled = enabled;
}

void CClothesSync::Process()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    ++s_processCalls;
    if (s_processCalls == 1)
        FLog("[CJ] tick por frame ativo (CClothesSync::Process chamado)");

    if (s_entries.empty() || !g_libGTASA || !pNetGame) return;

    CPlayerPool* pool = pNetGame->GetPlayerPool();
    if (!pool) return;

    const uint64_t now = NowMs();
    if (now < s_nextAllowedMs) return;

    const uint16_t localId = pool->GetLocalPlayerID();

    // jogador local tem prioridade
    auto itLocal = s_entries.find(localId);
    if (itLocal != s_entries.end() && TryProcess(pool, localId, true, itLocal->second, now))
        return;

    if (!s_remoteEnabled) return;

    for (auto& kv : s_entries)
    {
        if (kv.first == localId) continue;
        if (TryProcess(pool, kv.first, false, kv.second, now)) return;   // 1 rebuild por frame
    }
}

std::string CClothesSync::DebugCommand(const std::string& args)
{
    if (!pNetGame || !pNetGame->GetPlayerPool())
        return "[CJ] sem conexão com servidor";

    CPlayerPool* pool = pNetGame->GetPlayerPool();
    const uint16_t localId = pool->GetLocalPlayerID();

    std::istringstream in(args);
    std::string a;
    in >> a;

    if (a.empty() || a == "help")
        return "[CJ] /cj <componente 0-17> <textura|-> [modelo] | /cj status | /cj reset | /cj remote <0|1>";

    if (a == "status")
    {
        unsigned long long frames = 0, applied = 0;
        size_t pend = 0;
        std::string block, last;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            frames = s_processCalls;
            applied = s_applyCount;
            block = s_lastBlock;
            last = s_lastResult;
            for (auto& kv : s_entries) if (kv.second.dirty) ++pend;
        }
        int skin = -1;
        if (CPlayerPedGta* ped = ResolvePed(pool, localId, true)) skin = (int)ped->m_nModelIndex;

        char buf[300];
        std::snprintf(buf, sizeof(buf), "[CJ] frames=%llu pend=%zu aplicados=%llu skin=%d bloqueio=%s ultimo=%s",
                      frames, pend, applied, skin, block.c_str(), last.c_str());
        return buf;
    }

    if (a == "reset")
    {
        ClearPlayer(localId);
        return "[CJ] dados locais apagados (a aparência volta ao normal ao renascer)";
    }

    if (a == "remote")
    {
        int v = 0;
        in >> v;
        SetRemoteEnabled(v != 0);
        return v ? "[CJ] jogadores remotos: LIGADO (experimental)" : "[CJ] jogadores remotos: DESLIGADO";
    }

    char* end = nullptr;
    const long comp = std::strtol(a.c_str(), &end, 10);
    if (!end || *end != '\0' || comp < 0 || comp >= CLOTHES_NUM_COMPONENTS)
        return "[CJ] componente inválido (0-17)";

    std::string tex, mdl;
    in >> tex >> mdl;
    if (tex.empty())
        return "[CJ] faltou a textura (ou '-' para remover)";
    if (tex == "-") tex.clear();
    if (tex.size() >= sizeof(ClothesItem::texture) || mdl.size() >= sizeof(ClothesItem::model))
        return "[CJ] nome muito longo (máx. 31)";

    ClothesItem it{};
    it.component = (uint8_t)comp;
    std::snprintf(it.texture, sizeof(it.texture), "%s", tex.c_str());
    std::snprintf(it.model, sizeof(it.model), "%s", mdl.c_str());
    if (Receive(localId, false, &it, 1) == 0)
        return "[CJ] item rejeitado (use só letras, números e _)";

    return "[CJ] enfileirado; use /cj status e veja o samp_log.txt (linhas [CJ])";
}
