#pragma once

#include "hudstate.h"

// Widget da HUD (canto superior direito). Veja hudstate.h para o modelo de dados e hud.cpp
// para o desenho. Substitui vida / colete / dinheiro nativos do jogo.
// Toque no circulo "pessoas online" da HUD = abre a Central da Cidade (/central). Chamada por MaximoMenu::HandleRawTouch; true = consumiu o toque.
// blockNew = nao comecar toque novo (menu aberto).
bool HudHandleRawTouch(int type, int num, int x, int y, bool blockNew);

class HudInfo : public Widget
{
public:
	// Area ocupada pelo widget (canto superior direito) para uma tela de tamanho 'screen'.
	static ImVec2 SizeFor(const ImVec2& screen);
	static ImVec2 PosFor(const ImVec2& screen);

	HudInfo();

	virtual void draw(ImGuiRenderer* renderer) override;
	// A HUD e so visual: nao consome toques (o jogo continua recebendo).
	virtual void touchEvent(const ImVec2& pos, TouchType type) override {}
};
