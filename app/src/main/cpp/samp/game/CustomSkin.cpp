#include "CustomSkin.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include <unistd.h>

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "DffPreflight.h"

#ifndef CUSTOMSKIN_HOST_STUBS
#include "../net/netgame.h"
#include "playerped.h"
#include "Streaming.h"
extern CNetGame* pNetGame;
#endif

#ifdef CUSTOMSKIN_HOST_STUBS
extern void* g_hostModelInfo;     // definidos pelo teste de host
extern bool  g_hostModelLoaded;
#endif

namespace
{
    // libGTASA.so 2.10 arm64 (conferido na tabela de símbolos)
    constexpr uintptr_t OFF_RW_STREAM_FIND_CHUNK = 0x27C9F4; // _Z17RwStreamFindChunkP8RwStreamjPjS1_
    constexpr uintptr_t OFF_RP_CLUMP_STREAM_READ = 0x2BBA10; // _Z17RpClumpStreamReadP8RwStream
    constexpr uintptr_t OFF_PED_SET_CLUMP        = 0x45D514; // _ZN13CPedModelInfo8SetClumpEP7RpClump
    constexpr uintptr_t OFF_PED_DELETE_RWOBJECT  = 0x45D744; // _ZN13CPedModelInfo14DeleteRwObjectEv
    constexpr uintptr_t OFF_PED_MODELINFO_VTABLE = 0x82F310; // vtable gravada por CModelInfo::AddPedModel (projeto)
    constexpr uintptr_t OFF_GOT_MODELINFO_PTRS   = 0x850DB8; // GOT -> CModelInfo::ms_modelInfoPtrs (o projeto aponta p/ o array dele)

    constexpr size_t    kMaxFileBytes = 64u * 1024u * 1024u;
    constexpr uint32_t  kRwIdClump    = 0x10;

    std::atomic<bool>   s_reqLoad{false};
    std::atomic<bool>   s_reqUnload{false};
    std::atomic<bool>   s_reqApply{false};
    std::atomic<int>    s_reqWear{0};          // 0 nada, 1 vestir modelo 7, 2 voltar ao modelo 0

    std::atomic<bool>   s_loadedOk{false};
    std::atomic<bool>   s_applied{false};
    std::atomic<size_t> s_bytesSize{0};
    bool                s_autoTried = false;
    bool                s_autoApplied = false;
    bool                s_autoWarned = false;
    uint64_t            s_autoWaitStartMs = 0;
    uint64_t            s_markerClearAtMs = 0;   // quando apagar SAMP/skins/.applying (0 = sem marcador pendente)

    std::mutex          s_infoMutex;
    std::string         s_info = "nada carregado (use /skin7 load)";

    std::vector<uint8_t> s_bytes;              // conteúdo validado do arquivo (só thread do jogo)

    void SetInfo(const std::string& text)
    {
        {
            std::lock_guard<std::mutex> lock(s_infoMutex);
            s_info = text;
        }
        FLog("[SKIN7] %s", text.c_str());
    }

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
#else
    void* ModelInfoOf(int) { return g_hostModelInfo; }
    bool  StreamedModelLoaded(int) { return g_hostModelLoaded; }
    bool  WearLocal(int, std::string& err) { err = "host"; return false; }
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

    // ---- /skin7 load -------------------------------------------------------
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

        s_bytes = std::move(buf);
        s_bytesSize = s_bytes.size();
        s_loadedOk = true;

        char msg[300];
        std::snprintf(msg, sizeof(msg),
            "OK: arquivo válido e legível pelo jogo (atomics=%d, tris=%llu, mats=%d, ossos=%d, %zu aviso(s)); use /skin7 apply",
            atomics, (unsigned long long)info.tris, info.materials, info.bones, warnings.size());
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

    // ---- /skin7 apply ------------------------------------------------------
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

        FLog("[SKIN7] apply: modelInfo=%p; DeleteRwObject + SetClump(%p)", mi, (void*)fresh);
        CHook::CallFunction<void>(g_libGTASA + OFF_PED_DELETE_RWOBJECT, mi);
        FLog("[SKIN7] apply: DeleteRwObject ok");
        CHook::CallFunction<void>(g_libGTASA + OFF_PED_SET_CLUMP, mi, fresh);
        FLog("[SKIN7] apply: SetClump ok");

        s_applied = true;
        SetInfo("APLICADO ao modelo 7: peds criados a partir de agora usam o skin.dff (teste com /skin7 wear)");
    }

    // ---- /skin7 wear | unwear ---------------------------------------------
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

    uint64_t NowMs()
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
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

void CCustomSkin::RequestLoad()        { s_reqLoad.store(true); }
void CCustomSkin::RequestUnload()      { s_reqUnload.store(true); }
void CCustomSkin::RequestApply()       { s_reqApply.store(true); }
void CCustomSkin::RequestWear(bool on) { s_reqWear.store(on ? 1 : 2); }

void CCustomSkin::Process()
{
    if (s_reqUnload.exchange(false)) DoUnload();
    if (s_reqLoad.exchange(false))   DoLoad();
    if (s_reqApply.exchange(false))  DoApply();

    const int w = s_reqWear.exchange(0);
    if (w == 1) DoWear(true);
    else if (w == 2) DoWear(false);

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
}

std::string CCustomSkin::Info()
{
    std::lock_guard<std::mutex> lock(s_infoMutex);
    return s_info;
}

std::string CCustomSkin::DebugCommand(const std::string& args)
{
    if (args == "load")   { RequestLoad();   return "[SKIN7] carga pedida (roda no próximo frame); use /skin7 info"; }
    if (args == "apply")  { RequestApply();  return "[SKIN7] instalação pedida (roda no próximo frame); use /skin7 info"; }
    if (args == "wear")   { RequestWear(true);  return "[SKIN7] vestindo o modelo 7 no ped local (próximo frame)"; }
    if (args == "unwear") { RequestWear(false); return "[SKIN7] voltando o ped local ao modelo 0 (próximo frame)"; }
    if (args == "unload") { RequestUnload(); return "[SKIN7] descarga pedida"; }

    if (args == "info" || args == "status")
    {
        char buf[420];
        std::snprintf(buf, sizeof(buf), "[SKIN7] arquivo_valido=%s bytes=%zu aplicado=%s auto=%s | %s",
                      s_loadedOk.load() ? "sim" : "nao", s_bytesSize.load(),
                      s_applied.load() ? "sim" : "nao", kAutoApply ? "ligado" : "desligado", Info().c_str());
        return buf;
    }

    const std::string p = SkinPath();
    return "[SKIN7] /skin7 load | apply | wear | unwear | info | unload   arquivo: " +
           (p.empty() ? std::string("(storage indisponível)") : p);
}
