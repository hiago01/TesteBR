#pragma once

// =====================================================================================
//  Barra de armas na HUD
//  - lista TODAS as armas que o jogador tem (m_aWeapons[13] do ped local)
//  - cada arma vira um quadrado; toque no quadrado = coloca a arma na mao
//  - a arma que esta na mao fica marcada (borda dourada + fundo verde)
//  Implementacao: weaponbar.cpp
// =====================================================================================

class WeaponBar : public Widget
{
public:
	WeaponBar();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override;

private:
	struct Cell { ImVec2 min, max; int slot; };
	std::vector<Cell> m_cells;     // quadrados desenhados no ultimo frame
	int m_pushedSlot;              // slot sob o dedo no push (-1 = nenhum)
	int m_pendingSlot;             // troca pedida pelo toque, aplicada no draw (thread de render)
};
