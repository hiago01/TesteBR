#pragma once
// ---------------------------------------------------------------------------
//  Tela de FERIDO / contagem de morte (Maximo RP). O SERVIDOR manda tudo (RPC 245, ops 50/51/52, maximo_socorro.inc); o cliente
//  so desenha, SEGURA o personagem no lugar da morte e pede "acionar os medicos" (chat /acionarmedicos).
//   op 52: u8 ligado (1 = o sistema de socorro esta ativo neste servidor; sem isso o jogo se comporta como sempre)
//   op 50: u8 modo | u16 segundos restantes | u8 medicos em servico | u8 ja acionou | f32 x | f32 y | f32 z   (x,y,z = local da morte, do SERVIDOR)
//   op 51: u8 motivo (0 hospital | 1 reanimado por medico | 2 liberado pelo servidor)
// ---------------------------------------------------------------------------
#include <cstdint>

class ImGuiRenderer;
struct ImVec2;

namespace DeathScreen
{
	void SetEnabled(bool on);                                   // op 52
	void OnState(int mode, int secs, int medics, int called, bool hasPos, float x, float y, float z);   // op 50
	void Hide(int reason);                                      // op 51
	void Reset();                                               // nova conexao
	void OnLocalDeath(float x, float y, float z);               // thread do jogo: o proprio jogador morreu aqui
	bool Enabled();
	bool Active();                                              // tela visivel
	bool Holding();                                             // o cliente deve SEGURAR o personagem (tela visivel ou morte recem detectada)
	bool Releasing();                                           // o servidor soltou: segura so a posicao por 3 s ate o nascimento
	bool HoldPos(float& x, float& y, float& z);                 // local onde segurar
	bool TakeCleanup();                                         // true UMA vez depois que a espera acabou (soltar animacao e controles)
	void Draw(ImGuiRenderer* r, const ImVec2& scr);
	void OnTouch(int type, int x, int y);                       // 2 = baixou, 3 = moveu, 1 = soltou
}
