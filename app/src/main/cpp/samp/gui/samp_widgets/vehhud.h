#pragma once

#include <cstdint>

// =====================================================================================
//  Avisos no canto (sucesso / erro / aviso) e velocimetro do veiculo (velocidade, marcha, combustivel, botoes de
//  motor e trava). Tudo vem do servidor pelo RPC 247 (filterscripts/veiculo.pwn + qawno/include/maximo_notify.inc):
//
//    u8 op
//    op 1  aviso     : u8 tipo (0 sucesso, 1 erro, 2 aviso) | string titulo | string texto | u16 duracao em ms
//    op 2  veiculo   : u8 combustivel (0..100) | u8 flags (bit0 motor ligado, bit1 trancado)
//    string = u8 tamanho + bytes
//
//  Os botoes mandam "/motor" e "/trancar" ao servidor (mesmo caminho de quem digita o comando no chat).
// =====================================================================================

namespace VehHud
{
	void OnServerPacket(const unsigned char* data, int bytes);
	void Reset();   // desconexao: limpa avisos e estado do veiculo
}

class VehHudWidget : public Widget
{
public:
	static ImVec2 SizeFor(const ImVec2& screen) { return screen; }
	static ImVec2 PosFor(const ImVec2& screen)  { (void)screen; return ImVec2(0.0f, 0.0f); }

	VehHudWidget();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override;

private:
	// botoes (centro/raio do ultimo frame; raio 0 = escondido)
	ImVec2 m_btnC[2];
	float  m_btnR = 0.0f;
	int    m_down = -1;
	int64_t m_lastTap = 0;
};
