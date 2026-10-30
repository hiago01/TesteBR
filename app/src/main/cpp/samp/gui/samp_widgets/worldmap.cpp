#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/playerpool.h"
#include "../../util/CUtil.h"
#include "../../game/RadarSkin.h"
#include "../../vendor/stb/stb_image.h"   // implementacao em game/sprite2d.cpp
#include "worldmap.h"
#include "WorldMapUI.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

extern UI*       pUI;
extern CNetGame* pNetGame;
extern CGame*    pGame;

namespace
{
	using namespace WorldMap;

	int64_t NowMsReal()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- imagem unica opcional (SAMP/radar/mapa.png, quadrada, mundo todo) ----
	RwRaster* MakeRaster(const unsigned char* px, int w, int h)
	{
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) return nullptr;
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image)) { if (image) RwImageDestroy(image); return nullptr; }
		for (int y = 0; y < h; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888; rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;
		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image)) { if (RwRasterDestroy) RwRasterDestroy(raster); RwImageDestroy(image); return nullptr; }
		RwImageDestroy(image);
		return raster;
	}

	// So manda os comandos ocultos /mapax depois que o servidor mandou algum pacote do RPC 204.
	// Servidor sem o maximo_mapa.inc respondia "SERVER: Unknown command." a cada acao.
	std::atomic<bool> g_serverSupports{ false };

	class GamePlatform : public IPlatform
	{
	public:
		RwRaster* m_full = nullptr; bool m_fullTried = false;
		RwTexture* m_tex[kTiles * kTiles] = {};
		bool m_texFailed[kTiles * kTiles] = {};
		int  m_loaded = 0, m_failed = 0;
		int  m_src = -1;                 // -1 ainda nao descobriu, -2 nenhuma, 0 = GetTexture global, 1.. = banco da lista
		bool m_probeLogged = false;
		uint32_t m_blip = 0;

		static const char* const* DbList(int& n)
		{
			static const char* const k[] = { "gta3", "gta_int", "samp", "menu", "txd", "radar" };
			n = (int)(sizeof(k) / sizeof(k[0]));
			return k;
		}
		RwTexture* TryLoad(int src, const char* name)
		{
			if (src == 0) return (RwTexture*)CUtil::GetTexture(name);
			int n; const char* const* dbs = DbList(n);
			if (src - 1 >= n) return nullptr;
			return (RwTexture*)CUtil::LoadTextureFromDB(dbs[src - 1], name);
		}
		void Probe()
		{
			int n; DbList(n);
			for (int s = 0; s <= n; s++)
			{
				RwTexture* t = TryLoad(s, "radar00");
				if (t && t->raster)
				{
					m_src = s; m_tex[0] = t; m_loaded++;
					FLog("[MAPA] tiles do radar encontrados (fonte %d: %s)", s, s == 0 ? "global" : DbList(n)[s - 1]);
					return;
				}
			}
			m_src = -2;
			FLog("[MAPA] tiles radarNN NAO encontrados em nenhum banco; use SAMP/radar/mapa.png ou me envie a lista de texturas");
		}

		ImVec2 screen() override { return ImGui::GetIO().DisplaySize; }
		bool player(float& x, float& y, float& hd) override
		{
			if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
			CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
			if (!lp) return false;
			CPlayerPed* pp = lp->GetPlayerPed();
			if (!pp || !pp->m_pPed) return false;
			CVector p = pp->m_pPed->GetPosition();
			x = p.x; y = p.y;
			hd = pp->m_pPed->m_fCurrentRotation * 57.2957795f;
			return true;
		}
		ImTextureID tile(int idx) override
		{
			if (idx < 0 || idx >= kTiles * kTiles) return 0;
			if (void* skin = RadarSkin::UiTileRaster(idx)) return (ImTextureID)skin;   // tiles PNG (Google Maps), se ligados
			if (m_src == -2) return 0;
			if (m_tex[idx]) return (ImTextureID)m_tex[idx]->raster;
			if (m_texFailed[idx]) return 0;
			if (m_src == -1) { Probe(); if (m_src == -2) return 0; if (m_tex[idx]) return (ImTextureID)m_tex[idx]->raster; }
			static int perFrame = 0; static int lastFrame = -1;
			const int fr = ImGui::GetFrameCount();
			if (fr != lastFrame) { lastFrame = fr; perFrame = 0; }
			if (perFrame++ >= 4) return 0;     // carrega aos poucos
			char nm[16]; std::snprintf(nm, sizeof(nm), "radar%02d", idx);
			RwTexture* t = TryLoad(m_src, nm);
			if (t && t->raster) { m_tex[idx] = t; m_loaded++; return (ImTextureID)t->raster; }
			m_texFailed[idx] = true; m_failed++;
			if (m_failed == 1) FLog("[MAPA] tile %s nao carregou", nm);
			return 0;
		}
		ImTextureID fullMap() override
		{
			if (m_fullTried) return (ImTextureID)m_full;
			m_fullTried = true;
			if (!g_pszStorage) return 0;
			std::string path = std::string(g_pszStorage) + "SAMP/radar/mapa.png";
			int w = 0, h = 0, ch = 0;
			unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
			if (!px) return 0;                 // sem arquivo = usa os tiles
			m_full = MakeRaster(px, w, h);
			stbi_image_free(px);
			FLog("[MAPA] mapa.png %dx%d %s", w, h, m_full ? "ok" : "falhou");
			return (ImTextureID)m_full;
		}
		void setGps(float x, float y, float z) override
		{
			clearGps();
			if (!pGame) return;
			m_blip = pGame->CreateRadarMarkerIcon(0, x, y, z, 1005, 0);   // blip colorido (mesmo do checkpoint do SA-MP): aparece no minimapa, preso na borda quando longe. (sprite 41 era o cassino, nao destino)
		}
		void clearGps() override
		{
			if (m_blip && pGame) pGame->DisableMarker(m_blip);
			m_blip = 0;
		}
		void sendCommand(const char* cmd) override { if (pNetGame && g_serverSupports.load()) pNetGame->SendChatCommand(cmd); }
		int64_t nowMs() override { return NowMsReal(); }
		int tilesLoaded() override { return m_loaded; }
		int mapDim() override { return RadarSkin::Enabled() ? 0 : 70; }
	};

	GamePlatform g_plat;
	Ui g_ui(&g_plat);
}

