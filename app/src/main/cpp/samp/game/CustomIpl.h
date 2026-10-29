#pragma once

// ---------------------------------------------------------------------------
// IPL por ENTRADA (instancia) - mistura no ponto de carregamento nativo   (CIplStore::LoadIpl)
//
// Mesma ideia do COL v3 (CustomCol.h): o IPL binario do gta3.img/gta_int.img chega ao jogo por
//     CStreaming::ConvertBufferToObject -> CIplStore::LoadIpl(int slot, uchar* buf, int size)   (0x395270 -> 0x33b5f4)
// Aqui o buffer do IMG e' misturado com os ajustes de  texdb/ipl/<nome_do_ipl>.ipl  e o buffer misturado vai ao
// LoadIpl NATIVO (mesmo slot). Qualquer duvida -> entrega o buffer ORIGINAL do IMG.
// Layout, enderecos confirmados no desassembly e formato do arquivo: ver CustomIplParse.h e LEIA-ME_IPL.md.
//
// ONDE COLOCAR (dentro de Android/data/com.samp.mobile/files/):
//     texdb/ipl/<nome_do_ipl>.ipl    e    SAMP/ipl/<nome_do_ipl>.ipl      (ordem: texdb, depois SAMP, alfabetica;
//     em conflito no mesmo indice o ULTIMO vence)
// MARCADOR (arquivo vazio):  SAMP/ipl_off.on  = desliga tudo
// LOG (samp_log.txt): prefixo [IPL]
//
// Cobre so IPL BINARIO em streaming (os "*_stream*.ipl" do gta3.img/gta_int.img). IPL em texto carregado pelo gta.dat
// (data/maps) passa por CFileLoader::LoadScene e NAO e' tocado.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../main.h"   // g_pszStorage, FLog
#include "../vendor/armhook/patch.h"
#include "game/Models/ModelInfo.h"
#include "constants.h"      // TOTAL_DFF_MODEL_IDS
#include "CustomIplParse.h"

namespace CustomIpl
{
    inline constexpr size_t kMaxFileBytes = 2u * 1024u * 1024u;   // por arquivo de ajuste
    inline constexpr int    kMaxNotesLogged = 14;

