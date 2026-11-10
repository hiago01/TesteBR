#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Agencia de Empregos "Maximo RP" (sobreposicao ImGui). Mesmo padrao do Provador/Inventario:
//   - abre por ordem do SERVIDOR (RPC 245 op 1) e fecha pelo X / aba Sair / Voltar;
//   - enquanto aberta consome TODOS os toques (HandleRawTouch, chamado de AND_TouchEvent_hook);
//   - o cliente so desenha e PEDE (RPC 246): contratar, pedir demissao, fechar. O SERVIDOR valida nivel, vaga, vagas e
//     grava no PostgreSQL; a tela so muda quando o servidor manda o novo estado.
//   - os empregos NAO existem aqui: tudo vem do cadastro do administrador (painel web do servidor).
//  Tocar na recepcionista ou no botao ao lado dela (marcadores, RPC 245 op 4): tudo por toque, sem tecla.
//  Marcadores: RPC 245 op 4 (emprego, grupo A) e op 6 (patinete, grupo B); tipos 1 agencia, 2 ponto de trabalho, 3 suporte de patinete
//  (anel + icone + botao; toque envia RPC 246 op 8) e 4 botao fixo na tela (Montar/Dobrar patinete; toque envia op 9). Protocolo: servidor patinete.pwn.
//  Tocar na recepcionista (actor do servidor): ObserveWorldTouch() reconhece o toque curto e, no desenho, procura o actor
//  sob o dedo e envia RPC 246 op 1 (o servidor confere a distancia e abre a tela).
//  Protocolo: ver servidor filterscripts/emprego.pwn.
// =====================================================================================

class JobsUI : public Widget
{
public:
	JobsUI();

	static bool IsOpen();
	static void Close(bool notifyServer = true);
	static void Reset();                       // desconexao: fecha e limpa

	static bool HandleRawTouch(int type, int num, int x, int y);
	static bool ConsumeFlushRequest();
	static bool OnBackPressed();               // true se estava aberta (e foi fechada)

	// Toque de mundo (a tela NAO esta aberta): chamado no fim do AND_TouchEvent_hook para os toques que seguem para o jogo.
	// NAO consome nada. type: 2 = push, 3 = move, 1 = pop.
	static void ObserveWorldTouch(int type, int num, int x, int y);

	// RPC 245 (servidor -> cliente). data/bytes = payload completo.
	static void OnServerPacket(const unsigned char* data, int bytes);

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};

// Anel no chao + icone flutuante NA CENA 3D (RwIm3D, com teste de profundidade). Chamar 1x por frame em hooks.cpp RenderEffects().
void JobsUI_RenderWorld();
