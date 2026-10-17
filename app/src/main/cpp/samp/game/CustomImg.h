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
}
