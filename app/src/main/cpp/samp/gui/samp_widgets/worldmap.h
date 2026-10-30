#pragma once
// =====================================================================================
//  Mapa aberto "Maximo RP" (sobreposicao ImGui). Mesmo padrao do inventario:
//   - abre por /mapa, pela aba MAPA do menu ou por ordem do SERVIDOR (RPC 204 op 2);
//   - enquanto aberto consome TODOS os toques (HandleRawTouch em AND_TouchEvent_hook);
//   - imagem = tiles do radar nativo (radarNN) ou SAMP/radar/mapa.png (opcional);
//   - o servidor manda os locais/rota (RPC 204) e recebe avisos pelo comando oculto "/mapax ...".
//  Protocolo: ver WorldMapCore.h e servidor qawno/include/maximo_mapa.inc.
// =====================================================================================
#include <string>
#include "../widget.h"

class WorldMapWidget : public Widget
{
public:
	WorldMapWidget();
	static bool IsOpen();
	static void Open();
	static void Close(bool notifyServer = true);
	static void Toggle();
	static void Reset();                          // desconexao
	static bool HandleRawTouch(int type, int num, int x, int y);
	static bool ConsumeFlushRequest();
	static bool OnBackPressed();
	static void OnServerPacket(const unsigned char* data, int bytes);   // RPC 204
	static std::string DebugCommand(const std::string& args);           // /mapa grade | info

	virtual void draw(ImGuiRenderer* renderer) override;
	virtual void touchEvent(const ImVec2& pos, TouchType type) override { (void)pos; (void)type; }
};
