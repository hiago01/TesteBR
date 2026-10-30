#include "CustomSkin.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/stat.h>

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "DffPreflight.h"
#include "SkinImageUtil.h"
#include "SkinSlots.h"
#include "ProvadorModel.h"

#ifndef CUSTOMSKIN_HOST_STUBS
#include "../vendor/stb/stb_image.h"      // a implementação (STB_IMAGE_IMPLEMENTATION) está em sprite2d.cpp
#include "../net/netgame.h"
#include "playerped.h"
#include "Streaming.h"
#include "Textures/TextureDatabaseRuntime.h"   // banco de texturas do jogo (fluxo Android)
#include "Entity/PlayerPedGta.h"               // modelo do ped local (LocalPedModel)
#include "CorpTex.h"                           // espelho do snapshot 232 do servidor (autoridade do /provador)
extern CNetGame* pNetGame;
#else
extern void* g_hostModelInfo;                // definidos pelo teste de host
extern bool  g_hostModelLoaded;
extern RpClump* (*g_hostRead)();             // devolve um clump "fresco" (com materiais novos)
extern void     (*g_hostInstall)(RpClump*);  // instala (destrói o molde antigo)
extern RpClump* (*g_hostTemplate)();         // molde instalado
extern bool     (*g_hostCollect)(RpClump*, std::vector<RpMaterial*>&);
extern int   g_hostDestroyedTextures;
extern bool                        g_hostOnline;        // simulação da rede para o teste de host
extern bool                        g_hostSrvHave;
extern uint32_t                    g_hostSrvVer;
extern ProvadorModel::SrvTable     g_hostSrv;
extern int                         g_hostLocalModel;
extern std::vector<std::string>    g_hostChat;
extern int                         g_hostWearCalls;
extern bool                        g_hostSuspend;       // CorpTex::SetLocalPreview
#endif

// ---------------------------------------------------------------------------
// COMO A TROCA DO MODELO 7 FUNCIONA (conferido na libGTASA 2.10 arm64 do projeto)
//   * Modelo 7 = MODEL_MALE01, o ped "placeholder": CStreaming::Update (projeto) o mantém sempre
//     carregado e CStreaming::RemoveModel nunca o descarrega. Por isso a troca é feita NO LUGAR,
//     no CPedModelInfo já carregado.
//   * Sequência (a mesma do engine em RemoveModel + LoadClumpFile):
//       RpClumpStreamRead(bytes do arquivo)        -> clump novo (leitor do próprio jogo)
//       [texturização + materiais ocultos no clump novo, antes de instalar]
//       CPedModelInfo::DeleteRwObject()  @0x45D744 -> destrói o molde antigo e o hit-col
//       CPedModelInfo::SetClump(novo)    @0x45D514 -> base CClumpModelInfo::SetClump (liga HAnim aos
//                                                    atomics skinned, normaliza pesos), SetFrameIds
//                                                    (m_pPedIds) e CreateHitColModelSkinned
//   * Peds já existentes são clones (compartilham geometria/materiais com o molde); só quem for
//     criado depois usa o molde novo. CPed::SetModelIndex chama RpAnimBlendClumpInit na instância.
//   * A troca não persiste: reiniciar volta ao original (kAutoApply refaz na partida seguinte).
//
// APLICAÇÃO AUTOMÁTICA (kAutoApply) E TRAVAS
//   SAMP/skins/disable (arquivo vazio) desliga; SAMP/skins/.applying é criado antes de instalar e
//   apagado ~15 s depois; se existir na partida seguinte a aplicação automática é PULADA.
//
// LIMITES: o DFF não tem os 12 frames-marcador (Smid, Shead, ...) que SetFrameIds procura por
//   nome (o jogo ignora os ausentes); sendo o placeholder, qualquer ped sem modelo carregado usa o 7.
// ---------------------------------------------------------------------------

namespace
{
    // libGTASA.so 2.10 arm64 (conferido na tabela de símbolos)
    constexpr uintptr_t OFF_RW_STREAM_FIND_CHUNK = 0x27C9F4; // _Z17RwStreamFindChunkP8RwStreamjPjS1_
    constexpr uintptr_t OFF_RP_CLUMP_STREAM_READ = 0x2BBA10; // _Z17RpClumpStreamReadP8RwStream
    constexpr uintptr_t OFF_RP_MATERIAL_SET_TEX  = 0x2BE620; // _Z20RpMaterialSetTextureP10RpMaterialP9RwTexture
    constexpr uintptr_t OFF_PED_SET_CLUMP        = 0x45D514; // _ZN13CPedModelInfo8SetClumpEP7RpClump
    constexpr uintptr_t OFF_PED_DELETE_RWOBJECT  = 0x45D744; // _ZN13CPedModelInfo14DeleteRwObjectEv
    constexpr uintptr_t OFF_PED_MODELINFO_VTABLE = 0x82F310; // vtable gravada por CModelInfo::AddPedModel (projeto)
    constexpr uintptr_t OFF_GOT_MODELINFO_PTRS   = 0x850DB8; // GOT -> CModelInfo::ms_modelInfoPtrs (o projeto aponta p/ o array dele)
    constexpr uintptr_t OFF_MODELINFO_CLUMP      = 0x40;     // CClumpModelInfo::m_pRwObject (clump-molde)
    constexpr uintptr_t OFF_GEOM_MATLIST         = 0x28;     // RpGeometry::matList.materials (RpGeometryForAllMaterials)
    constexpr uintptr_t OFF_GEOM_NUMMATERIALS    = 0x30;     // RpGeometry::matList.numMaterials

    constexpr size_t    kMaxFileBytes = 64u * 1024u * 1024u;
    constexpr uint32_t  kRwIdClump    = 0x10;
    constexpr int       kMaxMaterials = 512;
    constexpr uint64_t  kGraveyardMs  = 5000;
    constexpr int       kMaxTexSize   = 1024;   // teto de lado das texturas de skin no celular (potência de 2)

    enum TexMode { TEX_AUTO = 0, TEX_IDX = 1, TEX_NONE = 2 };

    // ---- estado --------------------------------------------------------
    std::mutex                  s_cmdMutex;
    std::vector<std::string>    s_cmds;                 // chat -> thread do jogo

    std::atomic<bool>           s_loadedOk{false};
    std::atomic<bool>           s_applied{false};
    std::atomic<size_t>         s_bytesSize{0};
    std::atomic<int>            s_hiddenCount{0};
    std::atomic<int>            s_materialCount{0};
    std::atomic<int>            s_texMode{TEX_AUTO};

    bool                        s_autoTried = false;
    bool                        s_autoApplied = false;
    bool                        s_autoWarned = false;
    uint64_t                    s_autoWaitStartMs = 0;
    uint64_t                    s_markerClearAtMs = 0;

    std::mutex                  s_infoMutex;
    std::string                 s_info = "nada carregado (use /skin7 load)";

    // só a thread do jogo mexe daqui para baixo
    std::vector<uint8_t>        s_bytes;                // conteúdo validado do arquivo
    DffPreflight::Info          s_parsed;               // inventário do último load válido
    std::vector<uint8_t>        s_hidden;               // 1 = material oculto (índice global)
    std::unordered_map<std::string, int> s_slotVar;     // slot -> última variante numérica aplicada (/skin7 slot)

    // [LOOKX-STATE-BEGIN] ---- sincronização com o servidor (/lookx set|reset) -----------------------------
    // Só a thread do jogo mexe. O envio acontece em LookFlush (1x por frame, no máx. 1 comando a cada 300 ms).
    struct LookOp { int slotId; int est; int var; };       // est: 0 oculto | 1 textura original | 2 variante do pack
    std::vector<LookOp>       s_lookQ;                     // a enviar (1 por slot: o pedido mais recente vence)
    std::vector<LookOp>       s_lookPrevQ;                 // prévia do /provador: só vai ao servidor no "commit"
    bool                      s_lookResetQ   = false;      // enviar "/lookx reset"
    bool                      s_lookInPrev   = false;      // DoSlot chamado por "ui prev"
    bool                      s_lookSuppress = false;      // autoexec: restaura o estado LOCAL sem sobrescrever o do servidor
    uint64_t                  s_lookLastSendMs = 0;
    constexpr uint64_t        kLookDebounceMs = 300;       // o servidor descarta pedidos com menos de 200 ms (LOOK_OP_COOLDOWN)
    constexpr int             kLookMaxVar     = 1023;      // LOOK_MAXVAR_DEFAULT do servidor
    // [LOOKX-STATE-END]
    std::map<int, std::string>  s_matPack;              // material -> pasta/arquivo do pack aplicado (usado por /skin7 save)
    RpClump*                    s_installed = nullptr;  // clump-molde instalado por nós
    std::vector<RwTexture*>     s_enabledTex;           // 1 ref nossa por material (textura "ligada")
    std::vector<uint8_t>        s_origAlpha;
    RwTexture*                  s_hiddenTex = nullptr;  // textura transparente (1 ref nossa, permanente)
    std::vector<std::pair<RwTexture*, uint64_t>> s_graveyard;   // texturas a soltar com atraso

    // ---- prévia da interface /provador (só a thread do jogo mexe) ---------------------------------------
    // Foto do estado ANTES da prévia: ocultos, pasta/arquivo por material, variante por slot e uma ref EXTRA de
    // cada textura "ligada". A ref extra impede que a troca ao vivo (ApplyPack manda a antiga para o cemitério)
    // destrua a textura original do DFF; no cancelar a ref passa de volta para s_enabledTex.
    struct UiBackup
    {
        bool                                 active  = false;
        bool                                 changed = false;   // alguma prévia foi aplicada com sucesso
        std::vector<uint8_t>                 hidden;
        std::map<int, std::string>           matPack;
        std::unordered_map<std::string, int> slotVar;
        std::vector<RwTexture*>              tex;               // 1 ref extra por material (nullptr = sem textura)
    } s_bak;

    // Pedido confirmado localmente que ainda espera o eco do servidor (RPC 232). O backup da prévia continua vivo até lá:
    // se o servidor não gravar o que foi pedido, volta-se exatamente ao estado anterior (RestoreBackup).
    struct Awaiting
    {
        bool                            active = false;
        uint64_t                        startMs = 0;
        uint64_t                        lastEchoMs = 0;
        uint32_t                        verAtCommit = 0;
        std::vector<ProvadorModel::Op>  ops;
    } s_await;
    constexpr uint64_t kAwaitSettleMs = 1500;   // sem novo eco por este tempo (e fila vazia) = o servidor terminou
    constexpr uint64_t kAwaitMaxMs    = 9000;
    constexpr uint64_t kPrepRetryMs   = 3000;   // a preparação automática não repete antes disso
    constexpr int      kPrepMaxAuto   = 3;      // tentativas automáticas falhas antes de desistir (PREPARAR manual continua valendo)

    bool        s_prepGiveUp   = false;
    int         s_prepAutoFail = 0;
    uint64_t    s_prepLastMs   = 0;
    std::string s_prepWhy;
    bool        s_moldStale    = false;         // o molde pode divergir do servidor (rollback/queda): próxima preparação reinstala
    bool        s_wasOnline    = false;
    uint64_t    s_lastSyncMs   = 0;
    uint32_t    s_srvSeenVer   = 0;
    std::vector<uint8_t> s_defHide;                 // materiais ocultos POR PADRAO (slot que o servidor nao lista): ver SyncMoldFromServer
    std::string s_notice;                       // evento assíncrono para a UI mostrar 1x
    uint32_t    s_noticeSeq    = 0;
    std::unordered_map<std::string, std::vector<int>> s_varCache;   // pasta -> variantes (evita varrer o pack a cada publicação)
    bool        s_varCacheOn   = false;

    std::mutex                s_uiMutex;
    CCustomSkin::UiState      s_uiState;                        // cópia publicada para a UI
    std::atomic<uint32_t>     s_uiVersion{0};
    std::atomic<bool>         s_uiWanted{false};                // UI aberta: republica a cada comando
    std::atomic<uint32_t>     s_uiNextId{0};
    uint32_t                  s_uiReqId = 0;                    // resultado do pedido da UI em andamento
    bool                      s_uiReqOk = true;
    std::string               s_uiReqMsg;

    void SetInfo(const std::string& text)
    {
        {
            std::lock_guard<std::mutex> lock(s_infoMutex);
            s_info = text;
        }
        FLog("[SKIN7] %s", text.c_str());
    }

    uint64_t NowMs()
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    }

    // ---- caminhos --------------------------------------------------------
    std::string SkinDir()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        p += "SAMP/skins/";
        return p;
    }

    std::string SkinPath()
    {
        const std::string d = SkinDir();
        return d.empty() ? d : d + "skin.dff";
    }

    bool FileExists(const std::string& path) { return !path.empty() && ::access(path.c_str(), F_OK) == 0; }

    std::string PngIdxPath(int i)
    {
        char b[40];
        std::snprintf(b, sizeof(b), "textures/mat_%02d.png", i);
        return SkinDir() + b;
    }

    // Índice (nome em minúsculas -> caminho real) da pasta textures/. O Android diferencia
    // maiúscula de minúscula no arquivo, o RenderWare não: "Jaqueta" no DFF acha "jaqueta.png".
    std::unordered_map<std::string, std::string> s_pngIndex;

    void RebuildPngIndex()
    {
        s_pngIndex = SkinImg::IndexPngDir(SkinDir() + "textures");
    }

    std::string PngRealPath(const std::string& texName)
    {
        if (texName.empty()) return {};
        const std::string exact = SkinDir() + "textures/" + texName + ".png";
        if (FileExists(exact)) return exact;
        const auto it = s_pngIndex.find(SkinImg::Lower(texName));
        return it != s_pngIndex.end() ? it->second : exact;   // sem achar: devolve o exato (FileExists=false)
    }

    // ===================================================================
    // Acesso ao engine (versão real) / simulado (host)
    // ===================================================================
