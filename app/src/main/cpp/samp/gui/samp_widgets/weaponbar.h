#pragma once

#include <atomic>

// =====================================================================================
//  Barra de armas na HUD  (parte inferior, centralizada)
//
//  Visual:
//    - [<] [arma] [arma] [ARMA SELECIONADA] [arma] [arma] [>]
//    - celulas com cantos chanfrados, fundo escuro translucido e borda branca fina
//    - arma selecionada: maior, borda verde com brilho, "colchetes" nos cantos de baixo
//      e triangulo verde apontando pra ela (em cima)
//    - setas laterais (chevron verde): arma anterior / proxima (da a volta no fim)
//    - icones ORIGINAIS do jogo (textura "<modelo>icon" do banco gta3, igual ao
//      CWidgetPlayerInfo::DrawWeaponIcon do libGTASA; punho = "fist")
//    - municao no formato  total-pente  (ex.: 196-7)
//
//  Comportamento:
//    - lista TODAS as armas que o jogador tem (m_aWeapons[13] do ped local)
//    - toque numa celula = coloca a arma na mao; setas = anterior / proxima
//    - mostra no maximo kMaxVisible celulas; com mais armas a janela acompanha a selecionada
//
//  Implementacao: weaponbar.cpp
// =====================================================================================

class WeaponBar : public Widget
{
public:
	// Altura total do widget = altura de UMA celula * kHeightRatio
	// (cabe: triangulo + celula selecionada (maior) + folga pro brilho).
	static constexpr float kHeightRatio = 1.42f;
	// Quantas celulas aparecem ao mesmo tempo.
	static constexpr int   kMaxVisible  = 5;

	// Tamanho / posicao do widget (inferior central) para uma tela de tamanho 'screen'.
	static ImVec2 SizeFor(const ImVec2& screen);
	static ImVec2 PosFor(const ImVec2& screen);

	WeaponBar();

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override;

private:
	// ids de toque: >= 0 e o slot da arma; negativos sao as setas
	enum : int { kHitNone = -1, kHitPrev = -2, kHitNext = -3 };

	struct Cell { ImVec2 min, max; int id; };
	static constexpr int kMaxCells = kMaxVisible + 2;

	// Escritos no draw (thread de render), lidos no touch (thread de input).
	Cell             m_cells[kMaxCells];
	std::atomic<int> m_cellCount;
	std::atomic<int> m_pushedId;     // id sob o dedo no push (kHitNone = nenhum)
	std::atomic<int> m_pendingSlot;  // troca pedida pelo toque, aplicada no draw (-1 = nenhuma)
	std::atomic<int> m_pendingStep;  // -1 / +1 pedido pelas setas, aplicado no draw
};
