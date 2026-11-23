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
	RwRaster* Raster();
	RwRaster* PhotoOf(uint32_t acc);                              // foto de uma conta (podio do ranking); avatar padrao enquanto a foto nao chega
	void DrawPhotoOf(ImGuiRenderer* r, uint32_t acc, float cx, float cy, float ar);   // foto da conta recortada em circulo
	void DrawPhotoRas(ImGuiRenderer* r, RwRaster* ras, float cx, float cy, float ar);                                          // nullptr = sem avatar
	uint32_t AccountId();                                        // 0 = ainda nao recebido
	bool PopMessage(std::string& out);                           // aviso curto para o toast do inventario
	void Reset();
	RwRaster* PngRaster(const unsigned char* png, int n);        // PNG (RGBA) -> textura; nullptr se falhar (thread de desenho)

	// ---- bordas do avatar (globais; cada jogador escolhe a sua, o servidor guarda por conta) ----
	constexpr int kBorderCount = 8;          // IDs: 0 Natural (gratis) | 1 Guirlanda | 2 Papai Noel | 3 Neon | 4 Arco-iris | 5 Fogos 2027 | 6 Champagne | 7 Contagem 2027 (igual ao servidor AV_BORDERS)
	constexpr int kLineCount = 12;           // modelos de fundo da linha (igual ao servidor AV_LINES)
	bool BorderUnlocked(int idx);            // moldura liberada na conta (0 e sempre gratis)
	bool LineUnlocked(int idx);              // modelo de fundo liberado na conta (0 e sempre gratis)
	int  Border();                           // borda escolhida (0..kBorderCount-1)
	void SetBorder(int idx);                 // aplica na hora e avisa o servidor (RPC 252 op 5)
	const char* BorderName(int idx);
	// desenha a borda em volta de um avatar circular de raio ar (centro cx,cy); u = escala da tela; tsec = tempo em segundos (animacoes)
	// foto do avatar (ou o avatar padrao) recortada em circulo
	void DrawPhoto(ImGuiRenderer* r, float cx, float cy, float ar);
	void DrawBorder(ImGuiRenderer* r, int idx, float cx, float cy, float ar, float u, double tsec);                                                // nova conexao / saiu do servidor
}
