#include "CustomSkin.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "RW/RwHelper.h"
#include "DffPreflight.h"

namespace
{
    // libGTASA.so 2.10 arm64 (conferido na tabela de símbolos)
    constexpr uintptr_t OFF_RW_STREAM_FIND_CHUNK = 0x27C9F4; // _Z17RwStreamFindChunkP8RwStreamjPjS1_
    constexpr uintptr_t OFF_RP_CLUMP_STREAM_READ = 0x2BBA10; // _Z17RpClumpStreamReadP8RwStream

    constexpr size_t    kMaxFileBytes = 64u * 1024u * 1024u;
    constexpr uint32_t  kRwIdClump    = 0x10;

    std::atomic<bool>   s_reqLoad{false};
    std::atomic<bool>   s_reqUnload{false};

    std::mutex          s_infoMutex;
    std::string         s_info = "nada carregado (use /skin7 load)";

    RpClump*            s_clump = nullptr;     // só acessado pela thread do jogo

    void SetInfo(const std::string& text)
    {
        {
            std::lock_guard<std::mutex> lock(s_infoMutex);
            s_info = text;
        }
        FLog("[SKIN7] %s", text.c_str());
    }

    std::string SkinPath()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        p += "SAMP/skins/skin.dff";
        return p;
    }

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

    RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data)
    {
        ++*static_cast<int*>(data);
        return atomic;
    }

    void DoUnload()
    {
        if (!s_clump) return;
        RpClumpDestroy(s_clump);
        s_clump = nullptr;
        SetInfo("descarregado");
    }

    void DoLoad()
    {
        DoUnload();   // recarregar: solta o anterior (thread do jogo)

        if (!g_libGTASA)
        {
            SetInfo("ERRO: libGTASA indisponível");
            return;
        }

        const std::string path = SkinPath();
        if (path.empty())
        {
            SetInfo("ERRO: raiz de armazenamento (g_pszStorage) ainda indisponível");
            return;
        }

        std::vector<uint8_t> buf;
        std::string err;
        if (!ReadFile(path, buf, err))
        {
            SetInfo("ERRO: " + err);
            return;
        }

        // 1) validação pura (sem engine)
        DffPreflight::Info info;
        if (!DffPreflight::Parse(buf.data(), buf.size(), info, err))
        {
            SetInfo("ERRO no DFF: " + err);
            return;
        }

        char head[320];
        std::snprintf(head, sizeof(head),
            "%s | %zu bytes | RW %s | frames=%d atomics=%d geom=%d tris=%llu verts=%llu mats=%d | skin: ossos=%d usados=%d pesos=%d | HAnim ids=%zu",
            path.c_str(), buf.size(), DffPreflight::VersionString(info.stamp).c_str(),
            info.frames, info.atomics, info.geoms,
            (unsigned long long)info.tris, (unsigned long long)info.verts, info.materials,
            info.bones, info.usedBones, info.maxWeights, info.hanimIds.size());
        FLog("[SKIN7] preflight OK: %s", head);
        for (const std::string& w : DffPreflight::Warnings(info))
            FLog("[SKIN7] AVISO: %s", w.c_str());

        // 2) leitor do jogo
        RpClump* clump = ReadClumpWithEngine(buf, err);
        if (!clump)
        {
            SetInfo("ERRO no leitor do jogo: " + err);
            return;
        }

        int atomics = 0;
        RpClumpForAllAtomics(clump, CountAtomicCB, &atomics);
        RpHAnimHierarchy* hier = GetAnimHierarchyFromSkinClump(clump);

        s_clump = clump;

        char msg[420];
        std::snprintf(msg, sizeof(msg),
            "OK: clump=%p atomics=%d hanim_no_atomic=%s | tris=%llu mats=%d ossos=%d | %zu aviso(s) | NÃO instalado no modelo %d",
            (void*)clump, atomics, hier ? "sim" : "nao",
            (unsigned long long)info.tris, info.materials, info.bones,
            DffPreflight::Warnings(info).size(), CCustomSkin::kTargetModelId);
        SetInfo(msg);
    }
}

void CCustomSkin::RequestLoad()   { s_reqLoad.store(true); }
void CCustomSkin::RequestUnload() { s_reqUnload.store(true); }

void CCustomSkin::Process()
{
    if (s_reqUnload.exchange(false)) DoUnload();
    if (s_reqLoad.exchange(false))   DoLoad();
}

std::string CCustomSkin::Info()
{
    std::lock_guard<std::mutex> lock(s_infoMutex);
    return s_info;
}

std::string CCustomSkin::DebugCommand(const std::string& args)
{
    if (args == "load")
    {
        RequestLoad();
        return "[SKIN7] carga pedida (roda no próximo frame); use /skin7 info";
    }
    if (args == "unload")
    {
        RequestUnload();
        return "[SKIN7] descarga pedida";
    }
    if (args == "info")
        return "[SKIN7] " + Info();

    const std::string p = SkinPath();
    return "[SKIN7] /skin7 load | info | unload   arquivo: " + (p.empty() ? std::string("(storage indisponível)") : p);
}