#ifndef CUSTOMSKIN_HOST_STUBS
    // O projeto aponta o GOT do jogo para o array dele; lemos pelo mesmo caminho que o engine usa.
    void* ModelInfoOf(int id)
    {
        void** arr = *reinterpret_cast<void***>(g_libGTASA + OFF_GOT_MODELINFO_PTRS);
        return arr ? arr[id] : nullptr;
    }

    bool StreamedModelLoaded(int id) { return CStreaming::IsModelLoaded(id); }

    bool WearLocal(int model, std::string& err)
    {
        if (!pNetGame || !pNetGame->GetPlayerPool()) { err = "sem conexão com servidor"; return false; }
        CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
        if (!lp || !lp->GetPlayerPed())               { err = "ped local indisponível"; return false; }
        lp->GetPlayerPed()->SetModelIndex((uint)model);
        return true;
    }

    // Leitor do próprio jogo: stream em memória -> RwStreamFindChunk(clump) -> RpClumpStreamRead
    RpClump* ReadClumpWithEngine(std::vector<uint8_t>& buf, std::string& err)
    {
        RwMemory mem;
        mem.start  = buf.data();
        mem.length = (RwUInt32)buf.size();

        RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
        if (!stream)
        {
            err = "RwStreamOpen falhou";
            return nullptr;
        }

        RwUInt32 length = 0, version = 0;
        const RwBool found = CHook::CallFunction<RwBool>(
            g_libGTASA + OFF_RW_STREAM_FIND_CHUNK, stream, (RwUInt32)kRwIdClump, &length, &version);

        RpClump* clump = nullptr;
        if (found)
            clump = CHook::CallFunction<RpClump*>(g_libGTASA + OFF_RP_CLUMP_STREAM_READ, stream);

        RwStreamClose(stream, &mem);

        if (!found)  { err = "RwStreamFindChunk não achou o clump"; return nullptr; }
        if (!clump)  { err = "RpClumpStreamRead retornou nulo";     return nullptr; }
        return clump;
    }

    void InstallClump(void* modelInfo, RpClump* clump)
    {
        CHook::CallFunction<void>(g_libGTASA + OFF_PED_DELETE_RWOBJECT, modelInfo);
        FLog("[SKIN7] apply: DeleteRwObject ok");
        CHook::CallFunction<void>(g_libGTASA + OFF_PED_SET_CLUMP, modelInfo, clump);
        FLog("[SKIN7] apply: SetClump ok");
    }

    // ---- rede / ped local (real) -------------------------------------------
    bool Online() { return pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED; }
    void SendChat(const char* c) { if (pNetGame) pNetGame->SendChatCommand(c); }
    void PreviewSuspend(bool on) { CorpTex::SetLocalPreview(on); }
    bool SrvSnapshot(ProvadorModel::SrvTable& t, uint32_t& ver)
    {
        std::vector<CorpTex::LocalSlot> v;
        const bool have = CorpTex::LocalSlots(v, ver);
        t = ProvadorModel::FromSnapshot(v);
        return have;
    }
    int LocalPedModel()
    {
        if (!pNetGame || !pNetGame->GetPlayerPool()) return -1;
        CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
        if (!lp || !lp->GetPlayerPed() || !lp->GetPlayerPed()->m_pPed) return -1;
        return (int)static_cast<CPlayerPedGta*>(lp->GetPlayerPed()->m_pPed)->m_nModelIndex;
    }

    RpClump* TemplateClump()
    {
        void* mi = ModelInfoOf(CCustomSkin::kTargetModelId);
        return mi ? *reinterpret_cast<RpClump**>(reinterpret_cast<uintptr_t>(mi) + OFF_MODELINFO_CLUMP) : nullptr;
    }

    RpAtomic* CollectGeomCB(RpAtomic* atomic, void* data)
    {
        auto* geoms = static_cast<std::vector<RpGeometry*>*>(data);
        RpGeometry* g = RpAtomicGetGeometry(atomic);
        if (g && std::find(geoms->begin(), geoms->end(), g) == geoms->end())
            geoms->push_back(g);
        return atomic;
    }

    // Materiais de todas as geometrias do clump (índice global = ordem de geometria, depois do material).
    bool CollectMaterials(RpClump* clump, std::vector<RpMaterial*>& out)
    {
        out.clear();
        if (!clump) return false;
        std::vector<RpGeometry*> geoms;
        RpClumpForAllAtomics(clump, CollectGeomCB, &geoms);
        for (RpGeometry* g : geoms)
        {
            const uintptr_t base = reinterpret_cast<uintptr_t>(g);
            RpMaterial** arr = *reinterpret_cast<RpMaterial***>(base + OFF_GEOM_MATLIST);
            const int n = *reinterpret_cast<int32_t*>(base + OFF_GEOM_NUMMATERIALS);
            if (!arr || n < 0 || n > kMaxMaterials) return false;
            for (int i = 0; i < n; ++i) out.push_back(arr[i]);
        }
        return !out.empty();
    }

    RwTexture* MatTexture(RpMaterial* m)              { return m->texture; }
    void       SetMatTexture(RpMaterial* m, RwTexture* t)
    {
        if (m->texture != t)
            CHook::CallFunction<RpMaterial*>(g_libGTASA + OFF_RP_MATERIAL_SET_TEX, m, t);   // ajusta as refs
    }
    uint8_t    MatAlpha(RpMaterial* m)                { return m->color.alpha; }
    void       SetMatAlpha(RpMaterial* m, uint8_t a)  { m->color.alpha = a; }

    void TexAddRef(RwTexture* t)  { if (t) t->refCount++; }          // refCount em +0x64 (conferido em RpMaterialSetTexture)
    void TexRelease(RwTexture* t) { if (t) RwTextureDestroy(t); }
    std::string TexName(RwTexture* t)  { return t ? std::string(t->name, strnlen(t->name, sizeof(t->name))) : std::string("NULL"); }
    bool TexHasRaster(RwTexture* t)    { return t && t->raster; }

    // RGBA 8-bit -> RwTexture (mesmo caminho do radar em sprite2d.cpp: RwImage -> RwRaster -> RwTexture),
    // com os ajustes para o GLES do Android (os que faltavam e deixavam partes brancas):
    //   1) dimensões potência de 2 e teto kMaxTexSize (PNG de PC costuma ser NPOT/gigante);
    //   2) raster SEM flags de mipmap: SetFromImage só preenche o nível 0, os demais ficariam
    //      vazios (branco/preto à distância) e, com filtro de mip, a textura fica "incompleta";
    //   3) formato nunca paletizado e sempre com pixel-format definido;
    //   4) filtro LINEAR (sem mip) e endereçamento WRAP gravados explicitamente na textura.
    RwTexture* CreateTextureRgba(const uint8_t* px, int w, int h, const char* name)
    {
        if (!px || w <= 0 || h <= 0) return nullptr;

        std::vector<uint8_t> scaled;
        int tw = SkinImg::PotFor(w, kMaxTexSize);
        int th = SkinImg::PotFor(h, kMaxTexSize);
        const uint8_t* src = px;
        if (tw != w || th != h)
        {
            if (!SkinImg::ResizeRgba(px, w, h, tw, th, scaled)) return nullptr;
            src = scaled.data();
            FLog("[SKIN7] textura '%s': %dx%d -> %dx%d (potencia de 2 / teto %d)", name ? name : "?", w, h, tw, th, kMaxTexSize);
        }

        RwImage* image = RwImageCreate(tw, th, 32);
        if (!image) return nullptr;
        if (!RwImageAllocatePixels(image)) { RwImageDestroy(image); return nullptr; }

        for (int y = 0; y < th; ++y)
            std::memcpy(image->cpPixels + (size_t)y * image->stride, src + (size_t)y * tw * 4, (size_t)tw * 4);

        int rw = tw, rh = th, rd = 32, rf = 0;
        RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);

        rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
        if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
        {
            rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
            rf |= rwRASTERFORMAT8888;
            rd = 32;
        }
        if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

        RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
        if (!raster) { RwImageDestroy(image); return nullptr; }
        if (!RwRasterSetFromImage(raster, image))
        {
            if (RwRasterDestroy) RwRasterDestroy(raster);
            RwImageDestroy(image);
            return nullptr;
        }
        RwImageDestroy(image);

        RwTexture* tex = RwTextureCreate(raster);
        if (!tex)
        {
            if (RwRasterDestroy) RwRasterDestroy(raster);
            return nullptr;
        }
        RwTextureSetName(tex, name ? name : "skin7");
        RwTextureSetFilterMode(tex, rwFILTERLINEAR);
        RwTextureSetAddressing(tex, rwTEXTUREADDRESSWRAP);
        FLog("[SKIN7] textura '%s' criada: raster %dx%d depth=%d fmt=0x%x (sem mipmap, filtro linear, wrap)",
             name ? name : "?", rw, rh, rd, rf);
        return tex;
    }

    // Textura pelo nome no banco de texturas do jogo (é onde o Android guarda as texturas do GTA SA:
    // dxt/etc/pvr). GetTexture devolve SEM ref; quem guarda (SetMatTexture) já soma a ref.
    RwTexture* LookupGameTexture(const std::string& name)
    {
        if (name.empty() || name.size() >= 32) return nullptr;
        RwTexture* t = TextureDatabaseRuntime::GetTexture(name.c_str());
        if (!t)
        {
            const std::string lo = SkinImg::Lower(name);
            if (lo != name) t = TextureDatabaseRuntime::GetTexture(lo.c_str());
        }
        return (t && t->raster) ? t : nullptr;
    }

    RwTexture* LoadPngTexture(const std::string& path, const char* name, std::string& err)
    {
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!px)
        {
            err = std::string("stbi_load: ") + stbi_failure_reason();
            return nullptr;
        }
        RwTexture* t = CreateTextureRgba(px, w, h, name);
        stbi_image_free(px);
        if (!t) err = "falha ao criar RwTexture";
        return t;
    }
#else
    // ---- simulação para teste no host -------------------------------------
    void* ModelInfoOf(int) { return g_hostModelInfo; }
    bool  StreamedModelLoaded(int) { return g_hostModelLoaded; }
    bool  WearLocal(int, std::string& err) { ++g_hostWearCalls; err = ""; return true; }
    bool  Online() { return g_hostOnline; }
    void  SendChat(const char* c) { g_hostChat.push_back(c); }
    void  PreviewSuspend(bool on) { g_hostSuspend = on; }
    bool  SrvSnapshot(ProvadorModel::SrvTable& t, uint32_t& ver) { t = g_hostSrv; ver = g_hostSrvVer; return g_hostSrvHave; }
    int   LocalPedModel() { return g_hostLocalModel; }

    RpClump* ReadClumpWithEngine(std::vector<uint8_t>&, std::string&) { return g_hostRead(); }
    void InstallClump(void*, RpClump* c)
    {
        CHook::calls.push_back(OFF_PED_DELETE_RWOBJECT);
        CHook::calls.push_back(OFF_PED_SET_CLUMP);
        g_hostInstall(c);
    }
    RpClump* TemplateClump() { return g_hostTemplate(); }

    bool CollectMaterials(RpClump* c, std::vector<RpMaterial*>& out) { out.clear(); return g_hostCollect(c, out) && !out.empty(); }
    RwTexture* MatTexture(RpMaterial* m) { return m->texture; }
    void TexAddRef(RwTexture* t) { if (t) ++t->refCount; }
    void TexRelease(RwTexture* t) { if (!t) return; if (--t->refCount <= 0) { ++g_hostDestroyedTextures; delete t; } }
    void SetMatTexture(RpMaterial* m, RwTexture* t)
    {
        if (m->texture == t) return;
        if (t) ++t->refCount;
        RwTexture* o = m->texture;
        m->texture = t;
        if (o) TexRelease(o);
    }
    uint8_t MatAlpha(RpMaterial* m) { return m->color.alpha; }
    void SetMatAlpha(RpMaterial* m, uint8_t a) { m->color.alpha = a; }
    std::string TexName(RwTexture* t) { return t ? std::string(t->name) : std::string("NULL"); }
    bool TexHasRaster(RwTexture* t) { return t && t->raster; }

    RwTexture* CreateTextureRgba(const uint8_t*, int, int, const char* name)
    {
        RwTexture* t = new RwTexture{};
        t->refCount = 1;
        t->raster = reinterpret_cast<void*>(0x1);
        std::snprintf(t->name, sizeof(t->name), "%s", name ? name : "");
        return t;
    }
    RwTexture* LoadPngTexture(const std::string& path, const char* name, std::string& err)
    {
        if (!FileExists(path)) { err = "arquivo ausente"; return nullptr; }
        return CreateTextureRgba(nullptr, 1, 1, name);
    }
    RwTexture* LookupGameTexture(const std::string&) { return nullptr; }
