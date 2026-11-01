#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
//  MAXIMO OS - tablet interno do Maximo RP (ETAPA 1: infraestrutura).
//
//  Sobreposicao 100% nativa: desenha com o ImGuiRenderer existente (nenhuma WebView, View Java ou segunda engine grafica).
//  Toda a logica (apps, navegacao, gestos, layout) esta em gui/maximoos/ (cabecalhos puros, testados no PC);
//  este arquivo e so o ADAPTADOR com o jogo: toque bruto -> InputManager, Canvas -> ImGuiRenderer, comandos, botao Voltar.
//
//  - Abre com /tablet (ou /maximoos). Enquanto aberto, TODO toque e consumido aqui (HandleRawTouch, em AND_TouchEvent_hook):
//    HUD, camera, joystick e SA-MP nao recebem nada; o dedo que comecou no tablet fica reivindicado ate o UP/CANCEL.
//  - Voltar do Android: navega dentro do app / volta a tela inicial / fecha o tablet (OnBackPressed).
//  - Nada aqui envia/recebe RPC: os apps da etapa 1 sao placeholders.
//
//  Documentacao: MAXIMO_OS_ARCHITECTURE.md
// =====================================================================================

class MaximoOSWidget : public Widget
{
public:
	MaximoOSWidget();

	// ---- estado global (thread-safe; o efeito real ocorre na thread de desenho, no proximo quadro) ----
	static bool IsOpen();
	static void Open();
	static void Close();
	static void Toggle();

	// Thread do jogo, no inicio de AND_TouchEvent_hook. type: 2 = push, 3 = move, 1 = pop. true = consumido.
	static bool HandleRawTouch(int type, int num, int x, int y);
	// true UMA vez apos abrir: o hook solta os dedos que o jogo ainda considera pressionados.
	static bool ConsumeFlushRequest();
	// Botao Voltar do Android. true = tratado (o tablet esta aberto).
	static bool OnBackPressed();

	// Notificacao do sistema (usada por futuras etapas / servidor). level: 0 info, 1 aviso, 2 alerta.
	static void PostNotification(const std::string& title, const std::string& text, const std::string& appId = std::string(), int level = 0);

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
