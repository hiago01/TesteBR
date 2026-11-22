#pragma once

#include <vector>

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  NOTA FISCAL das lojas (empresas do Maximo RP): tela do cliente no formato "cupom" (bordas picotadas, icone da empresa, itens,
//  Sub-total / Imposto / Valor total, codigo de barras e os botoes Cancelar / Finalizar pedido), em PALETA AZUL.
//  Fica dentro da tela do jobs.cpp (como a garagem): usa o mesmo canal de toques e o mesmo RPC.
//   servidor -> cliente  RPC 245 op 16 (abre a nota) / op 17 (fecha)
//        op 16: u16 numero | u8 icone | u8 imposto% | u32 total | u32 imposto | str empresa | u8 n (max 8) | n x { str produto | u16 qtd }
//               (str = u8 tamanho + bytes; o servidor manda texto ASCII)
//   cliente -> servidor  RPC 246 op 14 + u16 numero (a nota apareceu: confirma que este cliente tem a tela nova)
//                        RPC 246 op 15 + u16 numero (Finalizar pedido)   |   op 16 + u16 numero (Cancelar / voltar do celular)
//  O cliente so desenha e PEDE: quem confere dinheiro, estoque e inventario e o servidor (maximo_empresas.inc).
//  Icones (u8): 0 mercado (carrinho) | 1 auto pecas (chave) | 2 roupas (camiseta) | 3 barbearia (tesoura) | 4 lanchonete (hamburguer) | outro = sacola.
//  Os icones sao desenhados por codigo (linhas e poligonos): nenhum arquivo de imagem novo, nenhum IMG novo.
// =====================================================================================
struct InvoiceTouch { int type; int num; float x; float y; bool cancel; };

namespace InvoicePanel
{
	bool OnShow(RakNet::BitStream& bs);   // op 16 (depois do byte da op). false = pacote invalido (a nota anterior e mantida)
	void Reset();                         // desconexao / op 17: apaga a nota
	int  Seq();
	// Tile azul da empresa com o glifo branco (0 mercado, 1 auto pecas, 2 roupas, 3 barbearia, 4 lanchonete, outro = sacola): o mesmo icone do topo da nota; usado no topo da loja.
	void DrawCompanyIcon(ImGuiRenderer* r, int icon, float x, float y, float size, int alpha);                           // numero da nota na tela (0 = nenhuma)
	// Desenha e processa os toques. answer: 0 nada | 1 tocou em Finalizar pedido | 2 tocou em Cancelar.
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<InvoiceTouch>& events, int& answer);
}
