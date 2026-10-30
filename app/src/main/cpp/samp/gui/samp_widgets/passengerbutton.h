#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Botao "P2" (entrar como PASSAGEIRO).
//
//  O botao nativo do carro (WIDGET_ENTER_CAR) sempre entra como MOTORISTA ("P1"). Entrar de passageiro so existia
//  pelo botao "G" do ButtonPanel, que fica escondido (setVisible(false)) e portanto nunca podia ser tocado.
//
//  Este botao aparece ao lado do botao nativo do carro, SO quando: jogador a pe e vivo, sem dialogo/teclado/lista/spawn/
//  splash, e ha um veiculo do SA-MP a menos de kShowDist metros. Tocar = CLocalPlayer::HandlePassengerEntry
//  (via bNeedEnterVehicle, o mesmo caminho do antigo "G"): EnterVehicle(passageiro=true) + SendEnterVehicleNotification.
//
//  Toque: o dedo que cai no botao e reivindicado por onRawTouch() (chamado de AND_TouchEvent_hook, como o WeaponBar)
//  e NAO chega ao jogo (a camera nao gira). Logica pura (posicao/area/visibilidade) em passengerbutton_logic.h.
// =====================================================================================

class PassengerButton : public Widget
{
public:
	static ImVec2 SizeFor(const ImVec2& screen) { return screen; }
	static ImVec2 PosFor(const ImVec2& screen)  { (void)screen; return ImVec2(0.0f, 0.0f); }

	PassengerButton();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	// type: 2 = push, 3 = move, 1 = pop (igual a AND_TouchEvent). true = o evento e do botao (nao segue para o jogo).
	bool onRawTouch(int type, int num, int x, int y);

private:
	std::atomic<bool>    m_available;    // visivel/usavel neste frame (escrito no draw, lido no toque)
	std::atomic<int>     m_finger;       // dedo dono do toque (-1 = nenhum)
	std::atomic<float>   m_cx, m_cy, m_r;// centro/raio do ultimo frame desenhado (px)
	std::atomic<int64_t> m_lastUseMs;    // anti-repeticao
};
