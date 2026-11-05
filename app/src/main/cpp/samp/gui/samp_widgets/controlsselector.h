#pragma once

// =====================================================================================
//  Tela "ESCOLHA SEUS CONTROLES": dois cartoes (CONTROLE CLASSICO / CONTROLE MODERNO) com previa, botao SELECIONAR,
//  indicador do cartao marcado e botoes CONFIRMAR / VOLTAR. Responsiva (paisagem e retrato).
//
//  - NAO abre sozinho. Abre so por pedido: opcao "Sistema de controles" do menu (configuracao) ou o comando /controles
//    (/controles classico | /controles moderno troca direto, sem abrir a tela). Trocar nao exige reiniciar.
//  - Enquanto aberta consome TODOS os toques (HandleRawTouch no inicio do hook de toque); nada vaza para o jogo.
//  - VOLTAR nao altera o modo; na primeira vez ele so registra "ja escolhi" (continua no CLASSICO) para nao repetir.
//  Logica pura: controlmode_logic.h (SelectorLayout/SelectorState), testada em testes/controles.
// =====================================================================================
class ControlsSelector : public Widget
{
public:
	ControlsSelector();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	static bool IsOpen();
	static void Open();
	static void Close();
	static bool HandleRawTouch(int type, int num, int x, int y);   // true = consumido
	static bool ConsumeFlushRequest();                             // true 1x ao abrir: o hook solta os dedos do jogo
};
