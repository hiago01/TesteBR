#pragma once

// =====================================================================================
//  CONTROLES MODERNOS: joystick analogico (base translucida, centro movel, 360 graus, intensidade proporcional,
//  zona morta configuravel) + botoes independentes (Corrida, Pulo, Agachar, Ataque, Microfone).
//
//  - So existe/atua com o modo MODERNO escolhido, a pe e vivo (ControlMode). No CLASSICO nao desenha nem processa nada.
//  - Multitouch por ID do dedo: o dedo que comeca num controle pertence a ele ate soltar; nenhum evento dele chega ao
//    jogo (onRawTouch e chamado em AND_TouchEvent_hook, igual ao JumpButton/WeaponBar).
//  - O resultado chega ao motor pelos hooks de CPad ja existentes (game/pad.cpp) via ModernInput (controlmode.h).
//  - Modo de edicao: arrastar cada controle para a posicao desejada (grava em [controles_<nick>]).
//  Logica pura: controlmode_logic.h (testada em testes/controles).
// =====================================================================================
class ModernControls : public Widget
{
public:
	ModernControls();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	// type: 2 = push, 3 = move, 1 = pop. true = o evento e dos controles modernos (nao segue para o jogo).
	static bool onRawTouch(int type, int num, int x, int y);

	// Mira arrastavel: toques SINTETICOS (dedo do Ataque -> regiao de camera/mira do jogo). O hook de toque os repassa a
	// AND_TouchEvent logo apos onRawTouch. type: 2 push, 3 move, 1 pop; num = mesmo indice do dedo do Ataque.
	struct AimTouch { int type; int num; int x; int y; };
	static int DrainAimTouches(AimTouch* out, int max);

	static void StartEdit();      // fecha-se sozinho no botao OK
	static bool IsEditing();
};
