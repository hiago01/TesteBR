#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Provador "Maximo RP" (sobreposicao ImGui) -- interface de personalizacao do /skin7.
//
//  - Abre/fecha com /provador (chat). NAO pausa o jogo: e uma sobreposicao, no mesmo padrao
//    do MaximoMenu e do InventoryUI.
//  - Enquanto aberto, TODO toque e consumido aqui (HandleRawTouch, chamado de
//    AND_TouchEvent_hook): os controles do GTA SA / SA-MP por baixo nao recebem nada.
//  - Nao reimplementa roupas: cada opcao vira um pedido ao CCustomSkin (game/CustomSkin.cpp),
//    que roda o MESMO "/skin7 slot <nome> <n|on|off>" na thread do jogo (inclusive a
//    exclusividade por categoria). A UI so desenha o ESTADO REAL publicado por la.
//  - Previa: cada escolha e aplicada ao vivo; APLICAR confirma, CANCELAR/VOLTAR restauram
//    o que havia antes (backup feito pelo CCustomSkin).
//
//  Implementacao: provador.cpp
// =====================================================================================

class Provador : public Widget
{
public:
	Provador();

	static bool IsOpen();
	static void Open();
	static void Close();                       // restaura a previa nao confirmada e fecha (SEM perguntar: uso interno/forcado)
	static void RequestClose();                // Voltar / /provador: com previa nao salva, pergunta SALVAR E SAIR / SAIR SEM SALVAR / CONTINUAR
	static void Toggle();

	// Thread do jogo, no inicio de AND_TouchEvent_hook. type: 2 = push, 3 = move, 1 = pop.
	// Retorna true se o evento foi consumido (NAO repassar ao jogo nem aos outros widgets).
	static bool HandleRawTouch(int type, int num, int x, int y);

	// true UMA vez apos abrir: o hook solta os dedos que o jogo ainda considera pressionados.
	static bool ConsumeFlushRequest();

	// Botao Voltar do Android. true se tratado aqui (fechou a tela, ou engoliu o eco do mesmo toque).
	static bool OnBackPressed();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
