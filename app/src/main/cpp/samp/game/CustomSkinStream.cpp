// CustomSkinStream.cpp
//
// COMO FUNCIONA
//   O jogo carrega cada modelo por CStreaming::ConvertBufferToObject(buffer, modelId, x): le o DFF do
//   gta3.img para um buffer de streaming e o jogo mesmo cria o clump, as refs de TXD, o hit-col, etc.
//   Este modulo so troca A ORIGEM DOS BYTES. Modelos substituidos (tabela s_skins):
//     7 -> SAMP/skins/skin.dff                (male normal: corpo do pack)
//     9 -> SAMP/skins/skin9.dff  ou  SAMP/skins/(FeminaNormal)skin[ParaModelo9].dff   (FemininaNormal)
//   271 -> SAMP/skins/skin271.dff  ou  SAMP/skins/male.dff                              (male_police.dff: policia; mesmo corpo do 7)
//   Quando o modelo esta na tabela e o DFF existe, o buffer
//   e o tamanho (m_nCdSize, em setores de 2048 bytes) passam a ser os do skin.dff. Todo o resto e o
//   fluxo original do jogo. Descarregar/recarregar o modelo 7 tambem usa a skin (o hook roda de novo).
//
//   Texturas: o DFF so tem NOMES. O jogo resolve cada nome no banco de texturas (texdb) do TXD do modelo,
//   exatamente como faz com "male01" no 7 original. Os nomes das texturas do skin.dff precisam existir
//   no mesmo banco onde esta "male01" (procure "male01" no editor de texdb).
//
//   SAMP/skins/disable (arquivo vazio) desliga tudo; SAMP/skins/disable7 ou disable9 desligam so aquele modelo.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "Streaming.h"
#include "CustomSkinStream.h"
#include "DffPreflight.h"

namespace
{
    constexpr uint32_t  kRwClump   = 0x10;          // id do chunk "clump" do RenderWare
    constexpr uintptr_t OFF_CONVERT_BUFFER = 0x395114;   // CStreaming::ConvertBufferToObject(char*, int, int)
    constexpr uintptr_t OFF_GET_TEXTURE    = 0x286718;   // TextureDatabaseRuntime::GetTexture(const char*)

    bool  (*s_convertOrig)(char*, int, int) = nullptr;
    void* (*s_getTexOrig)(const char*)      = nullptr;

    // Um DFF por modelo substituido. O primeiro nome que existir em SAMP/skins/ e usado.
    struct Skin
    {
        int         model;
        const char* files[2];
        std::vector<uint8_t> data;       // conteudo do DFF
        DffPreflight::Info   info;       // inventario (exposto por GetInfo para o CorpTex)
        bool infoOk = false;
        bool tried  = false;
        bool ok     = false;
    };
    Skin s_skins[] = {
        { 7, { "skin.dff",  nullptr } },                                              // male normal (CorpTex: aparencia + roupas pack)
        { 9, { "skin9.dff", "(FeminaNormal)skin[ParaModelo9].dff" } },                // FemininaNormal
        { 271, { "skin271.dff", "male.dff" } },                                       // male_police.dff (CorpTex: corp + aparencia do 7)
    };
    Skin* FindSkin(int model) { for (Skin& k : s_skins) if (k.model == model) return &k; return nullptr; }

    bool s_inSkinLoad = false;        // true enquanto o jogo converte um modelo com a nossa skin
    bool s_fallbackOn = false;        // SAMP/skins/fallback_tex existe: textura nao achada -> usa "male01"

    // Lista de bancos de textura registrados (suposicao: GOT 0x851CF8 -> TDBArray {numAlloced, numEntries, dataPtr}).
    // Loga so quando MUDA (n ou nulos) para achar QUANDO um banco nulo entra na lista.
    void CheckRegistered(const char* tag, int model)
    {
        static int lastN = -1, lastNulls = -1, lines = 0;
        if (lines > 120) return;
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(g_libGTASA + 0x851CF8);
        if (!arr) return;
        const uint32_t n = *reinterpret_cast<uint32_t*>(arr + 4);
        uintptr_t* d = *reinterpret_cast<uintptr_t**>(arr + 8);
        if (n > 256 || (n && !d)) { if (lines++ < 3) FLog("[SKIN7S] registered(%s,modelo=%d): lista incoerente (n=%u)", tag, model, n); return; }
        int nulls = 0;
        for (uint32_t i = 0; i < n; ++i) if (!d[i]) ++nulls;
        if ((int)n == lastN && nulls == lastNulls) return;
        lastN = (int)n; lastNulls = nulls; ++lines;
        FLog("[SKIN7S] registered(%s,modelo=%d): n=%u nulos=%d%s", tag, model, n, nulls, nulls ? "   <== BANCO NULO NA LISTA" : "");
        for (uint32_t i = 0; i < n && i < 6; ++i) FLog("[SKIN7S]    registered[%u]=%p", i, (void*)d[i]);
    }

