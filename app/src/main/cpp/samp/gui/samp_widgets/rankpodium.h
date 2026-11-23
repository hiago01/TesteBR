#pragma once
// Podio dos 3 primeiros do ranking (Menu jogador > Ranking): foto de perfil + moldura do avatar, plaquinha do nome com o
// fundo de linha da Central, valor, coroa, louros e pedestais 1o / 2o / 3o. Dados vem do servidor (RPC 245 op 22, fim do pacote).
#include <cstdint>
#include <string>

class ImGuiRenderer;

namespace RankPodium
{
	struct Entry { bool ok = false; uint32_t acc = 0; uint8_t border = 0, style = 0; std::string name, value; };
	constexpr float kHeight = 172.0f;     // altura em unidades de desenho (1280x720)
	// desenha o podio em (x0,y0), largura w e altura kHeight*u (pixels). e[0] = 1o lugar, e[1] = 2o, e[2] = 3o.
	void Draw(ImGuiRenderer* r, float x0, float y0, float w, float u, double tsec, const Entry e[3]);
}