WorldMapWidget::WorldMapWidget() {}
bool WorldMapWidget::IsOpen() { return g_ui.IsOpen(); }
void WorldMapWidget::Open() { g_ui.Open(); }
void WorldMapWidget::Close(bool notify) { g_ui.Close(notify); }
void WorldMapWidget::Toggle() { if (g_ui.IsOpen()) g_ui.Close(true); else g_ui.Open(); }
void WorldMapWidget::Reset() { g_serverSupports.store(false); g_ui.Reset(); g_plat.clearGps(); }
bool WorldMapWidget::HandleRawTouch(int type, int num, int x, int y)
{
	extern volatile bool g_bTouchCancel;
	return g_ui.HandleRawTouch(type, num, x, y, g_bTouchCancel);
}
bool WorldMapWidget::ConsumeFlushRequest() { return g_ui.ConsumeFlush(); }
bool WorldMapWidget::OnBackPressed() { return g_ui.OnBack(); }
void WorldMapWidget::draw(ImGuiRenderer* r) { g_ui.Draw(r); }

void WorldMapWidget::OnServerPacket(const unsigned char* data, int bytes)
{
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	WorldMap::Packet p;
	if (!WorldMap::ParsePacket(bs, p)) return;
	g_serverSupports.store(true);
	g_ui.Apply(p);
}

std::string WorldMapWidget::DebugCommand(const std::string& args)
{
	if (args == "grade") { g_ui.debugGrid = !g_ui.debugGrid; return g_ui.debugGrid ? "[MAPA] grade ligada" : "[MAPA] grade desligada"; }
	char b[160];
	std::snprintf(b, sizeof(b), "[MAPA] servidor=%d aberto=%d locais=%d tiles=%d fonte=%d", (int)g_serverSupports.load(), (int)g_ui.IsOpen(), (int)g_ui.locations().size(), g_plat.m_loaded, g_plat.m_src);
	return b;
}
