#pragma once
// =====================================================================================
//  CarBau (Maximo RP) - BAUS (malas laterais + bauleto) da moto modelo 1194 (mx_bf400), por VEICULO.
//
//  COMO O JOGO FAZ: no DFF o bau e o frame "extra1" (um "extra" do GTA). Ao criar cada moto o jogo
//  (CVehicleModelInfo::CreateInstance -> ChooseComponent) SORTEIA se clona o extra para dentro do veiculo: por isso o bau
//  aparecia "aleatoriamente".  Aqui o sorteio e trocado por uma decisao do servidor:
//    * ao criar uma moto 1194 forcamos CVehicleModelInfo::ms_compsToUse = {0, -1} (extra1 SEMPRE clonado, 2o extra nenhum;
//      o proprio jogo devolve o valor para -2 = sorteio normal depois de usar; nos restauramos de novo por seguranca);
//    * a visibilidade do bau e ligada/desligada no atomico clonado (flag rpATOMICRENDER, a mesma que o jogo usa para esconder
//      componentes) conforme a lista que o servidor manda.  Sem lista ainda => baus escondidos.
//  SERVIDOR -> CLIENTE  RPC 245 op 30:  u16 n | n x u16 (ids dos veiculos 1194 COM bau)   -- lista completa (substitui a anterior)
//  So o modelo 1194 e tocado; qualquer falha deixa o jogo como estava.
// =====================================================================================
class CVehicleGTA;
namespace RakNet { class BitStream; }

namespace CarBau
{
    constexpr int kModel = 1194;
    void OnPacket(RakNet::BitStream& bs);          // op 30 (depois do byte da op)
    void Reset();                                  // nova conexao/desconexao: lista vazia (baus escondidos)
    void BeforeCreate(int modelId);                // CVehiclePool::New, antes de criar o veiculo
    void AfterCreate(int vehId, CVehicleGTA* v);   // depois de criar: restaura o sorteio e aplica a visibilidade
    void Tick();                                   // 1x por frame (game/hooks.cpp): reaplica se a lista mudou e confere de tempos em tempos
}
