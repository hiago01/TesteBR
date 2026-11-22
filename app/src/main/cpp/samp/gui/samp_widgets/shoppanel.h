#pragma once

#include <vector>

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  LOJA das empresas do Maximo RP (mercado, roupas, lanchonete...): tela do cliente no layout "Loja + Carrinho"
//  (painel escuro, titulo + subtitulo, aba "Todos", cartoes com icone / nome / descricao / preco / "Adicionar ao carrinho",
//  coluna "Carrinho" com - e + , Total e "Finalizar Compra") e o aviso "Informacao" (circulo verde + texto) depois da compra.
//  Fica dentro da tela do jobs.cpp (como a garagem e a nota fiscal): mesmo canal de toques e mesmo RPC.
//   servidor -> cliente  RPC 245 op 18 (abre a loja) / op 19 (fecha) / op 20 (aviso "Informacao")
//        op 18: u16 numero | str titulo | str subtitulo | u8 n (max 20) | n x { str nome | str descricao | u32 preco | u16 estoque | u8 icone | u8 cor | u8 qtd_no_carrinho }
//               (str = u8 tamanho + bytes; o servidor manda texto ASCII; a ordem dos produtos e o indice que volta no pedido)
//        op 20: u8 tipo (0 compra feita: texto = "3x Cola, 1x Pizza" | 1 aviso/erro: texto livre) | str texto
//   cliente -> servidor  RPC 246 op 17 + u16 numero (a loja apareceu: confirma que este cliente tem a tela nova)
//                        RPC 246 op 18 + u16 numero + u8 n + n x { u8 indice | u8 qtd }   (Finalizar Compra)
//                        RPC 246 op 19 + u16 numero (Fechar / voltar do celular)
//  O cliente so desenha e PEDE: quem confere dinheiro, estoque e inventario e o servidor (maximo_empresas.inc).
//  Icone do produto (u8) = glifo desenhado por codigo (0 cola, 1 lima, 2 agua, 3 chips, 4 cereal, 5 molho, 6 pao, 7 camisetas, 8 jeans, 9 tenis, 10 camiseta no cabide, 11 pizza, 12 caixa de pizza, 13 caixa de hamburguer, 14 hamburguer, 15 batata, 16 kit de reparo, 17 pneu, 18 oleo, 19 galao, 20 pomada, 21 navalha, 22 shampoo; 100+n = icone n do inventario); cor (u8) 0..5 = ponto/descricao do cartao.
//  Tudo desenhado por codigo (retangulos, linhas, poligonos): nenhum arquivo de imagem novo, nenhum IMG novo.
// =====================================================================================
struct ShopTouch { int type; int num; float x; float y; bool cancel; };
struct ShopLine { int idx; int qty; };

// definido em jobs.cpp (chama InventoryUI::DrawHotbarIcon): desenha o icone de inventario `icon` dentro do retangulo a..b
void ShopDrawItemIcon(ImGuiRenderer* r, int icon, const float a[2], const float b[2], int alpha);

namespace ShopPanel
{
	bool OnShow(RakNet::BitStream& bs);    // op 18 (depois do byte da op). false = pacote invalido (a loja anterior e mantida)
	void Reset();                          // desconexao / op 19: apaga a loja da tela
	int  Seq();                            // numero da loja na tela (0 = nenhuma)
	// Desenha e processa os toques. answer: 0 nada | 1 tocou em Finalizar Compra (order = carrinho) | 2 tocou em Fechar.
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, std::vector<ShopLine>& order);

	// icone de produto desenhado por codigo (0..22 = produtos conhecidos, outro = sacola) dentro de a..b
	void DrawGlyph(ImGuiRenderer* r, int g, const float a[2], const float b[2], int alpha);

	// Aviso "Informacao" no canto de baixo (circulo + titulo + texto). Pode aparecer com a loja fechada.
	bool OnToast(RakNet::BitStream& bs);   // op 20
	void LocalToast(int kind, const char* text);
	void DrawToast(ImGuiRenderer* r, float scrW, float scrH);
}