    // ----------------------------------------------------------------- caminhos
    inline std::string Root()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        return p;
    }
    inline bool Exists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }
    inline bool Marker(const char* rel) { const std::string r = Root(); return !r.empty() && Exists(r + "SAMP/" + rel); }

    inline std::string Norm(const char* name)    // "CountryE_Stream0.IPL" -> "countrye_stream0"
    {
        std::string s = name ? name : "";
        const size_t sl = s.find_last_of("/\\"); if (sl != std::string::npos) s.erase(0, sl + 1);
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".ipl") == 0) s.erase(s.size() - 4);
        return s;
    }

    // ----------------------------------------------------------------- indice dos arquivos
    struct Src { std::string path; std::string key; long long mtime = 0, size = 0; CustomIplParse::OverrideFile of; };

    inline std::mutex g_m;
    inline std::vector<Src> g_src;
    inline std::map<std::string, std::vector<size_t>> g_byIpl;   // chave -> indices em g_src (ordem de prioridade)
    inline std::string g_sig;
    inline bool g_scanned = false;
    inline bool g_off = false;                                    // SAMP/ipl_off.on presente
    inline std::chrono::steady_clock::time_point g_lastHide{};
    inline std::chrono::steady_clock::time_point g_lastCheck{};
    inline std::set<std::string> g_seenNames;                     // diagnostico: nomes de IPL ja vistos
    inline std::vector<CustomIplParse::HideOp> g_hide;           // secao 'hide' de todos os arquivos (global)
    inline std::set<std::string> g_loggedKey;                     // loga a mistura de cada IPL so na 1a vez

    inline bool ReadFile(const std::string& p, std::string& out, long long& mt, long long& sz)
    {
        struct stat st{};
        if (::stat(p.c_str(), &st) != 0) return false;
        mt = (long long)st.st_mtime; sz = (long long)st.st_size;
        if (st.st_size < 0 || (size_t)st.st_size > kMaxFileBytes) { FLog("[IPL]   %s: %lld bytes passa do limite de %zu -> ignorado", p.c_str(), sz, kMaxFileBytes); return false; }
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f)
        {
            const int e = errno;   // mesmo diagnostico do [COL]: quem e' o dono, qual o modo, e se ESTE app (uid) consegue ler
            FLog("[IPL]   %s: nao consegui abrir (errno=%d %s) | modo=%o dono=%d grupo=%d | app uid=%d | R_OK=%s", p.c_str(), e, std::strerror(e),
                 (unsigned)(st.st_mode & 07777), (int)st.st_uid, (int)st.st_gid, (int)::getuid(), ::access(p.c_str(), R_OK) == 0 ? "sim" : "NAO");
            return false;
        }
        out.resize((size_t)st.st_size);
        const size_t got = out.empty() ? 0 : std::fread(&out[0], 1, out.size(), f);
        std::fclose(f);
        if (got != out.size()) { FLog("[IPL]   %s: leitura incompleta (%zu de %zu)", p.c_str(), got, out.size()); return false; }
        return true;
    }

    inline void ListDir(const std::string& dir, std::vector<std::string>& out)
    {
        DIR* d = ::opendir(dir.c_str());
        if (!d) return;
        std::vector<std::string> v;
        while (dirent* e = ::readdir(d))
        {
            const std::string n = e->d_name;
            if (n.size() > 4 && ::strcasecmp(n.c_str() + n.size() - 4, ".ipl") == 0) v.push_back(n);
        }
        ::closedir(d);
        std::sort(v.begin(), v.end());
        for (auto& n : v) out.push_back(dir + n);
    }

    inline std::vector<std::string> FileList()
    {
        std::vector<std::string> v; const std::string r = Root();
        if (r.empty()) return v;
        ListDir(r + "texdb/ipl/", v);
        ListDir(r + "SAMP/ipl/", v);
        return v;
    }

    inline std::string Signature(const std::vector<std::string>& files)
    {
        std::string s;
        for (auto& p : files) { struct stat st{}; if (::stat(p.c_str(), &st) == 0) s += p + "|" + std::to_string((long long)st.st_mtime) + "|" + std::to_string((long long)st.st_size) + ";"; }
        return s;
    }

    inline void Rescan(const std::vector<std::string>& files, const std::string& sig)
    {
        g_src.clear(); g_byIpl.clear(); g_hide.clear(); g_loggedKey.clear(); g_sig = sig; g_scanned = true;
        if (files.empty()) { FLog("[IPL] nenhum .ipl em texdb/ipl nem SAMP/ipl -> mistura inativa (jogo usa so os IPLs do IMG)"); return; }
        FLog("[IPL] %zu arquivo(s) de ajuste:", files.size());
        for (auto& p : files)
        {
            Src s; s.path = p;
            std::string base = p.substr(p.find_last_of('/') + 1); s.key = Norm(base.c_str());
            std::string txt;
            if (!ReadFile(p, txt, s.mtime, s.size)) continue;
            CustomIplParse::ParseOverride(txt.data(), txt.size(), s.of);
            FLog("[IPL]   %s -> IPL '%s': replace=%zu remove=%zu inst=%zu | linhas ok=%d ruins=%d", p.c_str(), s.key.c_str(), s.of.rep.size(), s.of.rem.size(), s.of.add.size(), s.of.linesOk, s.of.linesBad);
            for (size_t i = 0; i < s.of.errors.size() && i < 10; i++) FLog("[IPL]     %s", s.of.errors[i].c_str());
            if (s.of.errors.size() > 10) FLog("[IPL]     ... e mais %zu erro(s)", s.of.errors.size() - 10);
            if (s.of.Empty()) continue;
            for (auto& hd : s.of.hide) g_hide.push_back(hd);
            g_src.push_back(std::move(s));
            g_byIpl[g_src.back().key].push_back(g_src.size() - 1);
        }
        FLog("[IPL] %zu IPL(s) com ajustes validos, %zu regra(s) hide", g_byIpl.size(), g_hide.size());
    }

    // Reavalia a lista de arquivos no maximo a cada 2 s (arquivo novo/editado entra sem reiniciar o jogo)
    inline bool Refresh()
    {
        if ((g_off = Marker("ipl_off.on"))) { static bool said = false; if (!said) { said = true; FLog("[IPL] SAMP/ipl_off.on presente: mistura DESLIGADA"); } return false; }
        const auto now = std::chrono::steady_clock::now();
        if (g_scanned && now - g_lastCheck < std::chrono::seconds(2)) return !g_byIpl.empty();
        g_lastCheck = now;
        const std::vector<std::string> files = FileList();
        const std::string sig = Signature(files);
        if (!g_scanned || sig != g_sig) Rescan(files, sig);
        return !g_byIpl.empty();
    }

    // ----------------------------------------------------------------- jogo: modelo valido?
    using FindFn = CBaseModelInfo* (*)(const char*, int*);
    inline bool ModelIdExists(int id)
    {
        return id >= 0 && id < (int)TOTAL_DFF_MODEL_IDS && CModelInfo::ms_modelInfoPtrs[id] != nullptr;
    }
    // Resolve pelo NOME (como o jogo); se nao achar, vale o ID numerico (IPL decodificado com nome "dummy"/ID restaurado).
    // Retorna -1 quando nao ha ModelInfo => a entrada e' ignorada (LoadIpl nativo nao testa retorno nulo e cairia).
    inline int ResolveModel(const char* name, int fileId)
    {
        static FindFn find = reinterpret_cast<FindFn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        if (find && name && name[0] && ::strcasecmp(name, "dummy") != 0)
        {
            int id = -1;
            CBaseModelInfo* mi = find(name, &id);
            if (mi && ModelIdExists(id)) return id;
        }
        return ModelIdExists(fileId) ? fileId : -1;
    }

    // ----------------------------------------------------------------- hook
    // assinatura confirmada pelo simbolo exportado _ZN9CIplStore7LoadIplEiPhi:  bool LoadIpl(int slot, uchar* buf, int size) (retorna true)
    inline bool (*g_origLoadIpl)(int, uint8_t*, int) = nullptr;
    inline thread_local std::vector<uint8_t> t_mix;

    inline const char* SlotName(int slot)
    {
        static const char* (*get)(int) = reinterpret_cast<const char* (*)(int)>(CHook::getSym("_ZN9CIplStore10GetIplNameEi"));
        return get ? get(slot) : nullptr;
    }

    inline bool LoadIplHook(int slot, uint8_t* buf, int size)
    {
        uint8_t* useBuf = buf; int useSize = size; bool mixed = false; std::string key;
        if (g_origLoadIpl && buf && size >= (int)CustomIplParse::kHdrSize && std::memcmp(buf, "bnry", 4) == 0)
        {
            try
            {
                std::lock_guard<std::mutex> lk(g_m);
                key = Norm(SlotName(slot));
                // diagnostico: mostra (uma vez por IPL, ate 80) o nome real do slot = nome que o arquivo texdb/ipl/<nome>.ipl precisa ter
                if (g_seenNames.size() < 80 && g_seenNames.insert(key).second)
                {
                    CustomIplParse::BinView bv; const auto st = CustomIplParse::ReadBin(buf, (size_t)size, bv);
                    FLog("[IPL] IPL binario carregado: slot=%d nome='%s' instancias=%u (%d bytes)%s", slot, key.c_str(), st == CustomIplParse::BinStatus::Ok ? bv.count : 0u, size,
                         g_byIpl.count(key) ? " [com ajustes]" : "");
                }
                if (Refresh())
                {
                    auto it = g_byIpl.find(key);
                    if (!key.empty() && it != g_byIpl.end())
                    {
                        std::vector<const CustomIplParse::OverrideFile*> files;
                        for (size_t i : it->second) files.push_back(&g_src[i].of);
                        CustomIplParse::MergeReport rep; std::string why;
                        const bool first = g_loggedKey.insert(key).second;
                        if (CustomIplParse::Merge(buf, (size_t)size, files, ResolveModel, t_mix, rep, &why))
                        {
                            const std::string bad = CustomIplParse::Validate(t_mix.data(), t_mix.size(), nullptr);
                            if (bad.empty())
                            {
                                useBuf = t_mix.data(); useSize = (int)t_mix.size(); mixed = true;
                                if (first)
                                {
                                    FLog("[IPL] MISTURA '%s' slot=%d: %zu instancias no IMG -> %zu (trocadas=%zu removidas=%zu acrescentadas=%zu ignoradas=%zu lod-corrigidos=%zu), buffer %d -> %d bytes",
                                         key.c_str(), slot, rep.origCount, rep.finalCount, rep.replaced, rep.removed, rep.added, rep.skipped, rep.lodFixed, size, useSize);
                                    for (size_t i = 0; i < rep.notes.size() && i < (size_t)kMaxNotesLogged; i++) FLog("[IPL]   %s", rep.notes[i].c_str());
                                    if (rep.notes.size() > (size_t)kMaxNotesLogged) FLog("[IPL]   ... e mais %zu nota(s)", rep.notes.size() - kMaxNotesLogged);
                                }
                            }
                            else FLog("[IPL] MISTURA '%s' reprovada na validacao do buffer final: %s -> uso o buffer original", key.c_str(), bad.c_str());
                        }
                        else if (first) FLog("[IPL] '%s' slot=%d: sem mistura (%s)%s", key.c_str(), slot, why.c_str(), rep.notes.empty() ? "" : " - motivos abaixo");
                        if (first && !mixed) for (size_t i = 0; i < rep.notes.size() && i < (size_t)kMaxNotesLogged; i++) FLog("[IPL]   %s", rep.notes[i].c_str());
                    }
                }
            }
            catch (...) { FLog("[IPL] excecao na mistura do slot %d -> uso o buffer original", slot); useBuf = buf; useSize = size; mixed = false; }
        }
        if (!g_origLoadIpl) return false;
        const bool ok = g_origLoadIpl(slot, useBuf, useSize);
        if (mixed && !ok) FLog("[IPL] '%s' slot=%d: LoadIpl nativo retornou 0 com o buffer misturado", key.c_str(), slot);
        return ok;
    }

    // ----------------------------------------------------------------- hide (IPL principal em texto)
    // Chamado por CFileLoader::LoadObjectInstance(const char*) (o unico ponto que LoadScene e LoadIpl-texto usam para 'inst').
    // Devolve o modelo a usar: o proprio (nada muda) ou o modelo invisivel. A instancia continua sendo criada NA MESMA POSICAO
    // da lista => o indice de todas as outras (os lods dos IPLs de streaming) permanece igual.
    inline constexpr int kInvisibleModel = 19300;
    inline int HideModelFor(int modelId, const float pos[3])
    {
        std::lock_guard<std::mutex> lk(g_m);
        const auto now = std::chrono::steady_clock::now();            // chamado 1x por linha 'inst' de TODO IPL texto: so reavalia a cada 2 s
        if (!g_scanned || now - g_lastHide >= std::chrono::seconds(2)) { g_lastHide = now; Refresh(); }
        if (g_off || g_hide.empty() || !CustomIplParse::HideMatch(g_hide, modelId, pos)) return modelId;
        if (!ModelIdExists(kInvisibleModel))
        {
            static bool said = false; if (!said) { said = true; FLog("[IPL] hide: modelo invisivel %d NAO existe neste cliente -> hide ignorado (nada foi escondido)", kInvisibleModel); }
            return modelId;
        }
        FLog("[IPL] hide: instancia do IPL principal escondida: id=%d pos=(%.2f, %.2f, %.2f) -> modelo %d", modelId, pos[0], pos[1], pos[2], kInvisibleModel);
        return kInvisibleModel;
    }

    // Funil de TODA instancia criada pelo jogo: CFileLoader::LoadObjectInstance(CFileObjectInstance*, const char*) (0x4d20fc).
    // Confirmado no desassembly: a versao texto (0x4d206c) faz sscanf e chama este (bl 0x4d20fc); LoadIpl binario chama este via PLT.
    // Layout (lido do desassembly): +0x00 x, +0x04 y, +0x08 z (floats), +0x1C modelId (int32). Nao depende do FileLoader.cpp.
    inline void* (*g_origLoadObjInst)(void*, const char*) = nullptr;
    inline void* LoadObjInstHook(void* inst, const char* name)
    {
        if (inst)
        {
            auto* b = static_cast<unsigned char*>(inst);
            float pos[3]; int32_t id;
            std::memcpy(pos, b, sizeof(pos)); std::memcpy(&id, b + 0x1C, sizeof(id));
            const int nid = HideModelFor(id, pos);
            if (nid != id) { const int32_t v = nid; std::memcpy(b + 0x1C, &v, sizeof(v)); }
        }
        return g_origLoadObjInst ? g_origLoadObjInst(inst, name) : nullptr;
    }

    inline void Install()
    {
        static bool done = false; if (done) return; done = true;
        CHook::InlineHook("_ZN9CIplStore7LoadIplEiPhi", &LoadIplHook, &g_origLoadIpl);
        CHook::InlineHook("_ZN11CFileLoader18LoadObjectInstanceEP19CFileObjectInstancePKc", &LoadObjInstHook, &g_origLoadObjInst);
        FLog("[IPL] hook instalado: CIplStore::LoadIpl (mistura por instancia) + CFileLoader::LoadObjectInstance (hide); texdb/ipl e SAMP/ipl");
    }
}
