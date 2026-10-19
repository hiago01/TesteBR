#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
//  Cartao de identidade 3D (Maximo RP)
//
//  Quad texturizado em RwIm3D (mesma API do Hose.cpp), ancorado no osso da cabeca (osso 8, igual
//  ao CPlayerTags), billboard voltado para a camera, altura FIXA em metros (nao escala com a
//  distancia), nao e HUD 2D. Abaixo do banner ha uma "placa" com Nome (ID) / Cargo | N. funcional,
//  desenhada em runtime com stb_truetype na fonte do cliente (<storage>/fonts/arial_bold.ttf).
//
//  Animacao: na PRIMEIRA vez que o cartao de um jogador aparece ele sobe ate a altura final
//  (ease-out cubico + fade-in) e para. Nao repete ao sair/entrar da distancia (use /cartao anim).
//
//  Dados: nome e ID vem do pool real de jogadores; categoria/cargo/n. funcional vem do RPC 202
//  (servidor Pawn -> cliente), ver servidor/maximo_idcard.inc.
//
//  Chamadas:
//    IdCard3D_Render()      1x por frame, dentro da cena 3D  -> game/hooks.cpp, RenderEffects()
//    IdCard3D_OnServerPacket()  -> net/scriptrpc.cpp (RPC 202)
//    IdCard3D_ClearPlayer()     -> net/playerpool.cpp (jogador saiu)
//    IdCard3D_Reset()           -> nova conexao / desconexao (RegisterScriptRPCs / UnregisterScriptRPCs)
// =====================================================================================

namespace IdCardCat
{
	enum : uint8_t { Civil = 0, Policial, Delegado, Medico, Administrador, Militar,
					 Cnh,          // 6: documento de habilitacao (Departamento de Transito)
					 Funcional,    // 7: identidade funcional policial (carteira)
					 Count };
}

// visible=false remove o cartao do jogador.  cargo/numFuncional: texto UTF-8 (ate 31 / 23 bytes).
void IdCard3D_OnServerPacket(uint16_t playerId, bool visible, uint8_t category,
							 const char* cargo, const char* numFuncional);

void IdCard3D_ClearPlayer(uint16_t playerId);   // jogador saiu do servidor
void IdCard3D_Reset();                          // limpa tudo (reconexao / saiu do servidor)

void IdCard3D_Render();                         // desenho (dentro da cena 3D)

// Comando do chat: "/cartao ..." -> texto de resposta (ver IDCARD_MAXIMO.md)
std::string IdCard3D_Command(const std::string& args);
