#pragma once

// ---------------------------------------------------------------------------
// touch.img  (veiculos + skins customizados, carregados por ID)
//
//   IMG:       <raiz>/texdb/touch.img       (raiz = Android/data/com.samp.mobile/files/)
//   Texturas:  <raiz>/texdb/touch/          (banco de texturas "touch")
//   IDs:       SAMP/samp.ide e SAMP/vehicles.ide (e peds.ide, se for skin) - ja existentes
//
// COMO FUNCIONA (nada de comando novo):
//   1. CStreaming::InitImageList registra TEXDB\TOUCH.IMG na lista de IMGs. O jogo le so o
//      DIRETORIO do IMG (nome/posicao/tamanho de cada entrada); o conteudo NAO vai para a memoria.
//   2. Ao ler o IDE, CFileLoader::LoadVehicleObject / LoadPedObject chamam
//      CStreaming::GetModelCDName(id) -> "touch" e fazem SetTexDictionary(txd, "touch").
//      Ou seja, o banco de texturas de cada modelo do touch.img e' o banco "touch".
//   3. TextureDatabaseRuntime::Load("touch", fullyLoad=false) carrega so o indice do banco;
//      cada textura e' lida do disco quando o modelo e' usado.
//   4. O DFF/TXD so e' lido do touch.img quando o seu codigo existente cria o veiculo/skin com o
//      ID (CVehicle::CVehicle / CPlayerPed::SetModelIndex / CActor -> CStreaming::TryLoadModel).
//
// Se touch.img (ou a pasta texdb/touch) nao existir, o modulo simplesmente nao registra nada.
//
// POR QUE OS VEICULOS NAO CARREGAVAM (causa raiz, confirmada no desassembly da libGTASA):
//   CStreaming::Init2 chama CModelInfoAccelerator::Begin("MODELS\\MINFO.BIN") e depois, para cada IMG,
//   CStreaming::LoadCdDirectory(img, slot). Para cada .dff do diretorio o jogo chama
//   CModelInfoAccelerator::GetEntry(nome). Quando o arquivo MINFO.BIN ja existe, o GetEntry NAO procura
//   o modelo pelo nome: ele le o ID da N-esima posicao do cache (GetNextModelInfoId) e so aceita se o
//   hash do nome bater. Esse cache e POSICIONAL e foi gravado numa execucao antiga (sem o touch.img /
//   sem esses IDs no IDE; posicao sem modelo vira 0xFFFF). Resultado: todas as 45 entradas do touch.img
//   caem no "nao achei", o modelo fica com m_nCdSize=0 e o TryLoadModel desiste.
//   Correcao: AcceleratorGetEntry (abaixo) substitui o GetEntry por uma busca direta pelo nome
//   (CModelInfo::GetModelInfo(nome,&id)), ignorando o MINFO.BIN. Plano B: AttachFromTouch liga o
//   modelo ao touch.img na hora do TryLoadModel se o mapeamento automatico ainda assim falhar.
// ---------------------------------------------------------------------------

#include <string>
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <dirent.h>
#include <strings.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/stat.h>

#include "../main.h"   // g_pszStorage
#include "../vendor/armhook/patch.h"
#include "Core/KeyGen.h"
#include "Streaming.h"
#include "game/Models/ModelInfo.h"

namespace CustomImg
{
    inline constexpr const char* kImgListName = "TEXDB\\TOUCH.IMG";   // nome na lista de IMGs do jogo
    inline constexpr const char* kDbName      = "touch";              // banco de texturas (texdb/touch/)

