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
#include <sys/stat.h>
#include <thread>
#include <vector>

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
			m_blip = pGame->CreateRadarMarkerIcon(0, x, y, z, 1006, 0);   // blip dourado (1006 = 0xE2C063, igual ao GPS da interface; 1005 era vermelho escuro e parecia um ponto preto): aparece no minimapa, preso na borda quando longe. (sprite 41 era o cassino, nao destino)
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

		// ---- ruas (NODES0..63.DAT) e estilo das linhas ----------------------------------------------------------------
		// O grafo so e carregado quando o mapa PRECISA dele (GPS ligado ou camada de ruas): em segundo plano, uma vez. Enquanto
		// carrega (ou se falhar) roads() devolve nullptr e o mapa usa a rota do servidor / linha reta, como antes.
		Roads::Graph m_graph;
		std::atomic<int> m_roadState{ 0 };           // 0 nao pedido, 1 carregando, 2 pronto, 3 falhou
		std::vector<std::string> m_roadLog;          // escrito pela thread ANTES de publicar o estado
		bool m_roadLogged = false;
		Lines::Styles m_styles = Lines::Defaults();
		bool m_stylesLoaded = false;
		std::atomic<bool> m_stylesReload{ false };
		std::atomic<int> m_layerOverride{ -1 };      // /mapa ruas on|off (-1 = valor do linhas.ini)

		static std::string Root() { return g_pszStorage ? std::string(g_pszStorage) : std::string(); }

		void LoadStyles()
		{
			m_styles = Lines::Defaults();
			const std::string root = Root();
			if (root.empty()) return;
			const std::string path = root + "SAMP/radar/linhas.ini";
			FILE* f = fopen(path.c_str(), "rb");
			if (f)
			{
				std::string txt; char buf[1024]; size_t n;
				while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && txt.size() < 65536) txt.append(buf, n);
				fclose(f);
				std::vector<std::string> warn;
				Lines::ParseIni(txt, m_styles, &warn);
				FLog("[LINHAS] linhas.ini lido (%d avisos)", (int)warn.size());
				for (size_t i = 0; i < warn.size() && i < 6; i++) FLog("[LINHAS] aviso: %s", warn[i].c_str());
			}
			else
			{
				// primeira vez: grava um modelo com todas as categorias (as sem implementacao tambem) para configuracao posterior
				mkdir((root + "SAMP").c_str(), 0755); mkdir((root + "SAMP/radar").c_str(), 0755);
				if (FILE* o = fopen(path.c_str(), "wb"))
				{
					const std::string t = Lines::TemplateText(m_styles);
					fwrite(t.data(), 1, t.size(), o); fclose(o);
					FLog("[LINHAS] linhas.ini nao existia: modelo gravado em SAMP/radar/linhas.ini");
				}
			}
		}
		const Lines::Styles& lineStyles() override
		{
			if (!m_stylesLoaded || m_stylesReload.exchange(false)) { m_stylesLoaded = true; LoadStyles(); }
			const int ov = m_layerOverride.load();
			if (ov >= 0) m_styles.roadLayer = ov != 0;
			return m_styles;
		}
		const Roads::Graph* roads() override
		{
			const int s = m_roadState.load(std::memory_order_acquire);
			if (s == 0)
			{
				int expected = 0;
				if (m_roadState.compare_exchange_strong(expected, 1))
				{
					const std::string root = Root();
					std::thread([this, root]() {
						Roads::Graph tmp; std::vector<std::string> log;
						const bool ok = !root.empty() && Roads::LoadAll(root, tmp, log);
						m_roadLog = std::move(log);
						if (ok) m_graph = std::move(tmp);
						m_roadState.store(ok ? 2 : 3, std::memory_order_release);
					}).detach();
				}
				return nullptr;
			}
			if (s >= 2 && !m_roadLogged)
			{
				m_roadLogged = true;
				for (const std::string& l : m_roadLog) FLog("%s", l.c_str());      // FLog nao e thread-safe: so aqui, na thread de desenho
				FLog(s == 2 ? "[NODES] grafo de ruas pronto (GPS pelas ruas ligado)" : "[NODES] grafo de ruas INDISPONIVEL: GPS segue a rota do servidor ou a linha reta");
			}
			return s == 2 ? &m_graph : nullptr;
		}
		const char* roadStateName() const
		{
			switch (m_roadState.load()) { case 0: return "nao pedido"; case 1: return "carregando"; case 2: return "pronto"; default: return "falhou"; }
		}
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
	char b[200];
	if (args == "linhas")                                   // recarrega SAMP/radar/linhas.ini na proxima passada de desenho
	{
		g_plat.m_stylesReload.store(true);
		return "[LINHAS] linhas.ini sera relido (veja avisos no samp_log.txt)";
	}
	if (args.rfind("ruas", 0) == 0)                         // /mapa ruas on|off  (camada com todas as ruas)
	{
		const bool on = args.find("off") == std::string::npos && (args.find("on") != std::string::npos || !g_plat.lineStyles().roadLayer);
		g_plat.m_layerOverride.store(on ? 1 : 0);
		if (on) g_plat.roads();                             // pede o carregamento do grafo
		std::snprintf(b, sizeof(b), "[MAPA] camada de ruas %s (grafo: %s). Aberta so com o mapa aberto (/mapa)", on ? "LIGADA" : "desligada", g_plat.roadStateName());
		return b;
	}
	if (args.rfind("minimapa", 0) == 0)                     // /mapa minimapa on|off  (linha da rota no minimapa)
	{
		const bool on = args.find("off") == std::string::npos && (args.find("on") != std::string::npos || !MiniRoute::gEnabled.load());
		MiniRoute::gEnabled.store(on);
		return on ? "[MAPA] rota no minimapa LIGADA" : "[MAPA] rota no minimapa desligada";
	}
	if (args == "nodes")
	{
		const char* st = g_plat.roadStateName();
		if (g_plat.m_roadState.load() == 3) g_plat.m_roadState.store(0);     // permite tentar de novo
		if (g_plat.m_roadState.load() == 2)
		{
			const Roads::Stats& s = g_plat.m_graph.stats();
			std::snprintf(b, sizeof(b), "[NODES] pronto: %u nos, %u ligacoes, areas %d/64, %u KB", s.nodes, s.directedEdges, s.areasLoaded, (unsigned)(g_plat.m_graph.MemoryBytes() / 1024));
		}
		else std::snprintf(b, sizeof(b), "[NODES] grafo: %s (se falhou, abra o mapa de novo para tentar; detalhes no samp_log.txt)", st);
		return b;
	}
	std::snprintf(b, sizeof(b), "[MAPA] servidor=%d aberto=%d locais=%d tiles=%d fonte=%d ruas=%s", (int)g_serverSupports.load(), (int)g_ui.IsOpen(), (int)g_ui.locations().size(), g_plat.m_loaded, g_plat.m_src, g_plat.roadStateName());
	return b;
}
