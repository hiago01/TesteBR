#include "CustomSkin.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
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

#ifndef CUSTOMSKIN_HOST_STUBS
#include "../vendor/stb/stb_image.h"      // a implementação (STB_IMAGE_IMPLEMENTATION) está em sprite2d.cpp
#include "../net/netgame.h"
#include "playerped.h"
#include "Streaming.h"
#include "Textures/TextureDatabaseRuntime.h"   // banco de texturas do jogo (fluxo Android)
extern CNetGame* pNetGame;
#else
extern void* g_hostModelInfo;                // definidos pelo teste de host
extern bool  g_hostModelLoaded;
extern RpClump* (*g_hostRead)();             // devolve um clump "fresco" (com materiais novos)
extern void     (*g_hostInstall)(RpClump*);  // instala (destrói o molde antigo)
extern RpClump* (*g_hostTemplate)();         // molde instalado
extern bool     (*g_hostCollect)(RpClump*, std::vector<RpMaterial*>&);
extern int   g_hostDestroyedTextures;
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
    RpClump*                    s_installed = nullptr;  // clump-molde instalado por nós
    std::vector<RwTexture*>     s_enabledTex;           // 1 ref nossa por material (textura "ligada")
    std::vector<uint8_t>        s_origAlpha;
    RwTexture*                  s_hiddenTex = nullptr;  // textura transparente (1 ref nossa, permanente)
    std::vector<std::pair<RwTexture*, uint64_t>> s_graveyard;   // texturas a soltar com atraso

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
    bool  WearLocal(int, std::string& err) { err = "host"; return false; }

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
            err = "arquivo não encontrado: " + path;
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
                            problems.push_back("material " + std::to_string(i) + " textura '" + mi.texture +
                                               "' NAO ENCONTRADA (sem PNG em textures/, sem textura no DFF/banco do jogo): ficara branco");
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

        for (RwTexture* t : oldEnabled) TexRelease(t);        // refs do apply anterior (os materiais antigos seguram as deles)

        s_applied = true;
        char msg[260];
        std::snprintf(msg, sizeof(msg),
            "APLICADO ao modelo 7 (%d PNG, %d oculto(s)): peds criados a partir de agora usam o skin.dff (teste com /skin7 wear)",
            texCount, hiddenCount);
        SetInfo(msg);
    }

    void DoWear(bool on)
    {
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
    std::unordered_map<std::string, int> s_slotVar;   // slot -> última variante numérica aplicada

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

    // ---- /skin7 slot <nome> <n | + | - | on | off | pasta/arquivo> ------------------------------
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

        if (arg == "on" || arg == "off")
        {
            for (int i : idx) { s_hidden.resize(s_parsed.materialsList.size(), 0); s_hidden[(size_t)i] = (arg == "off") ? 1 : 0; }
            RecountHidden();
            std::string note; ApplyHiddenToTemplate(note);
            SetInfo(name + (arg == "off" ? " oculto" : " visível") + " (materiais " + JoinList(idx) + ") | " + note);
            return;
        }

        std::string rel;
        int newVar = 0;
        if (arg.find('/') != std::string::npos) rel = arg;                 // caminho explícito (marcas: versace/tshirts/2_1)
        else
        {
            const std::vector<int> var = PackVariants(sl->folder);
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
            rel = std::string(sl->folder) + "/" + std::to_string(newVar);
        }

        std::string note;
        if (!ApplyPack(idx, rel, note)) { SetInfo(note); return; }
        if (newVar > 0) s_slotVar[sl->name] = newVar;
        SetInfo(name + " = " + note + (sl->ev == 0 ? " | HIPOTESE de mapeamento: confira no jogo" : ""));
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
        else                      SetInfo("comando desconhecido: " + cmd);
    }

    void PushCmd(const std::string& c)
    {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        if (s_cmds.size() < 64) s_cmds.push_back(c);
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

void CCustomSkin::Process()
{
    std::vector<std::string> cmds;
    {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        cmds.swap(s_cmds);
    }
    for (const std::string& c : cmds) Execute(c);

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
    static const char* kNames[] = {"load", "apply", "wear", "unwear", "unload", "mats", "mat", "tex", "png", "pack", "slots", "slot", "set"};
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
    return "[SKIN7] /skin7 load|apply|wear|unwear|unload|info | mats | mat list|reset|off|on|only <lista> | tex auto|idx|none | png | pack | slots | slot <nome> <n|+|-|on|off|pasta/arq> | set <lista> <pasta/arq>   arquivo: " +
           (p.empty() ? std::string("(storage indisponível)") : p);
}
