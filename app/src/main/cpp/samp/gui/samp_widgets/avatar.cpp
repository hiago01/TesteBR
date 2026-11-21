#include "../../main.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../java/jniutil.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "avatar_jpeg.h"
#include "avatar_default.h"
#include "avatar.h"
#include "avatar_logic.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

extern CNetGame*     pNetGame;
extern CJavaWrapper* pJavaWrapper;

namespace
{
	std::mutex g_m;
	std::atomic<uint32_t> g_id{0};
	bool                  g_infoKnown = false;      // INFO do servidor ja chegou
	AvatarLogic::Assembler g_asm;                   // avatar que o servidor esta mandando
	std::vector<uint8_t>  g_decode;                 // bytes prontos para virar textura
	bool                  g_decodePending = false;
	bool                  g_clear = false;          // servidor disse "sem avatar"
	std::vector<uint8_t>  g_candidate;              // foto escolhida, aguardando o OK do servidor
	std::vector<uint8_t>  g_upload;                 // pronto para enviar
	bool                  g_uploadPending = false;
	std::string           g_msg;
	bool                  g_msgPending = false;
	int                   g_reqTries = 0;
	int64_t               g_lastReq = 0;
	RwRaster*             g_raster = nullptr;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	void Say(const char* s) { g_msg = s; g_msgPending = true; }       // chamar com g_m travado

	bool Connected() { return pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED && pNetGame->GetRakClient(); }

	void SendBs(RakNet::BitStream& bs) { pNetGame->GetRakClient()->RPC(&RPC_AvatarOut, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr); }

	void SendSimple(uint8_t op) { RakNet::BitStream bs; bs.Write(op); SendBs(bs); }

	void SendUpload(const std::vector<uint8_t>& d)
	{
		const int total = (int)d.size();
		const int n = AvatarLogic::ChunkCount(total);
		{
			RakNet::BitStream bs;
			bs.Write((uint8_t)1); bs.Write((uint32_t)total); bs.Write((uint16_t)n);
			SendBs(bs);
		}
		for (int c = 0; c < n; c++)
		{
			const int len = AvatarLogic::ChunkLen(total, c);
			RakNet::BitStream bs;
			bs.Write((uint8_t)2); bs.Write((uint16_t)c); bs.Write((uint16_t)len);
			for (int i = 0; i < len; i++) bs.Write((uint8_t)d[(size_t)c * AvatarLogic::kChunk + i]);   // um byte por vez: mesmo formato que o Pawn le (PR_UINT8)
			SendBs(bs);
		}
	}

	// JPEG -> RwRaster 32 bits (mesmo caminho do atlas do inventario; o JPEG e decodificado por AvatarJpeg, ja que o stb do jogo e so PNG)
	RwRaster* MakeRaster(const uint8_t* jpg, int jn)
	{
		std::vector<uint8_t> rgba; int w = 0, h = 0;
		if (!AvatarJpeg::Decode(jpg, jn, rgba, w, h)) { FLog("[AVATAR] JPEG invalido"); return nullptr; }
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) return nullptr;
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image))
		{
			if (image) RwImageDestroy(image);
			return nullptr;
		}
		for (int y = 0; y < h; ++y)
			memcpy(image->cpPixels + (size_t)y * image->stride, rgba.data() + (size_t)y * w * 4, (size_t)w * 4);

		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888;
			rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image))
		{
			if (RwRasterDestroy) RwRasterDestroy(raster);
			RwImageDestroy(image);
			return nullptr;
		}
		RwImageDestroy(image);
		return raster;
	}
}

void Avatar::Reset()
{
	std::lock_guard<std::mutex> l(g_m);
	g_id.store(0); g_infoKnown = false; g_asm.Reset();
	g_decode.clear(); g_decodePending = false; g_clear = true;
	g_candidate.clear(); g_upload.clear(); g_uploadPending = false;
	g_msg.clear(); g_msgPending = false; g_reqTries = 0; g_lastReq = 0;
}

