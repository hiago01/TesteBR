#pragma once

// ---------------------------------------------------------------------------
// Skin customizada (DFF) para o modelo 7
//
//   arquivo:  <raiz de armazenamento>/SAMP/skins/skin.dff
//             (normalmente Android/data/com.samp.mobile/files/SAMP/skins/skin.dff)
//
// Comandos de chat (/skin7 ...), todos executados na thread do jogo pelo tick:
//   load    lê o arquivo, valida (DffPreflight, sem engine) e testa o leitor do jogo
//   apply   instala o DFF no modelo 7 (carrega antes, se preciso)
//   wear    põe o ped LOCAL no modelo 7 (para ver o resultado sem mexer no servidor)
//   unwear  devolve o ped local ao modelo 0
//   info / status / unload
//
// COMO A TROCA FUNCIONA (conferido na libGTASA 2.10 arm64 do projeto)
//   * Modelo 7 = MODEL_MALE01, o ped "placeholder": o jogo o carrega na partida e o
//     CStreaming::RemoveModel do projeto nunca o descarrega. Por isso a troca é feita
//     NO LUGAR, no CPedModelInfo já carregado, e não por hook de carregamento.
//   * Sequência (a mesma do engine em RemoveModel + LoadClumpFile):
//       RpClumpStreamRead(bytes do arquivo)        -> clump novo (leitor do próprio jogo)
//       CPedModelInfo::DeleteRwObject()  @0x45D744 -> destrói o clump-molde antigo e o
//                                                    hit-col (e solta a ref do TXD)
//       CPedModelInfo::SetClump(novo)    @0x45D514 -> base CClumpModelInfo::SetClump
//                                                    (liga HAnim aos atomics skinned,
//                                                    normaliza pesos, ref do TXD), SetFrameIds
//                                                    (m_pPedIds) e CreateHitColModelSkinned
//   * Peds já existentes são clones e não mudam; só quem for criado depois usa o novo molde.
//   * CPed::SetModelIndex chama RpAnimBlendClumpInit na INSTÂNCIA (conferido no binário);
//     o molde não precisa disso.
//   * A troca não persiste: reiniciar o jogo volta ao modelo original (e kAutoApply refaz
//     a troca na partida seguinte, se o arquivo existir).
//
// APLICAÇÃO AUTOMÁTICA (kAutoApply) E TRAVAS DE SEGURANÇA
//   * Quando o modelo 7 estiver carregado, lê SAMP/skins/skin.dff e instala (1x por sessão).
//   * SAMP/skins/disable  (arquivo vazio) desliga a aplicação automática.
//   * SAMP/skins/.applying é criado antes de instalar e apagado ~15 s depois; se ele existir
//     na partida seguinte, a aplicação automática é PULADA (a anterior pode ter crashado).
//   * Ped que já usa o modelo 7 não muda sozinho: use /skin7 wear ou troque a skin de novo.
//
// LIMITES CONHECIDOS
//   * As 42 texturas do DFF não existem nos bancos TEXDB: aparecem sem textura (parte 3:
//     carregar PNGs/TXD próprios).
//   * O DFF não tem os 12 frames-marcador (Smid, Shead, ...) que SetFrameIds procura por
//     nome; o jogo ignora os ausentes.
//   * Por ser o placeholder, qualquer ped sem modelo carregado aparece com o modelo 7.
//
// Thread: tudo que toca o RenderWare roda na thread do jogo (CGame::Process ->
// ClothesFrameTick -> CCustomSkin::Process). O chat só liga flags.
// ---------------------------------------------------------------------------

#include <string>

class CCustomSkin
{
public:
    static constexpr int  kTargetModelId = 7;      // MODEL_MALE01
    static constexpr bool kAutoApply     = true;   // aplica sozinho (1x por sessão) quando o modelo 7 estiver pronto

    static void RequestLoad();     // qualquer thread
    static void RequestUnload();
    static void RequestApply();
    static void RequestWear(bool on);

    static void Process();         // thread do jogo, 1x por frame

    static std::string Info();     // última mensagem
    static std::string DebugCommand(const std::string& args);   // "/skin7 ..."
};
