#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
//  Giroflex (Maximo RP) - luzes de emergencia por corona, baseado nos dados do GiroflexVSL
//
//  Dados: <storage>/SAMP/giroflex.txt (gerado por tools/giroflex_export.py a partir dos JSON do GiroflexVSL).
//  Cada modelo listado la tem grupos de luzes (posicao no veiculo, cor, padrao de piscada). Sem o arquivo
//  o modulo nao faz nada. Leve: so desenha os 3 veiculos ligados mais proximos (ate 40 coronas no total).
//
//  Estado ligado/desligado: o SERVIDOR manda (RPC 205, ver servidor maximo_giro.inc):
//      uint16 vehicleId | uint8 estado (0 desligado, 1 ligado)
//  Grupos "sempre ligados" (ex.: luzes de posicao do helicoptero) piscam sem precisar de sirene.
//
//  Chamadas:
//    Giroflex_Render()          1x por frame antes de CCoronas::Render()   -> game/hooks.cpp, RenderEffects()
//    Giroflex_OnServerPacket()  -> net/scriptrpc.cpp (RPC 205)
//    Giroflex_Reset()           -> nova conexao / desconexao
//    Giroflex_Command()         -> /giroc (cliente): status | recarregar | teste (liga no veiculo atual)
// =====================================================================================

void Giroflex_OnServerPacket(uint16_t vehicleId, bool on);
void Giroflex_Reset();
void Giroflex_Render();
std::string Giroflex_Command(const std::string& args);
