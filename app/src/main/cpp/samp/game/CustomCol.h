#pragma once

// ---------------------------------------------------------------------------
// countrye_10.col / CE_brewery  +  CINFO.BIN
//
// PASTAS (as duas do pacote do cliente; raiz = Android/data/com.samp.mobile/files/):
//   1) <raiz>/SAMP/    -> pasta do CLIENTE (samp.ide, vehicles.ide, e aqui tambem SAMP/col/)
//   2) <raiz>/texdb/   -> pasta do touch.img (texdb/touch.img + texdb/touch/)
//
// 1) AUDITORIA + NOVA LEITURA do countrye_10.col a partir de OUTRO LOCAL
//    Procura o arquivo, nesta ordem:  SAMP/col/  ->  SAMP/  ->  texdb/
//    Passo a passo (tudo no samp_log.txt, prefixo [COL]):
//      A. acha o arquivo e mostra tamanho
//      B. le o arquivo inteiro e percorre cada entrada (ident, nome, size, offset)
//      C. confirma se a entrada "CE_brewery" esta DENTRO do countrye_10.col
//      D. cruza com o jogo: CModelInfo "CE_brewery" -> id (esperado 12931) -> CColModel
//      E. (so se existir SAMP/col_reload.on) entrega o arquivo ao CFileLoader::LoadCollisionFile
//         do proprio jogo (leitura por NOME, igual ao COLFILE do gta.dat) e mostra o antes/depois
//    Sem nenhum countrye_10.col nessas pastas: 1 linha no log e nada mais (comportamento original).
//    A leitura (A-D) e' somente-leitura; so o passo E altera estado do jogo e e' opt-in.
//
// 2) CINFO.BIN (cache posicional de colisao do CColAccel; o nome "CINFO.BIN" esta no libGTASA)
//    Hook em CColAccel::startCache: ANTES do jogo ler o cache, compara uma assinatura das entradas
//    (countrye_10.col nas pastas acima, touch.img, *.ide de SAMP/) com a gravada em SAMP/cinfo.sig.
//    Cache ausente de assinatura (1a vez) ou com assinatura diferente => cache velho:
//    o arquivo e' RENOMEADO para CINFO.BIN.bak-<tempo> (nunca apagado) e o jogo regera o cache.
//    Cache de 0 bytes tambem e' tratado como invalido. SAMP/cinfo_keep.on desliga tudo isso.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <algorithm>
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

    // ordem de busca do .col: pasta do cliente (SAMP/col, SAMP) e depois a do touch.img (texdb)
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
        if (fileSize > kMaxColBytes) return false;
        FILE* f = ::fopen(path.c_str(), "rb");
        if (!f) return false;
        out.resize(fileSize);
        const size_t got = fileSize ? ::fread(out.data(), 1, fileSize, f) : 0;
        ::fclose(f);
        if (got != fileSize) { out.clear(); return false; }
        return true;
    }

    // ------------------------------------------------- estado do jogo p/ CE_brewery
    using FindFn = CBaseModelInfo* (*)(const char*, int*);

    struct GameState { int id = -1; void* mi = nullptr; void* col = nullptr; int slot = -1; bool vol = false; unsigned tris = 0; };

    inline GameState Snapshot()
    {
        static FindFn find = reinterpret_cast<FindFn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        GameState g;
        int id = -1;
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

    // ------------------------------------------------------------------ AUDITORIA
    inline void Audit(const char* quando)
    {
        static bool done = false;
        if (done) return;
        done = true;

        const std::string r = Root();
        if (r.empty()) { FLog("[COL][AUDIT:%s] sem raiz de storage: nada a fazer", quando); return; }
        FLog("[COL][AUDIT:%s] raiz=%s | cliente=%sSAMP/ | touch.img=%stexdb/", quando, r.c_str(), r.c_str(), r.c_str());

        // A. localizar
        std::string found;
        for (const auto& c : ColCandidates())
        {
            struct stat st {};
            const bool ok = ::stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode);
            FLog("[COL] A. %-70s %s", c.c_str(), ok ? "ENCONTRADO" : "nao existe");
            if (ok && found.empty()) found = c;
        }
        if (found.empty()) { FLog("[COL][AUDIT:%s] %s nao esta em SAMP/col, SAMP nem texdb: auditoria encerrada (jogo segue como antes)", quando, kColFile); return; }

        // B. ler e percorrer
        std::vector<uint8_t> buf; size_t sz = 0;
        if (!ReadAll(found, buf, sz)) { FLog("[COL] B. falha ao ler %s (tamanho=%zu, limite=%zu)", found.c_str(), sz, kMaxColBytes); return; }
        const auto res = CustomColParse::Parse(buf.data(), buf.size());
        FLog("[COL] B. %s: %zu bytes, %zu entradas, status=%s (parou em +%u, validos=%u bytes)",
             found.c_str(), sz, res.entries.size(), CustomColParse::StatusStr(res.status), res.failOffset, res.bytesOk);
        const size_t maxLog = 60;
        for (size_t i = 0; i < res.entries.size() && i < maxLog; i++)
        {
            const auto& e = res.entries[i];
            FLog("[COL] B.   #%03zu +%-8u %s size=%-8u modelId=%-5u nome='%s'", i, e.offset, e.ident, e.size, (unsigned)e.modelId, e.name);
        }
        if (res.entries.size() > maxLog) FLog("[COL] B.   ... (+%zu entradas nao listadas)", res.entries.size() - maxLog);

        // C. CE_brewery dentro do arquivo?
        const int idx = CustomColParse::Find(res, kTargetModel);
        if (idx >= 0)
        {
            const auto& e = res.entries[idx];
            FLog("[COL] C. '%s' ESTA dentro de %s: entrada #%d, +%u, %s, size=%u, modelId(no arquivo)=%u",
                 kTargetModel, kColFile, idx, e.offset, e.ident, e.size, (unsigned)e.modelId);
        }
        else
            FLog("[COL] C. '%s' NAO esta em %s (%zu entradas lidas) -> confira o nome da entrada no .col", kTargetModel, kColFile, res.entries.size());

        // D. cruzar com o jogo
        const GameState before = Snapshot();
        LogState("D. jogo agora", before);
        if (!before.mi)       FLog("[COL] D. o jogo nao conhece '%s': o IDE/IPL que declara CE_brewery nao foi lido", kTargetModel);
        else if (!before.col) FLog("[COL] D. '%s' existe no jogo mas SEM CColModel (sem colisao carregada)", kTargetModel);

        // E. nova leitura (opt-in)
        if (!Marker("col_reload.on"))
        {
            FLog("[COL] E. nova leitura desligada (para ligar: crie o arquivo vazio SAMP/col_reload.on)");
        }
        else if (res.status != CustomColParse::Status::Ok || idx < 0)
        {
            FLog("[COL] E. RECUSADO (nada foi entregue ao jogo): %s",
                 res.status != CustomColParse::Status::Ok ? CustomColParse::StatusStr(res.status) : "o arquivo e' integro mas nao contem a entrada CE_brewery");
        }
        else
        {
            // o buffer e' propositalmente "vazado" (nunca liberado): custo = tamanho do .col, uma unica vez,
            // em troca de nenhum risco de ponteiro pendurado se o loader do jogo guardar referencia ao buffer.
            auto* keep = new uint8_t[buf.size()];
            std::memcpy(keep, buf.data(), buf.size());
            using LoadFn = void (*)(uint8_t*, uint32_t, uint8_t);
            static LoadFn load = reinterpret_cast<LoadFn>(CHook::getSym("_ZN11CFileLoader17LoadCollisionFileEPhjh"));
            FLog("[COL] E. entregando %zu bytes ao CFileLoader::LoadCollisionFile (colId=0 = ligacao por nome, como COLFILE do gta.dat)", buf.size());
            load(keep, (uint32_t)buf.size(), 0);
            const GameState after = Snapshot();
            LogState("E. depois", after);
            FLog("[COL] E. %s", (after.col && after.tris > 0) ? "CE_brewery com colisao carregada apos a nova leitura"
                                                               : "CE_brewery continua SEM triangulos apos a nova leitura");
        }
        FLog("[COL][AUDIT:%s] fim", quando);
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
    }

    inline void InstallCinfoGuard()
    {
        CHook::InlineHook("_ZN9CColAccel10startCacheEv", &StartCacheHook, &g_origStartCache);
    }
}
