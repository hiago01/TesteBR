#pragma once

// ---------------------------------------------------------------------------
// countrye_10.col (CE_brewery) -> REPLACE DIRETO no carregamento nativo + CINFO.BIN   (v2)
//
// POR QUE O PATCH v1 NAO FUNCIONAVA (evidencia no libGTASA.so arm64 do projeto):
//   * O jogo carrega o countrye_10.col do gta3.img por:
//         CStreaming::ConvertBufferToObject (0x395114) -> CColStore::LoadCol(int slot, uchar* buf, int size) (0x3a579c)
//         -> CFileLoader::LoadCollisionFileFirstTime(buf,size,slot)   (1a vez do slot, monta o intervalo de modelos)
//         ou CFileLoader::LoadCollisionFile(buf,size,slot)            (demais vezes; procura o nome SO dentro do
//            intervalo de modelos do slot: CModelInfo::GetModelInfo(nome,min,max))
//   * v1 chamava LoadCollisionFile(buf,size, colId=0) UMA vez em CGame::Initialize: slot 0 nao e' o slot do
//     countrye_10 (nome fora do intervalo -> nada carregado) e, mesmo se carregasse, o streaming recarregava o
//     IMG depois (RemoveCol/LoadCol) e a colisao original voltava. E o passo era opt-in (col_reload.on).
//
// v2: hook em CColStore::LoadCol. Quando o buffer que o streaming entrega e' o countrye_10 (mesma colecao de nomes),
//     entrega ao loader NATIVO o arquivo externo no lugar, com o MESMO slot. Toda a cadeia nativa (validacao de versao,
//     AllocateData, SetColModel, IncludeModelIndex, CColAccel::addCacheCol) roda normalmente sobre os seus dados.
//     Nao ha bypass de validacao: o arquivo passa antes por Parse + ValidateAll (CustomColParse.h); se reprovar,
//     o buffer original do IMG segue (jogo como antes) e o motivo vai para o log.
//
// PASTAS (raiz = Android/data/com.samp.mobile/files/), ordem de busca do arquivo:
//     SAMP/col/countrye_10.col  ->  SAMP/countrye_10.col  ->  texdb/countrye_10.col
// MARCADORES (arquivos vazios em SAMP/):   col_off.on = desliga o replace (testar o original)   cinfo_keep.on = nao mexe no CINFO.BIN
// LOG (samp_log.txt): prefixos [COL] e [CINFO]
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../main.h"   // g_pszStorage, FLog
#include "../vendor/armhook/patch.h"
#include "game/Models/ModelInfo.h"
#include "game/Enums/eModelID.h"
#include "CustomColParse.h"

namespace CustomCol
{
    inline constexpr const char* kColFile     = "countrye_10.col";
    inline constexpr const char* kTargetModel = "CE_brewery";
    inline constexpr int         kTargetId    = MODEL_CE_BREWERY;      // 12931
    inline constexpr size_t      kMaxColBytes = 64u * 1024u * 1024u;   // trava de seguranca

