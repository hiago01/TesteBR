#include "IdCard3D.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "World.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"
#include "../vendor/stb/stb_image.h"   // implementacao (STB_IMAGE_IMPLEMENTATION) esta em game/sprite2d.cpp
#include "idcard_assets.h"

// stb_truetype privado deste arquivo (STBTT_STATIC: nao colide com a copia do imgui_draw.cpp)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/imgui/imstb_truetype.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

extern CGame*    pGame;
extern CNetGame* pNetGame;
extern uintptr_t g_libGTASA;

// =====================================================================================
//  Configuracao (ajustavel por /cartao)
// =====================================================================================

namespace
{
	struct Config
	{
		float maxDist    = 15.0f;   // distancia maxima da camera (m)
		float headOffset = 0.35f;   // base da placa acima do osso da cabeca (m) - FIXO, nao escala com distancia
		float width      = 0.95f;   // largura do cartao no mundo (m)
		float riseDist   = 0.90f;   // quanto ele sobe durante a animacao (m)
		float riseTime   = 0.85f;   // duracao da subida (s)
		float fadeTime   = 0.30f;   // fade-in (s)
		bool  los        = true;    // esconde atras de predios/objetos (mesma checagem do CPlayerTags)
		bool  showSelf   = false;   // mostra o cartao do jogador local
	};
	Config g_cfg;

	constexpr int   kBone      = 8;       // osso da cabeca (igual a CPlayerTags::Render)
	constexpr float kGap       = 0.02f;   // espaco banner <-> placa (m)
	constexpr int   kPlateW    = 512;
	constexpr int   kPlateH    = 120;

	using Clock = std::chrono::steady_clock;

	struct Entry
	{
		bool     used = false;
		bool     test = false;          // criado por /cartao teste (aparece mesmo no jogador local)
		uint8_t  cat  = 0;
		char     cargo[32] = {};
		char     func[24]  = {};

		char     nameShown[32] = {};    // texto com que a placa foi gerada
		int      idShown = -1;
		RwRaster* plate = nullptr;
		float    plateU1 = 1.0f, plateV1 = 1.0f;
		bool     plateDirty = true;

		bool     started = false;       // animacao de entrada ja comecou (so 1x)
		Clock::time_point t0;
	};

	std::mutex             g_mtx;
	Entry                  g_e[MAX_PLAYERS];
	std::vector<RwRaster*> g_dead;       // rasters a destruir na thread de render

	bool      g_cardsTried = false;

	// ---- fonte ----
	std::vector<unsigned char> g_fontData;
	stbtt_fontinfo             g_font;
	bool                       g_fontOk = false, g_fontTried = false;

