#pragma once

#include <string>

// =====================================================================================
//  Painel de ajuste do celular na mao ("/celofs" sem parametros). So cliente; so existe enquanto estiver aberto.
//  Fica na lateral ESQUERDA, acima do joystick. Cada toque em + / - manda "/celofs <modo> <eixo> <valor>" ao servidor, que
//  reposiciona o objeto na mao ao vivo (filterscript poses.pwn; precisa ser admin RCON). "Salvar" = /celsalvar.
//  Toque: so o dedo que COMECA dentro do painel e capturado; joystick e camera continuam no jogo. Logica em celofs_logic.h.
// =====================================================================================

class CelOfsPanel : public Widget
{
public:
	CelOfsPanel();

	static bool IsOpen();
	static void Open();
	static void Close();
	static void Toggle();

	// Thread do jogo, em AND_TouchEvent_hook. type: 2 = push, 3 = move, 1 = pop. true = consumido.
	static bool HandleRawTouch(int type, int num, int x, int y);
	// Mensagem de chat vinda do servidor. true = era linha do painel ("[CELOFS] ...") e NAO deve ir ao chat.
	static bool OnServerLine(const std::string& msg);

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
