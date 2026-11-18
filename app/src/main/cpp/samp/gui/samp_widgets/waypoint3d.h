#pragma once

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  Marcacoes 3D de GPS do jogador (servidor org.pwn: /marcar /marcacoes /marcacao /desmarcar; banco: phone.gps_pins).
//  Cada marcacao e desenhada NO MUNDO como um pino + NOME (maiusculas) + DISTANCIA ("15m", "1,2km") logo abaixo, no estilo do HUD,
//  e varias podem aparecer ao mesmo tempo (o servidor manda ate 12 por vez, so as visiveis do interior atual).
//    servidor -> cliente  RPC 245 op 20:  u8 qtd | qtd x ( f32 x | f32 y | f32 z | u8 tam + nome UTF-8 )   (a lista SUBSTITUI a anterior; qtd 0 limpa)
//  Alem da lista, o servidor pode ligar UMA marcacao temporaria (op 21), usada pela sede do grupo; ela nao entra na lista salva.
//  O cliente so desenha: nao guarda nada nem envia nada. Desenhado de dentro do JobsUI::draw (mesma camada dos marcadores de mundo).
//  Some a menos de 4 m (chegou) e aparece aos poucos ate 10 m. Atras da camera / fora da tela nao desenha.
// =====================================================================================
namespace Waypoint3D
{
	void OnPacket(RakNet::BitStream& bs);    // op 20 (depois do byte da op)
	void OnSede(RakNet::BitStream& bs);      // op 21: u8 ligado | f32 x,y,z | str rotulo -> 1 marcacao extra "SEDE (grupo)" do /grupo > Marcar no GPS (o servidor liga e desliga)
	void Reset();                            // desconexao: limpa a lista
	void Draw(ImGuiRenderer* r, float k);    // k = altura da tela / 540
}
