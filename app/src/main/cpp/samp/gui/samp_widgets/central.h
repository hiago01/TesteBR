#pragma once

#include <atomic>
#include <cstdint>
#include <string>

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

	// ---- usados pelo seletor padronizado (avatarpicker.h): Menu jogador, Central e aba Adesivos do inventario ----
	static int  MyStyle();                         // modelo de fundo da MINHA linha (0 = Padrao); so vale depois de HaveList()
	static bool HaveList();                        // ja chegou alguma lista do servidor
	static void RequestList();                     // pede a lista ("/cenx req"; o servidor limita a 1 pedido a cada 1,5 s)
	static void ChooseStyle(int idx);              // escolhe o modelo (o servidor confere se esta liberado na conta)
	static void DrawStylePreview(ImGuiRenderer* r, ImVec2 a, ImVec2 b, int idx, float u, double tsec);   // fundo de linha com o efeito

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
	virtual void keyboardEvent(const std::string& input) override;     // texto da caixa de pesquisa (Keyboard::show)
};
