#pragma once

#include <cstdint>

// =====================================================================================
//  Avisos no canto (sucesso / erro / aviso) e velocimetro do veiculo (velocidade, marcha, combustivel, botoes de
//  motor e trava). Tudo vem do servidor pelo RPC 247 (filterscripts/veiculo.pwn + qawno/include/maximo_notify.inc):
//
//    u8 op
//    op 1  aviso     : u8 tipo (0 sucesso, 1 erro, 2 aviso) | string titulo | string texto | u16 duracao em ms
//    op 2  veiculo   : u8 combustivel (0..100) | u8 flags (bit0 motor ligado, bit1 trancado, bit2 farol, bit3 pisca alerta) | [opcional, no fim] u32 hodometro em decimos de km
//    op 3  entrar    : u8 ativo | u16 veiculo   (ha um porta-malas ABERTO ao alcance: botao "Entrar no porta-malas")
//    op 4  dentro    : u8 estado (0 fora | 1 dentro com o porta-malas aberto | 2 dentro com ele fechado = tela escura)
//    string = u8 tamanho + bytes
//
//  O botao de entrar manda "/entrarmalas <veiculo>"; o de sair, "/malasair" (so se o porta-malas estiver aberto).
//  As teclas mandam "/motor", "/trancar", "/farol" e "/pisca" ao servidor (mesmo caminho de quem digita o comando no chat).
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
	ImVec4 m_xb = ImVec4(0, 0, 0, 0);   // botao de tela (Entrar / Sair do porta-malas); m_xOn = visivel neste frame
	bool   m_xOn = false; int m_xCmd = 0, m_xVeh = 0;
	ImVec4 m_key[4];   // x0,y0,x1,y1 das 4 teclas do painel (motor, trava, farol, pisca)
	float  m_btnR = 0.0f;
	int    m_down = -1;
	int64_t m_lastTap = 0;
};
