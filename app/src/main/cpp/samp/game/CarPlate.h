#pragma once
#include <string>

// =====================================================================================
//  CarPlate (Maximo RP) - placa brasileira (Mercosul) DINAMICA, por veiculo, para qualquer carro.
//
//  COMO FUNCIONA
//   * O modelo do carro tem UM material de placa com textura de nome conhecido (padrao: "carplate" e "brasil mercosul";
//     lista em SAMP/placas.ini). Nenhum ID de modelo esta no codigo: qualquer DFF que siga essa regra recebe a placa.
//   * Os materiais do DFF sao COMPARTILHADOS por todos os carros do mesmo modelo. Por isso a troca e feita so durante o desenho
//     de CADA carro: o jogo chama CAutomobile::CustomCarPlate_BeforeRenderingStart(this, modelInfo) antes de desenhar e
//     CustomCarPlate_AfterRenderingStop depois (simbolos conferidos pelo nome; antes eles eram so um "ret").
//     No Before pomos a textura da placa DESTE carro no material, no After devolvemos a original.
//   * O texto vem do servidor (SetVehicleNumberPlate -> CVehicle::m_szPlateText). Sem texto valido, gera-se um texto
//     estavel a partir do ID do veiculo (LLLNLNN).
//   * A textura (512x256, desenho Mercosul: faixa azul, BRASIL, bandeira, caracteres pretos) e gerada UMA vez por texto
//     (stb_truetype, fonte <storage>/fonts/BarlowCondensed-Black.ttf ou a fonte padrao arial_bold.ttf), guardada em cache
//     (maximo 32) e nunca gerada por frame.
//   * Qualquer falha (simbolo ausente, fonte ausente, textura nao criada) = a placa original do DFF continua; nada de crash.
//
//  ARQUIVO  <storage>/SAMP/placas.ini   (opcional)
//      [placas]
//      enabled=1
//      debug=0                       ; 1 = escreve os logs [PLACA] detalhados em samp_log.txt
//      textures=carplate,brasil mercosul
//
//  Os DFFs com placa (mx_cheetah 1195 e mx_bf400 1194) ja estao DENTRO do viaturas.img; o cliente nao le/troca DFF fora do IMG.
// =====================================================================================
namespace CarPlate
{
    // Instala os dois ganchos (1x, em InstallHooks). Devolve false se nao instalou (ai o chamador mantem os "ret" nativos).
    bool InstallHooks();
    bool Installed();


    // Gera o texto de teste de uma placa (usado pelo log de depuracao). So para diagnostico.
    std::string NormalizePlate(const char* raw, int vehicleId);
}