	// =================================================================================
	//  Raster a partir de pixels RGBA (mesmo caminho do atlas do HUD)
	// =================================================================================
	RwRaster* CreateRasterRGBA(const unsigned char* px, int w, int h)
	{
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat ||
			!RwRasterCreate || !RwRasterSetFromImage)
		{
			FLog("[IDCARD] funcoes RenderWare indisponiveis");
			return nullptr;
		}

		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image))
		{
			if (image) RwImageDestroy(image);
			return nullptr;
		}
		for (int y = 0; y < h; ++y)
			memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);

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

	// O raster do RenderWare/GLES precisa de dimensoes potencia de 2 e RwRasterSetFromImage COPIA sem escalar:
	// uma imagem 512x198 virava raster 512x128 e perdia o rodape. Aqui preenchemos ate a proxima potencia
	// de 2 (transparente) e guardamos a fracao usada para ajustar o UV.
	struct Tex { RwRaster* r = nullptr; float u1 = 1.0f, v1 = 1.0f; };

	int NextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

	Tex CreateTexPOT(const unsigned char* px, int w, int h)
	{
		Tex t;
		const int pw = NextPow2(w), ph = NextPow2(h);
		std::vector<unsigned char> pad((size_t)pw * ph * 4, 0);
		for (int y = 0; y < ph; y++)
			for (int x = 0; x < pw; x++)
			{
				const int sx = std::min(x, w - 1), sy = std::min(y, h - 1);   // borda replicada (alpha 0 fora): sem franja
				const unsigned char* sp = px + ((size_t)sy * w + sx) * 4;
				unsigned char* dp = &pad[((size_t)y * pw + x) * 4];
				dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
				dp[3] = (x < w && y < h) ? sp[3] : 0;
			}
		t.r  = CreateRasterRGBA(pad.data(), pw, ph);
		t.u1 = ((float)w - 0.5f) / (float)pw;     // meio texel de folga contra sangramento do filtro linear
		t.v1 = ((float)h - 0.5f) / (float)ph;
		return t;
	}

	Tex g_card[IdCardAssets::kCount];

	void EnsureCardRasters()
	{
		if (g_cardsTried) return;
		g_cardsTried = true;

		for (int i = 0; i < IdCardAssets::kCount; i++)
		{
			const IdCardAssets::Card& c = IdCardAssets::kCards[i];
			int w = 0, h = 0, ch = 0;
			unsigned char* px = stbi_load_from_memory(c.png, (int)c.size, &w, &h, &ch, 4);
			if (!px) { FLog("[IDCARD] stbi falhou (%s): %s", c.name, stbi_failure_reason()); continue; }
			g_card[i] = CreateTexPOT(px, w, h);
			stbi_image_free(px);
			FLog("[IDCARD] textura %s %dx%d (pot %dx%d) %s", c.name, w, h, NextPow2(w), NextPow2(h), g_card[i].r ? "ok" : "FALHOU");
		}
	}

	void FreeGraveyard()
	{
		for (RwRaster* r : g_dead)
			if (r && RwRasterDestroy) RwRasterDestroy(r);
		g_dead.clear();
	}

	void KillEntry(Entry& e)
	{
		if (e.plate) g_dead.push_back(e.plate);   // destruido em IdCard3D_Render (thread de render)
		e = Entry();
	}

	// =================================================================================
	//  Placa de texto (stb_truetype -> RGBA -> raster)
	// =================================================================================
	bool LoadFont()
	{
		if (g_fontTried) return g_fontOk;
		g_fontTried = true;

		std::string path = std::string(g_pszStorage ? g_pszStorage : "") + "fonts/" + FONT_NAME;
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) { FLog("[IDCARD] fonte nao encontrada: %s (placa de texto desligada)", path.c_str()); return false; }
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (sz <= 0) { fclose(f); return false; }
		g_fontData.resize((size_t)sz);
		size_t rd = fread(g_fontData.data(), 1, (size_t)sz, f);
		fclose(f);
		if (rd != (size_t)sz) return false;

		if (!stbtt_InitFont(&g_font, g_fontData.data(), stbtt_GetFontOffsetForIndex(g_fontData.data(), 0)))
		{
			FLog("[IDCARD] stbtt_InitFont falhou");
			return false;
		}
		g_fontOk = true;
		return true;
	}

	int NextCodepoint(const char*& s)
	{
		const unsigned char* p = (const unsigned char*)s;
		int cp;
		if (p[0] < 0x80)                       { cp = p[0]; s += 1; }
		else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); s += 2; }
		else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
		                                       { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); s += 3; }
		else if (p[0] >= 0xA0)                 { cp = p[0]; s += 1; }   // byte solto >=0xA0: Latin-1 (servidor .pwn salvo em ANSI/CP1252: ç ã é...)
		else                                   { cp = '?'; s += 1; }
		return cp;
	}

	float TextWidth(const char* s, float scale)
	{
		float w = 0;
		while (*s)
		{
			int cp = NextCodepoint(s), adv = 0, lsb = 0;
			stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
			w += adv * scale;
		}
		return w;
	}

	void DrawText(std::vector<uint8_t>& px, int W, int H, float x, int baseline,
				  const char* s, float scale, uint8_t r, uint8_t g, uint8_t b)
	{
		while (*s)
		{
			int cp = NextCodepoint(s), adv = 0, lsb = 0;
			stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);

			int x0, y0, x1, y1;
			stbtt_GetCodepointBitmapBox(&g_font, cp, scale, scale, &x0, &y0, &x1, &y1);
			int gw = x1 - x0, gh = y1 - y0;
			if (gw > 0 && gh > 0)
			{
				std::vector<unsigned char> bmp((size_t)gw * gh);
				stbtt_MakeCodepointBitmap(&g_font, bmp.data(), gw, gh, gw, scale, scale, cp);
				for (int yy = 0; yy < gh; yy++)
				{
					int py = baseline + y0 + yy;
					if (py < 0 || py >= H) continue;
					for (int xx = 0; xx < gw; xx++)
					{
						int pxx = (int)x + x0 + xx;
						if (pxx < 0 || pxx >= W) continue;
						float a = bmp[(size_t)yy * gw + xx] / 255.0f;
						if (a <= 0) continue;
						uint8_t* d = &px[((size_t)py * W + pxx) * 4];
						float da = d[3] / 255.0f;
						float oa = a + da * (1.0f - a);
						d[0] = (uint8_t)((r * a + d[0] * da * (1.0f - a)) / oa);
						d[1] = (uint8_t)((g * a + d[1] * da * (1.0f - a)) / oa);
						d[2] = (uint8_t)((b * a + d[2] * da * (1.0f - a)) / oa);
						d[3] = (uint8_t)(oa * 255.0f);
					}
				}
			}
			x += adv * scale;
		}
	}

	bool InRoundRect(float px, float py, float x0, float y0, float w, float h, float r)
	{
		float cx = std::clamp(px, x0 + r, x0 + w - r);
		float cy = std::clamp(py, y0 + r, y0 + h - r);
		float dx = px - cx, dy = py - cy;
		return dx * dx + dy * dy <= r * r;
	}

	// Gera a placa:  [ Nome (ID) ]  /  [ Cargo | N. funcional ]  com borda na cor da categoria.
	Tex BuildPlate(const Entry& e, const char* name, int id)
	{
		if (!LoadFont()) return Tex();

		const IdCardAssets::Card& c = IdCardAssets::kCards[e.cat < IdCardAssets::kCount ? e.cat : 0];
		std::vector<uint8_t> px((size_t)kPlateW * kPlateH * 4, 0);

		for (int y = 0; y < kPlateH; y++)
			for (int x = 0; x < kPlateW; x++)
			{
				uint8_t* d = &px[((size_t)y * kPlateW + x) * 4];
				float fx = x + 0.5f, fy = y + 0.5f;
				if (InRoundRect(fx, fy, 4, 4, kPlateW - 8, kPlateH - 8, 20)) { d[0] = 10; d[1] = 10; d[2] = 14; d[3] = 225; }
				else if (InRoundRect(fx, fy, 0, 0, kPlateW, kPlateH, 24))    { d[0] = c.accentR; d[1] = c.accentG; d[2] = c.accentB; d[3] = 255; }
			}

		char l1[96], l2[96];
		snprintf(l1, sizeof(l1), "%s (%d)", name, id);
		if (e.func[0]) snprintf(l2, sizeof(l2), "%s  |  %s", e.cargo, e.func);
		else           snprintf(l2, sizeof(l2), "%s", e.cargo);

		const float maxW = kPlateW - 44.0f;

		float s1 = stbtt_ScaleForPixelHeight(&g_font, 40.0f);
		float w1 = TextWidth(l1, s1);
		if (w1 > maxW) { s1 *= maxW / w1; w1 = maxW; }
		DrawText(px, kPlateW, kPlateH, (kPlateW - w1) * 0.5f, 50, l1, s1, 255, 255, 255);

		float s2 = stbtt_ScaleForPixelHeight(&g_font, 30.0f);
		float w2 = TextWidth(l2, s2);
		if (w2 > maxW) { s2 *= maxW / w2; w2 = maxW; }
		DrawText(px, kPlateW, kPlateH, (kPlateW - w2) * 0.5f, 94, l2, s2, c.accentR, c.accentG, c.accentB);

		return CreateTexPOT(px.data(), kPlateW, kPlateH);
	}

	// =================================================================================
	//  Geometria / desenho (RwIm3D)
	// =================================================================================
	struct V3 { float x, y, z; };
	inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
	inline V3 Norm(V3 a, V3 fb) { float l = Len(a); return l > 1e-5f ? a * (1.0f / l) : fb; }

	void SetStates()
	{
		CHook::CallFunction<void>(g_libGTASA + 0x6F4EC4);   // DefinedState(), igual ao Hose.cpp / CRope::Render

		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
		RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));    // some atras de paredes/carros
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(FALSE));   // translucido: nao escreve Z
		RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
		RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERLINEAR));
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS,    RWRSTATE(rwTEXTUREADDRESSCLAMP));
	}

	// Quad com cantos BL, BR, TR, TL.
	void DrawQuad(RwRaster* raster, V3 bl, V3 br, V3 tr, V3 tl, V3 normal, uint8_t alpha, float u1, float v1)
	{
		if (!raster || alpha == 0) return;

		RxObjSpace3DVertex v[4];
		const V3 p[4] = { bl, br, tr, tl };
		const float u[4] = { 0, u1, u1, 0 }, t[4] = { v1, v1, 0, 0 };   // so a parte usada da textura POT
		const uint32_t col = ((uint32_t)alpha << 24) | 0x00FFFFFFu;
		for (int i = 0; i < 4; i++)
		{
			v[i].objVertex = { p[i].x, p[i].y, p[i].z };
			v[i].objNormal = { normal.x, normal.y, normal.z };
			v[i].color = col;
			v[i].u = u[i];
			v[i].v = t[i];
		}
		static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };   // indice Im3D de 16 bits (ver nota no Hose.cpp)

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(raster));
		if (RwIm3DTransform(v, 4, nullptr, rwIM3D_VERTEXUV))
		{
			RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST,
										 reinterpret_cast<RwImVertexIndex*>(const_cast<uint16_t*>(idx)), 6);
			RwIm3DEnd();
		}
	}

	struct Item { int id; V3 head; float dist; };

	const char* const kCatKey[IdCardCat::Count]  = { "civil", "policial", "delegado", "medico", "administrador", "militar", "cnh", "funcional" };
	const char* const kCatName[IdCardCat::Count] = { "Civil", "Policial", "Delegado", "Médico", "Administrador", "Militar do Exército", "Habilitação", "Identidade Funcional" };
}