    inline std::string Root()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        return p;
    }

    // Sem raiz de storage nao da para checar: assume que existe (comportamento igual ao dos outros IMGs).
    // CHAVE MESTRA: false = nenhum conteudo custom (touch.img, banco touch, mistura de COL, CINFO.BIN) e' usado; jogo 100% original.
    constexpr bool kCustomContent = false;

    inline bool ImgPresent()
    {
        if (!kCustomContent) return false;
        const std::string r = Root();
        if (r.empty()) return true;
        return ::access((r + "texdb/touch.img").c_str(), R_OK) == 0;
    }

    inline bool DbPresent()
    {
        if (!kCustomContent) return false;
        const std::string r = Root();
        if (r.empty()) return true;
        struct stat st {};
        return ::stat((r + "texdb/touch").c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    // Diagnostico: escreve no log o estado real de um ID customizado, em cada etapa do caminho
    //   IDE -> ModelInfo -> diretorio do IMG -> slot do IMG -> banco de texturas.
    inline void LogModelState(int modelId, const char* quando)
    {
        if (modelId < 0 || modelId >= (int)TOTAL_DFF_MODEL_IDS) return;
        auto* mi = CModelInfo::GetModelInfo(modelId);
        auto& info = CStreaming::GetInfo(modelId);
        const int img = info.m_nImgId;
        const char* imgName = (img >= 0 && img < (int)TOTAL_IMG_ARCHIVES) ? CStreaming::ms_files[img].m_szName : "?";
        const int imgHandle = (img >= 0 && img < (int)TOTAL_IMG_ARCHIVES) ? CStreaming::ms_files[img].m_StreamHandle : -1;
        const char* db = info.HasCdPosnAndSize() ? CStreaming::GetModelCDName(modelId) : "(sem entrada no IMG)";
        FLog("[TOUCH][%s] id=%d modelInfo=%p txdIdx=%d | img=%d '%s' handle=%d posn=%u size=%u state=%d | db='%s'",
             quando, modelId, (void*)mi, mi ? (int)mi->m_nTxdIndex : -1,
             img, imgName, imgHandle, (unsigned)info.m_nCdPosn, (unsigned)info.m_nCdSize,
             (int)info.m_nLoadState, db);
        if (!mi)
            FLog("[TOUCH] id=%d: SEM ModelInfo -> o vehicles.ide nao foi lido (ou a linha falhou no parse)", modelId);
    }

    // -----------------------------------------------------------------------------------------
    // CORRECAO: ignora o cache posicional MODELS\MINFO.BIN do CModelInfoAccelerator.
    // Substitui CModelInfoAccelerator::GetEntry(CBaseModelInfo** outMi, int* outId, char* nome)
    // (chamado por CStreaming::LoadCdDirectory para cada .dff do diretorio de cada IMG) por uma
    // busca direta pelo nome, que e exatamente o caminho "sem cache" do proprio jogo.
    // -----------------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------------
    // INDICE PROPRIO nome->id (substitui o MINFO.BIN posicional): SAMP/modelindex.bin
    //   - guarda pares (chave do nome, id do modelo), NAO posicoes: uma troca de IMG nao o corrompe;
    //   - cada acerto e CONFERIDO (ms_modelInfoPtrs[id]->m_nKey == chave do nome); se nao confere, cai na busca lenta
    //     do proprio jogo e o par e corrigido: o indice nunca liga um nome ao modelo errado;
    //   - assinatura (nome+tamanho+data) de texdb/*.img, SAMP/*.ide e SAMP/extra_imgs.txt: se mudou, o arquivo antigo e
    //     descartado inteiro (IMG novo, mapa novo, IDE novo -> reconstroi sozinho na proxima carga);
    //   - nomes SEM modelo nao sao guardados (um IDE novo passa a ser achado na hora).
    // -----------------------------------------------------------------------------------------
    namespace ModelIndex
    {
        inline std::unordered_map<uint32_t, int32_t> g_map;
        inline bool     g_loaded = false, g_dirty = false;
        inline uint64_t g_sig = 0;

        inline void Fnv(uint64_t& h, const char* s) { while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; } h ^= 0xFF; h *= 1099511628211ULL; }
        inline void FnvNum(uint64_t& h, unsigned long long v) { char b[32]; snprintf(b, sizeof(b), "%llu", v); Fnv(h, b); }

        inline void SigDir(uint64_t& h, const std::string& dir, const char* ext)
        {
            std::vector<std::string> names;
            if (DIR* d = ::opendir(dir.c_str())) {
                while (dirent* e = ::readdir(d)) {
                    const size_t n = strlen(e->d_name), l = strlen(ext);
                    if (n > l && strcasecmp(e->d_name + n - l, ext) == 0) names.push_back(e->d_name);
                }
                ::closedir(d);
            }
            std::sort(names.begin(), names.end());
            for (const auto& n : names) {
                struct stat st {};
                if (::stat((dir + n).c_str(), &st) != 0) continue;
                Fnv(h, n.c_str()); FnvNum(h, (unsigned long long)st.st_size); FnvNum(h, (unsigned long long)st.st_mtime);
            }
        }

        inline uint64_t Signature()
        {
            uint64_t h = 1469598103934665603ULL;
            const std::string r = Root();
            SigDir(h, r + "texdb/", ".img");
            SigDir(h, r + "SAMP/", ".ide");
            struct stat st {};
            if (::stat((r + "SAMP/extra_imgs.txt").c_str(), &st) == 0) { FnvNum(h, (unsigned long long)st.st_size); FnvNum(h, (unsigned long long)st.st_mtime); }
            return h;
        }

        inline std::string FilePath() { const std::string r = Root(); return r.empty() ? r : r + "SAMP/modelindex.bin"; }

        inline void LoadOnce()
        {
            if (g_loaded) return;
            g_loaded = true;
            g_sig = Signature();
            const std::string p = FilePath();
            if (p.empty()) return;
            FILE* f = ::fopen(p.c_str(), "rb");
            if (!f) { FLog("[MINDEX] sem indice salvo: sera criado nesta carga"); g_dirty = true; return; }
            char magic[4]; uint32_t ver = 0, cnt = 0; uint64_t sig = 0;
            bool ok = ::fread(magic, 1, 4, f) == 4 && ::memcmp(magic, "MIDX", 4) == 0 && ::fread(&ver, 4, 1, f) == 1 && ver == 1 &&
                      ::fread(&sig, 8, 1, f) == 1 && ::fread(&cnt, 4, 1, f) == 1 && cnt < 200000;
            if (ok && sig != g_sig) { FLog("[MINDEX] assinatura mudou (IMG/IDE alterado): indice antigo descartado, sera refeito"); ok = false; g_dirty = true; }
            if (ok) {
                g_map.reserve(cnt);
                for (uint32_t i = 0; i < cnt; i++) { uint32_t k; int32_t id; if (::fread(&k, 4, 1, f) != 1 || ::fread(&id, 4, 1, f) != 1) { ok = false; break; } g_map[k] = id; }
                if (!ok) { g_map.clear(); g_dirty = true; FLog("[MINDEX] arquivo truncado/corrompido: sera refeito"); }
                else FLog("[MINDEX] indice carregado: %u nomes", (unsigned)g_map.size());
            } else if (!g_dirty) { g_dirty = true; FLog("[MINDEX] indice invalido: sera refeito"); }
            ::fclose(f);
        }

        inline void SaveIfDirty()
        {
            if (!g_dirty || !g_loaded) return;
            g_dirty = false;
            const std::string p = FilePath();
            if (p.empty() || g_map.empty()) return;
            const std::string tmp = p + ".tmp";
            FILE* f = ::fopen(tmp.c_str(), "wb");
            if (!f) return;
            const uint32_t ver = 1, cnt = (uint32_t)g_map.size();
            bool ok = ::fwrite("MIDX", 1, 4, f) == 4 && ::fwrite(&ver, 4, 1, f) == 1 && ::fwrite(&g_sig, 8, 1, f) == 1 && ::fwrite(&cnt, 4, 1, f) == 1;
            for (const auto& kv : g_map) { if (!ok) break; ok = ::fwrite(&kv.first, 4, 1, f) == 1 && ::fwrite(&kv.second, 4, 1, f) == 1; }
            ::fclose(f);
            if (ok && ::rename(tmp.c_str(), p.c_str()) == 0) FLog("[MINDEX] indice gravado: %u nomes", cnt);
            else { ::remove(tmp.c_str()); FLog("[MINDEX] NAO consegui gravar o indice (sem espaco/permissao?)"); }
        }
    }

    inline void AcceleratorGetEntry(void* /*accelerator*/, CBaseModelInfo** outMi, int* outId, const char* name)
    {
        using Fn = CBaseModelInfo* (*)(const char*, int*);
        static Fn fn = reinterpret_cast<Fn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        // estatistica de custo: chamadas, acertos do indice, sem-modelo e tempo total, logados de 4000 em 4000 chamadas
        static unsigned long long s_calls = 0, s_hits = 0, s_nomodel = 0, s_ns = 0;
        const auto t0 = std::chrono::steady_clock::now();
        ModelIndex::LoadOnce();
        const uint32_t key = CKeyGen::GetUppercaseKey(name);
        bool hit = false;
        const auto it = ModelIndex::g_map.find(key);
        if (it != ModelIndex::g_map.end()) {
            const int32_t id = it->second;
            CBaseModelInfo* mi = (id >= 0 && id < (int32_t)CModelInfo::NUM_MODEL_INFOS) ? CModelInfo::ms_modelInfoPtrs[id] : nullptr;
            if (mi && mi->m_nKey == key) { *outMi = mi; *outId = id; hit = true; s_hits++; }   // conferido: nome <-> modelo certo
        }
        if (!hit) {
            *outMi = fn(name, outId);                                   // busca do proprio jogo (lenta, sempre correta)
            if (*outMi) {
                const auto cur = ModelIndex::g_map.find(key);
                if (cur == ModelIndex::g_map.end() || cur->second != *outId) { ModelIndex::g_map[key] = *outId; ModelIndex::g_dirty = true; }
            }
        }
        s_ns += (unsigned long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        s_calls++;
        if (!*outMi) s_nomodel++;
        if (s_calls % 4000 == 0)
            FLog("[MINFO] ligacao por nome: %llu entradas, %llu do indice, %llu sem modelo no IDE, %.1f ms no total", s_calls, s_hits, s_nomodel, s_ns / 1e6);
    }

    inline void InstallAcceleratorBypass()
    {
        CHook::Redirect("_ZN21CModelInfoAccelerator8GetEntryEPP14CBaseModelInfoPiPc", &AcceleratorGetEntry);
    }

    // -----------------------------------------------------------------------------------------
    // Diretorio do touch.img lido direto do disco (so o cabecalho, nada vai para a memoria do jogo).
    // -----------------------------------------------------------------------------------------
    struct DirEntry
    {
        uint32_t offset;     // em setores de 2048 bytes
        uint32_t size;       // em setores (streaming size)
        char     name[28];   // nome completo, ex.: "audip8.dff"
        char     base[28];   // sem extensao, ex.: "audip8"
        bool     isDff;
        uint32_t key;        // CKeyGen::GetUppercaseKey(base)
    };

    inline const std::vector<DirEntry>& Directory()
    {
        static std::vector<DirEntry> dir;
        static bool loaded = false;
        if (loaded) return dir;
        loaded = true;

        const std::string r = Root();
        if (r.empty()) return dir;
        FILE* f = ::fopen((r + "texdb/touch.img").c_str(), "rb");
        if (!f) { FLog("[TOUCH] Directory(): nao abriu %stexdb/touch.img", r.c_str()); return dir; }

        char magic[4] = {};
        uint32_t count = 0;
        if (::fread(magic, 1, 4, f) == 4 && ::fread(&count, 4, 1, f) == 1 && ::memcmp(magic, "VER2", 4) == 0 && count < 65536)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                uint8_t raw[32];
                if (::fread(raw, 1, 32, f) != 32) break;
                DirEntry e{};
                uint32_t off; uint16_t s1, s2;
                ::memcpy(&off, raw + 0, 4); ::memcpy(&s1, raw + 4, 2); ::memcpy(&s2, raw + 6, 2);
                e.offset = off;
                e.size   = s2 ? s2 : s1;                      // mesma regra do LoadCdDirectory
                ::memcpy(e.name, raw + 8, 24); e.name[24] = 0;
                ::strncpy(e.base, e.name, sizeof(e.base) - 1);
                char* dot = ::strchr(e.base, '.');
                e.isDff = dot && ::strncasecmp(dot, ".dff", 4) == 0;
                if (dot) *dot = 0;
                e.key = CKeyGen::GetUppercaseKey(e.base);
                dir.push_back(e);
            }
        }
        else
            FLog("[TOUCH] Directory(): touch.img nao e um IMG v2 valido (magic/contagem)");
        ::fclose(f);
        return dir;
    }

    inline int ImgSlot()
    {
        for (int i = 0; i < (int)TOTAL_IMG_ARCHIVES; i++)
            if (::strcmp(CStreaming::ms_files[i].m_szName, kImgListName) == 0) return i;
        return -1;
    }

    // PLANO B: se o modelo (pelo hash do nome) existe no touch.img mas o diretorio nao foi ligado
    // a ele pelo LoadCdDirectory, liga na mao (mesmos campos que o proprio LoadCdDirectory escreve).
    inline bool AttachFromTouch(int modelId)
    {
        if (modelId < 0 || modelId >= (int)TOTAL_DFF_MODEL_IDS) return false;
        auto* mi = CModelInfo::GetModelInfo(modelId);
        if (!mi) return false;
        const int slot = ImgSlot();
        if (slot < 0) return false;
        for (const auto& e : Directory())
        {
            if (!e.isDff || e.key != mi->m_nKey) continue;
            auto& info = CStreaming::GetInfo(modelId);
            info.m_nCdPosn = e.offset;
            info.m_nCdSize = e.size;
            info.m_nImgId  = (uint8_t)slot;
            FLog("[TOUCH] plano B: modelo %d ('%s') ligado manualmente ao touch.img (slot %d posn=%u size=%u)",
                 modelId, e.name, slot, e.offset, e.size);
            return info.HasCdPosnAndSize();
        }
        return false;
    }

    // -----------------------------------------------------------------------------------------
    // AUDITORIA: para CADA entrada do touch.img, diz se o nome existe em algum IDE, se o diretorio
    // foi ligado ao modelo e se aponta para o slot certo. Chamada uma vez, depois que os IDEs e os
    // diretorios dos IMGs ja foram lidos (CGame::Initialize).
    // -----------------------------------------------------------------------------------------
    inline void Audit(const char* quando)
    {
        const auto& dir = Directory();
        const int slot = ImgSlot();
        FLog("[TOUCH][AUDIT:%s] touch.img: %d entradas | slot do IMG=%d | handle=%d",
             quando, (int)dir.size(), slot, slot >= 0 ? CStreaming::ms_files[slot].m_StreamHandle : -1);

        int ok = 0, semIde = 0, semLink = 0, outroImg = 0, ignorados = 0;
        using Fn = CBaseModelInfo* (*)(const char*, int*);
        static Fn find = reinterpret_cast<Fn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        for (const auto& e : dir)
        {
            if (!e.isDff) { ignorados++; continue; }
            int id = -1;
            CBaseModelInfo* mi = find(e.base, &id);
            if (!mi || id < 0)
            {
                semIde++;
                FLog("[TOUCH][AUDIT] %-24s SEM MODELO: nenhum IDE declara '%s' (confira o nome no vehicles.ide/samp.ide)", e.name, e.base);
                continue;
            }
            auto& info = CStreaming::GetInfo(id);
            if (!info.HasCdPosnAndSize())
            {
                semLink++;
                FLog("[TOUCH][AUDIT] %-24s id=%d: IDE ok, mas o diretorio do IMG NAO foi ligado (m_nCdSize=0)", e.name, id);
            }
            else if (slot >= 0 && info.m_nImgId != slot)
            {
                outroImg++;
                FLog("[TOUCH][AUDIT] %-24s id=%d: ligado a OUTRO IMG (img=%d '%s'), nao ao touch.img (%d) - nome duplicado?",
                     e.name, id, info.m_nImgId,
                     info.m_nImgId < TOTAL_IMG_ARCHIVES ? CStreaming::ms_files[info.m_nImgId].m_szName : "?", slot);
            }
            else
            {
                ok++;
                FLog("[TOUCH][AUDIT] %-24s id=%d OK (posn=%u size=%u setores)", e.name, id, (unsigned)info.m_nCdPosn, (unsigned)info.m_nCdSize);
            }
        }
        FLog("[TOUCH][AUDIT:%s] RESUMO: ok=%d | sem modelo no IDE=%d | IDE ok mas sem ligacao ao IMG=%d | ligado a outro IMG=%d | nao-dff=%d",
             quando, ok, semIde, semLink, outroImg, ignorados);
    }
}
