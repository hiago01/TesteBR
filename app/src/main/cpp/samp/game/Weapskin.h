#pragma once
#include <cstdint>

// =====================================================================================
//  Weapskin (Maximo RP) - "skin" de arma com modelo 3D novo + som proprio, mantendo as caracteristicas da arma original
//
//  Ex.: o jogador usa a M4 (arma 31, dano/municao/animacao normais) mas ve o FN FAL (modelo 332) na mao e ouve o tiro
//  novo (SAMP/sons/fnfal.wav). Quem NAO esta com a skin continua com a M4 original. Quem manda e o SERVIDOR (RPC 206):
//      uint16 playerId | uint8 tipoDeArma | uint16 modeloNovo (0 = volta ao modelo original)
//  Visto por todos que tem o jogador no stream. Maximo 1 skin por jogador (uma arma).
//
//  Ganchos (nativos, por simbolo): CPed::AddWeaponModel (troca o modelo da mao) e
//  CAEWeaponAudioEntity::WeaponFire (toca o som novo no lugar do original).
//  Se o modelo nao estiver carregavel ou o BASS nao estiver iniciado, cai no comportamento original.
// =====================================================================================

void Weapskin_InstallHooks();                                   // 1x, junto dos outros hooks (game/hooks.cpp)
void Weapskin_OnServerPacket(uint16_t playerId, uint8_t weapon, uint16_t model);
void Weapskin_Reset();
void Weapskin_Process();                                        // 1x por frame (thread do jogo): reaplica o modelo na mao