// =====================================================================================
//  API (rede)
// =====================================================================================

void IdCard3D_OnServerPacket(uint16_t playerId, bool visible, uint8_t category,
							 const char* cargo, const char* numFuncional)
{
	if (playerId >= MAX_PLAYERS) return;
	std::lock_guard<std::mutex> lk(g_mtx);

	Entry& e = g_e[playerId];
	if (!visible || category >= IdCardCat::Count) { KillEntry(e); return; }

	e.used = true;
	e.cat  = category;
	snprintf(e.cargo, sizeof(e.cargo), "%s", cargo ? cargo : "");
	snprintf(e.func,  sizeof(e.func),  "%s", numFuncional ? numFuncional : "");
	e.plateDirty = true;          // NAO reinicia `started`: a animacao de entrada e so na 1a vez
}

void IdCard3D_ClearPlayer(uint16_t playerId)
{
	if (playerId >= MAX_PLAYERS) return;
	std::lock_guard<std::mutex> lk(g_mtx);
	KillEntry(g_e[playerId]);
}

void IdCard3D_Reset()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	for (Entry& e : g_e) KillEntry(e);
}

// =====================================================================================
//  Render (1x por frame, dentro da cena 3D)
// =====================================================================================

void IdCard3D_Render()
{
	std::lock_guard<std::mutex> lk(g_mtx);

	FreeGraveyard();
	if (!pNetGame || !pGame) return;
	CPlayerPool* pool = pNetGame->GetPlayerPool();
	if (!pool) return;

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector camPosC = TheCamera.GetPosition();
	const V3 camPos = { camPosC.x, camPosC.y, camPosC.z };
	const PLAYERID localId = pool->GetLocalPlayerID();

	static Item items[MAX_PLAYERS];
	int n = 0;

	for (int id = 0; id < MAX_PLAYERS; id++)
	{
		Entry& e = g_e[id];
		if (!e.used) continue;

		CPlayerPed* ped = nullptr;
		if (id == localId)
		{
			if (!(g_cfg.showSelf || e.test)) continue;
			CLocalPlayer* lp = pool->GetLocalPlayer();
			ped = lp ? lp->GetPlayerPed() : nullptr;
		}
		else
		{
			if (!pool->GetSlotState((PLAYERID)id)) continue;
			CRemotePlayer* rp = pool->GetAt((PLAYERID)id);
			if (!rp || !rp->IsActive() || rp->IsNPC()) continue;
			ped = rp->GetPlayerPed();
		}
		if (!ped || !ped->m_pPed || !ped->m_pPed->IsAdded()) continue;

		const float dist = ped->m_pPed->GetDistanceFromCamera();
		if (dist > g_cfg.maxDist) continue;

		CVector head(0.0f, 0.0f, 0.0f);
		ped->GetBonePosition(kBone, &head);      // posicao desta frame: sem atraso

		if (g_cfg.los &&
			!CWorld::GetIsLineOfSightClear(head, camPosC, true, false, false, true, false, false, false))
			continue;

		items[n++] = { id, { head.x, head.y, head.z }, dist };
	}
	if (n == 0) return;

	std::sort(items, items + n, [](const Item& a, const Item& b) { return a.dist > b.dist; });   // translucido: de tras pra frente

	EnsureCardRasters();
	SetStates();

	const auto now = Clock::now();

	for (int i = 0; i < n; i++)
	{
		const Item& it = items[i];
		Entry& e = g_e[it.id];
		if (e.cat >= IdCardAssets::kCount) continue;

		// ---- animacao: sobe e PARA (so na primeira vez que o cartao aparece) ----
		if (!e.started) { e.started = true; e.t0 = now; }
		const float el = std::chrono::duration<float>(now - e.t0).count();
		float tt = std::clamp(el / g_cfg.riseTime, 0.0f, 1.0f);
		const float ease = 1.0f - (1.0f - tt) * (1.0f - tt) * (1.0f - tt);   // ease-out cubico
		const float zAnim = -(1.0f - ease) * g_cfg.riseDist;                 // 0 quando termina
		const float fade = std::clamp(el / g_cfg.fadeTime, 0.0f, 1.0f);

		// ---- placa (texto) ----
		const char* name = (it.id == localId) ? pool->GetLocalPlayerName() : pool->GetPlayerName((PLAYERID)it.id);
		if (!name) name = "";
		if (e.plateDirty || e.idShown != it.id || strncmp(e.nameShown, name, sizeof(e.nameShown) - 1) != 0)
		{
			if (e.plate) g_dead.push_back(e.plate);
			const Tex pt = BuildPlate(e, name, it.id);
			e.plate = pt.r; e.plateU1 = pt.u1; e.plateV1 = pt.v1;
			snprintf(e.nameShown, sizeof(e.nameShown), "%s", name);
			e.idShown = it.id;
			e.plateDirty = false;
		}

		// ---- billboard voltado para a camera (eixo vertical = Z do mundo) ----
		const V3 base = { it.head.x, it.head.y, it.head.z + g_cfg.headOffset + zAnim };
		const V3 f = Norm(base - camPos, { 0, 1, 0 });                  // da camera para o cartao
		const V3 R = Norm(Cross(f, { 0, 0, 1 }), { 1, 0, 0 });          // direita na tela (mundo destro, Z para cima)
		const V3 U = Cross(R, f);                                       // cima na tela
		const V3 nrm = f * -1.0f;

		const IdCardAssets::Card& c = IdCardAssets::kCards[e.cat];
		const float w      = g_cfg.width;
		const float cardH  = w * (float)c.h / (float)c.w;               // mantem a proporcao da arte
		const float plateH = w * (float)kPlateH / (float)kPlateW;
		const uint8_t a    = (uint8_t)(255.0f * fade);

		// placa: base fixa em `base`; banner logo acima dela
		if (e.plate)
		{
			const V3 bl = base - R * (w * 0.5f);
			DrawQuad(e.plate, bl, bl + R * w, bl + R * w + U * plateH, bl + U * plateH, nrm, a, e.plateU1, e.plateV1);
		}
		{
			const V3 bl = base + U * ((e.plate ? plateH + kGap : 0.0f)) - R * (w * 0.5f);
			DrawQuad(g_card[e.cat].r, bl, bl + R * w, bl + R * w + U * cardH, bl + U * cardH, nrm, a, g_card[e.cat].u1, g_card[e.cat].v1);
		}
	}
}