// ---- RPC 251 (servidor -> cliente) ----
void Avatar::OnServerPacket(const unsigned char* data, int bytes)
{
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	uint8_t op = 0;
	if (!bs.Read(op)) return;
	std::lock_guard<std::mutex> l(g_m);
	if (op == 1)         // INFO: id da conta + tem avatar
	{
		uint32_t id = 0; uint8_t has = 0;
		if (!bs.Read(id) || !bs.Read(has)) return;
		g_id.store(id); g_infoKnown = true;
		if (!has) g_clear = true;
	}
	else if (op == 2)    // BEGIN
	{
		uint32_t total = 0; uint16_t n = 0;
		if (!bs.Read(total) || !bs.Read(n)) return;
		if (!g_asm.Begin(total, n)) FLog("[AVATAR] BEGIN invalido total=%u n=%u", total, (unsigned)n);
	}
	else if (op == 3)    // CHUNK
	{
		uint16_t idx = 0, len = 0;
		if (!bs.Read(idx) || !bs.Read(len)) return;
		if (len == 0 || len > AvatarLogic::kChunk) { g_asm.Reset(); return; }
		uint8_t tmp[AvatarLogic::kChunk];
		for (int i = 0; i < len; i++) { if (!bs.Read(tmp[i])) { g_asm.Reset(); return; } }
		if (!g_asm.Chunk(idx, tmp, len)) { FLog("[AVATAR] pedaco %u invalido", (unsigned)idx); return; }
		if (g_asm.Done())
		{
			if (AvatarLogic::LooksLikeJpeg(g_asm.buf.data(), g_asm.buf.size())) { g_decode = g_asm.buf; g_decodePending = true; g_clear = false; }
			g_asm.Reset();
		}
	}
	else if (op == 4)    // RESULT do envio
	{
		uint8_t code = 0;
		if (!bs.Read(code)) return;
		if (code == 0)
		{
			if (!g_candidate.empty()) { g_decode = g_candidate; g_decodePending = true; g_clear = false; }
			g_candidate.clear();
			Say("Avatar atualizado");
		}
		else
		{
			g_candidate.clear();
			switch (code)
			{
				case 1: Say("Foto grande demais"); break;
				case 2: Say("Arquivo de foto invalido"); break;
				case 3: Say("Aguarde alguns segundos para trocar de novo"); break;
				case 4: Say("Faca login para trocar o avatar"); break;
				default: Say("Nao foi possivel salvar o avatar"); break;
			}
		}
	}
}

// ---- JNI: foto escolhida (128x128 JPEG) ----
void Avatar::OnPicked(const uint8_t* jpg, int n)
{
	std::lock_guard<std::mutex> l(g_m);
	if (!jpg || n <= 0 || n > AvatarLogic::kMaxBytes || !AvatarLogic::LooksLikeJpeg(jpg, (size_t)n)) { Say("Foto invalida"); return; }
	g_candidate.assign(jpg, jpg + n);
	g_upload = g_candidate;
	g_uploadPending = true;
}

void Avatar::OnPickFailed()
{
	std::lock_guard<std::mutex> l(g_m);
	Say("Nao foi possivel abrir a foto");
}

void Avatar::RequestPick()
{
	if (!Connected()) return;
	if (pJavaWrapper) pJavaWrapper->PickAvatar();
}

// ---- thread de desenho ----
void Avatar::Tick()
{
	std::vector<uint8_t> up, dec;
	bool doUp = false, doDec = false, doClear = false, doReq = false;
	{
		std::lock_guard<std::mutex> l(g_m);
		if (g_uploadPending) { up.swap(g_upload); g_uploadPending = false; doUp = true; }
		if (g_decodePending) { dec.swap(g_decode); g_decodePending = false; doDec = true; }
		if (g_clear) { doClear = true; g_clear = false; }
		if (!g_infoKnown && g_reqTries < 6 && Connected())
		{
			const int64_t now = NowMs();
			if (g_lastReq == 0 || now - g_lastReq > 6000) { g_lastReq = now; g_reqTries++; doReq = true; }
		}
	}
	if (doUp && Connected()) SendUpload(up);
	else if (doUp) { std::lock_guard<std::mutex> l(g_m); g_candidate.clear(); Say("Sem conexao com o servidor"); }
	if (doReq) SendSimple(3);
	if (doClear && g_raster) { if (RwRasterDestroy) RwRasterDestroy(g_raster); g_raster = nullptr; }
	if (doDec)
	{
		if (RwRaster* nr = MakeRaster(dec.data(), (int)dec.size()))
		{
			if (g_raster && RwRasterDestroy) RwRasterDestroy(g_raster);
			g_raster = nr;
		}
	}
}

RwRaster* Avatar::Raster()
{
	if (g_raster) return g_raster;
	static RwRaster* s_def = nullptr; static bool s_defFailed = false;      // avatar padrao (logo Maximo RP) quando o jogador nao tem foto
	if (!s_def && !s_defFailed) { s_def = MakeRaster(kAvatarDefaultJpg, kAvatarDefaultJpgSize); if (!s_def) s_defFailed = true; }
	return s_def;
}
uint32_t  Avatar::AccountId() { return g_id.load(); }

bool Avatar::PopMessage(std::string& out)
{
	std::lock_guard<std::mutex> l(g_m);
	if (!g_msgPending) return false;
	out = g_msg; g_msgPending = false;
	return true;
}