#endif

    bool ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            const int e = errno;
            err = (e == ENOENT ? std::string("arquivo não encontrado: ")
                               : std::string("não consegui abrir (") + std::strerror(e) + "): ") + path;
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0 || (size_t)sz > kMaxFileBytes)
        {
            std::fclose(f);
            err = "tamanho inválido (" + std::to_string(sz) + " bytes; máx. 64 MB)";
            return false;
        }
        try { out.resize((size_t)sz); }
        catch (const std::bad_alloc&) { std::fclose(f); err = "sem memória para ler o arquivo"; return false; }

        const size_t got = std::fread(out.data(), 1, out.size(), f);
        std::fclose(f);
        if (got != out.size())
        {
            err = "leitura incompleta do arquivo";
            return false;
        }
        return true;
    }

    RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data)
    {
        ++*static_cast<int*>(data);
        return atomic;
    }

    // ===================================================================
    // Materiais: textura PNG por material, ocultar/mostrar
    // ===================================================================
    RwTexture* EnsureHiddenTex()
    {
        if (!s_hiddenTex)
        {
            uint8_t px[4 * 4 * 4];
            for (int i = 0; i < 16; ++i) { px[i * 4 + 0] = 255; px[i * 4 + 1] = 0; px[i * 4 + 2] = 255; px[i * 4 + 3] = 0; }   // magenta com alfa 0
            s_hiddenTex = CreateTextureRgba(px, 4, 4, "skin7_hidden");
            if (!s_hiddenTex) FLog("[SKIN7] ERRO: não consegui criar a textura transparente");
        }
        return s_hiddenTex;
    }

    // PNG a usar para o material i (ou "" se nenhum existir), conforme o modo.
    std::string PickPng(int i, int mode)
    {
        if (mode == TEX_NONE || i < 0 || i >= (int)s_parsed.materialsList.size()) return {};
        if (mode == TEX_AUTO)
        {
            const std::string real = PngRealPath(s_parsed.materialsList[(size_t)i].texture);
            if (FileExists(real)) return real;
        }
        const std::string idx = PngIdxPath(i);
        return FileExists(idx) ? idx : std::string();
    }

    // Aplica visibilidade do material i (usa as refs que já seguramos: nunca zera a contagem de ninguém).
    void SetMaterialVisible(RpMaterial* m, int i, bool visible)
    {
        RwTexture* want = visible ? s_enabledTex[(size_t)i] : EnsureHiddenTex();
        if (!visible && !want) return;                       // sem textura transparente: não dá para ocultar
        SetMatTexture(m, want);
        SetMatAlpha(m, visible ? s_origAlpha[(size_t)i] : (uint8_t)0);
    }

    // Texturiza o clump (ainda não instalado ou já instalado). reload=true: troca as refs antigas com atraso.
    int TexturePass(const std::vector<RpMaterial*>& mats, bool reload, int mode, std::vector<std::string>& problems)
    {
        const size_t n = mats.size();
        RebuildPngIndex();                                      // PNG achado sem diferenciar maiúscula/minúscula
        if (!reload)
        {
            s_enabledTex.assign(n, nullptr);
            s_origAlpha.assign(n, 255);
        }

        std::unordered_map<std::string, RwTexture*> cache;      // caminho -> textura (ref de criação nossa)
        int assigned = 0;
        const uint64_t now = NowMs();

        for (size_t i = 0; i < n; ++i)
        {
            const std::string path = PickPng((int)i, mode);
            RwTexture* t = nullptr;
            if (!path.empty())
            {
                auto it = cache.find(path);
                if (it != cache.end()) t = it->second;
                else
                {
                    std::string err;
                    char nm[40];
                    const std::string& tn = s_parsed.materialsList[i].texture;
                    std::snprintf(nm, sizeof(nm), "%s", tn.empty() ? "mat" : tn.c_str());
                    t = LoadPngTexture(path, nm, err);
                    if (!t) problems.push_back("material " + std::to_string(i) + " (" + path + "): " + err);
                    cache[path] = t;
                }
            }

            if (reload)
            {
                if (!t) continue;                                   // recarga: sem PNG novo, mantém o que está
                RwTexture* old = s_enabledTex[i];
                TexAddRef(t);
                s_enabledTex[i] = t;
                if (!s_hidden[i]) SetMatTexture(mats[i], t);
                if (old) s_graveyard.emplace_back(old, now + kGraveyardMs);
                ++assigned;
            }
            else
            {
                if (t) { SetMatTexture(mats[i], t); ++assigned; }
                RwTexture* cur = MatTexture(mats[i]);                // PNG novo ou a textura original do DFF

                // Sem PNG e sem textura utilizável vinda do DFF (nula, sem raster ou "marcador" com outro nome):
                // é isto que aparece como parte BRANCA. Tenta o banco de texturas do jogo pelo nome do DFF.
                const DffPreflight::MaterialInfo& mi = s_parsed.materialsList[i];
                if (!t && mi.textured && !mi.texture.empty())
                {
                    const bool nameDiffers = cur && SkinImg::Lower(TexName(cur)) != SkinImg::Lower(mi.texture);
                    if (!cur || !TexHasRaster(cur) || nameDiffers)
                    {
                        RwTexture* db = LookupGameTexture(mi.texture);
                        if (db)
                        {
                            SetMatTexture(mats[i], db);
                            cur = db;
                            FLog("[SKIN7] material %zu: textura '%s' veio do banco de texturas do jogo", i, mi.texture.c_str());
                        }
                        else if (!cur || !TexHasRaster(cur))
                        {
                            // SAMP/skins/fallback_tex (arquivo vazio): no lugar de ficar branco usa a textura "male01"
                            RwTexture* fb = FileExists(SkinDir() + "fallback_tex") ? LookupGameTexture("male01") : nullptr;
                            if (fb)
                            {
                                SetMatTexture(mats[i], fb);
                                cur = fb;
                                FLog("[SKIN7] material %zu: textura '%s' nao achada -> fallback male01", i, mi.texture.c_str());
                            }
                            else
                            {
                                problems.push_back("material " + std::to_string(i) + " textura '" + mi.texture +
                                                   "' NAO ENCONTRADA (sem PNG em textures/, sem textura no DFF/banco do jogo): ficara branco" +
                                                   " (crie SAMP/skins/fallback_tex para usar male01)");
                            }
                        }
                    }
                }
                TexAddRef(cur);
                s_enabledTex[i] = cur;
                s_origAlpha[i] = MatAlpha(mats[i]);
            }
        }

        for (auto& kv : cache) TexRelease(kv.second);                // solta a ref de criação (materiais/slots seguram as deles)
        return assigned;
    }

    int HidePass(const std::vector<RpMaterial*>& mats)
    {
        int hidden = 0;
        for (size_t i = 0; i < mats.size(); ++i)
        {
            const bool vis = !(i < s_hidden.size() && s_hidden[i]);
            SetMaterialVisible(mats[i], (int)i, vis);
            if (!vis) ++hidden;
        }
        return hidden;
    }

    bool InstalledOurs() { return s_installed && TemplateClump() == s_installed; }

    void RecountHidden()
    {
        int h = 0;
        for (uint8_t v : s_hidden) if (v) ++h;
        s_hiddenCount = h;
    }

    // Solta o backup da prévia (confirmar, ou o molde foi reinstalado e as texturas do backup deixaram de valer).
    void DiscardBackup()
    {
        for (RwTexture*& t : s_bak.tex) { TexRelease(t); t = nullptr; }
        s_bak = UiBackup();
        s_lookPrevQ.clear();                                  // cancelar / molde novo: a prévia nunca chegou ao servidor
        PreviewSuspend(false);                                // o ped local volta a mostrar o snapshot do servidor
    }

    // ---- listas de índices: "all", "5", "3-7", "1,4,9-12" ----------------
    bool ParseIndexList(const std::string& text, int maxN, std::vector<int>& out, std::string& err)
    {
        out.clear();
        if (text == "all")
        {
            for (int i = 0; i < maxN; ++i) out.push_back(i);
            return true;
        }
        std::string t = text;
        for (char& c : t) if (c == ' ') c = ',';
        std::stringstream ss(t);
        std::string tok;
        while (std::getline(ss, tok, ','))
        {
            if (tok.empty()) continue;
            int a = 0, b = 0;
            char dash = 0;
            const int got = std::sscanf(tok.c_str(), "%d%c%d", &a, &dash, &b);
            if (got == 1) b = a;
            else if (!(got == 3 && dash == '-')) { err = "lista inválida: '" + tok + "'"; return false; }
            if (a < 0 || b < a || b >= maxN) { err = "índice fora de 0-" + std::to_string(maxN - 1) + ": '" + tok + "'"; return false; }
            for (int i = a; i <= b; ++i) out.push_back(i);
        }
        if (out.empty()) { err = "lista vazia"; return false; }
        return true;
    }

    std::string JoinList(const std::vector<int>& v, size_t maxShown = 24)
    {
        std::string s;
        for (size_t i = 0; i < v.size() && i < maxShown; ++i) { if (i) s += ","; s += std::to_string(v[i]); }
        if (v.size() > maxShown) s += ",...(" + std::to_string(v.size()) + ")";
        return s;
    }

    // ===================================================================
    // Comandos (thread do jogo)
    // ===================================================================
    bool DoLoad(bool engineTest = true)
    {
        s_loadedOk = false;
        s_bytesSize = 0;
        s_bytes.clear();

        if (!g_libGTASA)
        {
            SetInfo("ERRO: libGTASA indisponível");
            return false;
        }

        const std::string path = SkinPath();
        if (path.empty())
        {
            SetInfo("ERRO: raiz de armazenamento (g_pszStorage) ainda indisponível");
            return false;
        }

        std::vector<uint8_t> buf;
        std::string err;
        if (!ReadFile(path, buf, err))
        {
            SetInfo("ERRO: " + err);
            return false;
        }

        // 1) validação pura (sem engine)
        DffPreflight::Info info;
        if (!DffPreflight::Parse(buf.data(), buf.size(), info, err))
        {
            SetInfo("ERRO no DFF: " + err);
            return false;
        }

        FLog("[SKIN7] preflight OK: %s | %zu bytes | RW %s | frames=%d atomics=%d geom=%d tris=%llu verts=%llu mats=%d | skin: ossos=%d usados=%d pesos=%d | HAnim ids=%zu",
             path.c_str(), buf.size(), DffPreflight::VersionString(info.stamp).c_str(),
             info.frames, info.atomics, info.geoms,
             (unsigned long long)info.tris, (unsigned long long)info.verts, info.materials,
             info.bones, info.usedBones, info.maxWeights, info.hanimIds.size());
        const std::vector<std::string> warnings = DffPreflight::Warnings(info);
        for (const std::string& w : warnings)
            FLog("[SKIN7] AVISO: %s", w.c_str());

        // 2) teste do leitor do jogo (clump descartado em seguida; /skin7 apply lê de novo)
        int atomics = info.atomics;
        if (engineTest)
        {
            RpClump* test = ReadClumpWithEngine(buf, err);
            if (!test)
            {
                SetInfo("ERRO no leitor do jogo: " + err);
                return false;
            }
            atomics = 0;
            RpClumpForAllAtomics(test, CountAtomicCB, &atomics);
            RpClumpDestroy(test);
        }

        s_parsed = std::move(info);
        s_bytes = std::move(buf);
        s_bytesSize = s_bytes.size();
        s_hidden.resize(s_parsed.materialsList.size(), 0);   // preserva o que já estava oculto
        s_materialCount = (int)s_parsed.materialsList.size();
        RecountHidden();
        s_loadedOk = true;

        char msg[300];
        std::snprintf(msg, sizeof(msg),
            "OK: arquivo válido e legível pelo jogo (atomics=%d, tris=%llu, mats=%zu, ossos=%d, %zu aviso(s)); use /skin7 apply",
            atomics, (unsigned long long)s_parsed.tris, s_parsed.materialsList.size(), s_parsed.bones, warnings.size());
        SetInfo(msg);
        return true;
    }

    void DoUnload()
    {
        s_bytes.clear();
        s_bytes.shrink_to_fit();
        s_bytesSize = 0;
        s_loadedOk = false;
        if (!s_lookSuppress) { s_lookQ.clear(); s_lookPrevQ.clear(); s_lookResetQ = true; }   // servidor volta ao visual base (/lookx reset)
        SetInfo(s_applied ? "arquivo descarregado da memória; o modelo 7 CONTINUA com o skin.dff (reinicie o jogo para voltar ao original)"
                          : "descarregado");
    }

    void DoApply()
    {
        if (!s_loadedOk && !DoLoad(false))
            return;   // DoLoad já informou o erro

        void* mi = ModelInfoOf(CCustomSkin::kTargetModelId);
        if (!mi)
        {
            SetInfo("ERRO: o modelo 7 não tem model info");
            return;
        }
        if (*reinterpret_cast<uintptr_t*>(mi) != g_libGTASA + OFF_PED_MODELINFO_VTABLE)
        {
            SetInfo("ERRO: o modelo 7 não é um CPedModelInfo (vtable inesperada)");
            return;
        }
        if (!StreamedModelLoaded(CCustomSkin::kTargetModelId))
        {
            SetInfo("ERRO: o modelo 7 não está carregado no streaming");
            return;
        }

        std::string err;
        RpClump* fresh = ReadClumpWithEngine(s_bytes, err);
        if (!fresh)
        {
            SetInfo("ERRO ao ler o clump para instalar: " + err);
            return;
        }

        // texturização + materiais ocultos NO CLUMP NOVO, antes de instalar
        std::vector<RwTexture*> oldEnabled;
        oldEnabled.swap(s_enabledTex);
        std::vector<RpMaterial*> mats;
        int texCount = 0, hiddenCount = 0;
        const int mode = s_texMode.load();
        if (CollectMaterials(fresh, mats) && mats.size() == s_parsed.materialsList.size())
        {
            std::vector<std::string> problems;
            texCount = TexturePass(mats, false, mode, problems);
            for (const std::string& p : problems) FLog("[SKIN7] AVISO PNG: %s", p.c_str());
            s_hidden.resize(mats.size(), 0);
            hiddenCount = HidePass(mats);
            RecountHidden();
            FLog("[SKIN7] apply: %zu materiais; %d com PNG (modo %s); %d ocultos", mats.size(), texCount,
                 mode == TEX_AUTO ? "auto" : (mode == TEX_IDX ? "idx" : "none"), hiddenCount);
        }
        else
        {
            FLog("[SKIN7] AVISO: contagem de materiais do engine (%zu) difere do DFF (%zu); sem PNG/ocultação neste apply",
                 mats.size(), s_parsed.materialsList.size());
        }

        FLog("[SKIN7] apply: modelInfo=%p; DeleteRwObject + SetClump(%p)", mi, (void*)fresh);
        InstallClump(mi, fresh);
        s_installed = fresh;
        s_matPack.clear();                                    // o molde novo volta às texturas do DFF
        s_slotVar.clear();
        DiscardBackup();                                      // prévia da UI: as texturas guardadas eram do molde antigo

        for (RwTexture* t : oldEnabled) TexRelease(t);        // refs do apply anterior (os materiais antigos seguram as deles)

        s_applied = true;
        char msg[260];
        std::snprintf(msg, sizeof(msg),
            "APLICADO ao modelo 7 (%d PNG, %d oculto(s)): peds criados a partir de agora usam o skin.dff (teste com /skin7 wear)",
            texCount, hiddenCount);
        SetInfo(msg);
    }

    int RunAutoexec();   // definida mais abaixo (mesmo namespace)

    void DoWear(bool on)
    {
        // "wear" sozinho já basta: se o molde ainda não foi instalado, faz load + apply + autoexec antes de vestir
        if (on && !s_applied)
        {
            FLog("[SKIN7] wear: molde ainda não aplicado -> load + apply + autoexec antes de vestir");
            DoApply();
            if (s_applied) RunAutoexec();
        }
        const int model = on ? CCustomSkin::kTargetModelId : 0;
        FLog("[SKIN7] wear: ped local -> modelo %d (antes)", model);
        std::string err;
        if (!WearLocal(model, err))
        {
            SetInfo("ERRO ao trocar o ped local: " + err);
            return;
        }
        FLog("[SKIN7] wear: ped local -> modelo %d (depois, sem crash)", model);
        SetInfo(on ? "ped local agora usa o modelo 7" : "ped local voltou ao modelo 0");
    }

    // ---- /skin7 mats -------------------------------------------------------
    void DoMats()
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        RebuildPngIndex();

        std::vector<RpMaterial*> eng;
        const bool haveEngine = InstalledOurs() && CollectMaterials(TemplateClump(), eng) && eng.size() == s_parsed.materialsList.size();

        const std::string csvPath = SkinDir() + "materials.csv";
        FILE* csv = std::fopen(csvPath.c_str(), "wb");
        if (csv)
            std::fputs("indice,textura,mascara,R,G,B,A,texturizado,triangulos,estado,png_real,existe_real,png_indice,existe_indice,engine_textura,engine_raster\n", csv);

        int hidden = 0, realFound = 0, idxFound = 0;
        for (const DffPreflight::MaterialInfo& m : s_parsed.materialsList)
        {
            const size_t gi = (size_t)(&m - &s_parsed.materialsList[0]);
            const bool off = gi < s_hidden.size() && s_hidden[gi];
            if (off) ++hidden;
            const std::string real = PngRealPath(m.texture);
            const std::string idx = PngIdxPath((int)gi);
            const bool hasReal = FileExists(real), hasIdx = FileExists(idx);
            if (hasReal) ++realFound;
            if (hasIdx) ++idxFound;
            std::string engTex = "-", engRaster = "-";
            if (haveEngine)
            {
                RwTexture* t = MatTexture(eng[gi]);
                engTex = TexName(t);
                engRaster = TexHasRaster(t) ? "sim" : "nao";
            }
            const std::string realRel = m.texture.empty() ? "" : "textures/" + m.texture + ".png";
            char idxRel[40];
            std::snprintf(idxRel, sizeof(idxRel), "textures/mat_%02d.png", (int)gi);

            FLog("[SKIN7-MAT] %02d '%s' mask='%s' rgba=%u,%u,%u,%u tex=%d tris=%u estado=%s | png_real=%s png_idx=%s | engine=%s raster=%s",
                 (int)gi, m.texture.c_str(), m.mask.c_str(), m.r, m.g, m.b, m.a, m.textured ? 1 : 0, m.tris,
                 off ? "OFF" : "on", hasReal ? "sim" : "nao", hasIdx ? "sim" : "nao", engTex.c_str(), engRaster.c_str());
            if (csv)
                std::fprintf(csv, "%d,%s,%s,%u,%u,%u,%u,%d,%u,%s,%s,%s,%s,%s,%s,%s\n",
                             (int)gi, m.texture.c_str(), m.mask.c_str(), m.r, m.g, m.b, m.a, m.textured ? 1 : 0, m.tris,
                             off ? "OFF" : "on", realRel.c_str(), hasReal ? "sim" : "nao", idxRel, hasIdx ? "sim" : "nao",
                             engTex.c_str(), engRaster.c_str());
        }
        if (csv) std::fclose(csv);

        char msg[300];
        std::snprintf(msg, sizeof(msg), "%zu materiais (%d oculto(s)); PNG real=%d idx=%d; %s",
                      s_parsed.materialsList.size(), hidden, realFound, idxFound,
                      csv ? "CSV: SAMP/skins/materials.csv (e linhas [SKIN7-MAT] no log)" : "não consegui gravar o CSV (veja o log)");
        SetInfo(msg);
    }

    // ---- /skin7 mat ... ----------------------------------------------------
    void ApplyHiddenToTemplate(std::string& note)
    {
        if (!InstalledOurs()) { note = "estado guardado; entra em vigor no /skin7 apply"; return; }
        std::vector<RpMaterial*> mats;
        if (!CollectMaterials(TemplateClump(), mats) || mats.size() != s_hidden.size() || mats.size() != s_enabledTex.size())
        {
            note = "ERRO: molde instalado não bate com o inventário (rode /skin7 apply de novo)";
            return;
        }
        HidePass(mats);
        note = "aplicado ao vivo";
    }

    void DoMat(const std::string& rest)
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        const int n = (int)s_parsed.materialsList.size();
        s_hidden.resize((size_t)n, 0);

        std::istringstream in(rest);
        std::string sub;
        in >> sub;
        std::string args;
        std::getline(in, args);
        while (!args.empty() && args.front() == ' ') args.erase(args.begin());

        if (sub == "list" || sub.empty())
        {
            std::vector<int> off;
            for (int i = 0; i < n; ++i) if (s_hidden[(size_t)i]) off.push_back(i);
            SetInfo("materiais ocultos (" + std::to_string(off.size()) + "/" + std::to_string(n) + "): " +
                    (off.empty() ? std::string("nenhum") : JoinList(off, 60)));
            return;
        }
        if (sub == "reset")
        {
            std::fill(s_hidden.begin(), s_hidden.end(), 0);
            RecountHidden();
            std::string note; ApplyHiddenToTemplate(note);
            SetInfo("todos os materiais visíveis (" + note + ")");
            return;
        }
        if (sub != "off" && sub != "on" && sub != "only")
        {
            SetInfo("uso: /skin7 mat list | reset | off <lista> | on <lista> | only <lista>   (lista: 5 | 3-7 | 1,4,9-12 | all)");
            return;
        }

        std::vector<int> idx;
        std::string err;
        if (!ParseIndexList(args, n, idx, err)) { SetInfo("ERRO: " + err); return; }

        if (sub == "only")
        {
            std::fill(s_hidden.begin(), s_hidden.end(), 1);
            for (int i : idx) s_hidden[(size_t)i] = 0;
        }
        else
        {
            for (int i : idx) s_hidden[(size_t)i] = (sub == "off") ? 1 : 0;
        }
        RecountHidden();

        std::string note;
        ApplyHiddenToTemplate(note);
        SetInfo(std::string(sub == "off" ? "ocultados " : (sub == "on" ? "mostrados " : "só visíveis: ")) + JoinList(idx) +
                " | ocultos agora: " + std::to_string(s_hiddenCount.load()) + "/" + std::to_string(n) + " | " + note);
    }

    // ---- /skin7 tex ... | png ---------------------------------------------
    void DoTex(const std::string& arg)
    {
        if (arg == "auto")      s_texMode = TEX_AUTO;
        else if (arg == "idx")  s_texMode = TEX_IDX;
        else if (arg == "none") s_texMode = TEX_NONE;
        else { SetInfo("uso: /skin7 tex auto|idx|none"); return; }
        SetInfo("modo de PNG = " + arg + " (vale no próximo /skin7 apply ou /skin7 png)");
    }

    void DoPng()
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        if (!InstalledOurs()) { SetInfo("ERRO: nosso molde não está instalado (use /skin7 apply primeiro)"); return; }

        std::vector<RpMaterial*> mats;
        if (!CollectMaterials(TemplateClump(), mats) || mats.size() != s_parsed.materialsList.size() || mats.size() != s_enabledTex.size())
        {
            SetInfo("ERRO: molde instalado não bate com o inventário (rode /skin7 apply de novo)");
            return;
        }
        s_hidden.resize(mats.size(), 0);
        std::vector<std::string> problems;
        const int n = TexturePass(mats, true, s_texMode.load(), problems);
        for (const std::string& p : problems) FLog("[SKIN7] AVISO PNG: %s", p.c_str());
        SetInfo(std::to_string(n) + " PNG(s) recarregado(s) ao vivo" +
                (problems.empty() ? std::string() : " | " + std::to_string(problems.size()) + " falha(s): veja o log"));
    }

    // ===================================================================
    // Pack de texturas por slot: /skin7 pack | slots | slot | set
    //   <raiz>/SAMP/skins/pack/<pasta>/<n>.png      (tools_skin_pack.py converte os .jpeg em .png)
    //   O cliente compila o stb_image com STBI_ONLY_PNG: arquivo .jpeg NÃO carrega.
    // ===================================================================
    std::string PackDir()
    {
        const std::string d = SkinDir();
        return d.empty() ? d : d + "pack/";
    }

    bool IsDirPath(const std::string& path)
    {
        struct stat st {};
        return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    // Variantes numéricas "<n>.png" existentes em pack/<folder>, em ordem crescente.
    std::vector<int> PackVariants(const std::string& folder)
    {
        std::vector<int> v;
        DIR* d = ::opendir((PackDir() + folder).c_str());
        if (!d) return v;
        while (dirent* e = ::readdir(d))
        {
            const std::string fn = e->d_name;
            if (fn.size() < 5 || SkinImg::Lower(fn.substr(fn.size() - 4)) != ".png") continue;
            const std::string stem = fn.substr(0, fn.size() - 4);
            if (stem.empty() || stem.size() > 6) continue;
            bool digits = true;
            for (char c : stem) if (!std::isdigit((unsigned char)c)) { digits = false; break; }
            if (digits) v.push_back(std::atoi(stem.c_str()));
        }
        ::closedir(d);
        std::sort(v.begin(), v.end());
        return v;
    }

    // Arquivo do pack para rel ("tshirts/1/34"). Só aceita caminho relativo sem "..".
    std::string PackFile(const std::string& rel, std::string& err)
    {
        if (rel.empty() || rel[0] == '/' || rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos)
        {
            err = "caminho inválido (use pasta/n, ex.: tshirts/1/34)";
            return {};
        }
        const std::string base = PackDir() + rel;
        for (const char* ext : { ".png", ".PNG" })
            if (FileExists(base + ext)) return base + ext;
        for (const char* ext : { ".jpeg", ".jpg", ".JPEG", ".JPG" })
            if (FileExists(base + ext)) { err = "'" + rel + "' é JPEG: converta com tools_skin_pack.py (o cliente só lê PNG)"; return {}; }
        err = "não achei pack/" + rel + ".png";
        return {};
    }

    std::vector<int> SlotMaterials(const SkinSlots::Slot& sl)
    {
        std::vector<int> r;
        for (size_t i = 0; i < s_parsed.materialsList.size(); ++i)
            if (SkinSlots::TextureListHas(sl.texture, s_parsed.materialsList[i].texture.c_str())) r.push_back((int)i);
        return r;
    }

    // Troca a textura dos materiais idx pelo PNG do pack (ao vivo, no molde instalado) e os deixa visíveis.
    // Mesmas regras de referência do TexturePass(reload): 1 ref nossa por material em s_enabledTex,
    // a antiga vai para o cemitério com atraso.
    bool ApplyPack(const std::vector<int>& idx, const std::string& rel, std::string& note)
    {
        if (!InstalledOurs()) { note = "ERRO: nosso molde não está instalado (use /skin7 apply primeiro)"; return false; }
        std::vector<RpMaterial*> mats;
        if (!CollectMaterials(TemplateClump(), mats) || mats.size() != s_parsed.materialsList.size() || mats.size() != s_enabledTex.size())
        {
            note = "ERRO: molde instalado não bate com o inventário (rode /skin7 apply de novo)";
            return false;
        }
        s_hidden.resize(mats.size(), 0);
        if (idx.empty()) { note = "ERRO: nenhum material para aplicar"; return false; }

        std::string err;
        const std::string path = PackFile(rel, err);
        if (path.empty()) { note = "ERRO: " + err; return false; }

        char nm[40];
        const std::string& tn = s_parsed.materialsList[(size_t)idx[0]].texture;
        std::snprintf(nm, sizeof(nm), "%s", tn.empty() ? "mat" : tn.c_str());
        RwTexture* t = LoadPngTexture(path, nm, err);
        if (!t) { note = "ERRO ao carregar pack/" + rel + ": " + err; return false; }

        const uint64_t now = NowMs();
        for (int i : idx)
        {
            RwTexture* old = s_enabledTex[(size_t)i];
            TexAddRef(t);
            s_enabledTex[(size_t)i] = t;
            s_hidden[(size_t)i] = 0;
            SetMaterialVisible(mats[(size_t)i], i, true);
            if (old) s_graveyard.emplace_back(old, now + kGraveyardMs);
        }
        TexRelease(t);                       // solta a ref de criação (slots + materiais seguram as deles)
        for (int i : idx) s_matPack[i] = rel;
        RecountHidden();
        note = "pack/" + rel + ".png -> materiais " + JoinList(idx);
        return true;
    }

    // ---- /skin7 pack : confere a pasta pack/ (quantos .png e quantos .jpeg que o cliente não lê) -----
    void ScanPack(const std::string& rel, int depth, int& png, int& jpg, std::vector<std::string>& lines)
    {
        DIR* d = ::opendir((PackDir() + rel).c_str());
        if (!d) return;
        int p = 0, j = 0;
        std::vector<std::string> subs;
        while (dirent* e = ::readdir(d))
        {
            const std::string fn = e->d_name;
            if (fn == "." || fn == "..") continue;
            const std::string full = PackDir() + (rel.empty() ? "" : rel + "/") + fn;
            if (IsDirPath(full)) { subs.push_back(fn); continue; }
            const size_t dot = fn.rfind('.');
            if (dot == std::string::npos) continue;
            const std::string ext = SkinImg::Lower(fn.substr(dot));
            if (ext == ".png") ++p;
            else if (ext == ".jpg" || ext == ".jpeg") ++j;
        }
        ::closedir(d);
        if (p || j)
        {
            char b[160];
            std::snprintf(b, sizeof(b), "%s: png=%d jpeg=%d%s", rel.c_str(), p, j, j ? "  <-- converta (tools_skin_pack.py)" : "");
            lines.push_back(b);
        }
        png += p; jpg += j;
        if (depth >= 3) return;
        std::sort(subs.begin(), subs.end());
        for (const std::string& sub : subs) ScanPack(rel.empty() ? sub : rel + "/" + sub, depth + 1, png, jpg, lines);
    }

    void DoPack()
    {
        const std::string dir = PackDir();
        if (dir.empty() || !IsDirPath(dir))
        {
            SetInfo("pack ausente: copie as pastas para " + (dir.empty() ? std::string("<raiz>/SAMP/skins/pack/") : dir));
            return;
        }
        int png = 0, jpg = 0;
        std::vector<std::string> lines;
        ScanPack("", 0, png, jpg, lines);
        for (const std::string& l : lines) FLog("[SKIN7-PACK] %s", l.c_str());
        char msg[260];
        std::snprintf(msg, sizeof(msg), "pack: %d PNG ok, %d JPEG (o cliente NÃO lê JPEG; converta)%s; detalhes [SKIN7-PACK] no log",
                      png, jpg, jpg ? " <-- ATENÇÃO" : "");
        SetInfo(msg);
    }

    // ---- /skin7 slots : slot -> material(is) do DFF -> pasta do pack --------------------------
    void DoSlots()
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        const std::string csvPath = SkinDir() + "slots.csv";
        FILE* csv = std::fopen(csvPath.c_str(), "wb");
        if (csv) std::fputs("slot,materiais,textura_dff,pasta_pack,variantes,evidencia,estado,variante_atual\n", csv);

        std::vector<uint8_t> covered(s_parsed.materialsList.size(), 0);
        int withMat = 0, withVar = 0, hyp = 0;
        for (int k = 0; k < SkinSlots::kCount; ++k)
        {
            const SkinSlots::Slot& sl = SkinSlots::kSlots[k];
            const std::vector<int> mi = SlotMaterials(sl);
            for (int i : mi) covered[(size_t)i] = 1;
            const std::vector<int> var = PackVariants(sl.folder);
            if (!mi.empty()) ++withMat;
            if (!var.empty()) ++withVar;
            if (sl.ev == 0) ++hyp;
            int off = 0;
            for (int i : mi) if ((size_t)i < s_hidden.size() && s_hidden[(size_t)i]) ++off;
            const char* st = mi.empty() ? "SEM_MATERIAL" : (off == 0 ? "on" : (off == (int)mi.size() ? "OFF" : "misto"));
            const auto cur = s_slotVar.find(sl.name);
            const int curV = cur != s_slotVar.end() ? cur->second : 0;
            FLog("[SKIN7-SLOT] %-10s mat=%s pasta=%s variantes=%zu ev=%d estado=%s atual=%d%s", sl.name,
                 mi.empty() ? "-" : JoinList(mi, 12).c_str(), sl.folder, var.size(), sl.ev, st, curV,
                 sl.ev == 0 ? "  (HIPOTESE)" : "");
            if (csv)
                std::fprintf(csv, "%s,%s,%s,%s,%zu,%d,%s,%d\n", sl.name, mi.empty() ? "" : JoinList(mi, 40).c_str(),
                             sl.texture, sl.folder, var.size(), sl.ev, st, curV);
        }
        if (csv) std::fclose(csv);

        std::vector<int> orphan;
        for (size_t i = 0; i < covered.size(); ++i) if (!covered[i]) orphan.push_back((int)i);
        std::string names;
        for (size_t k = 0; k < orphan.size() && k < 40; ++k)
        {
            if (k) names += ", ";
            names += std::to_string(orphan[k]) + "=" + s_parsed.materialsList[(size_t)orphan[k]].texture;
        }
        FLog("[SKIN7-SLOT] materiais SEM slot (%zu): %s", orphan.size(), names.c_str());

        char msg[300];
        std::snprintf(msg, sizeof(msg), "%d slots: %d com material no DFF, %d com variantes no pack, %d hipótese(s); %zu material(is) sem slot; CSV: SAMP/skins/slots.csv (log [SKIN7-SLOT])",
                      SkinSlots::kCount, withMat, withVar, hyp, orphan.size());
        SetInfo(msg);
    }

    // Exclusividade por categoria (SkinSlots::kExclusiveGroups): oculta os OUTROS slots do mesmo grupo do slot `sl`,
    // com a mesma ocultação do "slot <outro> off" (s_hidden + ApplyHiddenToTemplate). Só chamar DEPOIS de o slot novo
    // estar visível, para uma variante inválida nunca derrubar a peça anterior. Não depende de s_slotVar (que guarda só a
    // última variante e continua preenchido após um "off"): o estado real é s_hidden. Retorna quantos materiais ocultou.
    int HideSameCategory(const SkinSlots::Slot& sl, std::string& note)
    {
        const std::vector<int> victims = SkinSlots::ExclusiveVictims(sl, [](const SkinSlots::Slot& s) { return SlotMaterials(s); });
        s_hidden.resize(s_parsed.materialsList.size(), 0);
        int changed = 0;
        for (int i : victims)
            if ((size_t)i < s_hidden.size() && !s_hidden[(size_t)i]) { s_hidden[(size_t)i] = 1; ++changed; }
        if (!changed) return 0;
        RecountHidden();
        ApplyHiddenToTemplate(note);
        return changed;
    }

    // Cobertura (SkinSlots::kCoverPieces/kCoverTargets): índices dos materiais do DFF com esse nome de textura.
    std::vector<int> MaterialsByTexture(const char* tex)
    {
        std::vector<int> r;
        for (size_t i = 0; i < s_parsed.materialsList.size(); ++i)
            if (SkinSlots::TextureListHas(tex, s_parsed.materialsList[i].texture.c_str())) r.push_back((int)i);
        return r;
    }

    // Materiais que HOJE estão totalmente cobertos por uma peça visível (camisa, calça/bermuda, tênis): pele do corpo e roupa íntima.
    // Função do estado atual de s_hidden: não guarda nada, então vale também depois de save/autoexec.
    std::vector<int> CoveredByGarments()
    {
        std::vector<int> r;
        std::vector<const SkinSlots::Box*> worn;
        for (const SkinSlots::Box& p : SkinSlots::kCoverPieces)
        {
            const std::vector<int> idx = MaterialsByTexture(p.name);
            if (idx.empty()) continue;                            // o DFF carregado não tem essa peça
            bool vis = true;
            for (int i : idx) if ((size_t)i < s_hidden.size() && s_hidden[(size_t)i]) vis = false;
            if (vis) worn.push_back(&p);
        }
        if (worn.empty()) return r;
        for (const SkinSlots::Box& t : SkinSlots::kCoverTargets)
        {
            bool covered = false;
            for (const SkinSlots::Box* p : worn) if (SkinSlots::PieceCovers(*p, t)) { covered = true; break; }
            if (!covered) continue;
            for (int i : MaterialsByTexture(t.name)) r.push_back(i);
        }
        return r;
    }

    // Depois de uma troca de roupa: oculta o que passou a ficar coberto e religa o que deixou de estar
    // (`before` = CoveredByGarments() medido ANTES da troca). Só mexe em materiais da tabela de cobertura.
    // Mesma ocultação do restante (s_hidden + ApplyHiddenToTemplate). Retorna quantos materiais mudaram.
    int UpdateCoverage(const std::vector<int>& before)
    {
        const std::vector<int> after = CoveredByGarments();
        s_hidden.resize(s_parsed.materialsList.size(), 0);
        int changed = 0;
        for (int i : after)
            if ((size_t)i < s_hidden.size() && !s_hidden[(size_t)i]) { s_hidden[(size_t)i] = 1; ++changed; }
        for (int i : before)
            if ((size_t)i < s_hidden.size() && s_hidden[(size_t)i] && std::find(after.begin(), after.end(), i) == after.end())
            { s_hidden[(size_t)i] = 0; ++changed; }
        if (!changed) return 0;
        RecountHidden();
        std::string note; ApplyHiddenToTemplate(note);
        return changed;
    }

    // [LOOKX-FUNCS-BEGIN] ---- fila de pedidos /lookx --------------------------------------------------------
    void LookQueue(const LookOp& op)
    {
        for (LookOp& q : s_lookQ) if (q.slotId == op.slotId) { q = op; return; }     // mesmo slot: o último vence
        s_lookQ.push_back(op);
    }

    // Registra o que o jogador PEDIU para o slot (depois de o /skin7 slot ter dado certo localmente).
    //   off  -> estado 0 | variante numérica da pasta do próprio slot (pack/<pasta>/<n>) -> estado 2 + n | senão estado 1.
    // Caminho avulso (ex.: versace/tshirts/2_1) não cabe no protocolo (só variante numérica): NÃO é enviado.
    // Cobertura (pele escondida sob roupa) e exclusividade de grupo NÃO são enviadas: o servidor e o cliente as derivam.
    std::string LookNoteSlot(const SkinSlots::Slot& sl, const std::vector<int>& idx, bool off)
    {
        if (s_lookSuppress) return {};
        const int slotId = (int)(&sl - SkinSlots::kSlots) + 1;               // mesmo id estável de gLookSlotName no servidor
        if (slotId < 1 || slotId > SkinSlots::kCount || idx.empty()) return {};

        LookOp op{ slotId, off ? 0 : 1, 0 };
        if (!off)
        {
            const auto it = s_matPack.find(idx[0]);
            if (it != s_matPack.end())
            {
                const std::string& v = it->second;
                const int n = SkinSlots::VariantFromRel(sl, v.c_str());      // rosto: "heads/3/5" -> 305 (tom*100+rosto)
                if (n < 0 || n > kLookMaxVar)
                {
                    FLog("[SKIN7] lookx: slot %s usa '%s' (fora do pack do slot): NAO sincronizado", sl.name, v.c_str());
                    return " | lookx: caminho avulso, não sincronizado";
                }
                op.est = 2; op.var = n;
            }
        }
        if (s_lookInPrev)
        {
            for (LookOp& q : s_lookPrevQ) if (q.slotId == op.slotId) { q = op; return " | lookx: prévia (envia ao confirmar)"; }
            s_lookPrevQ.push_back(op);
            return " | lookx: prévia (envia ao confirmar)";
        }
        LookQueue(op);
        return " | lookx: na fila (slot " + std::to_string(slotId) + " estado " + std::to_string(op.est) +
               (op.est == 2 ? " var " + std::to_string(op.var) : std::string()) + ")";
    }

    void LookCommitPrev()                       // "ui commit": a prévia confirmada passa para a fila de envio
    {
        // A fila da prévia guarda UMA operação por slot já experimentado, inclusive peças que o jogador largou ao trocar de
        // roupa (Camisa 2 -> 3 -> 4 -> 3). O servidor aplica em ordem e a exclusividade de grupo (Look_PutSlot) deixa a ÚLTIMA
        // enviada: a Camisa 4 era gravada enquanto a interface mostrava a 3. Só vai o que está VESTIDO no molde ao confirmar:
        // peça de grupo exclusivo, ligada na prévia (est != 0) e hoje oculta = substituída por outra do mesmo grupo -> descarta
        // (o servidor já limpa a antiga quando recebe a nova). DESLIGAR explícito (est 0) e slots sem grupo (pele, tom) seguem.
        for (const LookOp& op : s_lookPrevQ)
        {
            if (op.est != 0 && op.slotId >= 1 && op.slotId <= SkinSlots::kCount)
            {
                const SkinSlots::Slot& sl = SkinSlots::kSlots[op.slotId - 1];
                if (SkinSlots::GroupOf(sl.name) >= 0)
                {
                    const std::vector<int> idx = SlotMaterials(sl);
                    bool displaced = !idx.empty();
                    for (int i : idx) if (!((size_t)i < s_hidden.size() && s_hidden[(size_t)i])) displaced = false;
                    if (displaced)
                    {
                        FLog("[SKIN7] lookx: %s descartado ao confirmar (outra peça do mesmo grupo está vestida)", sl.name);
                        continue;
                    }
                }
            }
            LookQueue(op);
        }
        s_lookPrevQ.clear();
    }

    // 1x por frame (thread do jogo). No máximo 1 comando a cada kLookDebounceMs; offline: descarta tudo.
    void LookFlush()
    {
        if (s_lookQ.empty() && !s_lookResetQ) return;
        if (!Online())
        {
            s_lookQ.clear(); s_lookPrevQ.clear(); s_lookResetQ = false;      // nunca vaza pedido para outra sessão
            return;
        }
        const uint64_t now = NowMs();
        if (s_lookLastSendMs && now - s_lookLastSendMs < kLookDebounceMs) return;

        char buf[64];
        if (s_lookResetQ)
        {
            s_lookResetQ = false;
            std::snprintf(buf, sizeof(buf), "/lookx reset");
        }
        else
        {
            const LookOp op = s_lookQ.front();
            s_lookQ.erase(s_lookQ.begin());
            if (op.est == 2) std::snprintf(buf, sizeof(buf), "/lookx set %d 2 %d", op.slotId, op.var);
            else             std::snprintf(buf, sizeof(buf), "/lookx set %d %d", op.slotId, op.est);
        }
        SendChat(buf);
        s_lookLastSendMs = now;
        FLog("[SKIN7] lookx -> %s (restam %zu na fila)", buf, s_lookQ.size() + (s_lookResetQ ? 1u : 0u));
    }
    // [LOOKX-FUNCS-END]

    // ---- /skin7 slot <nome> <n | + | - | on | off | pasta/arquivo> ------------------------------
    std::vector<int> SlotVariants(const SkinSlots::Slot& sl);   // definida junto de CachedVariants
    void ToneCompanions(int tone);

    void DoSlot(const std::string& rest)
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        std::istringstream in(rest);
        std::string name, arg;
        in >> name >> arg;
        if (name.empty() || arg.empty())
        {
            SetInfo("uso: /skin7 slot <nome> <n | + | - | on | off | pasta/arquivo>   (nomes: /skin7 slots; ex.: slot torso1 34)");
            return;
        }
        const SkinSlots::Slot* sl = SkinSlots::Find(name.c_str());
        if (!sl) { SetInfo("slot desconhecido: '" + name + "' (veja /skin7 slots)"); return; }
        const std::vector<int> idx = SlotMaterials(*sl);
        if (idx.empty()) { SetInfo("o DFF carregado não tem o material do slot '" + name + "' (" + sl->texture + ")"); return; }
        const std::vector<int> covBefore = CoveredByGarments();     // cobertura antes da troca (camisa/calça/bermuda/tênis x pele/roupa íntima)

        if (arg == "on" || arg == "off")
        {
            for (int i : idx) { s_hidden.resize(s_parsed.materialsList.size(), 0); s_hidden[(size_t)i] = (arg == "off") ? 1 : 0; if ((size_t)i < s_defHide.size()) s_defHide[(size_t)i] = 0; }
            RecountHidden();
            std::string note; ApplyHiddenToTemplate(note);
            std::string exNote;
            const int exN = (arg == "on") ? HideSameCategory(*sl, exNote) : 0;   // ativar = oculta o resto da categoria
            const int cvN = UpdateCoverage(covBefore);
            const std::string lk = LookNoteSlot(*sl, idx, arg == "off");
            SetInfo(name + (arg == "off" ? " oculto" : " visível") + " (materiais " + JoinList(idx) + ") | " + note + lk +
                    (exN ? " | categoria: " + std::to_string(exN) + " material(is) de outros slots ocultados" : std::string()) +
                    (cvN ? " | cobertura: " + std::to_string(cvN) + " parte(s) do corpo ajustada(s)" : std::string()));
            return;
        }

        std::string rel;
        int newVar = 0;
        if (arg.find('/') != std::string::npos) rel = arg;                 // caminho explícito (marcas: versace/tshirts/2_1)
        else
        {
            const std::vector<int> var = SlotVariants(*sl);              // rosto: so os do tom atual; pele: so tons 1..6
            if (arg == "+" || arg == "-")
            {
                if (var.empty()) { SetInfo(std::string("sem variantes em pack/") + sl->folder + " (use /skin7 pack)"); return; }
                const auto it = s_slotVar.find(sl->name);
                const int cur = it != s_slotVar.end() ? it->second : 0;
                newVar = var.front();
                if (arg == "+") { for (int v : var) if (v > cur) { newVar = v; break; } }
                else            { newVar = var.back(); for (auto r = var.rbegin(); r != var.rend(); ++r) if (*r < cur) { newVar = *r; break; } }
            }
            else
            {
                char* endp = nullptr;
                const long n = std::strtol(arg.c_str(), &endp, 10);
                if (!endp || *endp || n < 0 || n > 99999) { SetInfo("variante inválida: '" + arg + "' (use número, +, -, on, off ou pasta/arquivo)"); return; }
                newVar = (int)n;
            }
            rel = SkinSlots::VariantRel(*sl, newVar);
        }

        std::string note;
        if (!ApplyPack(idx, rel, note)) { SetInfo(note); return; }
        if (newVar > 0) s_slotVar[sl->name] = newVar;
        std::string exNote;
        const int exN = HideSameCategory(*sl, exNote);       // só chega aqui se a variante carregou: falha não oculta nada
        const int cvN = UpdateCoverage(covBefore);
        const std::string lk = LookNoteSlot(*sl, idx, false);
        SetInfo(name + " = " + note + lk + (exN ? " | categoria: " + std::to_string(exN) + " material(is) de outros slots ocultados" : std::string()) +
                (cvN ? " | cobertura: " + std::to_string(cvN) + " parte(s) do corpo ajustada(s)" : std::string()) +
                (sl->ev == 0 ? " | HIPOTESE de mapeamento: confira no jogo" : ""));
    }

    // ---- /skin7 set <lista de materiais> <pasta/arquivo> : teste livre (confirma/refuta hipóteses) -----
    void DoSet(const std::string& rest)
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        std::istringstream in(rest);
        std::string list, rel;
        in >> list >> rel;
        if (list.empty() || rel.empty())
        {
            SetInfo("uso: /skin7 set <materiais> <pasta/arquivo>   ex.: /skin7 set 25 tshirts/1/34   (lista: 5 | 3-7 | 1,4,9-12)");
            return;
        }
        std::vector<int> idx;
        std::string err;
        if (!ParseIndexList(list, (int)s_parsed.materialsList.size(), idx, err)) { SetInfo("ERRO: " + err); return; }
        std::string note;
        ApplyPack(idx, rel, note);
        SetInfo(note);
    }

    // ---- autoexec: comandos guardados em SAMP/skins/autoexec.txt (1 por linha) ------------------
    //   Rodam sozinhos logo depois do auto-apply (kAutoApply). Gere o arquivo com /skin7 save.
    //   Linhas começando com # e linhas vazias são ignoradas; "/skin7 " no início é opcional.
    void Execute(const std::string& cmdline);   // definida mais abaixo (mesmo namespace)

    std::string RangeList(const std::vector<int>& v)   // v ordenado e sem repetição -> "0-20,22,28-30"
    {
        std::string s;
        for (size_t i = 0; i < v.size();)
        {
            size_t j = i;
            while (j + 1 < v.size() && v[j + 1] == v[j] + 1) ++j;
            if (!s.empty()) s += ",";
            s += std::to_string(v[i]);
            if (j > i) s += "-" + std::to_string(v[j]);
            i = j + 1;
        }
        return s;
    }

    // /skin7 save [default] [wear]
    //   (sem argumento) grava o estado atual (texturas do pack + materiais ocultos) em autoexec.txt
    //   default         grava o "homem base" sem depender do estado: pele, rosto1, sobrancelha1, cabelo1 e underwear
    //                   ligados, o resto desligado. O arquivo é criado pelo PRÓPRIO jogo, então ele sempre consegue lê-lo.
    //   wear            acrescenta a linha "wear"
    void DoSave(const std::string& rest)
    {
        if (!s_loadedOk && !DoLoad(false)) return;
        const std::string dir = SkinDir();
        if (dir.empty()) { SetInfo("ERRO: storage indisponível"); return; }

        bool wantWear = false, wantDefault = false;
        {
            std::istringstream ws(rest);
            std::string w;
            while (ws >> w) { if (w == "wear") wantWear = true; else if (w == "default") wantDefault = true; }
        }

        std::string out;
        int groupsN = 0, hid = 0;
        if (wantDefault)
        {
            static const char* kOn[] = { "skin_torso", "skin_leg", "skin_feet", "head1", "brow1", "hair1", "underwear" };
            out = "# gerado por /skin7 save default: homem base (pele, rosto1, sobrancelha1, cabelo1, underwear); resto desligado\n"
                  "# Troque as roupas com /skin7 slot <nome> <n> e guarde com /skin7 save\n";
            for (const char* n : kOn) out += std::string("slot ") + n + " 1\n";
            for (int k = 0; k < SkinSlots::kCount; ++k)
            {
                bool on = false;
                for (const char* n : kOn) if (std::strcmp(n, SkinSlots::kSlots[k].name) == 0) on = true;
                if (!on) { out += std::string("slot ") + SkinSlots::kSlots[k].name + " off\n"; ++hid; }
            }
            groupsN = (int)(sizeof(kOn) / sizeof(kOn[0]));
        }
        else
        {
            const int n = (int)s_parsed.materialsList.size();
            s_hidden.resize((size_t)n, 0);
            out = "# gerado por /skin7 save (roda sozinho depois do auto-apply; 1 comando por linha)\n";
            std::map<std::string, std::vector<int>> groups;          // pasta/arquivo -> materiais
            for (const auto& kv : s_matPack) if (kv.first >= 0 && kv.first < n) groups[kv.second].push_back(kv.first);
            for (const auto& g : groups) out += "set " + RangeList(g.second) + " " + g.first + "\n";   // set deixa visível: vem antes do mat
            groupsN = (int)groups.size();

            std::vector<int> vis;
            for (int i = 0; i < n; ++i) { if (s_hidden[(size_t)i]) ++hid; else vis.push_back(i); }
            if (hid) out += vis.empty() ? std::string("mat off all\n") : "mat only " + RangeList(vis) + "\n";
        }
        if (wantWear) out += "wear\n";

        const std::string path = dir + "autoexec.txt";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
        {
            SetInfo(std::string("ERRO: não consegui gravar ") + path + " (" + std::strerror(errno) +
                    "); se já existe um autoexec.txt de outro app, apague-o pelo gerenciador de arquivos");
            return;
        }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        SetInfo(std::string("salvo SAMP/skins/autoexec.txt") + (wantDefault ? " (homem base)" : "") + ": " +
                std::to_string(groupsN) + " grupo(s) de textura, " + std::to_string(hid) + " material(is)/slot(s) oculto(s)" +
                (wantWear ? ", com wear" : ""));
    }

    std::string LastInfo()
    {
        std::lock_guard<std::mutex> lock(s_infoMutex);
        return s_info;
    }

    // Mensagens de SetInfo que significam "o comando falhou" (para o resumo do autoexec).
    bool InfoLooksLikeError(const std::string& r)
    {
        static const char* kPrefix[] = { "ERRO", "slot desconhecido", "o DFF carregado não tem", "variante inválida",
                                         "sem variantes", "comando desconhecido", "uso:" };
        for (const char* p : kPrefix) if (r.rfind(p, 0) == 0) return true;
        return false;
    }

    // Roda SAMP/skins/autoexec.txt. Retorna quantos comandos rodou (-1 = não rodou).
    // Se o molde ainda não foi instalado, faz load + apply antes (os comandos slot/set precisam dele).
    // No fim deixa um RESUMO em /skin7 info (antes só o log mostrava o resultado).
    int RunAutoexec()
    {
        const std::string dir = SkinDir();
        const std::string path = dir.empty() ? std::string() : dir + "autoexec.txt";
        if (!FileExists(path))
        {
            SetInfo("autoexec: arquivo ausente: " + (path.empty() ? std::string("(storage indisponível)") : path) +
                    " (o nome tem que ser exatamente autoexec.txt, sem .txt duplicado)");
            return -1;
        }
        std::vector<uint8_t> buf;
        std::string err;
        if (!ReadFile(path, buf, err))
        {
            SetInfo("autoexec ilegível: " + err + " | dica: apague o autoexec.txt e rode /skin7 save default (o próprio jogo cria o arquivo)");
            return -1;
        }

        if (!s_applied)
        {
            FLog("[SKIN7] autoexec: molde ainda não aplicado -> load + apply antes");
            DoApply();
            if (!s_applied) return -1;                      // DoApply já deixou o motivo em /skin7 info
        }

        std::string text(buf.begin(), buf.end());
        if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
            text.erase(0, 3);                               // BOM UTF-8 (editores do Android costumam gravar)

        std::istringstream in(text);
        std::string line, firstErr;
        int ran = 0, errs = 0, lines = 0;
        s_lookSuppress = true;                              // restaura o estado local; não sobrescreve o visual gravado no servidor
        while (std::getline(in, line) && lines++ < 400)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            size_t b = 0;
            while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
            line.erase(0, b);
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '/') line.erase(0, 1);
            if (line.rfind("skin7 ", 0) == 0) line.erase(0, 6);
            const std::string first = line.substr(0, line.find(' '));
            if (first == "save" || first == "autoexec") continue;        // evita sobrescrever/recursão
            FLog("[SKIN7] autoexec: %s", line.c_str());
            Execute(line);
            ++ran;
            const std::string r = LastInfo();
            if (InfoLooksLikeError(r))
            {
                ++errs;
                FLog("[SKIN7] autoexec: ERRO na linha '%s': %s", line.c_str(), r.c_str());
                if (firstErr.empty()) firstErr = "'" + line + "' -> " + r;
            }
        }
        s_lookSuppress = false;
        FLog("[SKIN7] autoexec: %d comando(s) executado(s), %d erro(s)", ran, errs);
        SetInfo("autoexec: " + std::to_string(ran) + " comando(s), " + std::to_string(errs) + " erro(s)" +
                (errs ? " | 1º erro: " + firstErr : std::string()) + " (detalhes [SKIN7] no log)");
        return ran;
    }

    // ---- interface /provador: preparação, prévia, confirmar, cancelar e estado publicado -----------------
    // O resto do /skin7 não muda: "prev" chama o MESMO DoSlot (texturas, exclusividade por categoria, ocultação).
    // Aqui entram (a) a preparação automática do molde, (b) backup/restauração, (c) a confirmação pelo SERVIDOR
    // (autoridade) e (d) a verificação do resultado no ESTADO REAL, para a UI nunca mostrar sucesso que não aconteceu.
    bool AutoReady();                 // definidas mais abaixo (mesmo namespace)
    std::string AutoWhy();

    void Notice(const std::string& t) { s_notice = t; ++s_noticeSeq; FLog("[SKIN7] provador: %s", t.c_str()); }

    bool BeginBackup()
    {
        if (s_bak.active) return true;
        if (!InstalledOurs() || s_enabledTex.size() != s_parsed.materialsList.size()) return false;
        s_hidden.resize(s_enabledTex.size(), 0);
        s_bak = UiBackup();
        s_bak.active  = true;
        s_bak.hidden  = s_hidden;
        s_bak.matPack = s_matPack;
        s_bak.slotVar = s_slotVar;
        s_bak.tex     = s_enabledTex;
        for (RwTexture* t : s_bak.tex) TexAddRef(t);
        return true;
    }

    // Desfaz tudo desde o BeginBackup. Mesma ocultação do restante (ApplyHiddenToTemplate/HidePass).
    bool RestoreBackup(std::string& note)
    {
        if (!s_bak.active) { note = "nada a restaurar"; return true; }
        std::vector<RpMaterial*> mats;
        if (!InstalledOurs() || !CollectMaterials(TemplateClump(), mats) ||
            mats.size() != s_enabledTex.size() || s_bak.tex.size() != s_enabledTex.size() || s_bak.hidden.size() != s_enabledTex.size())
        {
            DiscardBackup();
            s_moldStale = true;
            note = "ERRO: o molde mudou durante a prévia; nada foi restaurado (a próxima preparação reinstala o molde)";
            return false;
        }
        const uint64_t now = NowMs();
        for (size_t i = 0; i < s_enabledTex.size(); ++i)
        {
            RwTexture* cur = s_enabledTex[i];
            if (cur != s_bak.tex[i])
            {
                s_enabledTex[i] = s_bak.tex[i];          // a ref extra do backup vira a ref de s_enabledTex
                s_bak.tex[i]    = nullptr;
                if (cur) s_graveyard.emplace_back(cur, now + kGraveyardMs);   // ref da prévia: solta com atraso
            }
        }
        s_hidden  = s_bak.hidden;
        s_matPack = s_bak.matPack;
        s_slotVar = s_bak.slotVar;
        DiscardBackup();                                 // solta as refs extras das texturas que não mudaram
        RecountHidden();
        ApplyHiddenToTemplate(note);                     // religa texturas/alfa de cada material conforme s_hidden
        return true;
    }

    // O slot ficou como pedido? (confere s_hidden e s_matPack, não o texto da mensagem)
    bool SlotMatches(const SkinSlots::Slot& sl, const std::vector<int>& idx, const std::string& what)
    {
        bool allVisible = true, allHidden = true;
        const std::vector<int> cov = CoveredByGarments();             // ocultos POR COBERTURA (pele sob a camisa/calça/tênis) valem como visíveis:
        for (int i : idx)                                             // o pedido foi cumprido, só não aparece enquanto a roupa cobrir
        {
            const bool h = (size_t)i < s_hidden.size() && s_hidden[(size_t)i];
            if (h && std::find(cov.begin(), cov.end(), i) == cov.end()) allVisible = false;
            if (!h) allHidden = false;
        }
        if (what == "off") return allHidden;
        if (what == "on")  return allVisible;
        if (!allVisible) return false;
        bool wnum = !what.empty() && what.size() <= 5;
        for (char c : what) if (!std::isdigit((unsigned char)c)) wnum = false;
        const std::string want = wnum ? SkinSlots::VariantRel(sl, std::atoi(what.c_str())) : std::string(sl.folder) + "/" + what;
        for (int i : idx)                                 // TODOS os materiais do slot (antes só o primeiro era conferido)
        {
            const auto it = s_matPack.find(i);
            if (it == s_matPack.end() || it->second != want) return false;
        }
        return true;
    }

    // Variantes do pack por pasta, com cache (a varredura da pasta custa; o pack não muda com o jogo aberto).
    const std::vector<int>& CachedVariants(const std::string& folder)
    {
        if (!s_varCacheOn) { s_varCache.clear(); s_varCacheOn = true; }
        auto it = s_varCache.find(folder);
        if (it == s_varCache.end()) it = s_varCache.emplace(folder, PackVariants(folder)).first;
        return it->second;
    }

    // Tom de pele atual do MOLDE (variante de skin_torso 1..6); sem pele aplicada = 1 (o padrão do servidor).
    int CurrentTone()
    {
        const SkinSlots::Slot* st = SkinSlots::Find("skin_torso");
        if (!st) return 1;
        const std::vector<int> idx = SlotMaterials(*st);
        if (idx.empty()) return 1;
        const auto it = s_matPack.find(idx[0]);
        if (it == s_matPack.end()) return 1;
        const int v = SkinSlots::VariantFromRel(*st, it->second.c_str());
        return (v >= 1 && v <= SkinSlots::kTones) ? v : 1;
    }

    // Variantes que a INTERFACE oferece para o slot: rosto = rostos do tom ATUAL (valor = tom*100+rosto, sem a textura transparente);
    // pele (torso/leg/feet) = tons 1..6 (sem a transparente); o resto = o que existe no pack.
    std::vector<int> SlotVariants(const SkinSlots::Slot& sl)
    {
        std::vector<int> out;
        if (SkinSlots::IsHeadSlot(sl))
        {
            const int t = CurrentTone();
            for (int f : CachedVariants("heads/" + std::to_string(t)))
                if (f >= 1 && f <= SkinSlots::kToneFaces[t]) out.push_back(t * SkinSlots::kHeadVarBase + f);
            return out;
        }
        out = CachedVariants(sl.folder);
        if (SkinSlots::IsToneSlot(sl)) out.erase(std::remove_if(out.begin(), out.end(), [](int v) { return v < 1 || v > SkinSlots::kTones; }), out.end());
        return out;
    }

    // Trocar o TOM (skin_torso = T) leva junto o resto do corpo e o rosto visível: pernas e pés no mesmo tom e a cabeça
    // visível com o MESMO rosto (limitado aos rostos que o tom tem). Só no caminho da prévia do provador.
    void ToneCompanions(int tone)
    {
        if (tone < 1 || tone > SkinSlots::kTones) return;
        DoSlot(std::string("skin_leg ") + std::to_string(tone));
        DoSlot(std::string("skin_feet ") + std::to_string(tone));
        for (int h = 1; h <= 5; ++h)
        {
            const std::string hn = "head" + std::to_string(h);
            const SkinSlots::Slot* hs = SkinSlots::Find(hn.c_str());
            if (!hs) continue;
            const std::vector<int> idx = SlotMaterials(*hs);
            if (idx.empty()) continue;
            bool vis = true;
            for (int i : idx) if ((size_t)i < s_hidden.size() && s_hidden[(size_t)i]) vis = false;
            if (!vis) continue;
            const auto it = s_matPack.find(idx[0]);
            if (it == s_matPack.end()) continue;
            const int cur = SkinSlots::VariantFromRel(*hs, it->second.c_str());
            if (cur < 0) continue;
            int face = SkinSlots::HeadVarFace(cur);
            if (face > SkinSlots::kToneFaces[tone]) face = SkinSlots::kToneFaces[tone];
            if (face < 1) face = 1;
            DoSlot(hn + " " + std::to_string(tone * SkinSlots::kHeadVarBase + face));
        }
    }

    // ---- molde: reconstrução a partir do servidor ------------------------------------------------------
    // Restaura no molde (que acabou de ser reinstalado = DFF puro) o que o SERVIDOR tem gravado para o jogador local.
    // Nunca envia nada ao servidor (s_lookSuppress, como o autoexec).
    void SyncMoldFromServer()
    {
        ProvadorModel::SrvTable t;
        uint32_t ver = 0;
        const bool have = SrvSnapshot(t, ver);
        s_srvSeenVer = ver;
        if (!have) return;                                // sem snapshot: molde puro = padrão
        int applied = 0, failed = 0;
        s_lookSuppress = true;
        // Slot que o servidor NAO lista = padrao = oculto (mesma regra do CorpTex::PlanWithDefaults para os outros jogadores).
        // Sem isso, conta nova (so 7 slots no .ini) ficava com todas as pecas do DFF "visiveis" (textura invisivel) e a cobertura
        // (CoveredByGarments) escondia pele/rosto/intima: no /provador so aparecia cabelo e maos.
        {
            s_hidden.resize(s_parsed.materialsList.size(), 0);
            s_defHide.assign(s_parsed.materialsList.size(), 0);
            int hid = 0;
            for (int id = 1; id <= SkinSlots::kCount; ++id)
            {
                const ProvadorModel::Srv& v = (size_t)id < t.size() ? t[(size_t)id] : ProvadorModel::Srv();
                if (v.known) continue;
                for (int i : SlotMaterials(SkinSlots::kSlots[id - 1]))
                    if ((size_t)i < s_hidden.size() && !s_hidden[(size_t)i]) { s_hidden[(size_t)i] = 1; s_defHide[(size_t)i] = 1; ++hid; }
            }
            if (hid) { RecountHidden(); std::string n; ApplyHiddenToTemplate(n); }
            FLog("[SKIN7] sync: %d material(is) de slots nao listados pelo servidor ocultados (padrao)", hid);
        }
        for (int id = 1; id <= SkinSlots::kCount; ++id)
        {
            const ProvadorModel::Srv& v = (size_t)id < t.size() ? t[(size_t)id] : ProvadorModel::Srv();
            if (!v.known) continue;
            const SkinSlots::Slot& sl = SkinSlots::kSlots[id - 1];
            if (SlotMaterials(sl).empty()) continue;
            std::string what = v.est == 0 ? "off" : (v.est == 1 ? "on" : std::to_string((int)v.var));
            DoSlot(std::string(sl.name) + " " + what);
            const std::vector<int> idx = SlotMaterials(sl);
            if (SlotMatches(sl, idx, what)) ++applied; else { ++failed; FLog("[SKIN7] sync: slot %s=%s do servidor NAO aplicado localmente: %s", sl.name, what.c_str(), LastInfo().c_str()); }
        }
        s_lookSuppress = false;
        FLog("[SKIN7] sync do molde com o servidor: %d slot(s) aplicados, %d falha(s)", applied, failed);
    }

    // Instala o molde (DFF puro) e leva o estado do servidor para ele. Chamado só quando o molde não é nosso/está suspeito.
    bool PrepareMold(bool autoMode, std::string& why)
    {
        DiscardBackup();
        s_await = Awaiting();
        s_lookQ.clear(); s_lookPrevQ.clear();
        s_varCacheOn = false;
        s_defHide.clear();
        std::fill(s_hidden.begin(), s_hidden.end(), 0);   // molde novo = DFF puro (DoApply reaproveitaria o oculto antigo)
        s_matPack.clear(); s_slotVar.clear();
        DoApply();
        if (!InstalledOurs())
        {
            why = LastInfo();
            s_prepWhy = why;
            return false;
        }
        s_moldStale = false;
        s_prepWhy.clear();
        SyncMoldFromServer();
        // Peds que já existem são clones do molde ANTIGO e não enxergam o novo: recria o ped local (mesmo caminho do /skin7 wear).
        // Só se ele já usa o modelo 7 (nunca força a skin do jogador).
        const int lm = LocalPedModel();
        if (lm == CCustomSkin::kTargetModelId)
        {
            std::string e;
            if (!WearLocal(CCustomSkin::kTargetModelId, e)) FLog("[SKIN7] prepare: nao consegui recriar o ped local: %s", e.c_str());
        }
        else FLog("[SKIN7] prepare: ped local usa o modelo %d (nao %d): molde instalado, ped nao recriado", lm, CCustomSkin::kTargetModelId);
        (void)autoMode;
        return true;
    }

    // Preparação AUTOMÁTICA ao abrir o /provador (e antes de uma prévia): verifica InstalledOurs() e só instala se preciso.
    //   * molde nosso e íntegro  -> não faz nada (sem reinstalações repetidas)
    //   * força=false (automática): respeita .applying (crash anterior), espera o modelo 7 carregar, intervalo mínimo entre
    //     tentativas e no máximo kPrepMaxAuto falhas; depois só o botão PREPARAR (força=true) tenta de novo.
    bool EnsurePrepared(bool force, std::string& why)
    {
        if (!s_loadedOk && !DoLoad(false)) { why = LastInfo(); s_prepWhy = why; return false; }   // sem skin.dff / arquivo inválido
        if (InstalledOurs() && !s_moldStale && !force) return true;
        if (!force)
        {
            if (s_prepGiveUp) { why = s_prepWhy; return false; }
            const uint64_t now = NowMs();
            if (s_prepLastMs && now - s_prepLastMs < kPrepRetryMs) { why = s_prepWhy.empty() ? "preparando..." : s_prepWhy; return false; }
            s_prepLastMs = now;
            if (!AutoReady()) { why = "aguardando o modelo 7: " + AutoWhy(); s_prepWhy = why; return false; }
            const std::string marker = SkinDir() + ".applying";
            if (FileExists(marker))
            {
                s_prepGiveUp = true;
                s_prepWhy = "a tentativa anterior de instalar a skin não terminou (SAMP/skins/.applying); toque PREPARAR";
                why = s_prepWhy;
                return false;
            }
            if (FILE* f = std::fopen(marker.c_str(), "wb")) { std::fputs("applying\n", f); std::fclose(f); }
            FLog("[SKIN7] provador: preparação automática (molde nosso=%d, obsoleto=%d)", InstalledOurs() ? 1 : 0, s_moldStale ? 1 : 0);
            if (PrepareMold(true, why))
            {
                s_prepAutoFail = 0; s_prepGiveUp = false;
                s_markerClearAtMs = NowMs() + 15000;          // sobreviveu 15 s => apaga o marcador (igual ao auto-apply)
                return true;
            }
            std::remove(marker.c_str());                      // falhou sem crashar: não trava a próxima partida
            if (++s_prepAutoFail >= kPrepMaxAuto) { s_prepGiveUp = true; s_prepWhy = why + " (use PREPARAR)"; why = s_prepWhy; }
            return false;
        }
        FLog("[SKIN7] provador: PREPARAR manual");
        s_prepGiveUp = false; s_prepAutoFail = 0;
        return PrepareMold(false, why);
    }

    // ---- confirmação pelo servidor -----------------------------------------------------------------------
    void FinishAwait(bool confirmed, const std::string& why)
    {
        s_await = Awaiting();
        if (confirmed)
        {
            DiscardBackup();                                  // o estado atual passa a ser o definitivo (o servidor gravou)
            Notice("Aparência salva no servidor");
            return;
        }
        std::string note;
        RestoreBackup(note);                                  // volta EXATAMENTE ao estado anterior à prévia
        s_moldStale = true;                                   // o servidor pode ter aceitado só parte: a próxima preparação reinstala
        if (Online()) { SendChat("/lookx sync"); s_lastSyncMs = NowMs(); }
        Notice(why);
    }

    void TickAwait()
    {
        if (!s_await.active) return;
        const uint64_t now = NowMs();
        if (!s_bak.active) { s_await = Awaiting(); return; }  // o backup foi descartado por fora (ex.: /skin7 apply)
        if (!Online()) { FinishAwait(false, "Conexão perdida antes do servidor confirmar: alteração cancelada"); return; }

        ProvadorModel::SrvTable cur;
        uint32_t ver = 0;
        const bool have = SrvSnapshot(cur, ver);
        if (have && ver != s_await.verAtCommit)
        {
            s_await.verAtCommit = ver;
            s_await.lastEchoMs  = now;
            // O que o servidor TERÁ depois de processar os pedidos em ordem (Look_PutSlot), só nos slots pedidos
            // (os demais podem ter sido mexidos pela exclusividade do grupo ou por outro caminho).
            const ProvadorModel::SrvTable want = ProvadorModel::Simulate(ProvadorModel::NewTable(), s_await.ops);
            bool ok = true;
            for (const ProvadorModel::Op& o : s_await.ops)
                if ((size_t)o.slotId >= cur.size() || cur[(size_t)o.slotId] != want[(size_t)o.slotId]) { ok = false; break; }
            if (ok) { FinishAwait(true, std::string()); return; }
        }
        const uint64_t last = std::max(s_lookLastSendMs, std::max(s_await.lastEchoMs, s_await.startMs));
        const bool queueDone = s_lookQ.empty() && !s_lookResetQ;
        if ((queueDone && now - last > kAwaitSettleMs) || now - s_await.startMs > kAwaitMaxMs)
            FinishAwait(false, "O servidor não confirmou a alteração: aparência anterior restaurada");
    }

    // Caiu a conexão: nada da prévia nem do que esperava confirmação pode vazar para outra sessão.
    void OnWentOffline()
    {
        const bool hadAwait = s_await.active;
        s_await = Awaiting();
        s_lookQ.clear(); s_lookPrevQ.clear(); s_lookResetQ = false;
        if (s_bak.active)
        {
            std::string note;
            RestoreBackup(note);
            if (hadAwait) s_moldStale = true;
            Notice("Conexão perdida: prévia cancelada");
        }
        PreviewSuspend(false);
    }

    void PublishUiState()
    {
        CCustomSkin::UiState st;
        st.skinReady = InstalledOurs();
        st.dffOk     = s_loadedOk;
        st.online    = Online();
        ProvadorModel::SrvTable srv;
        uint32_t srvVer = 0;
        st.serverSynced = SrvSnapshot(srv, srvVer);
        st.pending   = s_bak.active && s_bak.changed && !s_await.active;
        st.awaiting  = s_await.active;
        st.reqId     = s_uiReqId;
        st.reqOk     = s_uiReqOk;
        st.msg       = s_uiReqMsg;
        st.prepMsg   = st.skinReady ? std::string() : s_prepWhy;
        st.noticeSeq = s_noticeSeq;
        st.notice    = s_notice;
        st.slots.resize((size_t)SkinSlots::kCount);
        const std::vector<int> covered = st.skinReady ? CoveredByGarments() : std::vector<int>();
        for (int k = 0; k < SkinSlots::kCount; ++k)
        {
            const SkinSlots::Slot& sl = SkinSlots::kSlots[k];
            CCustomSkin::UiSlot& u = st.slots[(size_t)k];
            u.index = k;
            const std::vector<int> idx = SlotMaterials(sl);
            u.hasMaterial = !idx.empty();
            const ProvadorModel::Srv& sv = (size_t)(k + 1) < srv.size() ? srv[(size_t)(k + 1)] : ProvadorModel::Srv();
            u.srvKnown = sv.known; u.srvEst = sv.est; u.srvVar = sv.var;
            if (idx.empty()) { u.state = ProvadorModel::kUnavailable; continue; }

            ProvadorModel::Counts c;
            c.total = (int)idx.size();
            bool first = true;
            int variant = ProvadorModel::kNoPack;
            bool differsFromBackup = false;
            for (int i : idx)
            {
                const bool h = (size_t)i < s_hidden.size() && s_hidden[(size_t)i];
                if (h)
                {
                    ++c.hidden;
                    if (std::find(covered.begin(), covered.end(), i) != covered.end()) ++c.hiddenCover;
                }
                const auto it = s_matPack.find(i);
                const int vi = it != s_matPack.end() ? SkinSlots::VariantFromRel(sl, it->second.c_str()) : ProvadorModel::kNoPack;
                if (first) { variant = vi; first = false; }
                else if (vi != variant) variant = ProvadorModel::kMixed;
                if (s_bak.active)
                {
                    const bool hb = (size_t)i < s_bak.hidden.size() && s_bak.hidden[(size_t)i];
                    const auto ib = s_bak.matPack.find(i);
                    const std::string pb = ib != s_bak.matPack.end() ? ib->second : std::string();
                    const std::string pc = it != s_matPack.end() ? it->second : std::string();
                    if (hb != h || pb != pc) differsFromBackup = true;
                }
            }
            c.variant = variant;
            u.state     = ProvadorModel::Classify(c);
            // slot que o servidor nao registra = padrao = oculto de proposito (SyncMoldFromServer): para o jogador e "nada equipado",
            // nao "desligado". So um DESLIGAR feito na previa (diferente do backup) aparece como desligado.
            bool defHid = !idx.empty();
            for (int i : idx) if (!((size_t)i < s_defHide.size() && s_defHide[(size_t)i])) defHid = false;
            if (u.state == ProvadorModel::kDisabled && st.serverSynced && !sv.known && !differsFromBackup && defHid)
            {
                bool otherWorn = false;                              // oculto porque OUTRA peca do mesmo grupo esta vestida (hair1 com hair2): continua "desligado"
                const int g = SkinSlots::GroupOf(sl.name);
                if (g >= 0)
                    for (int o = 1; o <= SkinSlots::kCount && !otherWorn; ++o)
                        if (o != k + 1 && SkinSlots::GroupOf(SkinSlots::kSlots[o - 1].name) == g && (size_t)o < srv.size() && srv[(size_t)o].known && srv[(size_t)o].est != 0) otherWorn = true;
                if (!otherWorn) u.state = ProvadorModel::kDefault;
            }
            u.visible   = c.hidden == 0;
            u.partial   = c.hidden > 0 && c.hidden < c.total;
            u.previewed = s_bak.active && differsFromBackup;
            u.variants  = SlotVariants(sl);
            u.varMod    = SkinSlots::IsHeadSlot(sl) ? SkinSlots::kHeadVarBase : 0;
            u.variant   = variant >= 0 ? variant : -1;
        }
        {
            std::lock_guard<std::mutex> lock(s_uiMutex);
            s_uiState = std::move(st);
        }
        ++s_uiVersion;
    }

    // "ui <id> <refresh | prepare | sync | prev <slot> <n|on|off> | commit | cancel>"  (só chega aqui via CCustomSkin::UiRequest)
    void DoUi(const std::string& rest)
    {
        std::istringstream in(rest);
        unsigned id = 0;
        std::string op;
        in >> id >> op;
        s_uiReqId = id; s_uiReqOk = true; s_uiReqMsg.clear();
        auto fail = [&](const std::string& why) { s_uiReqOk = false; s_uiReqMsg = why; };

        if (op == "refresh")
        {
            s_varCacheOn = false;                        // relê o pack
            std::string why;
            if (!EnsurePrepared(false, why)) FLog("[SKIN7] provador: skin nao preparada: %s", why.c_str());
            if (Online() && NowMs() - s_lastSyncMs > 1100) { SendChat("/lookx sync"); s_lastSyncMs = NowMs(); }   // estado salvo (servidor = autoridade)
        }
        else if (op == "prepare")
        {
            std::string why;
            if (s_await.active) fail("aguardando o servidor confirmar");
            else if (!EnsurePrepared(true, why)) fail(why);
        }
        else if (op == "sync")
        {
            if (!Online()) fail("sem conexão com o servidor");
            else { SendChat("/lookx sync"); s_lastSyncMs = NowMs(); }
        }
        else if (op == "prev")
        {
            std::string name, what;
            in >> name >> what;
            const SkinSlots::Slot* sl = SkinSlots::Find(name.c_str());
            bool numeric = !what.empty();
            for (char c : what) if (!std::isdigit((unsigned char)c)) numeric = false;
            std::string why;
            if (!sl || (what != "on" && what != "off" && !numeric)) fail("pedido inválido");
            else if (s_await.active)                     fail("aguardando o servidor confirmar o pedido anterior");
            else if (!EnsurePrepared(false, why))        fail(why.empty() ? std::string("skin não preparada") : why);
            else if (!InstalledOurs())                   fail("skin não instalada: use PREPARAR");
            else
            {
                const std::vector<int> idx = SlotMaterials(*sl);
                if (idx.empty())                         fail(std::string("o DFF não tem o material de ") + sl->name);
                else if (!BeginBackup())                 fail("não consegui guardar o estado atual para a prévia");
                else
                {
                    s_lookInPrev = true;                 // prévia: só vai ao servidor no commit
                    DoSlot(name + " " + what);           // o mesmo caminho do /skin7 slot (inclui a exclusividade)
                    if (numeric && sl == SkinSlots::Find("skin_torso") && SlotMatches(*sl, idx, what))
                        ToneCompanions(std::atoi(what.c_str()));    // tom de pele: corpo inteiro + rosto acompanham
                    s_lookInPrev = false;
                    if (SlotMatches(*sl, idx, what)) { s_bak.changed = true; PreviewSuspend(true); }
                    else fail(LastInfo());
                }
            }
        }
        else if (op == "commit")
        {
            if (s_await.active) fail("aguardando o servidor confirmar");
            else if (!s_bak.active || !s_bak.changed) { DiscardBackup(); }             // nada a confirmar
            else if (!Online()) fail("sem conexão com o servidor: a prévia não foi salva (cancele ou reconecte)");
            else
            {
                LookCommitPrev();                        // /lookx do que foi confirmado -> fila de envio (debounce em LookFlush)
                s_await = Awaiting();
                for (const LookOp& q : s_lookQ) s_await.ops.push_back(ProvadorModel::Op{q.slotId, q.est, q.var});
                if (s_await.ops.empty())                 // nada que o protocolo carregue (ex.: caminho avulso): fica só local
                {
                    DiscardBackup();
                    Notice("Aplicado só neste aparelho (nada a enviar ao servidor)");
                }
                else
                {
                    ProvadorModel::SrvTable cur; uint32_t ver = 0;
                    SrvSnapshot(cur, ver);
                    s_await.active = true;
                    s_await.startMs = NowMs();
                    s_await.verAtCommit = ver;
                }
            }
        }
        else if (op == "cancel")
        {
            if (s_await.active) fail("aguardando o servidor confirmar");
            else
            {
                std::string note;
                if (!RestoreBackup(note)) fail(note);
            }
        }
        else fail("pedido desconhecido: " + op);
    }

    // ---- despacho ----------------------------------------------------------
    void Execute(const std::string& cmdline)
    {
        std::istringstream in(cmdline);
        std::string cmd;
        in >> cmd;
        std::string rest;
        std::getline(in, rest);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

        if      (cmd == "load")   DoLoad(true);
        else if (cmd == "unload") DoUnload();
        else if (cmd == "apply")  DoApply();
        else if (cmd == "wear")   DoWear(true);
        else if (cmd == "unwear") DoWear(false);
        else if (cmd == "mats")   DoMats();
        else if (cmd == "mat")    DoMat(rest);
        else if (cmd == "tex")    DoTex(rest);
        else if (cmd == "png")    DoPng();
        else if (cmd == "pack")   DoPack();
        else if (cmd == "slots")  DoSlots();
        else if (cmd == "slot")   DoSlot(rest);
        else if (cmd == "set")    DoSet(rest);
        else if (cmd == "save")   DoSave(rest);
        else if (cmd == "autoexec") RunAutoexec();
        else if (cmd == "ui")     DoUi(rest);                 // interface /provador (só via CCustomSkin::UiRequest)
        else                      SetInfo("comando desconhecido: " + cmd);
    }

    bool PushCmd(const std::string& c)
    {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        if (s_cmds.size() >= 64) return false;          // fila cheia: o chamador precisa saber (antes o pedido sumia em silêncio)
        s_cmds.push_back(c);
        return true;
    }

    // ---- aplicação automática ---------------------------------------------
    bool AutoReady()
    {
        if (!g_libGTASA || !g_pszStorage || !g_pszStorage[0]) return false;
        void* mi = ModelInfoOf(CCustomSkin::kTargetModelId);
        return mi && *reinterpret_cast<uintptr_t*>(mi) == g_libGTASA + OFF_PED_MODELINFO_VTABLE
                  && StreamedModelLoaded(CCustomSkin::kTargetModelId);
    }

    // Motivo pelo qual a aplicação automática ainda não pôde rodar (para o log).
    std::string AutoWhy()
    {
        if (!g_libGTASA)                          return "libGTASA nulo";
        if (!g_pszStorage || !g_pszStorage[0])    return "raiz de armazenamento indisponível";
        void* mi = ModelInfoOf(CCustomSkin::kTargetModelId);
        if (!mi)                                  return "model info do modelo 7 nulo";
        const uintptr_t vt = *reinterpret_cast<uintptr_t*>(mi);
        if (vt != g_libGTASA + OFF_PED_MODELINFO_VTABLE)
        {
            char b[96];
            std::snprintf(b, sizeof(b), "vtable do model info diferente da de CPedModelInfo (0x%llx)", (unsigned long long)vt);
            return b;
        }
        if (!StreamedModelLoaded(CCustomSkin::kTargetModelId)) return "modelo 7 não está carregado no streaming";
        return "pronto";
    }

    // Aplicação automática (1x por sessão): só se o arquivo existir, sem "disable" e sem marcador pendente.
    void DoAutoApply()
    {
        const std::string dir = SkinDir();
        if (!FileExists(SkinPath()))
        {
            FLog("[SKIN7] auto: %s ausente; nada a aplicar", SkinPath().c_str());
            return;
        }
        if (FileExists(dir + "disable"))
        {
            SetInfo("auto-apply desativado (existe SAMP/skins/disable)");
            return;
        }
        const std::string marker = dir + ".applying";
        if (FileExists(marker))
        {
            SetInfo("auto-apply PULADO: a tentativa anterior não terminou (crash?). Apague SAMP/skins/.applying para tentar de novo");
            return;
        }

        if (FILE* f = std::fopen(marker.c_str(), "wb")) { std::fputs("applying\n", f); std::fclose(f); }
        FLog("[SKIN7] auto: aplicando %s ao modelo %d", SkinPath().c_str(), CCustomSkin::kTargetModelId);
        DoApply();
        if (s_applied)
        {
            s_autoApplied = true;
            s_markerClearAtMs = NowMs() + 15000;   // sobreviveu 15 s => apaga o marcador
            RunAutoexec();                         // roupas/ocultos guardados (SAMP/skins/autoexec.txt), se existir
        }
        else
        {
            std::remove(marker.c_str());           // falhou sem crashar (erro tratado): não trava a próxima partida
        }
    }
}

