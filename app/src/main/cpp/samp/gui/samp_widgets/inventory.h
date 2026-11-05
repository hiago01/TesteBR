#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Inventario "Maximo RP" (sobreposicao ImGui). Mesmo padrao do MaximoMenu:
//   - abre por ordem do SERVIDOR (/inv -> RPC 203 op 2) e fecha pelo botao FECHAR / Voltar;
//   - enquanto aberto consome TODOS os toques (HandleRawTouch, chamado de AND_TouchEvent_hook);
//   - o cliente so desenha e envia gestos como o comando oculto "/invx <acao>"; o SERVIDOR
//     (Pawn) e a autoridade sobre posse, quantidade, uso, chao, negociacao e salvamento.
//   - a UI so muda quando o servidor manda o novo estado (RPC 203).
//  Gestos: toque = seleciona | segurar ~0,25 s e arrastar = move/usa/dropa/oferta |
//          arrastar logo ao tocar = rolar. Botoes USAR/DROPAR/NEGOCIAR dao a alternativa por toque.
//  Protocolo: ver servidor qawno/include/maximo_inv.inc.
// =====================================================================================

class InventoryUI : public Widget
{
public:
	InventoryUI();

	static bool IsOpen();
	static void Open();
	static void Close(bool notifyServer = true);
	static void Reset();                       // desconexao: limpa tudo

	static bool HandleRawTouch(int type, int num, int x, int y);
	static bool ConsumeFlushRequest();
	static bool OnBackPressed();               // true se estava aberto (e foi fechado)

	// ---- slots rapidos / botao Inventario do HUD moderno (ModernControls: B_SLOTS e B_INV) ----
	// Os slots rapidos espelham os primeiros slots do inventario do SERVIDOR (o estado vem do RPC 203; o servidor continua sendo
	// a autoridade: usar = "/invx use N"). O estado persiste depois de fechar o inventario.
	static bool HotbarGet(int slot, int& icon, int& qty);                 // true se ha item nesse slot
	static void HotbarUse(int slot);                                      // usa o item do slot (se houver)
	static void RequestOpen();                                            // pede ao servidor para abrir o inventario (/inv)
	static void DrawHotbarIcon(ImGuiRenderer* r, int icon, const ImVec2& a, const ImVec2& b, int alpha);

	// RPC 203 (servidor -> cliente). data/bytes = payload completo.
	static void OnServerPacket(const unsigned char* data, int bytes);

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
