#pragma once
// Avatar por jogador (Maximo RP). Escolhe da Galeria (Java), envia ao servidor em pedacos (RPC 252),
// recebe o ID da conta + o JPEG salvo (RPC 251) e desenha no circulo do inventario.
// Servidor: maximo_avatar.inc (scriptfiles/MaximoRP/Avatares/<id da conta>.jpg).
#include <cstdint>
#include <string>

struct RwRaster;

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
	void Reset();                                                // nova conexao / saiu do servidor
}
