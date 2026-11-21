#pragma once

#include <atomic>
#include <cstdint>

// =====================================================================================
//  Central da Cidade (scoreboard Maximo RP): lista todos os cidadaos online. Cada jogador escolhe o MODELO DE FUNDO da sua
//  linha (alguns com efeito), guardado por conta no servidor (maximo_central.inc, RPC 253/254) e visto por todos.
//  Abre por /central (servidor -> RPC 253 op 3) ou pela aba Perfil da barra /tablistmx. Fecha pelo X, tocando fora ou Voltar.
//  Enquanto aberta consome todos os toques (HandleRawTouch, chamado no inicio de MaximoMenu::HandleRawTouch).
//  Logica pura em central_logic.h.
// =====================================================================================

class CentralUI : public Widget
{
public:
	CentralUI();

	static bool IsOpen();
	static void Open();
	static void Close();
	static void Reset();                                   // desconexao
	static bool HandleRawTouch(int type, int num, int x, int y);
	static bool OnBackPressed();                           // true se estava aberta (e foi fechada)
	static void OnServerPacket(const unsigned char* data, int bytes);   // RPC 253

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
