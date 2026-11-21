// JPEG: copia ESTATICA do stb_image so com JPEG (mesmo truque de phonephotos.cpp) para nao colidir com a do jogo (so PNG).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../../vendor/stb/stb_image.h"
#include "avatar_jpeg.h"
#include <cstring>

bool AvatarJpeg::Decode(const uint8_t* data, int n, std::vector<uint8_t>& rgba, int& w, int& h)
{
	if (!data || n < 4) return false;
	int ch = 0;
	unsigned char* px = stbi_load_from_memory(data, n, &w, &h, &ch, 4);
	if (!px) return false;
	if (w < 8 || h < 8 || w > 1024 || h > 1024) { stbi_image_free(px); return false; }
	rgba.assign(px, px + (size_t)w * h * 4);
	stbi_image_free(px);
	return true;
}
