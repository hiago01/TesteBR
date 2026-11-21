#include "../../main.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../java/jniutil.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "avatar_jpeg.h"
#include "avatar_default.h"
#include "avatar_borders.h"
#include "../imguirenderer.h"
#include "../../vendor/stb/stb_image.h"   // PNG das bordas (a implementacao do jogo e so PNG; o JPEG do avatar vai por avatar_jpeg.cpp)
#include <cmath>
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
	std::atomic<int>      g_border{0};
	int64_t               g_lastBorderSend = 0;
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
	RwRaster* MakeRasterRGBA(const uint8_t* rgbaPx, int w, int h);
	RwRaster* MakeRaster(const uint8_t* jpg, int jn)
	{
		std::vector<uint8_t> rgba; int w = 0, h = 0;
		if (!AvatarJpeg::Decode(jpg, jn, rgba, w, h)) { FLog("[AVATAR] JPEG invalido"); return nullptr; }
		return MakeRasterRGBA(rgba.data(), w, h);
	}

	// PNG (bordas) -> RwRaster; PNG usa o stb do jogo
	RwRaster* MakeRasterPng(const unsigned char* png, int n)
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(png, n, &w, &h, &ch, 4);
		if (!px) { FLog("[AVATAR] PNG da borda invalido"); return nullptr; }
		RwRaster* r = MakeRasterRGBA(px, w, h);
		stbi_image_free(px);
		return r;
	}

	RwRaster* MakeRasterRGBA(const uint8_t* rgbaPx, int w, int h)
	{
		const std::vector<uint8_t> rgba(rgbaPx, rgbaPx + (size_t)w * h * 4);
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
	g_id.store(0); g_border.store(0); g_infoKnown = false; g_asm.Reset();
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
		uint32_t id = 0; uint8_t has = 0, bd = 0;
		if (!bs.Read(id) || !bs.Read(has)) return;
		if (bs.Read(bd) && bd < kBorderCount) g_border.store(bd);        // borda escolhida (campo opcional, servidor novo)
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

// ---------------------------------------------------------------------------------------------
//  Bordas
// ---------------------------------------------------------------------------------------------
int Avatar::Border() { const int b = g_border.load(); return (b >= 0 && b < kBorderCount) ? b : 0; }

void Avatar::SetBorder(int idx)
{
	if (idx < 0 || idx >= kBorderCount) return;
	g_border.store(idx);                                         // aplica ja; o servidor confirma no INFO
	if (!Connected()) return;
	const int64_t now = NowMs();
	if (now - g_lastBorderSend < 250) return;
	g_lastBorderSend = now;
	RakNet::BitStream bs; bs.Write((uint8_t)5); bs.Write((uint8_t)idx); SendBs(bs);
}

const char* Avatar::BorderName(int idx)
{
	static const char* n[kBorderCount] = { "Natural", "Guirlanda", "Noel", "Neon", "Arco-iris" };
	return (idx >= 0 && idx < kBorderCount) ? n[idx] : "";
}

namespace
{
	void Ring(ImGuiRenderer* r, float cx, float cy, float rad, float th, const ImColor& col, int N = 48)
	{
		for (int i = 0; i < N; i++)
		{
			const float a0 = 6.2831853f * (float)i / (float)N, a1 = 6.2831853f * (float)(i + 1) / (float)N;
			r->drawLine(ImVec2(cx + std::cos(a0) * rad, cy + std::sin(a0) * rad), ImVec2(cx + std::cos(a1) * rad, cy + std::sin(a1) * rad), col, th);
		}
	}
	void Disc(ImGuiRenderer* r, float cx, float cy, float rad, const ImColor& col)
	{
		constexpr int N = 20; ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = 6.2831853f * (float)i / (float)N; p[i] = ImVec2(cx + std::cos(a) * rad, cy + std::sin(a) * rad); }
		r->drawConvexPolyFilled(p, N, col);
	}
	RwRaster* BorderRaster(int idx)
	{
		static RwRaster* ras[2] = { nullptr, nullptr }; static bool failed[2] = { false, false };
		if (idx != 1 && idx != 2) return nullptr;
		const int k = idx - 1;
		if (!ras[k] && !failed[k])
		{
			ras[k] = (idx == 1) ? MakeRasterPng(kBorderWreathPng, kBorderWreathPngSize) : MakeRasterPng(kBorderSantaPng, kBorderSantaPngSize);
			if (!ras[k]) failed[k] = true;
		}
		return ras[k];
	}
	// raio interno do anel da imagem / metade da imagem (para o anel encostar na borda da foto)
	constexpr float kInnerWreath = 192.0f / 277.0f, kInnerSanta = 212.0f / 277.0f;
}

