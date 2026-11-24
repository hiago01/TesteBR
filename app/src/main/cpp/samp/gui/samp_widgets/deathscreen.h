#pragma once

#include <vector>
#include "shoppanel.h"      // ShopTouch (mesmo canal de toques das outras telas do jobs.cpp)

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  TELA DE MORTE do Maximo RP (contagem + "Acionar os medicos").
//  servidor -> cliente  RPC 245 op 50 (mostra / atualiza)   u8 modo(1 = nova morte, 2 = atualiza) | u16 segundos restantes | u8 medicos em servico | u8 acionados(0/1)
//                       RPC 245 op 51 (fecha)               u8 motivo(0 = foi para o hospital, 1 = foi socorrido)
//  cliente -> servidor  o botao envia o comando de chat "/acionarmedicos" (nada novo no protocolo)
//  Enquanto a tela esta ativa o cliente NAO pede o respawn automatico (localplayer.cpp: DeathScreen::Holding()); quem encerra e o servidor (op 51).
//  Trava de seguranca: se o servidor nao mandar o op 51 ate (segundos + 25 s), o cliente solta sozinho (nunca fica morto para sempre).
//  Todo o desenho e por codigo (coracao, pulso, botao): nenhuma textura nova.
// =====================================================================================
namespace DeathScreen
{
	bool OnState(RakNet::BitStream& bs);   // op 50 (depois do byte da op). false = pacote invalido
	void Hide();                           // op 51 / desconexao
	bool Active();                         // a tela esta ligada
	bool Holding();                        // o respawn automatico deve esperar (ativa e dentro da trava de seguranca)
	// answer: 0 nada | 1 tocou em "Acionar os medicos"
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer);
}