void CCustomSkin::RequestLoad()        { PushCmd("load"); }
void CCustomSkin::RequestUnload()      { PushCmd("unload"); }
void CCustomSkin::RequestApply()       { PushCmd("apply"); }
void CCustomSkin::RequestWear(bool on) { PushCmd(on ? "wear" : "unwear"); }

void CCustomSkin::UiSetActive(bool on) { s_uiWanted.store(on); }
uint32_t CCustomSkin::UiRequest(const std::string& op)
{
    const uint32_t id = ++s_uiNextId;
    if (!PushCmd("ui " + std::to_string(id) + " " + op)) return 0;
    return id;
}
uint32_t CCustomSkin::UiVersion() { return s_uiVersion.load(); }
bool CCustomSkin::UiGetState(UiState& out)
{
    std::lock_guard<std::mutex> lock(s_uiMutex);
    if (s_uiState.slots.empty()) return false;
    out = s_uiState;
    return true;
}

void CCustomSkin::Process()
{
    std::vector<std::string> cmds;
    {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        cmds.swap(s_cmds);
    }
    for (const std::string& c : cmds) Execute(c);

    // ---- /provador: conexão, molde trocado por fora, confirmação do servidor ----
    bool uiDirty = !cmds.empty();
    const bool online = Online();
    if (s_wasOnline && !online) { OnWentOffline(); uiDirty = true; }
    s_wasOnline = online;
    if (s_bak.active && !InstalledOurs())                       // o jogo/outro comando trocou o molde: as texturas do backup não valem
    {
        const bool hadAwait = s_await.active;
        s_await = Awaiting();
        DiscardBackup();
        s_moldStale = true;
        Notice(hadAwait ? "O molde mudou antes do servidor confirmar" : "O molde mudou: prévia descartada");
        uiDirty = true;
    }
    if (s_await.active)
    {
        const uint32_t seq = s_noticeSeq;
        TickAwait();
        if (seq != s_noticeSeq || !s_await.active) uiDirty = true;
    }
    if (s_uiWanted.load())
    {
        ProvadorModel::SrvTable t; uint32_t ver = 0;
        SrvSnapshot(t, ver);
        if (ver != s_srvSeenVer) { s_srvSeenVer = ver; uiDirty = true; }   // chegou snapshot do servidor (RPC 232)
        if (uiDirty) PublishUiState();                          // interface /provador aberta: estado real
    }
    LookFlush();                                                // /lookx set|reset ao servidor (debounce 300 ms)

    if (kAutoApply && !s_autoTried)
    {
        if (AutoReady())
        {
            s_autoTried = true;
            DoAutoApply();
        }
        else
        {
            const uint64_t now = NowMs();
            if (!s_autoWaitStartMs) s_autoWaitStartMs = now;
            else if (!s_autoWarned && now - s_autoWaitStartMs > 30000)
            {
                s_autoWarned = true;
                FLog("[SKIN7] auto: ainda aguardando há 30 s: %s", AutoWhy().c_str());
            }
        }
    }

    if (s_markerClearAtMs && NowMs() >= s_markerClearAtMs)
    {
        s_markerClearAtMs = 0;
        const std::string dir = SkinDir();
        if (!dir.empty()) std::remove((dir + ".applying").c_str());
        FLog("[SKIN7] auto: marcador .applying removido (sessão estável)");
    }

    // texturas antigas (troca de PNG ao vivo): solta a nossa ref depois do atraso
    if (!s_graveyard.empty())
    {
        const uint64_t now = NowMs();
        for (size_t i = 0; i < s_graveyard.size();)
        {
            if (now >= s_graveyard[i].second) { TexRelease(s_graveyard[i].first); s_graveyard.erase(s_graveyard.begin() + (long)i); }
            else ++i;
        }
    }
}