void Avatar::DrawBorder(ImGuiRenderer* r, int idx, float cx, float cy, float ar, float u, double t)
{
	if (!r) return;
	if (idx == 1 || idx == 2)
	{
		RwRaster* ras = BorderRaster(idx);
		if (!ras) { Ring(r, cx, cy, ar + 1.2f * u, 2.6f * u, ImColor(150, 150, 160, 255)); return; }   // textura indisponivel: cai no natural
		const float H = ar * 1.02f / (idx == 1 ? kInnerWreath : kInnerSanta);
		r->drawImage(ImVec2(cx - H, cy - H), ImVec2(cx + H, cy + H), (ImTextureID)ras, ImColor(255, 255, 255, 255), ImVec2(0, 0), ImVec2(1, 1));
		if (idx == 1)    // pisca-pisca: luzes coloridas alternando sobre a guirlanda
		{
			static const int col[4][3] = { { 255, 205, 70 }, { 255, 80, 80 }, { 100, 255, 130 }, { 100, 180, 255 } };
			constexpr int N = 16;
			const float rl = H * 0.83f;
			const int phase = (int)(t * 2.4);
			for (int i = 0; i < N; i++)
			{
				const float a = 6.2831853f * ((float)i + 0.5f) / (float)N;
				const float lx = cx + std::cos(a) * rl, ly = cy + std::sin(a) * rl;
				const bool on = ((phase + i) & 1) == 0;
				const int* c = col[i & 3];
				Disc(r, lx, ly, 4.6f * u, ImColor(c[0], c[1], c[2], on ? 70 : 0));
				Disc(r, lx, ly, 2.8f * u, ImColor(c[0], c[1], c[2], on ? 170 : 35));
				Disc(r, lx, ly, 1.4f * u, ImColor(255, 255, 255, on ? 255 : 70));
			}
		}
		return;
	}
	if (idx == 3)        // neon azul pulsando
	{
		const float p = 0.55f + 0.45f * (float)std::sin(t * 2.6);
		for (int k = 3; k >= 0; k--) Ring(r, cx, cy, ar + (1.6f + k * 1.5f) * u, (2.4f + k * 1.2f) * u, ImColor(40, 150, 255, (int)((70 - k * 14) * p)));
		Ring(r, cx, cy, ar + 1.6f * u, 2.0f * u, ImColor(140, 215, 255, (int)(160 + 95 * p)));
		return;
	}
	if (idx == 4)        // arco-iris girando
	{
		constexpr int N = 60;
		const float rad = ar + 2.0f * u;
		for (int i = 0; i < N; i++)
		{
			const float a0 = 6.2831853f * (float)i / (float)N, a1 = 6.2831853f * (float)(i + 1) / (float)N;
			float h = (float)i / (float)N + (float)(t * 0.25); h -= std::floor(h);
			r->drawLine(ImVec2(cx + std::cos(a0) * rad, cy + std::sin(a0) * rad), ImVec2(cx + std::cos(a1) * rad, cy + std::sin(a1) * rad), ImColor::HSV(h, 0.85f, 1.0f), 3.4f * u);
		}
		return;
	}
	// 0: natural cinza
	Ring(r, cx, cy, ar + 1.3f * u, 2.8f * u, ImColor(135, 135, 146, 255));
	Ring(r, cx, cy, ar + 0.5f * u, 1.0f * u, ImColor(205, 205, 214, 150));
}
