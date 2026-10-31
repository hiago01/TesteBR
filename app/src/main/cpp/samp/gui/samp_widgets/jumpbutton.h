#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Botao de PULO proprio (icone "hud_crouch" do texdb/mobile, arquivo mobile.etc.dat).
//
//  O pulo deixa de vir do botao nativo (o mesmo do corredor): CPad::JumpJustDown/GetJump do jogador LOCAL passam a
//  devolver so o pedido deste botao (game/pad.cpp). Aparece ao lado esquerdo do botao nativo de correr, so a pe e vivo,
//  sem dialogo/teclado/lista/spawn/splash/menu aberto. O dedo que cai no botao e reivindicado por onRawTouch()
//  (hooks.cpp, igual ao PassengerButton) e nao chega ao jogo. Logica pura em jumpbutton_logic.h.
// =====================================================================================

class JumpButton : public Widget
{
public:
	static ImVec2 SizeFor(const ImVec2& screen) { return screen; }
	static ImVec2 PosFor(const ImVec2& screen)  { (void)screen; return ImVec2(0.0f, 0.0f); }

	JumpButton();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	// type: 2 = push, 3 = move, 1 = pop (igual a AND_TouchEvent). true = o evento e do botao (nao segue para o jogo).
	bool onRawTouch(int type, int num, int x, int y);

private:
	std::atomic<bool>  m_available;
	std::atomic<int>   m_finger;
	std::atomic<float> m_cx, m_cy, m_r;
};

// Pedido de pulo lido por game/pad.cpp (thread do jogo).
namespace JumpButtonState
{
	bool ConsumeJustDown();   // true 1x por toque
	bool Held();              // true por ~120 ms apos o toque
}
