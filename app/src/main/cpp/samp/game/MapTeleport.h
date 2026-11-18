#pragma once
// =====================================================================================
//  Teleporte pelo mapa (admin) - Maximo RP.
//  O servidor (maximo_mapa.inc, RPC 204 op5) decide QUEM teleporta (admin) e manda x,y + dica de altura. O servidor NAO conhece a altura do
//  chao, entao o CLIENTE faz o resto: segura o ped no ar sobre o ponto ate o mundo carregar, acha o chao por uma linha vertical (a mesma
//  consulta de colisao do jogo), espera o valor ficar estavel, posiciona o ped e avisa o servidor (/mapax tpok <z>).
//  Se nao achar chao (ex.: mar), devolve o ped ao ponto de origem e avisa (/mapax tpfalha).
//
//  Chamadas:
//    MapTeleport_Start(x,y,zDica)  <- gui/samp_widgets/worldmap.cpp, OnServerPacket (op 5)   (qualquer thread: so guarda o pedido)
//    MapTeleport_Update()          <- game/hooks.cpp, RenderEffects() (thread do jogo)
//    MapTeleport_Reset()           <- nova conexao / desconexao
// =====================================================================================
void MapTeleport_Start(float x, float y, float zHint);   // zHint < -900 = sem dica
void MapTeleport_Update();
void MapTeleport_Reset();
bool MapTeleport_Active();
