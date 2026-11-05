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
// ONDE COLOCAR (dentro de Android/data/com.maximo.roleplay/files/):
//     texdb/ipl/<nome_do_ipl>.ipl    e    SAMP/ipl/<nome_do_ipl>.ipl      (ordem: texdb, depois SAMP, alfabetica;
//     em conflito no mesmo indice o ULTIMO vence)
// (sem chave para desligar: o IPL esta sempre ativo; nao ha SAMP/ipl_off.on nem chave 'ipl' no diag.ini)
// LOG (samp_log.txt): prefixo [IPL]
//
// Cobre so IPL BINARIO em streaming (os "*_stream*.ipl" do gta3.img/gta_int.img). IPL em texto carregado pelo gta.dat
// (data/maps) passa por CFileLoader::LoadScene e NAO e' tocado.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <map>
#include <set>
#include "CustomColNew.h"
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
    inline std::chrono::steady_clock::time_point g_lastHide{};
    inline std::chrono::steady_clock::time_point g_lastCheck{};
    inline std::set<std::string> g_seenNames;                     // diagnostico: nomes de IPL ja vistos
    inline std::vector<CustomIplParse::HideOp> g_hide;           // secao 'hide' de todos os arquivos (global)
    inline std::map<int, int> g_hideCount;                       // quantas vezes cada id foi escondido (limita o log)
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
        g_src.clear(); g_byIpl.clear(); g_hide.clear(); g_hideCount.clear(); g_loggedKey.clear(); g_sig = sig; g_scanned = true;
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
    // modelo novo cuja colisao nao pode ser criada (CustomColNew): instancia ignorada, o jogo nao cai em CEntity::GetBoundRect
    inline int ResolveModelSafe(const char* name, int fileId)
    {
        const int id = ResolveModel(name, fileId);
        if (id >= 0 && CustomColNew::IsFailed(id)) return -1;
        return id;
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
        CustomIplParse::g_removeByMove = !Marker("remove_drop_off.on");
        uint8_t* useBuf = buf; int useSize = size; bool mixed = false; std::string key;
        static const bool kIplOff = Marker("ipl_off.on");
        if (!kIplOff && g_origLoadIpl && buf && size >= (int)CustomIplParse::kHdrSize && std::memcmp(buf, "bnry", 4) == 0)
        {
            // O jogo pode carregar IPLs em streaming (LoadScene -> SetupRelatedIpls) ANTES de CGame::Initialize chegar ao fim, que e' onde
            // CustomColNew::Run roda. Modelo novo sem colisao derruba o jogo em CEntity::GetBoundRect (ex.: troca da prefeitura 3980 -> 12001).
            // Por isso a colisao dos modelos de SAMP/extra_cols.txt e' ligada aqui, uma vez, antes da primeira mistura (idempotente:
            // modelo que ja tem colisao e' mantido; o Run de CGame::Initialize depois so loga "ja tem colisao").
            static bool s_colNewDone = false;
            if (!s_colNewDone) { s_colNewDone = true; CustomColNew::Run("antes do IPL"); }
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
                        if (CustomIplParse::Merge(buf, (size_t)size, files, ResolveModelSafe, t_mix, rep, &why))
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
        if (g_hide.empty() || !CustomIplParse::HideMatch(g_hide, modelId, pos)) return modelId;
        if (!ModelIdExists(kInvisibleModel))
        {
            static bool said = false; if (!said) { said = true; FLog("[IPL] hide: modelo invisivel %d NAO existe neste cliente -> hide ignorado (nada foi escondido)", kInvisibleModel); }
            return modelId;
        }
        const int n = ++g_hideCount[modelId];
        if (n <= 5) FLog("[IPL] hide: instancia escondida: id=%d pos=(%.2f, %.2f, %.2f) -> modelo %d", modelId, pos[0], pos[1], pos[2], kInvisibleModel);
        else if (n == 6) FLog("[IPL] hide: id=%d casou com mais de 5 instancias (so id, sem posicao?) - confira se e' isso mesmo; log omitido", modelId);
        return kInvisibleModel;
    }

    // HIDE SEM MODELO INVISIVEL. O modelo 19300 tem geometria SEM buffer de GPU: quando a instancia escondida e' um LOD (ex.: 5535,
    // o LOD do posto antigo), o qsort de CRenderer::RenderEverythingBarRoads (sortLODs) le o buffer nulo e o jogo cai com
    // SIGSEGV 'Fault address 0x30' (libGTASA+0x250610). Agora a instancia mantem o PROPRIO modelo e so desce kHideDrop metros: fica
    // muito alem da distancia de desenho (LOD ou nao) e continua na mesma posicao da lista (o indice dos demais lods nao muda).
    inline constexpr float kHideDrop = 3000.0f;
    inline bool HideInstance(int modelId, float pos[3])
    {
        std::lock_guard<std::mutex> lk(g_m);
        const auto now = std::chrono::steady_clock::now();
        if (!g_scanned || now - g_lastHide >= std::chrono::seconds(2)) { g_lastHide = now; Refresh(); }
        if (g_hide.empty() || !CustomIplParse::HideMatch(g_hide, modelId, pos)) return false;
        const int n = ++g_hideCount[modelId];
        if (n <= 5) FLog("[IPL] hide: instancia escondida: id=%d pos=(%.2f, %.2f, %.2f) -> mesmo modelo, z - %.0f (sem modelo invisivel)", modelId, pos[0], pos[1], pos[2], kHideDrop);
        else if (n == 6) FLog("[IPL] hide: id=%d casou com mais de 5 instancias (so id, sem posicao?) - confira se e' isso mesmo; log omitido", modelId);
        pos[2] -= kHideDrop;
        return true;
    }

    // Funil de TODA instancia criada pelo jogo: CFileLoader::LoadObjectInstance(CFileObjectInstance*, const char*) (0x4d20fc).
    // Confirmado no desassembly: a versao texto (0x4d206c) faz sscanf e chama este (bl 0x4d20fc); LoadIpl binario chama este via PLT.
    // Layout (lido do desassembly): +0x00 x, +0x04 y, +0x08 z (floats), +0x1C modelId (int32). Nao depende do FileLoader.cpp.
    inline void* (*g_origLoadObjInst)(void*, const char*) = nullptr;
    inline void* LoadObjInstHook(void* inst, const char* name)
    {
        // INTERRUPTORES DE DIAGNOSTICO (arquivos vazios em SAMP/, lidos 1x por execucao; nenhum presente = comportamento normal):
        //   inst_off.on = este hook nao faz nada | hide_off.on = sem 'hide' | remove_drop_off.on = 'remove' apaga em vez de afastar
        //   colnew_off.on = nao liga extra_cols antes da 1a instancia | ipl_off.on = sem mistura de IPL binario
        static const bool kInstOff = Marker("inst_off.on"), kNoHide = Marker("hide_off.on"), kNoColNew = Marker("colnew_off.on");
        static bool s_said = false;
        if (!s_said) { s_said = true; FLog("[IPL] interruptores: inst_off=%d hide_off=%d colnew_off=%d ipl_off=%d remove_drop_off=%d", (int)kInstOff, (int)kNoHide, (int)kNoColNew, (int)Marker("ipl_off.on"), (int)Marker("remove_drop_off.on")); }
        if (kInstOff) return g_origLoadObjInst ? g_origLoadObjInst(inst, name) : nullptr;
        // A colisao dos modelos novos (SAMP/extra_cols.txt) precisa existir ANTES da primeira instancia de QUALQUER IPL
        // (texto em LoadScene ou binario em LoadIpl/SetupRelatedIpls). Quando o jogador salvou a posicao perto de um modelo novo
        // (ex.: prefeitura 12001), o IPL dessa area carrega dentro de CFileLoader::LoadScene, ANTES de CGame::Initialize terminar
        // (onde o Run antigo rodava). Idempotente: modelo que ja tem colisao e' mantido.
        static bool s_colNewDone = false;
        if (!s_colNewDone) { s_colNewDone = true; if (!kNoColNew) CustomColNew::Run("antes da 1a instancia"); }
        if (inst)
        {
            auto* b = static_cast<unsigned char*>(inst);
            float pos[3]; int32_t id;
            std::memcpy(pos, b, sizeof(pos)); std::memcpy(&id, b + 0x1C, sizeof(id));
            int nid = id;
            if (!kNoHide)
            {
                float p2[3] = { pos[0], pos[1], pos[2] };
                if (HideInstance(id, p2)) std::memcpy(b + 8, &p2[2], sizeof(float));   // z da instancia (+0x08)
            }
            // SEM colisao "vazia" aqui: dar colisao a um modelo antes de o jogo atribuir a verdadeira fazia o LOD ficar com a caixa de 1 m
            // (LODs sumiam). A rede de seguranca contra CEntity::GetBoundRect fica em CEntity::Add (hooks.cpp), so' para quem realmente cairia.
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
