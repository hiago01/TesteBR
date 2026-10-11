#pragma once

// ---------------------------------------------------------------------------
// Skin customizada (DFF) - FASE 1: leitura e validação
//
//   arquivo:  <raiz de armazenamento>/SAMP/skins/skin.dff
//             (normalmente Android/data/com.samp.mobile/files/SAMP/skins/skin.dff)
//   alvo:     skin id 7 (kTargetModelId)
//
// Fase 1 (esta): lê o arquivo, valida a estrutura SEM tocar no engine
// (DffPreflight.h) e então usa o próprio leitor do jogo (RwStreamOpen em memória +
// RwStreamFindChunk + RpClumpStreamRead, o mesmo que CFileLoader::LoadClumpFile usa)
// para criar o RpClump. O clump NÃO é instalado no modelo 7.
//
// Thread: tudo que toca o RenderWare roda na thread do jogo, dentro de
// CGame::Process() (ClothesFrameTick -> CCustomSkin::Process). O chat só liga flags.
//
// FASE 2 (futura) - substituição do modelo 7. Endereços conferidos na tabela de símbolos
// da libGTASA 2.10 arm64; o comportamento marcado "a confirmar" ainda não foi verificado:
//   * CModelInfo::ms_modelInfoPtrs[7]        @ g_libGTASA + 0xB666E0 (tipo ped = 7)
//   * CPedModelInfo::SetClump(RpClump*)      @ 0x45D514 (vtable +0x88): faz SetFrameIds
//     (IDs HAnim -> ossos) e CreateHitColModelSkinned; é o que LoadClumpFile chama.
//   * RpAnimBlendClumpInit (0x46A7D4): no SA original roda nas INSTÂNCIAS (CreateInstance),
//     não no clump-molde (a confirmar no 2.10).
//   * Para a troca "pegar": hookar CFileLoader::LoadClumpFile(RwStream*, uint) @ 0x4D054C
//     e, quando modelId == 7, instalar um RpClumpClone do nosso clump em vez de ler
//     o do gta3.img (CStreaming::ConvertBufferToObject continua com a contabilidade normal).
//   * Trocar só sem peds usando o modelo 7 (ou depois de CStreaming::RemoveModel(7)
//     @ 0x391FF0), nunca no meio do frame de render.
//   * As 42 texturas do DFF não existem nos bancos TEXDB do jogo: sem um TXD/DB
//     próprio elas aparecem sem textura.
// ---------------------------------------------------------------------------

#include <string>

class CCustomSkin
{
public:
    static constexpr int kTargetModelId = 7;

    static void RequestLoad();     // qualquer thread
    static void RequestUnload();   // qualquer thread
    static void Process();         // thread do jogo, 1x por frame

    static std::string Info();     // resumo do último carregamento
    static std::string DebugCommand(const std::string& args);   // "/skin7 ..."
};
