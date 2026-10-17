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
// ---------------------------------------------------------------------------

#include <string>
#include <unistd.h>
#include <sys/stat.h>

#include "../main.h"   // g_pszStorage
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
}
