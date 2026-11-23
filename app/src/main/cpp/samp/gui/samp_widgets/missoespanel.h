#pragma once

#include <vector>
#include "shoppanel.h"      // ShopTouch (mesmo canal de toques da loja)

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  MISSOES do Maximo RP (Diario / Semanal / Temporada / Especial / Eventos): tela cheia no layout da referencia (abas MISSOES / CONQUISTAS / TEMPORADA,
//  menu lateral das categorias, cartao central com a recompensa do periodo + barra de nivel da temporada, lista de missoes com progresso, IR / COLETAR / COLETADO).
//  Fica dentro da tela do jobs.cpp (como a loja, a nota fiscal e o menu jogador): mesmo canal de toques e mesmos RPCs.
//   servidor -> cliente  RPC 245 op 40 (abre / atualiza) / op 41 (lista de uma aba) / op 42 (resultado do COLETAR) / op 43 (fecha)   (ver include maximo_missoes.inc)
//        op 40: u16 numero | u8 temTemporada | str nome | u16 nivel | u32 expNoNivel | u32 expProximo | u16 nivelMax | str grandePremio | u32 segFimTemporada | u16 semana | u8 5 + 5 x u8 (prontas para coletar por aba)
//               (o mesmo numero ja aberto = so ATUALIZA os dados: a tela e a aba continuam onde estao)
//        op 41: u16 numero | u8 aba | u8 n + n x { u16 id | str nome | str descricao | u32 meta | u32 progresso | u8 status | u8 rep | u8 repFeitas | u8 destino | u32 segundos | u8 icone | u8 bonus | u8 nPremios + nPremios x { u8 tipo | i32 arg | u32 qtd } }
//        op 42: u16 numero | u16 id | u8 resultado | u8 nPremios + nPremios x { u8 tipo | i32 arg | u32 qtd }
//   cliente -> servidor  RPC 246 op 40 + u16 numero (a tela abriu) | op 41 + u16 numero + u8 aba | op 42 + u16 numero + u16 id (COLETAR) | op 43 + u16 numero + u16 id (IR) | op 44 + u16 numero (fechou)
//  O cliente so desenha e PEDE: progresso, status, premio e periodo vem do servidor/banco; o jogador nunca informa nada alem de "qual missao" e "qual aba".
//  O botao IR so aparece quando o servidor manda destino > 0. Todo o desenho (icones, barras, cartoes) e feito por codigo: nenhum IMG novo, nenhuma textura nova.
// =====================================================================================
namespace MissoesPanel
{
	bool OnOpen(RakNet::BitStream& bs);    // op 40 (depois do byte da op). false = pacote invalido (a tela anterior e mantida)
	bool OnList(RakNet::BitStream& bs);    // op 41
	bool OnClaim(RakNet::BitStream& bs);   // op 42
	void Reset();                          // desconexao / op 43: apaga a tela
	int  Seq();                            // numero da tela (0 = nenhuma)

	// Desenha e processa os toques.
	// answer: 0 nada | 1 Fechar | 2 pediu a lista de uma aba (arg = aba) | 3 COLETAR (arg = id da missao) | 4 IR (arg = id da missao)
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, int& arg);
}
