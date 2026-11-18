#pragma once

#include <cstdint>

// =====================================================================================
//  GERENCIADOR DE GRUPO (/grupo) - tela cheia com as abas Inicio / Membros / Farm / Cofre. Mesmo padrao do Inventario/Garagem/Tablet:
//   - abre por ordem do SERVIDOR (RPC 245 op 16: porta + token assinado) e fecha por "Fechar" / Voltar;
//   - enquanto aberta consome TODOS os toques (HandleRawTouch, em AND_TouchEvent_hook) e esconde o HUD;
//   - os DADOS vem do PostgreSQL pelo servico celular-service (HTTP /o/*, porta publica, token de 6 h); acoes que mexem no dinheiro/inventario
//     do jogador, convite, promocao e expulsao vao por RPC 246 (ops 40..45) e o SERVIDOR do jogo valida tudo;
//   - RPC 245 op 17 = acao concluida (recarrega a aba) | op 18 = mensagem do servidor (u8 erro + texto) mostrada no cartao do painel.
//  Tela e logica: gui/orgpanel/OrgPanelUI.h (testada no PC). Protocolo do servidor: filterscripts/org.pwn.
// =====================================================================================

class OrgPanelWidget : public Widget
{
public:
	OrgPanelWidget();

	static bool IsOpen();
	static void Reset();                                           // desconexao: fecha e limpa

	static bool HandleRawTouch(int type, int num, int x, int y);   // thread do jogo (AND_TouchEvent_hook). true = consumido
	static bool ConsumeFlushRequest();
	static bool OnBackPressed();                                   // true se estava aberta

	static void OnServerPacket(const unsigned char* data, int bytes);   // RPC 245: so as ops 16, 17 e 18 (as outras sao de outras telas)

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
