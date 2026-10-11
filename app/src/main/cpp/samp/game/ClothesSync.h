#pragma once

// ---------------------------------------------------------------------------
// CJ real - roupas / tatuagens / cabelo sincronizados pelo servidor
// (GTA SA 2.10 arm64, client SA-MP 0.3.7)
//
// Fluxo:
//   servidor (Pawn) --RPC 200--> ScrSetPlayerClothes() --> CClothesSync::Receive()
//   CGame::Process() --> ClothesFrameTick() --> CClothesSync::Process():
//       SetTextureAndModel() em cada peça  +  CClothes::RebuildPlayer(ped, true)
//
// Todas as chamadas nativas rodam na thread do jogo.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

struct ClothesItem
{
    uint8_t component;    // 0..17  (ver eClothesComponent em PedClothesDesc.h)
    char    texture[32];  // "" = remover a peça
    char    model[32];    // "" = sem modelo (tatuagens usam sempre "")
};

class CClothesSync
{
public:
    static constexpr int kMaxItems = 18;

    // Chamado pelo handler do RPC. replaceAll=true remove tatuagens/acessórios
    // (componentes 4..17) que não vierem na lista. Retorna quantas peças foram aceitas.
    static int  Receive(uint16_t playerId, bool replaceAll, const ClothesItem* items, int count);

    static void ClearPlayer(uint16_t playerId);
    static void Reset();                       // nova conexão (CNetGame novo)
    static void Process();                     // 1x por frame, thread do jogo
    static void SetRemoteEnabled(bool enabled);

    // Comando de teste do chat ("/cj ..."): devolve a resposta para o chat.
    static std::string DebugCommand(const std::string& args);
};

// Tick por frame (thread do jogo). Definido em hooks.cpp e chamado por
// CGame::Process() (game.cpp), logo depois de ProcessMainThreadTasks().
void ClothesFrameTick();
