#pragma once

#include <string>

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  Marcacoes 3D de GPS do jogador (servidor org.pwn: /marcar /marcacoes /marcacao /desmarcar; banco: phone.gps_pins).
//  Cada marcacao e desenhada NO MUNDO como um pino + NOME (maiusculas) + DISTANCIA ("15m", "1,2km") logo abaixo, no estilo do HUD,
//  e varias podem aparecer ao mesmo tempo (o servidor manda ate 12 por vez, so as visiveis do interior atual).
//    servidor -> cliente  RPC 245 op 20:  u8 qtd | qtd x ( f32 x | f32 y | f32 z | u8 tam + nome UTF-8 )   (a lista SUBSTITUI a anterior; qtd 0 limpa)
//  Alem da lista, o servidor pode ligar UMA marcacao temporaria (op 21), usada pela sede do grupo; ela nao entra na lista salva.
//  A coordenada z = -1000 significa "ao nivel do chao" (marcacao criada no mapa aberto): o cliente acha o chao sozinho quando a area esta carregada.
//  O cliente so desenha: nao guarda nada nem envia nada. Desenhado de dentro do JobsUI::draw (mesma camada dos marcadores de mundo).
//  Some a menos de 4 m (chegou) e aparece aos poucos ate 10 m. Atras da camera / fora da tela nao desenha.
// =====================================================================================
namespace Waypoint3D
{
	void OnPacket(RakNet::BitStream& bs);    // op 20 (depois do byte da op)
	void OnSede(RakNet::BitStream& bs);      // op 21: u8 ligado | f32 x,y,z | str rotulo -> 1 marcacao extra "SEDE (grupo)" do /grupo > Marcar no GPS (o servidor liga e desliga)
	void OnHq(RakNet::BitStream& bs);        // op 22: u8 qtd | qtd x ( f32 x,y,z | u8 tam + nome ) -> sedes de TODOS os grupos (lista substitui a anterior); so alimenta o MAPA ABERTO
	void Reset();                            // desconexao: limpa a lista
	// leitura para o MAPA ABERTO (worldmap.cpp): as marcacoes visiveis que o servidor mandou (sem a sede). Qualquer thread.
	int  Count();
	bool Get(int i, float& x, float& y, std::string& label);   // label = nome como o jogador digitou
	// sedes dos grupos (categoria "Grupos" do mapa). Qualquer thread.
	int  HqCount();
	bool HqGet(int i, float& x, float& y, float& z, std::string& name);
	// DESTINO do mapa aberto: UMA marcacao 3D exclusiva (so cliente, nao salva). Cada toque no mapa substitui a anterior; "limpar GPS"/chegada apagam.
	// Divide o mesmo lugar da sede do grupo (op 21): quem foi marcado por ultimo vale. z <= -900 = ao nivel do chao.
	void SetDest(float x, float y, float z, const std::string& label);
	void ClearDest();
	void Draw(ImGuiRenderer* r, float k);    // k = altura da tela / 540
}
