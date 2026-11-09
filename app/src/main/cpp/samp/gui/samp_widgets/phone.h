#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
//  CELULAR DO MAXIMO RP (iPhone vertical) - adaptador com o jogo.
//  Toda a logica (aparelho, tela inicial, apps, gestos) esta em gui/phone/ (cabecalhos puros, testados no PC).
//  Este arquivo so liga: toque bruto -> PhoneInput, Canvas -> ImGuiRenderer, comando /celular, botao Voltar, persistencia (settings.ini).
//  Sobreposicao 100% nativa: ImGuiRenderer existente (sem WebView, sem View Java, sem segunda engine).
//  Toque: so o dedo que COMECA dentro do aparelho e capturado; fora dele joystick/camera continuam no jogo.
// =====================================================================================

class PhoneWidget : public Widget
{
public:
	PhoneWidget();

	static bool IsOpen();
	static void Open();
	static void Close();
	static void Toggle();

	// Thread do jogo, em AND_TouchEvent_hook. type: 2 = push, 3 = move, 1 = pop. true = consumido.
	static bool HandleRawTouch(int type, int num, int x, int y);
	// Botao Voltar do Android. true = tratado (celular aberto).
	static bool OnBackPressed();
	// Notificacao no celular (usada pelo servidor/apps). appId: app aberto ao tocar (vazio = so fecha).
	static void PostNotification(const std::string& title, const std::string& text, const std::string& appId = std::string(), int level = 0);

	// Rede do celular. OnServerPacket: thread do jogo, RPC 241 (servidor -> cliente), bytes exatos do pacote. Reset: nova conexao / desconexao.
	static void OnServerPacket(const unsigned char* data, int bytes);
	static void Reset();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
