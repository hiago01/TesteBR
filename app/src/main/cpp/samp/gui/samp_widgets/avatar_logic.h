#pragma once
// Logica pura (sem Android/RenderWare) do avatar: pedacos do RPC 251/252 e validacao do JPEG. Testada no host.
#include <cstdint>
#include <cstddef>
#include <vector>

namespace AvatarLogic
{
	constexpr int kChunk = 512;        // bytes por pedaco (igual ao servidor: AV_CHUNK)
	constexpr int kMaxBytes = 24000;   // igual ao servidor: AV_MAX_BYTES
	constexpr int kMaxChunks = 64;     // igual ao servidor: AV_MAX_CHUNKS

	inline int ChunkCount(int total) { return total > 0 ? (total + kChunk - 1) / kChunk : 0; }
	inline int ChunkLen(int total, int idx)
	{
		int len = total - idx * kChunk;
		return len > kChunk ? kChunk : (len < 0 ? 0 : len);
	}

	// JPEG valido na forma: comeca com FF D8 e termina com FF D9
	inline bool LooksLikeJpeg(const uint8_t* d, size_t n)
	{
		return d && n >= 4 && d[0] == 0xFF && d[1] == 0xD8 && d[n - 2] == 0xFF && d[n - 1] == 0xD9;
	}

	// Remontagem do avatar que o servidor manda em pedacos (RPC 251 ops 2/3)
	struct Assembler
	{
		std::vector<uint8_t> buf;
		int total = 0, chunks = 0, next = 0;
		bool active = false;

		void Reset() { buf.clear(); total = chunks = next = 0; active = false; }
		bool Begin(uint32_t t, uint32_t n)
		{
			Reset();
			if (t < 4 || t > (uint32_t)kMaxBytes || n == 0 || n > (uint32_t)kMaxChunks || (int)n != ChunkCount((int)t)) return false;
			total = (int)t; chunks = (int)n; buf.reserve((size_t)t); active = true;
			return true;
		}
		// false = envio invalido (descartado)
		bool Chunk(uint32_t idx, const uint8_t* d, uint32_t len)
		{
			if (!active || !d || (int)idx != next || (int)idx >= chunks || (int)len != ChunkLen(total, (int)idx)) { Reset(); return false; }
			buf.insert(buf.end(), d, d + len);
			next++;
			return true;
		}
		bool Done() const { return active && next == chunks && (int)buf.size() == total; }
	};
}