    // ----------------------------------------------------------------- caminhos
    inline std::string Root()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        return p;
    }

    inline bool Exists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }
    inline bool Marker(const char* rel)
    {
        const std::string r = Root();
        return !r.empty() && Exists(r + "SAMP/" + rel);
    }

    inline std::vector<std::string> ColCandidates()
    {
        const std::string r = Root();
        if (r.empty()) return {};
        return { r + "SAMP/col/" + kColFile, r + "SAMP/" + kColFile, r + "texdb/" + kColFile };
    }

    inline bool ReadAll(const std::string& path, std::vector<uint8_t>& out, size_t& fileSize)
    {
        out.clear(); fileSize = 0;
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
        fileSize = (size_t)st.st_size;
        if (fileSize == 0 || fileSize > kMaxColBytes) return false;
        FILE* f = ::fopen(path.c_str(), "rb");
        if (!f) return false;
        out.resize(fileSize);
        const size_t got = ::fread(out.data(), 1, fileSize, f);
        ::fclose(f);
        if (got != fileSize) { out.clear(); return false; }
        return true;
    }

    // ------------------------------------------------- estado do jogo p/ CE_brewery (somente leitura)
    using FindFn = CBaseModelInfo* (*)(const char*, int*);
    struct GameState { int id = -1; void* mi = nullptr; void* col = nullptr; int slot = -1; bool vol = false; unsigned tris = 0; };

    inline GameState Snapshot()
    {
        static FindFn find = reinterpret_cast<FindFn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        GameState g; int id = -1;
        CBaseModelInfo* mi = find(kTargetModel, &id);
        g.id = id; g.mi = mi;
        if (mi && mi->m_pColModel)
        {
            CColModel* c = mi->m_pColModel;
            g.col = c; g.slot = c->m_nColSlot; g.vol = c->m_bHasCollisionVolumes; g.tris = c->GetTriCount();
        }
        return g;
    }
    inline void LogState(const char* tag, const GameState& g)
    {
        FLog("[COL][%s] '%s' -> id=%d (esperado %d)%s | modelInfo=%p | colModel=%p slot=%d volumes=%d triangulos=%u",
             tag, kTargetModel, g.id, kTargetId, g.id == kTargetId ? " OK" : " DIFERENTE", g.mi, g.col, g.slot, (int)g.vol, g.tris);
    }

    // ------------------------------------------------- arquivo personalizado em memoria (revalidado se mudar)
    struct Custom
    {
        std::string path; time_t mtime = 0; size_t size = 0;
        std::vector<uint8_t> data; CustomColParse::Result res;
        bool valid = false; char why[200] = {0};
        unsigned expectTris = 0;       // triangulos de CE_brewery segundo o ARQUIVO (para conferir o que o jogo carregou)
        bool logged = false;
    };
    inline Custom g_c;
    inline std::mutex g_m;
    inline std::chrono::steady_clock::time_point g_lastCheck{};
    inline bool g_haveChecked = false;

    inline unsigned TrisOf(const Custom& c, const char* name)
    {
        const int i = CustomColParse::Find(c.res, name);
        if (i < 0) return 0;
        const auto& e = c.res.entries[i];
        if (e.version < 2) return 0;
        return CustomColParse::Rd16(c.data.data() + e.offset + 4 + 72);
    }

    // chamar com g_m travado. true = existe arquivo valido pronto para o replace.
    inline bool Refresh(bool force = false)
    {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        if (!force && g_haveChecked && duration_cast<milliseconds>(now - g_lastCheck).count() < 2000) return g_c.valid;
        g_haveChecked = true; g_lastCheck = now;

        if (Marker("col_off.on"))
        {
            if (g_c.valid || !g_c.logged) FLog("[COL] SAMP/col_off.on presente: replace DESLIGADO (jogo usa o countrye_10 do IMG)");
            g_c = Custom{}; g_c.logged = true; return false;
        }
        std::string found; struct stat st {};
        for (const auto& c : ColCandidates())
            if (::stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode)) { found = c; break; }
        if (found.empty())
        {
            if (!g_c.logged || g_c.valid) FLog("[COL] %s nao esta em SAMP/col, SAMP nem texdb: replace inativo (jogo usa o IMG)", kColFile);
            g_c = Custom{}; g_c.logged = true; return false;
        }
        if (found == g_c.path && st.st_mtime == g_c.mtime && (size_t)st.st_size == g_c.size) return g_c.valid;   // nada mudou

        Custom n; n.path = found; n.mtime = st.st_mtime; n.size = (size_t)st.st_size; n.logged = true;
        size_t sz = 0;
        if (!ReadAll(found, n.data, sz))
        {
            std::snprintf(n.why, sizeof n.why, "falha ao ler (tamanho=%zu, limite=%zu)", sz, kMaxColBytes);
            FLog("[COL] %s: REPROVADO - %s -> o IMG continua valendo", found.c_str(), n.why);
            g_c = std::move(n); return false;
        }
        n.res = CustomColParse::Parse(n.data.data(), n.data.size());
        n.valid = CustomColParse::ValidateAll(n.data.data(), n.data.size(), n.res, n.why, sizeof n.why);
        FLog("[COL] %s: %zu bytes, %zu entradas, estrutura=%s, validacao profunda=%s%s%s",
             found.c_str(), n.data.size(), n.res.entries.size(), CustomColParse::StatusStr(n.res.status),
             n.valid ? "OK" : "REPROVADO", n.valid ? "" : " - ", n.valid ? "" : n.why);
        if (n.valid)
        {
            n.expectTris = TrisOf(n, kTargetModel);
            const int idx = CustomColParse::Find(n.res, kTargetModel);
            if (idx < 0) FLog("[COL] AVISO: '%s' nao esta dentro de %s (%zu entradas)", kTargetModel, kColFile, n.res.entries.size());
            else FLog("[COL] '%s' no arquivo: entrada #%d %s size=%u triangulos=%u", kTargetModel, idx, n.res.entries[idx].ident, n.res.entries[idx].size, n.expectTris);
            for (size_t i = 0; i < n.res.entries.size() && i < 60; i++)
                FLog("[COL]   #%03zu +%-8u %s size=%-8u nome='%s'", i, n.res.entries[i].offset, n.res.entries[i].ident, n.res.entries[i].size, n.res.entries[i].name);
        }
        g_c = std::move(n);
        return g_c.valid;
    }

    // ------------------------------------------------------------------ REPLACE: hook em CColStore::LoadCol
    // assinatura confirmada pelo simbolo exportado _ZN9CColStore7LoadColEiPhi:  bool LoadCol(int slot, uchar* buf, int size)
    // (o retorno e' bool: o corpo da funcao devolve 0/1 em w0 depois de LoadCollisionFile[FirstTime])
    inline bool (*g_origLoadCol)(int, uint8_t*, int) = nullptr;
    inline thread_local std::vector<uint8_t> t_work;   // copia mutavel entregue ao jogo (o arquivo original em g_c nunca e' tocado)

    inline bool LoadColHook(int slot, uint8_t* buf, int size)
    {
        uint8_t* useBuf = buf; int useSize = size; bool swapped = false; unsigned expect = 0;
        if (g_origLoadCol && buf && size > 0)
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (Refresh() && CustomColParse::SameCollection(g_c.res, buf, (size_t)size))
            {
                t_work = g_c.data;                      // copia: o loader nativo pode escrever no buffer
                useBuf = t_work.data(); useSize = (int)t_work.size(); expect = g_c.expectTris; swapped = true;
                FLog("[COL] REPLACE slot=%d: buffer do IMG (%d bytes) -> %s (%d bytes)", slot, size, g_c.path.c_str(), useSize);
            }
        }
        if (!g_origLoadCol) return false;
        const bool ok = g_origLoadCol(slot, useBuf, useSize);
        if (swapped)
        {
            const GameState gs = Snapshot();
            LogState("REPLACE depois", gs);
            FLog("[COL] REPLACE slot=%d: LoadCol retornou %d | triangulos esperados (arquivo)=%u, no jogo=%u -> %s", slot, (int)ok, expect, gs.tris,
                 (gs.col && gs.tris == expect) ? "DADOS PERSONALIZADOS EM USO" : "DIVERGENTE (veja as linhas acima)");
        }
        return ok;
    }

    // ------------------------------------------------------------------ AUDITORIA (somente leitura; roda em CGame::Initialize)
    inline void Audit(const char* quando)
    {
        static bool done = false;
        if (done) return;
        done = true;
        const std::string r = Root();
        if (r.empty()) { FLog("[COL][AUDIT:%s] sem raiz de storage", quando); return; }
        FLog("[COL][AUDIT:%s] raiz=%s", quando, r.c_str());
        for (const auto& c : ColCandidates())
            FLog("[COL] A. %-70s %s", c.c_str(), Exists(c) ? "ENCONTRADO" : "nao existe");
        { std::lock_guard<std::mutex> lk(g_m); Refresh(true); }
        const GameState s = Snapshot();
        LogState("D. jogo agora", s);
        if (!s.mi) FLog("[COL] D. o jogo nao conhece '%s'", kTargetModel);
        FLog("[COL][AUDIT:%s] fim (o replace acontece em CColStore::LoadCol, quando o streaming carrega o slot)", quando);
    }

    // ---------------------------------------------------------------- CINFO.BIN
    inline uint64_t Fnv(const std::string& s)
    {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }

    inline void SigPart(std::string& acc, const std::string& path)
    {
        struct stat st {};
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            acc += path + "|" + std::to_string((long long)st.st_size) + "|" + std::to_string((long long)st.st_mtime) + "\n";
    }

    // assinatura das entradas que alimentam o cache de colisao
    inline std::string Signature()
    {
        const std::string r = Root();
        std::string acc;
        for (const auto& c : ColCandidates()) SigPart(acc, c);
        SigPart(acc, r + "texdb/touch.img");
        std::vector<std::string> ides;
        if (DIR* d = ::opendir((r + "SAMP").c_str()))
        {
            while (auto* e = ::readdir(d))
            {
                const size_t n = std::strlen(e->d_name);
                if (n > 4 && ::strcasecmp(e->d_name + n - 4, ".ide") == 0) ides.push_back(r + "SAMP/" + e->d_name);
            }
            ::closedir(d);
        }
        std::sort(ides.begin(), ides.end());
        for (const auto& p : ides) SigPart(acc, p);
        acc += std::string("salt=LOADCOL-HOOK-v2|off=") + (Marker("col_off.on") ? "1" : "0") + "\n";
        char hex[32];
        std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)Fnv(acc));
        return hex;
    }

    inline std::string ReadText(const std::string& p)
    {
        std::string s; char b[64];
        if (FILE* f = ::fopen(p.c_str(), "rb")) { size_t n; while ((n = ::fread(b, 1, sizeof(b), f)) > 0) s.append(b, n); ::fclose(f); }
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    }

    inline bool WriteTextAtomic(const std::string& p, const std::string& s)
    {
        const std::string tmp = p + ".tmp";
        FILE* f = ::fopen(tmp.c_str(), "wb");
        if (!f) return false;
        const bool ok = ::fwrite(s.data(), 1, s.size(), f) == s.size();
        ::fclose(f);
        if (!ok) { ::remove(tmp.c_str()); return false; }
        return ::rename(tmp.c_str(), p.c_str()) == 0;
    }

    inline void CinfoGuard()
    {
        static bool done = false;
        if (done) return;
        done = true;

        const std::string r = Root();
        if (r.empty()) { FLog("[CINFO] sem raiz de storage: cache nao tocado"); return; }
        if (Marker("cinfo_keep.on")) { FLog("[CINFO] SAMP/cinfo_keep.on presente: cache nao tocado"); return; }

        const char* rels[] = { "CINFO.BIN", "cinfo.bin", "models/CINFO.BIN", "MODELS/CINFO.BIN", "texdb/CINFO.BIN" };
        const std::string sigPath = r + "SAMP/cinfo.sig";
        const std::string sigNow  = Signature();
        const std::string sigOld  = ReadText(sigPath);

        bool touched = false, anyFound = false;
        for (const char* rel : rels)
        {
            const std::string p = r + rel;
            struct stat st {};
            if (::stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            anyFound = true;
            const bool empty = st.st_size == 0;
            const bool stale = sigOld != sigNow;
            FLog("[CINFO] %s: %lld bytes | assinatura atual=%s gravada=%s -> %s", p.c_str(), (long long)st.st_size,
                 sigNow.c_str(), sigOld.empty() ? "(nenhuma)" : sigOld.c_str(), (empty || stale) ? "INVALIDO" : "ok");
            if (!empty && !stale) continue;

            // nome de backup que nunca sobrescreve um backup anterior (rename() sobrescreve em silencio)
            std::string bak = p + ".bak-" + std::to_string((long long)::time(nullptr));
            for (int n = 2; Exists(bak) && n < 1000; n++)
                bak = p + ".bak-" + std::to_string((long long)::time(nullptr)) + "-" + std::to_string(n);
            if (::rename(p.c_str(), bak.c_str()) == 0)
            {
                touched = true;
                FLog("[CINFO] cache antigo guardado em %s; o jogo vai gerar um CINFO.BIN novo nesta execucao", bak.c_str());
            }
            else
                FLog("[CINFO] NAO consegui renomear %s (errno=%d): cache mantido como esta", p.c_str(), errno);
        }
        if (!anyFound) FLog("[CINFO] nenhum CINFO.BIN em %s: o jogo vai gerar um (assinatura=%s)", r.c_str(), sigNow.c_str());

        // so grava a assinatura quando a decisao foi concluida sem erro (evita "marcar como ok" um cache que nao foi trocado)
        if (sigOld != sigNow && (touched || !anyFound))
            FLog("[CINFO] assinatura %s em %s", WriteTextAtomic(sigPath, sigNow + "\n") ? "gravada" : "NAO gravada", sigPath.c_str());
    }

    inline void (*g_origStartCache)() = nullptr;
    inline void StartCacheHook()
    {
        CinfoGuard();           // antes de o CColAccel abrir/ler o cache
        if (g_origStartCache) g_origStartCache();
        // valores CRUS das globais do CColAccel depois do startCache (diagnostico; o significado de cada numero
        // nao foi confirmado - compare 1a execucao x seguintes)
        static int* st  = reinterpret_cast<int*>(CHook::getSym("_ZN9CColAccel13m_iCacheStateE"));
        static int* num = reinterpret_cast<int*>(CHook::getSym("_ZN9CColAccel14m_iNumColItemsE"));
        FLog("[CINFO] depois do startCache: m_iCacheState=%d m_iNumColItems=%d", st ? *st : -1, num ? *num : -1);
    }


    inline void Install()
    {
        // 1) replace direto no ponto de carregamento nativo
        CHook::InlineHook("_ZN9CColStore7LoadColEiPhi", &LoadColHook, &g_origLoadCol);
        // 2) cache CINFO.BIN invalidado quando as entradas mudam
        CHook::InlineHook("_ZN9CColAccel10startCacheEv", &StartCacheHook, &g_origStartCache);
        FLog("[COL] hooks instalados: CColStore::LoadCol + CColAccel::startCache");
    }
    // nome antigo mantido: Streaming.cpp do patch v1 continua compilando sem alteracao
    inline void InstallCinfoGuard() { Install(); }
}
