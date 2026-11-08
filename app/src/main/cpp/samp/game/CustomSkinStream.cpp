// CustomSkinStream.cpp
//
// COMO FUNCIONA
//   O jogo carrega cada modelo por CStreaming::ConvertBufferToObject(buffer, modelId, x): le o DFF do
//   gta3.img para um buffer de streaming e o jogo mesmo cria o clump, as refs de TXD, o hit-col, etc.
//   Este modulo so troca A ORIGEM DOS BYTES. Modelos substituidos (tabela s_skins):
//     7 -> SAMP/skins/skin.dff                (male normal: corpo do pack)
//     9 -> SAMP/skins/skin9.dff  ou  SAMP/skins/(FeminaNormal)skin[ParaModelo9].dff   (FemininaNormal)
//   271 -> SAMP/skins/skin271.dff | _MasculinaCorp_skin.dff | male_police.dff | male.dff (o 1o que existir)                             (male_police.dff: policia; mesmo corpo do 7)
//   Quando o modelo esta na tabela e o DFF existe, o buffer
//   e o tamanho (m_nCdSize, em setores de 2048 bytes) passam a ser os do skin.dff. Todo o resto e o
//   fluxo original do jogo. Descarregar/recarregar o modelo 7 tambem usa a skin (o hook roda de novo).
//
//   Texturas: o DFF so tem NOMES. O jogo resolve cada nome no banco de texturas (texdb) do TXD do modelo,
//   exatamente como faz com "male01" no 7 original. Os nomes das texturas do skin.dff precisam existir
//   no mesmo banco onde esta "male01" (procure "male01" no editor de texdb).
//
//   Sempre ativo: os arquivos SAMP/skins/disable* foram removidos de proposito (nenhum arquivo desliga o sistema).
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
#include "CustomDff.h"
#include "game/Models/ModelInfo.h"

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
        const char* files[4];
        std::vector<uint8_t> data;       // conteudo do DFF
        DffPreflight::Info   info;       // inventario (exposto por GetInfo para o CorpTex)
        bool infoOk = false;
        bool tried  = false;
        bool ok     = false;
    };
    Skin s_skins[] = {
        { 7, { "skin.dff",  nullptr } },                                              // male normal (CorpTex: aparencia + roupas pack)
        { 9, { "skin9.dff", "(FeminaNormal)skin[ParaModelo9].dff" } },                // FemininaNormal
        { 271, { "skin271.dff", "_MasculinaCorp_skin.dff", "male_police.dff", "male.dff" } },                                       // male_police.dff (CorpTex: corp + aparencia do 7)
    };
    Skin* FindSkin(int model) { for (Skin& k : s_skins) if (k.model == model) return &k; return nullptr; }

    bool s_inSkinLoad = false;        // true enquanto o jogo converte um modelo com a nossa skin

    // DIAGNOSTICO: slots de TXD cujo banco (nome em slot+0x24) nao existe. SetCurrentTxd faz Register(GetDatabase(banco do slot)): banco inexistente = nulo na lista.
    // Layout (libGTASA arm64, CTxdStore::AddTxdSlot): pool em *(*(GOT+0x4e8)); objetos +0, flags +8 (byte<0 = livre), tamanho +0x10; slot de 64 bytes: nome +0x10, banco +0x24.
    void DumpBadTxdSlots(const char* why)
    {
        static int runs = 0;
        if (runs++ >= 3) return;
        uintptr_t* gotPtr = *reinterpret_cast<uintptr_t**>(g_libGTASA + 0x8514e8);
        if (!gotPtr) { FLog("[TXDDIAG] pool de TXD indisponivel"); return; }
        uintptr_t pool = *gotPtr;
        if (!pool) { FLog("[TXDDIAG] pool de TXD nulo"); return; }
        uint8_t* objs = *reinterpret_cast<uint8_t**>(pool);
        int8_t*  flags = *reinterpret_cast<int8_t**>(pool + 8);
        const int size = *reinterpret_cast<int*>(pool + 0x10);
        if (!objs || !flags || size <= 0 || size > 20000) { FLog("[TXDDIAG] pool incoerente (size=%d)", size); return; }
        FLog("[TXDDIAG] (%s) varrendo %d slots de TXD", why, size);
        int bad = 0, used = 0;
        for (int i = 0; i < size; ++i)
        {
            if (flags[i] < 0) continue;
            ++used;
            const char* nm = reinterpret_cast<const char*>(objs + (size_t)i * 64 + 0x10);
            char db[24]; std::memcpy(db, objs + (size_t)i * 64 + 0x24, 20); db[20] = 0;
            void* d = CHook::CallFunction<void*>(g_libGTASA + 0x287AF4, (const char*)db);
            if (!d && bad++ < 40) FLog("[TXDDIAG] slot %d nome='%.19s' banco='%s' -> NAO EXISTE", i, nm, db);
        }
        FLog("[TXDDIAG] slots usados=%d, com banco inexistente=%d", used, bad);
        for (int id = 12320; id <= 12370; ++id)
        {
            CBaseModelInfo* mi = CModelInfo::GetModelInfo(id);
            if (!mi) continue;
            const int t = (int)mi->m_nTxdIndex;
            if (t < 0 || t >= size || flags[t] < 0) { FLog("[TXDDIAG] modelo %d '%s': txd=%d INVALIDO", id, mi->m_modelName, t); continue; }
            char db[24]; std::memcpy(db, objs + (size_t)t * 64 + 0x24, 20); db[20] = 0;
            FLog("[TXDDIAG] modelo %d '%s': txd=%d '%.19s' banco='%s'", id, mi->m_modelName, t, objs + (size_t)t * 64 + 0x10, db);
        }
    }

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

        FLog("[SKIN7S] modelo %d: DFF lido: %zu bytes (%u setores)", model, sk.data.size(),
             (unsigned)((sk.data.size() + STREAMING_SECTOR_SIZE - 1) / STREAMING_SECTOR_SIZE));
        sk.ok = true;
        return true;
    }

    // log de cada textura pedida pelo DFF da skin: "-> (nil)" = nao existe no banco (parte branca).
    // (fallback_tex/male01 removidos) textura nao achada devolve NULL, como o jogo faz.
    // [historico] antes: devolvia a textura "male01" (teste: o crash some
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

        FLog("[SKIN7S] textura '%s' -> %p%s", name ? name : "(null)", r, r ? "" : "   <== NAO ACHADA NO TEXDB");
        return r;
    }

    // ---- DFF por entrada (texdb/DFF/<nome-do-modelo>.dff, ver CustomDff.h) --------------------------------
    // Mesma tecnica da skin: o jogo converte um buffer NOSSO, com o tamanho real do arquivo, e o resto do fluxo
    // (clump, refs de TXD, hit-col...) e' o original. true = o modelo foi tratado aqui (resultado em 'ok').
    bool ConvertLoose(char* buf, int model, int a3, bool& ok)
    {
        if (model < 0 || model >= (int)TOTAL_DFF_MODEL_IDS || !CustomDff::HasAny()) return false;
        CBaseModelInfo* mi = CModelInfo::GetModelInfo(model);
        if (!mi) return false;

        std::vector<uint8_t> data; std::string path, why;
        if (!CustomDff::Find(mi->m_nKey, data, path, why))
        {
            if (!why.empty()) FLog("[DFF] modelo %d: %s %s -> usando o modelo original do IMG", model, path.c_str(), why.c_str());
            return false;
        }

        CStreamingInfo& info = CStreaming::GetInfo(model);
        const uint32_t oldSectors = info.m_nCdSize;
        if (oldSectors == 0)
        {
            FLog("[DFF] modelo %d: sem entrada no IMG (m_nCdSize=0), nada a substituir -> ignorado", model);
            return false;
        }
        if (oldSectors > (uint32_t)CStreaming::ms_streamingBufferSize)
        {
            FLog("[DFF] modelo %d: o original ('big', %u setores > buffer %d) e' lido em 2 etapas pelo jogo -> nao trocado",
                 model, oldSectors, (int)CStreaming::ms_streamingBufferSize);
            return false;
        }

        const uint32_t sectors = (uint32_t)((data.size() + STREAMING_SECTOR_SIZE - 1) / STREAMING_SECTOR_SIZE);
        void* mem = nullptr;
        if (posix_memalign(&mem, 64, (size_t)sectors * STREAMING_SECTOR_SIZE) != 0 || !mem)
        {
            FLog("[DFF] modelo %d: sem memoria para %u setores -> usando o original", model, sectors);
            return false;
        }
        std::memset(mem, 0, (size_t)sectors * STREAMING_SECTOR_SIZE);
        std::memcpy(mem, data.data(), data.size());

        FLog("[DFF] modelo %d: DFF externo %s | %zu bytes = %u setores (original no IMG: %u setores, buffer de streaming: %d)",
             model, path.c_str(), data.size(), sectors, oldSectors, (int)CStreaming::ms_streamingBufferSize);
        info.m_nCdSize = sectors;          // o jogo monta o stream de memoria com GetCdSize() * 2048
        ok = s_convertOrig((char*)mem, model, a3);
        info.m_nCdSize = oldSectors;       // mantem a contabilidade original do streaming
        std::free(mem);
        FLog("[DFF] modelo %d convertido: ok=%d", model, (int)ok);
        return true;
    }

    int   s_curModel = -1;                                   // ultimo modelo que o jogo converteu (diagnostico do Register nulo)
    void (*s_registerOrig)(void*) = nullptr;

    // TextureDatabaseRuntime::Register(db): com db nulo o jogo poe um banco NULO na lista de registrados e o proximo GetTexture
    // (+0x110 de um ponteiro nulo) derruba o jogo / as texturas de corona e sombra falham (quadrados brancos). Ignora o nulo e loga quem chamou.
    void Register_hook(void* db)
    {
        if (!db)
        {
            static int n = 0;
            if (n++ < 20)
            {
                const uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                CBaseModelInfo* mi = (s_curModel >= 0 && s_curModel < (int)TOTAL_DFF_MODEL_IDS) ? CModelInfo::GetModelInfo(s_curModel) : nullptr;
                FLog("[TXDDIAG] Register(NULL) BLOQUEADO: chamador=libGTASA+0x%lx ultimo modelo convertido=%d '%s' txd=%d",
                     (unsigned long)(lr - g_libGTASA), s_curModel, mi ? mi->m_modelName : "?", mi ? (int)mi->m_nTxdIndex : -1);
            }
            return;
        }
        s_registerOrig(db);
    }

    bool Convert_hook(char* buf, int model, int a3)
    {
        s_curModel = model;
        Skin* sk = FindSkin(model);
        if (!sk || !EnsureFile(*sk))
        {
            bool looseOk = false;
            if (ConvertLoose(buf, model, a3, looseOk)) return looseOk;
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

    // AUDITORIA dos DFFs externos (texdb/DFF): para cada arquivo diz se o IDE declara o nome, quanto o modelo ocupa
    // no IMG e se o seu arquivo cabe/e' aceito. Chamar depois dos IDEs e dos diretorios dos IMGs (CGame::Initialize).
    void AuditDff(const char* quando)
    {
        if (!CustomDff::HasAny()) { FLog("[DFF][AUDIT:%s] nenhum .dff em texdb/DFF (nem texdb/dff, SAMP/dff)", quando); return; }
        using Fn = CBaseModelInfo* (*)(const char*, int*);
        static Fn find = reinterpret_cast<Fn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        FLog("[DFF][AUDIT:%s] buffer de streaming=%d setores (%d bytes) | memoria disponivel=%zu usada=%zu",
             quando, (int)CStreaming::ms_streamingBufferSize, (int)CStreaming::ms_streamingBufferSize * (int)STREAMING_SECTOR_SIZE,
             (size_t)CStreaming::ms_memoryAvailable, (size_t)CStreaming::ms_memoryUsed);
        int ok = 0, bad = 0;
        for (const auto& kv : CustomDff::Index())
        {
            const CustomDff::Entry& e = kv.second;
            int id = -1;
            CBaseModelInfo* mi = find(e.name.c_str(), &id);
            if (!mi || id < 0) { ++bad; FLog("[DFF][AUDIT] %-24s SEM MODELO: nenhum IDE declara '%s'", e.name.c_str(), e.name.c_str()); continue; }
            std::vector<uint8_t> data; std::string path, why;
            const bool good = CustomDff::Find(mi->m_nKey, data, path, why);
            const CStreamingInfo& si = CStreaming::GetInfo(id);
            const uint32_t sec = (uint32_t)((data.size() + STREAMING_SECTOR_SIZE - 1) / STREAMING_SECTOR_SIZE);
            if (!good) { ++bad; FLog("[DFF][AUDIT] %-24s id=%d REPROVADO: %s", e.name.c_str(), id, why.c_str()); continue; }
            if (!si.HasCdPosnAndSize()) { ++bad; FLog("[DFF][AUDIT] %-24s id=%d sem entrada no IMG (m_nCdSize=0): nada a substituir", e.name.c_str(), id); continue; }
            if (si.m_nCdSize > (uint32_t)CStreaming::ms_streamingBufferSize) { ++bad; FLog("[DFF][AUDIT] %-24s id=%d original 'big' (%u setores): nao sera trocado", e.name.c_str(), id, (unsigned)si.m_nCdSize); continue; }
            ++ok;
            FLog("[DFF][AUDIT] %-24s id=%d OK | seu arquivo=%zu bytes (%u setores) | original no IMG=%u setores | img=%d",
                 e.name.c_str(), id, data.size(), sec, (unsigned)si.m_nCdSize, (int)si.m_nImgId);
        }
        FLog("[DFF][AUDIT:%s] RESUMO: ok=%d | com problema=%d", quando, ok, bad);
    }

    void Install()
    {
        static bool done = false;
        if (done) return;
        done = true;

        void* s1 = shadowhook_hook_func_addr((void*)(g_libGTASA + OFF_CONVERT_BUFFER), (void*)&Convert_hook, (void**)&s_convertOrig);
        void* s2 = shadowhook_hook_func_addr((void*)(g_libGTASA + OFF_GET_TEXTURE),    (void*)&GetTexture_hook, (void**)&s_getTexOrig);
        void* s3 = shadowhook_hook_func_addr((void*)(g_libGTASA + 0x2865D8), (void*)&Register_hook, (void**)&s_registerOrig);
        FLog("[SKIN7S] hooks: ConvertBufferToObject=%s GetTexture=%s Register=%s", s1 ? "OK" : "ERRO", s2 ? "OK" : "ERRO", s3 ? "OK" : "ERRO");
    }
}