// =====================================================================================
//  Comando do chat: /cartao
// =====================================================================================

std::string IdCard3D_Command(const std::string& args)
{
	std::vector<std::string> a;
	{
		size_t i = 0;
		while (i < args.size())
		{
			while (i < args.size() && args[i] == ' ') i++;
			size_t j = i;
			while (j < args.size() && args[j] != ' ') j++;
			if (j > i) a.push_back(args.substr(i, j - i));
			i = j;
		}
	}

	auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
	char buf[300];

	if (a.empty())
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		int ativos = 0;
		for (const Entry& e : g_e) if (e.used) ativos++;
		snprintf(buf, sizeof(buf),
				 "[CARTAO] ativos: %d | dist=%.1f altura=%.2f largura=%.2f los=%s proprio=%s | "
				 "uso: teste [categoria|0-5] [id] | anim | limpar | dist <m> | altura <m> | largura <m> | los on|off | proprio on|off | "
				 "categorias: civil policial delegado medico administrador militar cnh funcional",
				 ativos, g_cfg.maxDist, g_cfg.headOffset, g_cfg.width,
				 g_cfg.los ? "on" : "off", g_cfg.showSelf ? "on" : "off");
		return buf;
	}

	const std::string cmd = lower(a[0]);

	if (cmd == "teste")
	{
		int cat = IdCardCat::Civil;
		if (a.size() >= 2)
		{
			const std::string k = lower(a[1]);
			cat = -1;
			for (int i = 0; i < IdCardCat::Count; i++) if (k == kCatKey[i]) cat = i;
			if (cat < 0 && k.size() == 1 && k[0] >= '0' && k[0] < '0' + IdCardCat::Count) cat = k[0] - '0';
			if (cat < 0) return "[CARTAO] categoria invalida (civil policial delegado medico administrador militar cnh funcional ou 0-7)";
		}
		if (!pNetGame || !pNetGame->GetPlayerPool()) return "[CARTAO] sem conexao com o servidor";
		CPlayerPool* pool = pNetGame->GetPlayerPool();

		int target = pool->GetLocalPlayerID();
		bool self = true;
		if (a.size() >= 3)
		{
			target = atoi(a[2].c_str());
			self = (target == (int)pool->GetLocalPlayerID());
			if (target < 0 || target >= MAX_PLAYERS || (!self && !pool->GetSlotState((PLAYERID)target)))
				return "[CARTAO] id de jogador invalido/ausente";
		}

		std::lock_guard<std::mutex> lk(g_mtx);
		Entry& e = g_e[target];
		KillEntry(e);
		e.used = true;
		e.test = true;
		e.cat  = (uint8_t)cat;
		snprintf(e.cargo, sizeof(e.cargo), "%s", kCatName[cat]);
		snprintf(e.func,  sizeof(e.func),  "MX-%04d", 400 + cat * 11 + (target % 10));
		snprintf(buf, sizeof(buf), "[CARTAO] teste: %s no jogador %d (animacao de subida na proxima frame). /cartao limpar remove.",
				 kCatName[cat], target);
		return buf;
	}

	if (cmd == "anim")
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		int n = 0;
		for (Entry& e : g_e) if (e.used) { e.started = false; n++; }
		snprintf(buf, sizeof(buf), "[CARTAO] animacao de subida reiniciada em %d cartao(oes)", n);
		return buf;
	}

	if (cmd == "limpar")
	{
		IdCard3D_Reset();
		return "[CARTAO] todos os cartoes removidos";
	}

	auto num = [&](float& dst, float lo, float hi, const char* nome) -> std::string
	{
		if (a.size() < 2) return std::string("[CARTAO] uso: ") + nome + " <valor>";
		float v = (float)atof(a[1].c_str());
		if (v < lo || v > hi) { snprintf(buf, sizeof(buf), "[CARTAO] %s fora do intervalo (%.2f..%.2f)", nome, lo, hi); return buf; }
		std::lock_guard<std::mutex> lk(g_mtx);
		dst = v;
		snprintf(buf, sizeof(buf), "[CARTAO] %s = %.2f", nome, v);
		return buf;
	};

	if (cmd == "dist")    return num(g_cfg.maxDist,    2.0f, 100.0f, "dist");
	if (cmd == "altura")  return num(g_cfg.headOffset, 0.0f, 3.0f,   "altura");
	if (cmd == "largura") return num(g_cfg.width,      0.3f, 3.0f,   "largura");

	if (cmd == "los" || cmd == "proprio")
	{
		if (a.size() < 2) return "[CARTAO] uso: " + cmd + " on|off";
		const bool on = lower(a[1]) == "on";
		std::lock_guard<std::mutex> lk(g_mtx);
		(cmd == "los" ? g_cfg.los : g_cfg.showSelf) = on;
		return "[CARTAO] " + cmd + (on ? " = on" : " = off");
	}

	return "[CARTAO] comando desconhecido. Use /cartao para ver o uso.";
}
