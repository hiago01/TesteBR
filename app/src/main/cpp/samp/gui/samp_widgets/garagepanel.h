#pragma once

#include <vector>

#include "../../vendor/imgui/imgui.h"
class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  Tela da Garagem (servidor garagem.pwn): abas Motos / Carros / Vips, cartoes dos veiculos do jogador, especificacoes
//  (gasolina, motor, milhas), descricao do veiculo selecionado e botao "Retirar veiculo".
//  Fica dentro da agencia de empregos (jobs.cpp): usa o mesmo canal de toques e a mesma tela cheia.
//   servidor -> cliente  RPC 245 op 13 (lista) / op 14 (fecha)    cliente -> servidor  RPC 246 op 12 + u16 registro
//  O cliente so desenha e pede: o servidor confere dono, chave, desmanchado e "ja na rua".
// =====================================================================================
struct GarageTouch { int type; int num; float x; float y; bool cancel; };

namespace GaragePanel
{
	void OnList(RakNet::BitStream& bs);            // op 13 (depois do byte da op). false no pacote invalido = lista mantida
	void Reset();
	// Silhuetas (moto, scooter, carro, patinete, bike, van, hoverboard) trocando ao acaso, mesmo efeito do topo da garagem. slot 0 = topo, 1 = botao P1 (estados separados).
	void DrawCycleIcon(ImGuiRenderer* r, ImVec2 c, float half, const ImColor& fg, const ImColor& bg, int slot);
	// Desenha e processa os toques. close = tocou em fechar; pullReg > 0 = tocou em Retirar veiculo (registro do carro).
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<GarageTouch>& events, bool& close, int& pullReg);
}
