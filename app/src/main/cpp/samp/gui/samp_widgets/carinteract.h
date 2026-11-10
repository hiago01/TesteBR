#pragma once

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  Interacao com o PROPRIO carro (servidor garagem.pwn): quem tem a chave e esta a ate 6 m do carro dele (na rua) ve o ponto
//  "Abrir interacao" no carro; tocar nele (ou no carro) abre o menu:
//    carro -> lista no estilo do HUD (hoje: Trancar / Destrancar veiculo; a lista aceita mais itens)
//    moto  -> um unico botao Trancar / Destrancar
//   servidor -> cliente  RPC 245 op 15: u8 ativo, u16 veiculo, u8 moto, u8 trancado       cliente -> servidor  RPC 246 op 13 + u16 veiculo
//  O cliente so desenha e pede: o servidor confere dono, chave, distancia e aplica a tranca.
//  Desenhado/tocado de dentro do JobsUI (mesmo canal de toque curto de mundo).
// =====================================================================================
namespace CarInteract
{
	void OnPacket(RakNet::BitStream& bs);    // op 15 (depois do byte da op)
	void Reset();                            // desconexao
	void CloseMenu();                        // outra tela abriu por cima
	// Desenha o ponto/menu. hasTap/tx/ty = toque curto de mundo pendente. Retorna true se consumiu o toque.
	// sendVeh > 0 = o jogador escolheu Trancar/Destrancar (id do veiculo).
	bool Draw(ImGuiRenderer* r, float k, bool hasTap, float tx, float ty, int& sendVeh);
}
