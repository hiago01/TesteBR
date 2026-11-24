#pragma once

#include <vector>
#include "shoppanel.h"      // ShopTouch (mesmo canal de toques das outras telas do jobs.cpp)

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  TELA DE MORTE do Maximo RP (contagem + "Acionar os medicos").
//  servidor -> cliente  RPC 245 op 52 (liga o sistema)      u8 ligado(0/1)   (enviado a cada spawn; sem ele o cliente morre como sempre)
//                       RPC 245 op 50 (mostra / atualiza)   u8 modo(1 = nova morte, 2 = atualiza) | u16 segundos restantes | u8 medicos em servico | u8 acionados(0/1)
//                       RPC 245 op 51 (fecha)               u8 motivo(0 = foi para o hospital, 1 = foi socorrido)
//  cliente -> servidor  o botao envia o comando de chat "/acionarmedicos" (nada novo no protocolo)
//
//  Enquanto o sistema esta ligado e o jogador morre, o cliente NAO faz spawn nem entra na selecao de classe: o ped fica NO LUGAR, vivo,
//  sem controle, em LOOP de animacao de ferido (localplayer.cpp, Process) e so a tela aparece. Quem encerra e o servidor (op 51).
//  "Pendente": entre a morte e o op 50 (rede) o cliente ja segura o respawn por ate 4 s (antes o ped se levantava nessa janela).
//  Trava de seguranca: se o servidor nao mandar o op 51 ate (segundos + 25 s), o cliente solta sozinho (nunca fica preso para sempre).
//  Todo o desenho e por codigo (coracao, pulso, botao): nenhuma textura nova.
// =====================================================================================
namespace DeathScreen
{
	bool OnState(RakNet::BitStream& bs);   // op 50 (depois do byte da op). false = pacote invalido
	void SetEnabled(bool on);              // op 52
	void Hide();                           // op 51
	void Reset();                          // nova conexao: desliga tudo
	void OnLocalDeath();                   // o cliente acabou de detectar a morte do jogador local (localplayer.cpp)
	bool Active();                         // a tela esta ligada
	bool Holding();                        // o respawn automatico deve esperar (tela ativa, ou morte pendente do servidor)
	// answer: 0 nada | 1 tocou em "Acionar os medicos"
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer);
}
