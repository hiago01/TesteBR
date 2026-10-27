#pragma once

// ---------------------------------------------------------------------------
// COL v3 - MISTURA POR ENTRADA + INDICE EM CACHE   (CColStore::LoadCol + CINFO.BIN)
//
// Evidencia (libGTASA.so arm64 do projeto): o jogo carrega cada .col do gta3.img por
//     CStreaming::ConvertBufferToObject -> CColStore::LoadCol(int slot, uchar* buf, int size)
//     -> CFileLoader::LoadCollisionFile[FirstTime](buf, size, slot)
// v3 intercepta LoadCol. Para o buffer que o streaming entrega (as entradas do .col do IMG), cada entrada cujo NOME
// existe nos seus arquivos externos e' trocada pela sua; as demais ficam como vieram do IMG. O buffer misturado
// (mesmo slot) vai ao loader NATIVO - nenhuma validacao nativa e' desligada. Antes de entregar, o buffer final
// passa por Parse + ValidateAll; se algo falhar entrega o buffer ORIGINAL do IMG.
//
// ONDE COLOCAR OS .col (dentro de Android/data/com.samp.mobile/files/):
//     texdb/col/*.col   e   SAMP/col/*.col     <- QUALQUER nome, cada arquivo so com os modelos alterados
//     (legado, ainda funciona: texdb/countrye_10.col, SAMP/countrye_10.col, countrye_10.col na raiz)
//   Ordem: legados, depois texdb/col (ordem alfabetica), depois SAMP/col. Nome repetido: o ULTIMO vence (vai pro log).
// INDICE: SAMP/col_index.txt guarda so os nomes/offsets (validado por caminho+mtime+tamanho de cada arquivo).
//     Arquivo de dados so e' lido do disco quando o LoadCol casa com um nome, e liberado logo depois.
// MARCADORES (arquivos vazios em SAMP/):  col_off.on = desliga tudo   cinfo_keep.on = nao mexe no CINFO.BIN
// LOG (samp_log.txt): prefixos [COL] e [CINFO]
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cerrno>
#include <strings.h>
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
    inline constexpr const char* kLegacyFile  = "countrye_10.col";
    inline constexpr const char* kTargetModel = "CE_brewery";          // so para a auditoria
    inline constexpr int         kTargetId    = MODEL_CE_BREWERY;      // 12931
    inline constexpr size_t      kMaxColBytes = 64u * 1024u * 1024u;   // por arquivo
    inline constexpr size_t      kMaxMixBytes = 96u * 1024u * 1024u;   // buffer misturado
    inline constexpr int         kMaxLoggedEntries = 80;

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

    inline std::string Lower(const char* s, size_t n)
    {
        std::string o; o.reserve(n);
        for (size_t i = 0; i < n && s[i]; i++) o.push_back((char)::tolower((unsigned char)s[i]));
        return o;
    }

    // arquivos .col candidatos: legados + texdb/col/*.col + SAMP/col/*.col (ordem de precedencia crescente)
    inline std::vector<std::string> ColCandidates()
    {
        std::vector<std::string> out;
        const std::string r = Root();
        if (r.empty()) return out;
        out.push_back(r + "texdb/" + kLegacyFile);
        out.push_back(r + "SAMP/" + kLegacyFile);
        out.push_back(r + kLegacyFile);
        for (const char* dir : { "texdb/col/", "SAMP/col/" })
        {
            std::vector<std::string> names;
            if (DIR* d = ::opendir((r + dir).c_str()))
            {
                while (auto* e = ::readdir(d))
                {
                    const size_t n = std::strlen(e->d_name);
                    if (n > 4 && ::strcasecmp(e->d_name + n - 4, ".col") == 0) names.push_back(e->d_name);
                }
                ::closedir(d);
            }
            std::sort(names.begin(), names.end());
            for (const auto& nm : names) out.push_back(r + dir + nm);
        }
        return out;
    }

    inline bool IsRegular(const std::string& p, struct stat* st = nullptr)
    {
        struct stat s {};
        if (::stat(p.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) return false;
        if (st) *st = s;
        return true;
    }

    // le o arquivo inteiro; em falha devolve errno e a etapa
    inline bool ReadAll(const std::string& path, std::vector<uint8_t>& out, size_t& fileSize, int* err = nullptr, const char** step = nullptr)
    {
        out.clear(); fileSize = 0; if (err) *err = 0; if (step) *step = "";
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) { if (err) *err = errno; if (step) *step = "stat"; return false; }
        fileSize = (size_t)st.st_size;
        if (fileSize == 0)           { if (step) *step = "arquivo vazio"; return false; }
        if (fileSize > kMaxColBytes) { if (step) *step = "maior que o limite"; return false; }
        FILE* f = ::fopen(path.c_str(), "rb");
        if (!f) { if (err) *err = errno; if (step) *step = "fopen"; return false; }
        out.resize(fileSize);
        errno = 0;
        const size_t got = ::fread(out.data(), 1, fileSize, f);
        const int e = errno;
        ::fclose(f);
        if (got != fileSize) { out.clear(); if (err) *err = e; if (step) *step = "fread (leu parcial)"; return false; }
        return true;
    }

    inline void DiagFile(const std::string& p)
    {
        struct stat st {};
        if (::stat(p.c_str(), &st) != 0) { FLog("[COL]   %s -> nao existe (errno=%d %s)", p.c_str(), errno, std::strerror(errno)); return; }
        FLog("[COL]   %s | %lld bytes | modo=%o dono=%d grupo=%d | app uid=%d | R_OK=%s", p.c_str(), (long long)st.st_size,
             (unsigned)(st.st_mode & 0777), (int)st.st_uid, (int)st.st_gid, (int)::getuid(), ::access(p.c_str(), R_OK) == 0 ? "sim" : "NAO");
    }

    // ------------------------------------------------- estado do jogo para um modelo (somente leitura)
    using FindFn = CBaseModelInfo* (*)(const char*, int*);
    struct GameState { int id = -1; void* mi = nullptr; void* col = nullptr; int slot = -1; bool vol = false; unsigned tris = 0; };

    inline GameState Snapshot(const char* model = kTargetModel)
    {
        static FindFn find = reinterpret_cast<FindFn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        GameState g; int id = -1;
        CBaseModelInfo* mi = find(model, &id);
        g.id = id; g.mi = mi;
        if (mi && mi->m_pColModel)
        {
            CColModel* c = mi->m_pColModel;
            g.col = c; g.slot = c->m_nColSlot; g.vol = c->m_bHasCollisionVolumes; g.tris = c->GetTriCount();
        }
        return g;
    }
    inline void LogState(const char* tag, const char* model, const GameState& g)
    {
        FLog("[COL][%s] '%s' -> id=%d | modelInfo=%p | colModel=%p slot=%d volumes=%d triangulos=%u", tag, model, g.id, g.mi, g.col, g.slot, (int)g.vol, g.tris);
    }

    // ------------------------------------------------------------------ INDICE (so nomes/offsets; dados ficam no disco)
    struct IdxFile  { std::string path; long long mtime = 0; long long size = 0; };
    struct IdxEntry { std::string name; int file = -1; uint32_t offset = 0; uint32_t size = 0; uint8_t ver = 0; uint32_t tris = 0; };

    inline std::vector<IdxFile> g_files;
    inline std::unordered_map<std::string, IdxEntry> g_idx;       // chave = nome em minusculas
    inline std::mutex g_m;
    inline std::chrono::steady_clock::time_point g_lastCheck{}, g_lastFail{};
    inline bool g_haveChecked = false, g_indexed = false, g_hadFail = false;
    inline std::string g_key;
    inline bool g_fromCache = false;                              // diagnostico: o ultimo indice veio do cache?

    inline std::string IndexCachePath() { const std::string r = Root(); return r.empty() ? std::string() : r + "SAMP/col_index.txt"; }

    inline std::string KeyOf(const std::vector<std::string>& c)
    {
        std::string k; struct stat st {};
        for (const auto& p : c)
            if (IsRegular(p, &st)) k += p + "|" + std::to_string((long long)st.st_mtime) + "|" + std::to_string((long long)st.st_size) + ";";
        if (Marker("col_off.on")) k += "OFF;";
        return k;
    }

    inline unsigned TrisOfEntry(const uint8_t* d, const CustomColParse::Entry& e)
    {
        return e.version >= 2 ? (unsigned)CustomColParse::Rd16(d + e.offset + 4 + 72) : 0u;
    }

    inline void SaveIndexCache()
    {
        const std::string p = IndexCachePath();
        if (p.empty()) return;
        std::string o = "V3\n";
        for (size_t i = 0; i < g_files.size(); i++)
        {
            o += "F\t" + g_files[i].path + "\t" + std::to_string(g_files[i].mtime) + "\t" + std::to_string(g_files[i].size) + "\n";
            // so as entradas cujo arquivo e' este E que ainda sao a versao vencedora no indice
            for (const auto& kv : g_idx)
                if (kv.second.file == (int)i)
                    o += "E\t" + kv.second.name + "\t" + std::to_string(kv.second.offset) + "\t" + std::to_string(kv.second.size) + "\t" +
                         std::to_string((int)kv.second.ver) + "\t" + std::to_string(kv.second.tris) + "\n";
        }
        const std::string tmp = p + ".tmp";
        if (FILE* f = ::fopen(tmp.c_str(), "wb"))
        {
            const bool ok = ::fwrite(o.data(), 1, o.size(), f) == o.size();
            ::fclose(f);
            if (ok && ::rename(tmp.c_str(), p.c_str()) == 0) return;
            ::remove(tmp.c_str());
        }
        FLog("[COL] aviso: nao consegui gravar o indice em %s (errno=%d %s) - sem cache, so mais lento no boot", p.c_str(), errno, std::strerror(errno));
    }

    // true se o cache em disco descreve EXATAMENTE os arquivos existentes agora (caminho+mtime+tamanho)
    inline bool LoadIndexCache(const std::vector<std::string>& existing)
    {
        const std::string p = IndexCachePath();
        if (p.empty() || !Exists(p)) return false;
        std::vector<uint8_t> raw; size_t sz = 0;
        if (!ReadAll(p, raw, sz)) return false;
        std::string txt(raw.begin(), raw.end());
        std::vector<IdxFile> files; std::unordered_map<std::string, IdxEntry> idx;
        size_t pos = 0; bool first = true;
        while (pos < txt.size())
        {
            size_t nl = txt.find('\n', pos); if (nl == std::string::npos) nl = txt.size();
            std::string line = txt.substr(pos, nl - pos); pos = nl + 1;
            if (first) { if (line != "V3") return false; first = false; continue; }
            std::vector<std::string> f; size_t a = 0;
            for (;;) { size_t t = line.find('\t', a); if (t == std::string::npos) { f.push_back(line.substr(a)); break; } f.push_back(line.substr(a, t - a)); a = t + 1; }
            if (f[0] == "F" && f.size() == 4) { IdxFile x; x.path = f[1]; x.mtime = std::atoll(f[2].c_str()); x.size = std::atoll(f[3].c_str()); files.push_back(x); }
            else if (f[0] == "E" && f.size() == 6 && !files.empty())
            {
                IdxEntry e; e.name = f[1]; e.file = (int)files.size() - 1; e.offset = (uint32_t)std::strtoul(f[2].c_str(), nullptr, 10);
                e.size = (uint32_t)std::strtoul(f[3].c_str(), nullptr, 10); e.ver = (uint8_t)std::atoi(f[4].c_str()); e.tris = (uint32_t)std::strtoul(f[5].c_str(), nullptr, 10);
                if (e.name.empty() || e.size < 24) return false;
                idx[Lower(e.name.c_str(), e.name.size())] = e;
            }
            else if (!line.empty()) return false;
        }
        if (files.size() != existing.size()) return false;
        struct stat st {};
        for (size_t i = 0; i < files.size(); i++)
        {
            if (files[i].path != existing[i] || !IsRegular(existing[i], &st)) return false;
            if (files[i].mtime != (long long)st.st_mtime || files[i].size != (long long)st.st_size) return false;
        }
        for (const auto& kv : idx)                         // sanidade: entrada dentro do arquivo
            if ((long long)kv.second.offset + 8 + kv.second.size > files[kv.second.file].size) return false;
        g_files = std::move(files); g_idx = std::move(idx);
        return true;
    }

    // le + valida cada arquivo e monta o indice. Entradas invalidas sao ignoradas (as boas do mesmo arquivo continuam).
    inline void BuildIndex(const std::vector<std::string>& existing)
    {
        g_files.clear(); g_idx.clear(); g_hadFail = false;
        int logged = 0;
        for (const auto& path : existing)
        {
            DiagFile(path);
            struct stat st {};
            if (!IsRegular(path, &st)) continue;
            std::vector<uint8_t> data; size_t sz = 0; int err = 0; const char* step = "";
            if (!ReadAll(path, data, sz, &err, &step))
            {
                FLog("[COL]     REPROVADO na etapa '%s' (errno=%d '%s', tamanho=%zu) -> arquivo ignorado (nova tentativa em 30 s)", step, err, std::strerror(err), sz);
                g_hadFail = true; continue;
            }
            const CustomColParse::Result res = CustomColParse::Parse(data.data(), data.size());
            if (res.status != CustomColParse::Status::Ok || res.entries.empty())
            { FLog("[COL]     estrutura invalida (%s) -> arquivo ignorado", CustomColParse::StatusStr(res.status)); continue; }
            IdxFile fi; fi.path = path; fi.mtime = (long long)st.st_mtime; fi.size = (long long)st.st_size;
            g_files.push_back(fi); const int fidx = (int)g_files.size() - 1;
            int ok = 0, bad = 0;
            for (const auto& e : res.entries)
            {
                char why[200] = {0};
                if (!CustomColParse::ValidateEntry(data.data(), data.size(), e, why, sizeof why))
                { bad++; FLog("[COL]     entrada '%s' REPROVADA: %s -> ignorada", e.name, why); continue; }
                IdxEntry x; x.name = e.name; x.file = fidx; x.offset = e.offset; x.size = e.size; x.ver = e.version; x.tris = TrisOfEntry(data.data(), e);
                const std::string key = Lower(e.name, 22);
                auto it = g_idx.find(key);
                if (it != g_idx.end() && it->second.file != fidx)
                    FLog("[COL]     '%s' ja vinha de %s -> o ultimo vence: %s", e.name, g_files[it->second.file].path.c_str(), path.c_str());
                g_idx[key] = x; ok++;
                if (logged++ < kMaxLoggedEntries) FLog("[COL]     #%03d %-24s %s size=%-8u triangulos=%u", ok, e.name, e.ident, e.size, x.tris);
            }
            FLog("[COL]     %s: %d entradas validas, %d reprovadas", path.c_str(), ok, bad);
        }
    }

    // chamar com g_m travado. true = existe indice nao vazio.
    inline bool Refresh(bool force = false)
    {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        if (!force && g_haveChecked && duration_cast<milliseconds>(now - g_lastCheck).count() < 2000) return !g_idx.empty();
        g_haveChecked = true; g_lastCheck = now;

        const auto cands = ColCandidates();
        const std::string key = KeyOf(cands);
        const bool retry = g_hadFail && duration_cast<seconds>(now - g_lastFail).count() >= 30;
        if (!force && g_indexed && key == g_key && !retry) return !g_idx.empty();
        g_key = key; g_indexed = true;

        if (Marker("col_off.on"))
        {
            FLog("[COL] SAMP/col_off.on presente: mistura DESLIGADA (jogo usa so os .col do IMG)");
            g_idx.clear(); g_files.clear(); g_hadFail = false; return false;
        }
        std::vector<std::string> existing;
        for (const auto& c : cands) if (IsRegular(c)) existing.push_back(c);
        if (existing.empty())
        {
            FLog("[COL] nenhum .col externo em texdb/col, SAMP/col nem nos caminhos legados -> mistura inativa (jogo usa o IMG)");
            g_idx.clear(); g_files.clear(); g_hadFail = false; return false;
        }
        FLog("[COL] %zu arquivo(s) .col externo(s):", existing.size());
        if (!force && LoadIndexCache(existing)) { g_fromCache = true; g_hadFail = false; FLog("[COL] indice carregado do CACHE (SAMP/col_index.txt): %zu nomes em %zu arquivos - nenhum .col foi lido do disco", g_idx.size(), g_files.size()); }
        else
        {
            g_fromCache = false;
            BuildIndex(existing);
            if (!g_hadFail) SaveIndexCache();
        }
        if (g_hadFail) g_lastFail = now;
        FLog("[COL] indice: %zu nomes validos em %zu arquivo(s)%s", g_idx.size(), g_files.size(), g_idx.empty() ? " -> mistura inativa" : "");
        return !g_idx.empty();
    }

    // ------------------------------------------------------------------ MISTURA
    struct Replaced { std::string name; unsigned expectTris; };

    // Monta em 'out' o buffer do IMG com as entradas trocadas. false = nada a trocar OU algo suspeito (entrega o original).
    inline bool BuildMixed(const uint8_t* in, size_t n, std::vector<uint8_t>& out, std::vector<Replaced>& rep, size_t& imgEntries)
    {
        struct Inc { size_t off; size_t sz; const IdxEntry* hit; };
        std::vector<Inc> v; size_t off = 0; bool any = false;
        while (off + 32 <= n && CustomColParse::VersionOf(in + off))
        {
            uint32_t sz = 0; std::memcpy(&sz, in + off + 4, 4);
            if (sz < 24 || (uint64_t)off + 8u + sz > n) break;
            auto it = g_idx.find(Lower((const char*)in + off + 8, 22));
            const IdxEntry* hit = (it != g_idx.end()) ? &it->second : nullptr;
            if (hit) any = true;
            v.push_back({ off, 8u + (size_t)sz, hit });
            off += 8u + sz;
        }
        imgEntries = v.size();
        if (!any || v.empty()) return false;
        for (size_t i = off; i < n; i++) if (in[i] != 0) { FLog("[COL] MISTURA abortada: sobra de %zu bytes NAO zerados depois da ultima entrada do IMG (offset %zu) -> uso o buffer original", n - off, off); return false; }

        std::map<int, std::vector<uint8_t>> files;           // arquivos lidos so durante esta chamada
        out.clear(); out.reserve(n + 4096);
        for (const auto& e : v)
        {
            if (!e.hit) { out.insert(out.end(), in + e.off, in + e.off + e.sz); continue; }
            auto fit = files.find(e.hit->file);
            if (fit == files.end())
            {
                std::vector<uint8_t> d; size_t sz = 0; int err = 0; const char* step = "";
                const IdxFile& f = g_files[e.hit->file]; struct stat st {};
                if (!IsRegular(f.path, &st) || (long long)st.st_size != f.size || (long long)st.st_mtime != f.mtime)
                { FLog("[COL] MISTURA abortada: %s mudou desde o indice (reindexa em ate 2 s) -> uso o buffer original", f.path.c_str()); g_haveChecked = false; g_key.clear(); return false; }
                if (!ReadAll(f.path, d, sz, &err, &step))
                { FLog("[COL] MISTURA abortada: falha ao ler %s (etapa '%s' errno=%d '%s') -> uso o buffer original", f.path.c_str(), step, err, std::strerror(err)); g_hadFail = true; g_lastFail = std::chrono::steady_clock::now(); return false; }
                fit = files.emplace(e.hit->file, std::move(d)).first;
            }
            const auto& d = fit->second; const IdxEntry& h = *e.hit;
            if ((uint64_t)h.offset + 8u + h.size > d.size() || CustomColParse::VersionOf(d.data() + h.offset) == 0)
            { FLog("[COL] MISTURA abortada: entrada '%s' fora do arquivo %s -> uso o buffer original", h.name.c_str(), g_files[h.file].path.c_str()); return false; }
            out.insert(out.end(), d.data() + h.offset, d.data() + h.offset + 8u + h.size);
            rep.push_back({ h.name, h.tris });
            if (out.size() > kMaxMixBytes) { FLog("[COL] MISTURA abortada: buffer final passou de %zu bytes", kMaxMixBytes); return false; }
        }
        // o buffer final tem que ser um COL valido inteiro
        const CustomColParse::Result r = CustomColParse::Parse(out.data(), out.size());
        char why[200] = {0};
        if (!CustomColParse::ValidateAll(out.data(), out.size(), r, why, sizeof why))
        { FLog("[COL] MISTURA reprovada na validacao do buffer final: %s -> uso o buffer original", why); return false; }
        return true;
    }

    // assinatura confirmada pelo simbolo exportado _ZN9CColStore7LoadColEiPhi:  bool LoadCol(int slot, uchar* buf, int size)
    inline bool (*g_origLoadCol)(int, uint8_t*, int) = nullptr;
    inline thread_local std::vector<uint8_t> t_mix;       // copia mutavel entregue ao jogo

    inline bool LoadColHook(int slot, uint8_t* buf, int size)
    {
        uint8_t* useBuf = buf; int useSize = size; std::vector<Replaced> rep; size_t imgEntries = 0;
        if (g_origLoadCol && buf && size > 0)
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (Refresh() && BuildMixed(buf, (size_t)size, t_mix, rep, imgEntries))
            {
                useBuf = t_mix.data(); useSize = (int)t_mix.size();
                FLog("[COL] MISTURA slot=%d: IMG %zu entradas (%d bytes) -> %zu trocada(s), buffer final %d bytes", slot, imgEntries, size, rep.size(), useSize);
            }
            else rep.clear();
        }
        if (!g_origLoadCol) return false;
        const bool ok = g_origLoadCol(slot, useBuf, useSize);
        if (!rep.empty())
        {
            FLog("[COL] MISTURA slot=%d: LoadCol retornou %d", slot, (int)ok);
            for (size_t i = 0; i < rep.size() && i < 8; i++)
            {
                const GameState gs = Snapshot(rep[i].name.c_str());
                const bool cmp = rep[i].expectTris != 0;
                FLog("[COL]   '%s': id=%d slot=%d triangulos esperados=%u no jogo=%u -> %s", rep[i].name.c_str(), gs.id, gs.slot, rep[i].expectTris, gs.tris,
                     !gs.col ? "SEM colModel no jogo" : (!cmp ? "(sem contagem para COL1)" : (gs.tris == rep[i].expectTris ? "DADOS PERSONALIZADOS EM USO" : "DIVERGENTE")));
            }
            if (rep.size() > 8) FLog("[COL]   ... e mais %zu entradas trocadas (conferencia so das 8 primeiras)", rep.size() - 8);
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
        { std::lock_guard<std::mutex> lk(g_m); Refresh(true); }
        const GameState s = Snapshot();
        LogState("jogo agora", kTargetModel, s);
        FLog("[COL][AUDIT:%s] fim (a mistura acontece em CColStore::LoadCol, quando o streaming carrega cada slot)", quando);
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
        acc += std::string("salt=LOADCOL-MIX-v3|off=") + (Marker("col_off.on") ? "1" : "0") + "\n";
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
        // 1) mistura por entrada no ponto de carregamento nativo
        CHook::InlineHook("_ZN9CColStore7LoadColEiPhi", &LoadColHook, &g_origLoadCol);
        // 2) cache CINFO.BIN invalidado quando as entradas mudam
        CHook::InlineHook("_ZN9CColAccel10startCacheEv", &StartCacheHook, &g_origStartCache);
        FLog("[COL] v3 (mistura por entrada) - hooks instalados: CColStore::LoadCol + CColAccel::startCache");
    }
    // nome antigo mantido: Streaming.cpp do patch v1 continua compilando sem alteracao
    inline void InstallCinfoGuard() { Install(); }
}
