#pragma once
// ---------------------------------------------------------------------------
//  CRIE SEU PERSONAGEM (Maximo RP): painel da conta nova (substitui a cutscene). Servidor: maximo_criar.inc.
//   servidor -> cliente (RPC 245) op 70: u8 modo (1 abre/atualiza, 0 fecha) | u8 sexo (0 M, 1 F) | u8 parte | u8 sel[4] (255 = nenhum)
//   cliente -> servidor (RPC 246) op 70: u8 acao: 1 sexo (u8) | 2 escolher (u8 cat, u8 idx) | 3 parte da camera (u8) | 4 finalizar
//   categorias: 0 camisa | 1 short | 2 tenis | 3 chinelo (tenis e chinelo se excluem)
// ---------------------------------------------------------------------------
#include <cstdint>

class ImGuiRenderer;
struct ImVec2;
namespace RakNet { class BitStream; }

namespace CharCreate
{
	bool OnPacket(RakNet::BitStream& bs);        // op 70 (apos ler o op)
	void Reset();
	bool Active();
	void Draw(ImGuiRenderer* r, const ImVec2& scr);
	void OnTouch(int type, int x, int y);        // 2 = baixou, 3 = moveu, 1 = soltou
}

class ImGuiRenderer;
void MxDrawSantaEmblem(ImGuiRenderer* r, float cx, float cy, float W, float H, float alpha);   // brasao Maximo RP com as maos animadas (alpha 0..1)
