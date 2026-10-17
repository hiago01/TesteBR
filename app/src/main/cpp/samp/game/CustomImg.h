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
    inline bool ImgPresent()
    {
        const std::string r = Root();
        if (r.empty()) return true;
        return ::access((r + "texdb/touch.img").c_str(), R_OK) == 0;
    }

    inline bool DbPresent()
    {
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
    inline void AcceleratorGetEntry(void* /*accelerator*/, CBaseModelInfo** outMi, int* outId, const char* name)
    {
        using Fn = CBaseModelInfo* (*)(const char*, int*);
        static Fn fn = reinterpret_cast<Fn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        *outMi = fn(name, outId);
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