std::string CCustomSkin::Info()
{
    std::lock_guard<std::mutex> lock(s_infoMutex);
    return s_info;
}

std::string CCustomSkin::DebugCommand(const std::string& args)
{
    static const char* kNames[] = {"load", "apply", "wear", "unwear", "unload", "mats", "mat", "tex", "png", "pack", "slots", "slot", "set", "save", "autoexec"};
    std::istringstream in(args);
    std::string first;
    in >> first;

    for (const char* n : kNames)
    {
        if (first == n)
        {
            PushCmd(args);
            return "[SKIN7] '" + args + "' pedido (roda no próximo frame); veja /skin7 info";
        }
    }

    if (first == "info" || first == "status")
    {
        const int mode = s_texMode.load();
        char buf[480];
        std::snprintf(buf, sizeof(buf), "[SKIN7] arquivo_valido=%s bytes=%zu aplicado=%s auto=%s materiais=%d ocultos=%d texmodo=%s | %s",
                      s_loadedOk.load() ? "sim" : "nao", s_bytesSize.load(),
                      s_applied.load() ? "sim" : "nao", kAutoApply ? "ligado" : "desligado",
                      s_materialCount.load(), s_hiddenCount.load(),
                      mode == TEX_AUTO ? "auto" : (mode == TEX_IDX ? "idx" : "none"), Info().c_str());
        return buf;
    }

    const std::string p = SkinPath();
    return "[SKIN7] /skin7 load|apply|wear|unwear|unload|info | mats | mat list|reset|off|on|only <lista> | tex auto|idx|none | png | pack | slots | slot <nome> <n|+|-|on|off|pasta/arq> | set <lista> <pasta/arq> | save [default] [wear] | autoexec   arquivo: " +
           (p.empty() ? std::string("(storage indisponível)") : p);
}
