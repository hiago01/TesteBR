#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Menu "Maximo RP" (sobreposicao ImGui) -- substitui o menu de pausa nativo.
//
//  - Abre com /menu (chat) ou com o botao Voltar do Android. NAO usa o Pause nativo:
//    o mundo e a conexao com o servidor continuam rodando.
//  - Enquanto aberto, TODO toque e consumido aqui (HandleRawTouch, chamado de
//    AND_TouchEvent_hook): os controles do GTA SA / SA-MP por baixo nao recebem nada.
//    O dedo que comecou no menu fica "reivindicado" ate o UP/CANCEL, mesmo se o menu
//    fechar no meio do gesto.
//  - So aparecem como funcionais as opcoes cujo efeito foi ligado a uma funcao/simbolo
//    comprovado do libGTASA.so ARM64 do projeto (ver maximomenu.cpp). As demais
//    aparecem desativadas ("Indisponivel"), nunca como controle falso.
//
//  Implementacao: maximomenu.cpp
// =====================================================================================

class MaximoMenu : public Widget
{
public:
	MaximoMenu();

	// ---- estado global (thread-safe) ----
	static bool IsOpen();
	static void Open();
	static void Close();
	static void Toggle();

	// Chamado na thread do jogo, no inicio de AND_TouchEvent_hook.
	// type: 2 = push, 3 = move, 1 = pop. Retorna true se o evento foi consumido pelo menu
	// (nesse caso NAO deve ser repassado ao jogo nem aos outros widgets).
	static bool HandleRawTouch(int type, int num, int x, int y);

	// true UMA vez apos abrir: o hook deve soltar (AND_TouchEvent(1,i,0,0)) os dedos que o
	// jogo ainda considera pressionados (botoes/joystick/camera).
	static bool ConsumeFlushRequest();

	// Botao Voltar do Android. Retorna true se o evento foi tratado aqui (fecha o menu, ou
	// o engole para nao acionar o Pause nativo).
	static bool OnBackPressed();

	// ---- consultas usadas pelos hooks do jogo ----
	static bool SkidmarksEnabled();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
