#pragma once
// Celular - codificador JPEG (baseline, 4:2:0, tabelas Huffman padrao) e reducao de imagem. Sem dependencias externas.
// Usado pela Camera: o jogo entrega os pixels da tela (RGBA), aqui viram um JPEG pequeno (~20-40 KB) para guardar e postar.
// Logica pura (testada no PC: os JPEGs gerados sao decodificados por outro programa nos testes).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Phone
{
	namespace Jpeg
	{
		namespace detail
		{
			static const uint8_t kZig[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
			                                  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
			static const uint8_t kQY[64] = { 16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
			                                 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
			static const uint8_t kQC[64] = { 17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
			                                 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
			static const uint8_t kDcBits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
			static const uint8_t kDcVal[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
			static const uint8_t kAcBits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
			static const uint8_t kAcVal[162] = {
				0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
				0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
				0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
				0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
				0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
				0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
				0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

			struct Huff { uint16_t code[256]; uint8_t len[256]; };
			inline void BuildHuff(Huff& h, const uint8_t* bits, const uint8_t* vals)
			{
				std::memset(&h, 0, sizeof(h));
				int code = 0, k = 0;
				for (int l = 1; l <= 16; ++l) { for (int i = 0; i < bits[l - 1]; ++i, ++k) { h.code[vals[k]] = (uint16_t)code; h.len[vals[k]] = (uint8_t)l; ++code; } code <<= 1; }
			}

			struct Out
			{
				std::string s; uint32_t acc = 0; int n = 0;
				void byte(int b) { s.push_back((char)b); }
				void word(int w) { byte(w >> 8); byte(w & 255); }
				void bits(uint32_t v, int len)
				{
					acc = (acc << len) | (v & ((1u << len) - 1)); n += len;
					while (n >= 8) { const int b = (acc >> (n - 8)) & 255; byte(b); if (b == 255) byte(0); n -= 8; }
				}
				void flush() { if (n > 0) bits(0x7f, 8 - n); }
			};

			inline void Dct8(const float* in, float* out, const float cs[8][8])
			{
				float tmp[64];
				for (int y = 0; y < 8; ++y) for (int u = 0; u < 8; ++u) { float a = 0; for (int x = 0; x < 8; ++x) a += in[y * 8 + x] * cs[u][x]; tmp[y * 8 + u] = a; }
				for (int u = 0; u < 8; ++u) for (int v = 0; v < 8; ++v) { float a = 0; for (int y = 0; y < 8; ++y) a += tmp[y * 8 + u] * cs[v][y]; out[v * 8 + u] = a; }
			}
		}

		// rgba: w*h*4 bytes (linha 0 = topo). quality 1..100. Retorna "" se os parametros forem invalidos.
		inline std::string Encode(const uint8_t* rgba, int w, int h, int quality)
		{
			using namespace detail;
			if (!rgba || w < 1 || h < 1 || w > 4096 || h > 4096) return std::string();
			quality = std::max(1, std::min(100, quality));
			const int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
			uint8_t qy[64], qc[64];
			for (int i = 0; i < 64; ++i)
			{
				qy[i] = (uint8_t)std::max(1, std::min(255, (kQY[i] * sc + 50) / 100));
				qc[i] = (uint8_t)std::max(1, std::min(255, (kQC[i] * sc + 50) / 100));
			}
			Huff hdc, hac; BuildHuff(hdc, kDcBits, kDcVal); BuildHuff(hac, kAcBits, kAcVal);
			float cs[8][8];
			for (int u = 0; u < 8; ++u) for (int x = 0; x < 8; ++x) cs[u][x] = (float)((u == 0 ? std::sqrt(1.0 / 8.0) : std::sqrt(2.0 / 8.0)) * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));

			const int mw = (w + 15) / 16, mh = (h + 15) / 16, pw = mw * 16, ph = mh * 16;
			std::vector<float> Y((size_t)pw * ph), Cb((size_t)pw * ph), Cr((size_t)pw * ph);
			for (int y = 0; y < ph; ++y)
				for (int x = 0; x < pw; ++x)
				{
					const uint8_t* p = rgba + ((size_t)std::min(y, h - 1) * w + std::min(x, w - 1)) * 4;
					const float r = p[0], g = p[1], b = p[2];
					const size_t i = (size_t)y * pw + x;
					Y[i] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
					Cb[i] = -0.168736f * r - 0.331264f * g + 0.5f * b;
					Cr[i] = 0.5f * r - 0.418688f * g - 0.081312f * b;
				}

			Out o; o.s.reserve((size_t)w * h / 8 + 1024);
			o.word(0xFFD8);
			o.word(0xFFDB); o.word(67); o.byte(0); for (int i = 0; i < 64; ++i) o.byte(qy[kZig[i]]);
			o.word(0xFFDB); o.word(67); o.byte(1); for (int i = 0; i < 64; ++i) o.byte(qc[kZig[i]]);
			o.word(0xFFC0); o.word(17); o.byte(8); o.word(h); o.word(w); o.byte(3);
			o.byte(1); o.byte(0x22); o.byte(0); o.byte(2); o.byte(0x11); o.byte(1); o.byte(3); o.byte(0x11); o.byte(1);
			o.word(0xFFC4); o.word(2 + 1 + 16 + 12); o.byte(0x00); for (int i = 0; i < 16; ++i) o.byte(kDcBits[i]); for (int i = 0; i < 12; ++i) o.byte(kDcVal[i]);
			o.word(0xFFC4); o.word(2 + 1 + 16 + 162); o.byte(0x10); for (int i = 0; i < 16; ++i) o.byte(kAcBits[i]); for (int i = 0; i < 162; ++i) o.byte(kAcVal[i]);
			o.word(0xFFDA); o.word(12); o.byte(3); o.byte(1); o.byte(0x00); o.byte(2); o.byte(0x00); o.byte(3); o.byte(0x00); o.byte(0); o.byte(63); o.byte(0);

			int dcPrev[3] = { 0, 0, 0 };
			auto nbits = [](int v) { v = v < 0 ? -v : v; int n = 0; while (v) { ++n; v >>= 1; } return n; };
			auto block = [&](const float* px, int stride, int comp, const uint8_t* q)
			{
				float in[64], co[64];
				for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) in[y * 8 + x] = px[y * stride + x];
				Dct8(in, co, cs);
				int z[64];
				for (int i = 0; i < 64; ++i) z[i] = (int)std::lround(co[kZig[i]] / (float)q[kZig[i]]);
				const int diff = z[0] - dcPrev[comp]; dcPrev[comp] = z[0];
				int n = nbits(diff);
				o.bits(hdc.code[n], hdc.len[n]);
				if (n) o.bits((uint32_t)(diff < 0 ? diff - 1 : diff), n);
				int run = 0;
				for (int i = 1; i < 64; ++i)
				{
					if (z[i] == 0) { ++run; continue; }
					while (run > 15) { o.bits(hac.code[0xF0], hac.len[0xF0]); run -= 16; }
					n = nbits(z[i]);
					const int sym = (run << 4) | n;
					o.bits(hac.code[sym], hac.len[sym]);
					o.bits((uint32_t)(z[i] < 0 ? z[i] - 1 : z[i]), n);
					run = 0;
				}
				if (run) o.bits(hac.code[0x00], hac.len[0x00]);
			};
			std::vector<float> sub(64);
			for (int my = 0; my < mh; ++my)
				for (int mx = 0; mx < mw; ++mx)
				{
					const size_t base = (size_t)my * 16 * pw + (size_t)mx * 16;
					block(&Y[base], pw, 0, qy);
					block(&Y[base + 8], pw, 0, qy);
					block(&Y[base + (size_t)8 * pw], pw, 0, qy);
					block(&Y[base + (size_t)8 * pw + 8], pw, 0, qy);
					for (int c = 1; c <= 2; ++c)
					{
						const std::vector<float>& pl = c == 1 ? Cb : Cr;
						for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
						{
							const size_t i = base + (size_t)(2 * y) * pw + 2 * x;
							sub[y * 8 + x] = (pl[i] + pl[i + 1] + pl[i + pw] + pl[i + pw + 1]) * 0.25f;
						}
						block(sub.data(), 8, c, qc);
					}
				}
			o.flush();
			o.word(0xFFD9);
			return o.s;
		}
	}

	// Reducao de imagem RGBA (media de caixa). flipY: a tela do OpenGL vem de baixo para cima. Retorna pixels RGBA opacos.
	namespace Image
	{
		inline std::vector<uint8_t> Fit(const uint8_t* src, int sw, int sh, int maxW, int maxH, bool flipY, int& ow, int& oh)
		{
			ow = oh = 0;
			if (!src || sw < 1 || sh < 1 || maxW < 1 || maxH < 1) return {};
			const double k = std::min(1.0, std::min((double)maxW / sw, (double)maxH / sh));
			ow = std::max(1, (int)std::lround(sw * k)); oh = std::max(1, (int)std::lround(sh * k));
			std::vector<uint8_t> out((size_t)ow * oh * 4);
			for (int y = 0; y < oh; ++y)
			{
				const int y0 = (int)((int64_t)y * sh / oh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * sh / oh));
				const int dy = flipY ? oh - 1 - y : y;
				for (int x = 0; x < ow; ++x)
				{
					const int x0 = (int)((int64_t)x * sw / ow), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * sw / ow));
					uint32_t r = 0, g = 0, b = 0, n = 0;
					for (int yy = y0; yy < y1 && yy < sh; ++yy)
						for (int xx = x0; xx < x1 && xx < sw; ++xx) { const uint8_t* p = src + ((size_t)yy * sw + xx) * 4; r += p[0]; g += p[1]; b += p[2]; ++n; }
					if (!n) n = 1;
					uint8_t* d = &out[((size_t)dy * ow + x) * 4];
					d[0] = (uint8_t)(r / n); d[1] = (uint8_t)(g / n); d[2] = (uint8_t)(b / n); d[3] = 255;
				}
			}
			return out;
		}
	}
}
