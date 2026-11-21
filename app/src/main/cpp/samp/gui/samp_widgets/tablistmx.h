#pragma once

#include <atomic>
#include <string>

// =====================================================================================
//  TabListMX -- barra de abas "Dialogo | Inventario | Perfil | Tarefas | Ajuda" com DOIS estilos escolhidos pelo chat:
//    /tablistmx 1        estilo 1 (pilula escura, aba escolhida levantada)
//    /tablistmx 2        estilo 2 (icones com contorno preto e separadores)
//    /tablistmx 0        oculta   |  /tablistmx on  volta   |  /tablistmx pos <6-96>  altura em % da tela
//  Padrao: estilo 1, centralizada na base. A escolha fica em SAMP/settings.ini, secao [tablistmx] (estilo, pos).
//
//  Acoes das abas (so o que tem funcao comprovada no projeto):
//    Inventario -> InventoryUI::RequestOpen() (envia /inv, igual ao botao do HUD moderno)
//    Dialogo / Perfil / Tarefas / Ajuda -> so marcam a aba, ate voce definir um comando de chat em settings.ini:
//        [tablistmx]  cmd_dialogo= cmd_perfil= cmd_tarefas= cmd_ajuda=   (ex.: cmd_ajuda=/ajuda)   cmd_inventario= (opcional)
//  O servidor atual (maximorp.pwn) so tem /inv e /inventario: nao inventei comandos que nao existem.
//
//  Toque: HandleRawTouch() e chamado no inicio de MaximoMenu::HandleRawTouch (mesmo ponto do AND_TouchEvent_hook, sem
//  mexer em hooks.cpp). O dedo que cai numa aba nao vai para o jogo; a acao dispara ao SOLTAR na mesma aba.
//  Logica pura em tablistmx_logic.h.
// =====================================================================================

class TabListMX : public Widget
{
public:
	static ImVec2 SizeFor(const ImVec2& screen) { return screen; }
	static ImVec2 PosFor(const ImVec2& screen)  { (void)screen; return ImVec2(0.0f, 0.0f); }

	TabListMX();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	// type: 2 = push, 3 = move, 1 = pop (igual a AND_TouchEvent). true = evento consumido (nao segue para o jogo).
	static bool HandleRawTouch(int type, int num, int x, int y);

	// /tablistmx <args>  -> texto de resposta para o chat
	static std::string DebugCommand(const std::string& args);
};