    std::string SkinDir()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        return p + "SAMP/skins/";
    }

    bool Exists(const std::string& p) { return !p.empty() && ::access(p.c_str(), F_OK) == 0; }

    // Le o DFF do modelo UMA vez (na primeira conversao desse modelo).
    bool EnsureFile(Skin& sk)
    {
        if (sk.tried) return sk.ok;
        sk.tried = true;
        const int model = sk.model;

        const std::string dir = SkinDir();
        if (dir.empty()) { FLog("[SKIN7S] storage indisponivel: usando o modelo %d original", model); return false; }
        if (Exists(dir + "disable")) { FLog("[SKIN7S] SAMP/skins/disable existe: usando o modelo %d original", model); return false; }
        if (Exists(dir + "disable" + std::to_string(model))) { FLog("[SKIN7S] SAMP/skins/disable%d existe: usando o modelo %d original", model, model); return false; }

        std::string path;
        for (const char* name : sk.files)
            if (name && Exists(dir + name)) { path = dir + name; break; }
        if (path.empty()) { FLog("[SKIN7S] nenhum DFF do modelo %d em %s (%s): usando o original", model, dir.c_str(), sk.files[0]); return false; }

        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { FLog("[SKIN7S] %s nao abriu: usando o modelo %d original", path.c_str(), model); return false; }

        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size < 12 || size > 64L * 1024 * 1024) { std::fclose(f); FLog("[SKIN7S] %s com tamanho invalido (%ld)", path.c_str(), size); return false; }

        sk.data.resize((size_t)size);
        const size_t got = std::fread(sk.data.data(), 1, sk.data.size(), f);
        std::fclose(f);
        if (got != sk.data.size()) { sk.data.clear(); FLog("[SKIN7S] leitura incompleta de %s", path.c_str()); return false; }

        uint32_t chunkType = 0;
        std::memcpy(&chunkType, sk.data.data(), 4);
        if (chunkType != kRwClump) { FLog("[SKIN7S] %s nao comeca com chunk clump (0x%x)", path.c_str(), chunkType); sk.data.clear(); return false; }

        // Pre-validacao estrutural (so le bytes, nao toca no engine). Se falhar, o jogo carrega o modelo original
        // em vez de entregar um DFF invalido ao RpClumpStreamRead (que quase nao valida limites).
        {
            DffPreflight::Info info;
            std::string err;
            if (!DffPreflight::Parse(sk.data.data(), sk.data.size(), info, err))
            {
                FLog("[SKIN7S] preflight REPROVOU %s: %s -> usando o modelo %d original", path.c_str(), err.c_str(), model);
                sk.data.clear();
                return false;
            }
            sk.info = info; sk.infoOk = true;
            FLog("[SKIN7S] modelo %d: preflight OK (%s) | RW %s | frames=%d atomics=%d geom=%d tris=%llu verts=%llu mats=%d | ossos=%d usados=%d pesos=%d | HAnim ids=%zu",
                 model, path.c_str(), DffPreflight::VersionString(info.stamp).c_str(), info.frames, info.atomics, info.geoms,
                 (unsigned long long)info.tris, (unsigned long long)info.verts, info.materials,
                 info.bones, info.usedBones, info.maxWeights, info.hanimIds.size());
            for (const std::string& w : DffPreflight::Warnings(info))
                FLog("[SKIN7S] AVISO: %s", w.c_str());
        }

        s_fallbackOn = Exists(dir + "fallback_tex");
        FLog("[SKIN7S] fallback_tex=%s", s_fallbackOn ? "LIGADO (textura nao achada usa 'male01')" : "desligado");
        FLog("[SKIN7S] modelo %d: DFF lido: %zu bytes (%u setores)", model, sk.data.size(),
             (unsigned)((sk.data.size() + STREAMING_SECTOR_SIZE - 1) / STREAMING_SECTOR_SIZE));
        sk.ok = true;
        return true;
    }

    // log de cada textura pedida pelo DFF da skin: "-> (nil)" = nao existe no banco (parte branca).
    // Com SAMP/skins/fallback_tex: no lugar de devolver NULL, devolve a textura "male01" (teste: o crash some
    // quando o DFF nao tem nenhuma busca "perdida"?).
    // true se a lista de bancos registrados tem entrada nula (GetTexture le um campo em +0x110 de cada banco:
    // o SIGSEGV "Fault address: 0x110" bate com isso). Hipotese, ainda nao confirmada.
    bool HasNullRegistered()
    {
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(g_libGTASA + 0x851CF8);
        if (!arr) return false;
        const uint32_t n = *reinterpret_cast<uint32_t*>(arr + 4);
        uintptr_t* d = *reinterpret_cast<uintptr_t**>(arr + 8);
        if (n > 256 || (n && !d)) return false;
        for (uint32_t i = 0; i < n; ++i) if (!d[i]) return true;
        return false;
    }

    void* GetTexture_hook(const char* name)
    {
        if (HasNullRegistered())
        {
            static int logged = 0;
            if (logged++ < 5) FLog("[SKIN7S] GetTexture('%s') com BANCO NULO na lista: devolvendo NULL para evitar SIGSEGV (inSkinLoad=%d)", name ? name : "(null)", (int)s_inSkinLoad);
            return nullptr;
        }
        void* r = s_getTexOrig(name);
        if (!s_inSkinLoad) return r;

        if (!r && s_fallbackOn)
        {
            static void* fb = nullptr;
            if (!fb) fb = s_getTexOrig("male01");
            if (fb)
            {
                FLog("[SKIN7S] textura '%s' nao achada -> fallback male01 %p", name ? name : "(null)", fb);
                return fb;
            }
        }
        FLog("[SKIN7S] textura '%s' -> %p%s", name ? name : "(null)", r, r ? "" : "   <== NAO ACHADA NO TEXDB");
        return r;
    }

    bool Convert_hook(char* buf, int model, int a3)
    {
        Skin* sk = FindSkin(model);
        if (!sk || !EnsureFile(*sk))
        {
            CheckRegistered("antes", model);
            return s_convertOrig(buf, model, a3);
        }

        CStreamingInfo& info = CStreaming::GetInfo(model);
        const uint32_t oldSectors = info.m_nCdSize;
        const uint32_t sectors = (uint32_t)((sk->data.size() + STREAMING_SECTOR_SIZE - 1) / STREAMING_SECTOR_SIZE);

        void* mem = nullptr;
        if (posix_memalign(&mem, 64, (size_t)sectors * STREAMING_SECTOR_SIZE) != 0 || !mem)
        {
            FLog("[SKIN7S] sem memoria para o buffer da skin: usando o modelo %d original", model);
            return s_convertOrig(buf, model, a3);
        }
        std::memset(mem, 0, (size_t)sectors * STREAMING_SECTOR_SIZE);
        std::memcpy(mem, sk->data.data(), sk->data.size());

        FLog("[SKIN7S] convertendo modelo %d a partir do DFF customizado (setores: original=%u skin=%u)", model, oldSectors, sectors);
        FLog("[SKIN7S] streaming: memoryUsed=%zu memoryAvailable=%zu bufferSize(setores)=%d skin=%u setores (%u bytes)",
             (size_t)CStreaming::ms_memoryUsed, (size_t)CStreaming::ms_memoryAvailable,
             (int)CStreaming::ms_streamingBufferSize, sectors, sectors * (uint32_t)STREAMING_SECTOR_SIZE);
        info.m_nCdSize = sectors;          // o jogo monta o stream de memoria com GetCdSize() * 2048
        CheckRegistered("antes-skin", model);
        s_inSkinLoad = true;
        const bool ok = s_convertOrig((char*)mem, model, a3);
        s_inSkinLoad = false;
        CheckRegistered("depois-skin", model);
        info.m_nCdSize = oldSectors;       // mantem a contabilidade original do streaming
        std::free(mem);

        FLog("[SKIN7S] modelo %d convertido: ok=%d", model, (int)ok);
        return ok;
    }
}

namespace CustomSkinStream
{
    const DffPreflight::Info* GetInfo(int model) { const Skin* k = FindSkin(model); return (k && k->infoOk) ? &k->info : nullptr; }

    void Install()
    {
        static bool done = false;
        if (done) return;
        done = true;

        void* s1 = shadowhook_hook_func_addr((void*)(g_libGTASA + OFF_CONVERT_BUFFER), (void*)&Convert_hook, (void**)&s_convertOrig);
        void* s2 = shadowhook_hook_func_addr((void*)(g_libGTASA + OFF_GET_TEXTURE),    (void*)&GetTexture_hook, (void**)&s_getTexOrig);
        FLog("[SKIN7S] hooks: ConvertBufferToObject=%s GetTexture=%s", s1 ? "OK" : "ERRO", s2 ? "OK" : "ERRO");
    }
}
