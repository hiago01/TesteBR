#pragma once
// Decodificador JPEG do avatar. O jogo compila o stb_image so com PNG (game/sprite2d.cpp); aqui vive uma copia ESTATICA (so JPEG) em avatar_jpeg.cpp.
#include <cstdint>
#include <vector>
namespace AvatarJpeg
{
	// RGBA 8888; false se nao for JPEG valido ou tamanho fora de 8..1024
	bool Decode(const uint8_t* data, int n, std::vector<uint8_t>& rgba, int& w, int& h);
}
