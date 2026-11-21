#pragma once
// Avatar por jogador (Maximo RP). Escolhe da Galeria (Java), envia ao servidor em pedacos (RPC 252),
// recebe o ID da conta + o JPEG salvo (RPC 251) e desenha no circulo do inventario.
// Servidor: maximo_avatar.inc (scriptfiles/MaximoRP/Avatares/<id da conta>.jpg).
#include <cstdint>
#include <string>

struct RwRaster;
class ImGuiRenderer;

namespace Avatar
{
	void OnServerPacket(const unsigned char* data, int bytes);   // RPC 251 (thread de rede)
	void OnPicked(const uint8_t* jpg, int n);                    // JNI: foto escolhida ja recortada 128x128 em JPEG (thread Java)
	void OnPickFailed();                                         // JNI: usuario cancelou / erro ao ler a foto
	void RequestPick();                                          // abre a Galeria (thread de desenho)
	void Tick();                                                 // thread de desenho: textura, envio pendente, pedido ao servidor
	RwRaster* Raster();                                          // nullptr = sem avatar
	uint32_t AccountId();                                        // 0 = ainda nao recebido
	bool PopMessage(std::string& out);                           // aviso curto para o toast do inventario
	void Reset();

	// ---- bordas do avatar (globais; cada jogador escolhe a sua, o servidor guarda por conta) ----
	constexpr int kBorderCount = 5;          // 0 Natural (cinza) | 1 Guirlanda (pisca-pisca) | 2 Papai Noel | 3 Neon pulsante | 4 Arco-iris
	int  Border();                           // borda escolhida (0..kBorderCount-1)
	void SetBorder(int idx);                 // aplica na hora e avisa o servidor (RPC 252 op 5)
	const char* BorderName(int idx);
	// desenha a borda em volta de um avatar circular de raio ar (centro cx,cy); u = escala da tela; tsec = tempo em segundos (animacoes)
	// foto do avatar (ou o avatar padrao) recortada em circulo
	void DrawPhoto(ImGuiRenderer* r, float cx, float cy, float ar);
	void DrawBorder(ImGuiRenderer* r, int idx, float cx, float cy, float ar, float u, double tsec);                                                // nova conexao / saiu do servidor
}
