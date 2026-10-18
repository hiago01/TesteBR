#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Seletor de armas radial (estilo GTA V) -- substitui a antiga barra inferior central.
//
//  HUD normal (roda fechada):
//    - so um botao pequeno no canto inferior direito, com o icone da arma atual e a
//      municao no formato  total-pente  (ex.: 9999-34). O centro inferior fica livre.
//
//  Toque no botao:
//    - toque CURTO  (< hold, padrao 280 ms): proxima arma disponivel, sem abrir a roda;
//    - toque LONGO  (>= hold)             : abre a roda; arraste ate o segmento e solte;
//      soltar fora de uma opcao valida (ou o Android cancelar o toque) mantem a arma atual.
//
//  Roda: fundo escuro translucido, centro = punhos, segmentos = categorias de armas
//  (ver weaponwheel_logic.h). Categorias com varias armas (pesadas/explosivos, outros)
//  abrem um leque de sub-celulas por fora do segmento.
//
//  Toques: o dedo que comeca no botao e "reivindicado" por onRawTouch() (chamado de
//  AND_TouchEvent_hook, que conhece o id do dedo) e NAO chega ao jogo, entao a camera
//  nao gira enquanto a roda esta aberta. A troca de arma em si continua sendo feita pela
//  funcao nativa CPed::SetCurrentWeapon (o SA-MP sincroniza a arma ativa no sync onfoot).
//
//  Implementacao: weaponbar.cpp | logica pura testavel: weaponwheel_logic.h
// =====================================================================================

class WeaponBar : public Widget
{
public:
	// O widget cobre a tela inteira (a roda e desenhada em coordenadas absolutas).
	static ImVec2 SizeFor(const ImVec2& screen) { return screen; }
	static ImVec2 PosFor(const ImVec2& screen)  { (void)screen; return ImVec2(0.0f, 0.0f); }

	// Tempo (ms) que diferencia toque curto de toque longo. Configuravel em tempo de execucao.
	static constexpr int kDefaultHoldMs = 280;
	static void SetHoldMs(int ms);
	static int  HoldMs();

	WeaponBar();

	virtual void draw(ImGuiRenderer* renderer) override;
	// A entrada e tratada em onRawTouch(); aqui nao ha nada a fazer (evita foco/ativacao do Widget).
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }

	// type: 2 = push, 3 = move, 1 = pop (igual a AND_TouchEvent). Retorna true se o evento
	// pertence ao botao/roda e portanto NAO deve seguir para o jogo nem para os outros widgets.
	bool onRawTouch(int type, int num, int x, int y);

private:
	enum State : int { kIdle = 0, kPressed = 1, kOpen = 2, kReleasing = 3 };

	// Escritos pela thread de entrada, lidos no draw (e vice-versa): tudo atomico.
	std::atomic<int>     m_state;
	std::atomic<int>     m_finger;        // id do dedo dono do toque (-1 = nenhum)
	std::atomic<int>     m_orphan;        // dedo cujo toque foi fechado a forca e ainda esta na tela
	std::atomic<int64_t> m_downMs;
	std::atomic<float>   m_downX, m_downY;
	std::atomic<float>   m_curX, m_curY;
	std::atomic<bool>    m_cancelRelease; // soltura por ACTION_CANCEL do Android
	std::atomic<bool>    m_available;     // botao visivel/usavel neste frame
	std::atomic<int>     m_pendingSlot;   // troca pedida (aplicada no draw), -1 = nenhuma
	std::atomic<int>     m_pendingStep;   // +1/-1: proxima/anterior (toque curto)

	float m_anim;                         // 0..1, so a thread de draw usa
};
