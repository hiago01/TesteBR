#pragma once
// =====================================================================================
//  Mapa aberto (Maximo RP) - INTERFACE (desenho + toque + estado). Header-only para poder rodar no PC
//  com o ImGui real e uma "plataforma" falsa (testes/host_test_worldmap_render.cpp).
//  No jogo, worldmap.cpp implementa IPlatform (tiles do radar nativo, blip de GPS, comando oculto, posicao).
//
//  Estilo: painel escuro, mapa a esquerda, lista de locais a direita (nome alinhado a direita + icone),
//  botao FECHAR. Toque: arrastar = mover; pinca = zoom; toque no marcador/lista = GPS; toque no mapa = GPS livre.
// =====================================================================================
#include "../imguirenderer.h"
#include "WorldMapCore.h"
#include "MapLines.h"
#include "RoadGraph.h"
#include "MiniRoute.h"
#include "MiniCity.h"
#include "ZoneNames.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace WorldMap
{
	class IPlatform
	{
	public:
		virtual ~IPlatform() {}
		virtual ImVec2 screen() = 0;
		virtual bool   player(float& x, float& y, float& headingDeg) = 0;   // false = sem jogador
		virtual ImTextureID uiIcon(int id) { (void)id; return 0; }              // icones do painel: 0 mover (mao), 1 ver (olho), 2 ocultar (olho cortado); 0 = desenha com formas
		virtual ImTextureID tile(int idx) = 0;     // textura do tile radar%02d (0 = indisponivel ainda)
		virtual ImTextureID fullMap() = 0;         // imagem unica opcional do mundo todo (0 = nenhuma)
		virtual void   setGps(float x, float y, float z) = 0;   // blip no radar do jogo
		virtual void   clearGps() = 0;
		virtual void   sendCommand(const char* cmd) = 0;        // comando oculto /mapax ...
		virtual int64_t nowMs() = 0;
		virtual int    tilesLoaded() { return 0; }
		virtual int    mapDim() { return 70; }   // escurecimento (0-255) por cima do mapa; 0 para tiles claros (Google Maps)
		// ---- linhas / GPS pelas ruas (opcional: a plataforma padrao nao tem grafo e tudo segue como antes) ----
		virtual const Roads::Graph* roads() { return nullptr; }   // grafo de ruas (NODES*.DAT); nullptr = ainda carregando/indisponivel
		virtual const Lines::Styles& lineStyles() { static Lines::Styles s = Lines::Defaults(); return s; }
		// ---- marcacoes 3D do jogador (servidor org.pwn: /marcar; o cliente guarda a lista em waypoint3d.cpp) ----
		virtual int  pinCount() { return 0; }
		virtual bool pinAt(int i, float& x, float& y, std::string& label) { (void)i; (void)x; (void)y; (void)label; return false; }
		// marcacao 3D do DESTINO (exclusiva: so existe uma; um novo toque substitui a anterior; nao e salva). z = -1000: o cliente acha o chao.
		virtual void setDestMarker(float x, float y, float z, const std::string& name) { (void)x; (void)y; (void)z; (void)name; }
		virtual void clearDestMarker() {}
		// ---- sedes de TODOS os grupos (servidor org.pwn op 22; GPS da /orgcfg, senao o 1o /orgspawn): categoria "Grupos" da lista ----
		virtual int  hqCount() { return 0; }
		// cats = categorias separadas por ";" (ex.: "Governamental;Saude", vazio = sem categoria); area = local de atuacao ("Los Santos", "Las Venturas"... ou "Global")
		virtual bool hqAt(int i, float& x, float& y, float& z, std::string& name, std::string& cats, std::string& area) { (void)i; (void)x; (void)y; (void)z; (void)name; (void)cats; (void)area; return false; }
		// ---- cidade nova (fora do mapa de San Andreas): imagem propria + locais do cliente (SAMP/radar/cidade/) ----
		virtual const City* city() { return nullptr; }                          // nullptr / !ok = cidade nao instalada (tudo segue como antes)
		virtual ImTextureID cityOverview() { return 0; }                         // imagem geral (0 = ainda nao carregou)
		virtual ImTextureID cityTile(int col, int row) { (void)col; (void)row; return 0; }   // tile de detalhe (0 = nao existe / ainda carregando)
		virtual void cityGrid(float& x0, float& y1, float& tileM, int& cols, int& rows) { x0 = 0; y1 = 0; tileM = 0; cols = 0; rows = 0; }   // canto noroeste da grade de tiles
		virtual void cityImageRect(float& x0, float& y0, float& x1, float& y1) { x0 = y0 = x1 = y1 = 0; }                                    // area coberta pela imagem geral
		virtual int  cityPlaceCount() { return 0; }
		virtual bool cityPlaceAt(int i, int& icon, float& x, float& y, float& z, std::string& name) { (void)i; (void)icon; (void)x; (void)y; (void)z; (void)name; return false; }
	};

	class Ui
	{
	public:
		explicit Ui(IPlatform* p) : P(p) {}

		// ---------- chamadas de QUALQUER thread ----------
		void Apply(const Packet& p)
		{
			if (p.op == 2) { if (p.open) Open(); else Close(false); return; }
			std::lock_guard<std::mutex> l(m_mx);
			if (m_pending.size() < 64) m_pending.push_back(p);
		}
		void Open()
		{
			if (m_open.load()) return;
			{ std::lock_guard<std::mutex> l(m_mx); m_events.clear(); }
			for (auto& c : m_claimed) c.store(false);
			m_flush.store(true);
			m_needInit.store(true);
			m_sendOpen.store(true);
			m_open.store(true);
		}
		void Close(bool notify = true)
		{
			if (!m_open.exchange(false)) return;
			{ std::lock_guard<std::mutex> l(m_mx); m_events.clear(); }
			if (notify) m_sendClose.store(true);
		}
		bool IsOpen() const { return m_open.load(); }
		bool ConsumeFlush() { return m_flush.exchange(false); }
		bool OnBack()
		{
			const int64_t now = P->nowMs();
			if (now - m_lastBack < 400) return true;     // onKeyDown + onBackPressed no mesmo toque
			if (!m_open.load()) return false;
			m_lastBack = now;
			Close(true);
			return true;
		}
		// type: 2 = push, 3 = move, 1 = pop (mesmo contrato do inventario)
		bool HandleRawTouch(int type, int num, int x, int y, bool cancel)
		{
			const int n = num & 3;
			const bool open = m_open.load();
			if (!open && !m_claimed[n].load()) return false;
			if (open)
			{
				m_claimed[n].store(true);
				std::lock_guard<std::mutex> l(m_mx);
				if (m_events.size() < 256) m_events.push_back({ type, n, (float)x, (float)y, cancel });
			}
			if (type == 1) m_claimed[n].store(false);
			return true;
		}
		void Reset()
		{
			m_open.store(false);
			m_sendOpen.store(false);
			m_sendClose.store(false);
			{ std::lock_guard<std::mutex> l(m_mx); m_events.clear(); m_pending.clear(); }
			m_resetReq.store(true);
		}

		// ---------- thread de desenho ----------
		void Draw(ImGuiRenderer* r);

		// ---------- acesso para testes (somente thread de desenho) ----------
		const std::vector<Location>& locations() const { return m_locs; }
		bool gpsOn() const { return m_gps.on; }
		Vec2 gpsPos() const { return { m_gps.x, m_gps.y }; }
		int  gpsLoc() const { return m_gps.loc; }
		const View& view() const { return m_view; }
		size_t routeSize() const { return m_route.size(); }
		size_t localRouteSize() const { return m_localRoute.size(); }
		bool   roadsCut() const { return m_roadsCut; }
		const std::string& localWhy() const { return m_localWhy; }
		bool debugGrid = false;      // /mapa grade: mostra o indice de cada tile (conferir a orientacao)

	private:
		struct TouchEv { int type; int num; float x; float y; bool cancel; };
		struct Gps { bool on = false; float x = 0, y = 0, z = 0; int loc = -1; };
		struct Finger
		{
			bool down = false; int region = 0;   // 1 mapa, 2 lista, 3 botao
			int btn = 0;
			float sx = 0, sy = 0, x = 0, y = 0, scroll0 = 0;
			int64_t t0 = 0;
			bool moved = false;
		};
		struct Layout
		{
			float k = 1, pad = 10;
			ImVec2 scr, panA, panB, mapA, mapB, listA, listB, cntA, cntB, filtA, filtB;
			float rowsY = 0;                  // inicio das linhas da lista (abaixo dos filtros, se houver)
			bool  hasFilt = false;
			float rowH = 30, barY = 0, barH = 34;
			ImVec2 closeA, closeB, clrA, clrB, zinA, zinB, zoutA, zoutB, ctrA, ctrB;
			ImVec2 eyeA, eyeB, moveA, moveB;  // botoes do menu (lista): ver/ocultar e mover (ficam mesmo com o menu oculto)
		};
		enum { BTN_NONE = 0, BTN_CLOSE, BTN_CLEAR, BTN_ZIN, BTN_ZOUT, BTN_CENTER, BTN_LIST_EYE, BTN_LIST_MOVE };

		IPlatform* P;
		float   m_vis = 0.0f;         // animacao "TV": 0 = desligada, 1 = ligada (sobe ao abrir, desce ao fechar)
		int64_t m_visT = 0;           // instante do ultimo quadro (para o dt)
		static constexpr float kTvOnSec = 0.42f, kTvOffSec = 0.30f;
		std::atomic<bool> m_open{ false }, m_flush{ false }, m_sendOpen{ false }, m_sendClose{ false }, m_needInit{ false }, m_resetReq{ false };
		std::atomic<bool> m_claimed[4] = { { false }, { false }, { false }, { false } };
		std::mutex m_mx;
		std::vector<TouchEv> m_events;
		std::vector<Packet>  m_pending;
		int64_t m_lastBack = 0;

		// estado (so a thread de desenho)
		std::vector<Location> m_locs;          // o que a lista/mapa mostram AGORA (filtrado pela zona: mundo ou cidade)
		std::vector<Location> m_locsSrv;       // o que o servidor mandou (RPC 204 op 1), sem filtro
		bool m_listHidden = false;             // menu (lista) oculto: so os botoes ver/mover ficam na tela
		mutable float m_listDx = 0, m_listDy = 0;   // deslocamento do menu em relacao ao canto padrao (limitado ao mapa em MakeLayout)
		bool m_zoneCity = false;               // true = jogador DENTRO da cidade nova: imagem, lista e GPS so da cidade
		City m_city;
		static constexpr int kCityIdBase = 60000;   // ids dos locais do cliente (locais.txt): o servidor nao os conhece
		// lista lateral em CATEGORIAS recolhiveis: cada linha e um cabecalho (kind 0, idx = categoria), um local do servidor (1), a sede de um grupo (2) ou uma marcacao salva (3)
		struct Row { int kind; int idx; int cat; };
		std::vector<Row> m_rows;
		bool m_catOpen[6] = { true, true, false, false, false, false };   // [1] (antigo "Grupos") nao e mais usado: cada categoria de GRUPO vira um cabecalho proprio (m_gcatNames)
		// grupos: categorias (varias por grupo) e local de atuacao; filtros por local e por categoria (toque nas pilulas do topo da lista)
		std::string m_fArea, m_fCat;                   // "" = todos
		std::vector<std::string> m_gcatNames;          // categorias de grupo mostradas (um cabecalho cada), na ordem da lista
		std::vector<int> m_gcatCount;                  // quantos grupos em cada uma
		std::vector<std::string> m_gclosed;            // cabecalhos de categoria de grupo FECHADOS (padrao: abertos)
		int m_hqShown = 0;                             // grupos que passam nos filtros
		static constexpr int kCats = 6;
		std::vector<Vec2> m_route;           // rota que o SERVIDOR mandou (RPC 204 op 4); tem prioridade
		std::vector<Vec2> m_routeDraw;       // m_route com cantos arredondados (cache para desenho)
		uint64_t m_routeDrawKey = 0;
		// GPS local pelas ruas (A* no grafo de NODES*.DAT) quando o servidor nao manda rota
		Roads::Search m_search;
		std::vector<Vec2> m_localRoute;
		bool  m_localValid = false;          // ja tentou para o destino m_localToX/Y
		float m_localToX = 0, m_localToY = 0, m_localFromX = 0, m_localFromY = 0;
		int64_t m_localTryAt = 0;
		float m_localLen = 0;
		bool  m_localUndirected = false;
		std::string m_localWhy;
		struct Seg { ImVec2 a, b; uint8_t c; };
		std::vector<Seg> m_segs;             // camada de ruas (reaproveitado a cada quadro)
		bool  m_roadsCut = false;
		Gps m_gps;
		View m_view;
		float m_listScroll = 0;
		int   m_selLoc = -1;                 // indice em m_locs
		Finger m_f[4];
		float m_prevPinchDist = 0, m_prevMidX = 0, m_prevMidY = 0;
		bool  m_pinching = false;
		std::string m_toast; int64_t m_toastUntil = 0;
		Layout m_L;

		static bool In(float x, float y, const ImVec2& a, const ImVec2& b) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; }

		Layout MakeLayout() const;
		void Toast(const std::string& s) { m_toast = s; m_toastUntil = P->nowMs() + 2600; }
		bool ZoneOk(float x, float y) const { return m_zoneCity ? InsideCity(m_city, x, y) : InsideWorld(x, y); }   // o ponto pertence ao mapa mostrado agora?
		void RebuildLocs(int keepId);
		void UpdateZone(bool havePlayer, float plx, float ply, bool forceInit);
		void DrawCityImage(ImGuiRenderer* r, const ImVec2& origin, int& drawn);
		void ResetDrawState();
		void DrainPending();
		void ProcessEvents();
		void OnTap(float x, float y);
		void SelectLocation(int idx, bool fromList);
		void BuildRows();
		static std::vector<std::string> SplitCats(const std::string& c);
		bool HqPass(const std::string& cats, const std::string& area) const;
		void FilterOptions(bool area, std::vector<std::string>& out) const;
		void CycleFilter(bool area);
		bool RowPos(const Row& rw, float& x, float& y, std::string& name) const;
		void SelectRow(const Row& rw);
		int  NearestHq(float rx, float ry, float radiusPx, float& wx, float& wy, std::string& name) const;
		static const char* CatName(int c) { static const char* n[kCats] = { "Meus locais", "Grupos", "Saude e servicos", "Trabalho", "Lojas", "Outros" }; return n[c]; }
		static int CatOfIcon(int icon) { return (icon == 2 || icon == 3 || icon == 9) ? 2 : (icon == 4 ? 3 : (icon == 7 ? 4 : 5)); }
		void SetFreeGps(float wx, float wy, bool create = true, const std::string& label = std::string());
		int  NearestPin(float rx, float ry, float radiusPx, float& wx, float& wy, std::string& label) const;
		void ClearGps(bool notify);
		void TickGps();
		void ClearLocalRoute() { m_localRoute.clear(); m_localValid = false; m_localWhy.clear(); }
		void UpdateLocalRoute(bool havePlayer, float plx, float ply);
		const std::vector<Vec2>& EffectiveRoute();
		void DrawRoads(ImGuiRenderer* r, const ImVec2& origin, float k);
		void DrawExitLink(ImGuiRenderer* r, const ImVec2& origin, float k);   // ponta da ligacao San Fierro <-> Cidade Nova (so o trecho de x >= -3000)
		void PublishMiniRoute();                       // minimapa: publica a rota (mundo) para o hook do radar
		void DrawMiniRoute(ImGuiRenderer* r);          // minimapa: desenha a rota ja convertida para pixels
		void DrawMiniLink(ImGuiRenderer* r);           // minimapa: rodovia de saida (continuacao SA <-> cidade nova)
		void DrawMiniCity(ImGuiRenderer* r);           // minimapa: imagem da cidade nova por cima do radar (so dentro da cidade)
		void DrawRouteLine(ImGuiRenderer* r, const std::vector<Vec2>& rt, const ImVec2& origin, float k, bool havePlayer, float plx, float ply);
		void Cmd(const char* fmt, int a = 0, int b = 0)
		{
			if (m_zoneCity) return;      // cidade nova: GPS so no cliente (o servidor nao conhece estes locais nem as coordenadas)
			char buf[64];
			std::snprintf(buf, sizeof(buf), fmt, a, b);
			P->sendCommand(buf);
		}
		int ButtonAt(float x, float y) const;
		void DrawIcon(ImGuiRenderer* r, int icon, const ImVec2& c, float s, const ImColor& col) const;
		void DrawPin(ImGuiRenderer* r, const ImVec2& tip, float s, const ImColor& fill, const ImColor& edge) const;
		void DrawButton(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const char* label, const ImColor& bg, float fs, bool hot) const;
	};

	// =================================================================================================
	//  implementacao
	// =================================================================================================
	namespace ui_colors
	{
		const ImColor dim(0, 0, 0, 105);            // escurece o jogo por tras (o mundo continua aparecendo)
		const ImColor panel(20, 22, 28, 232);
		const ImColor mapBg(16, 18, 22, 0);         // sem fundo: a agua azul do mapa e quase transparente e deixa o jogo (escurecido) aparecer
		const ImColor border(255, 255, 255, 70);
		const ImColor glass(8, 10, 14, 150);        // vidro escuro da lista e dos botoes
		const ImColor hqCol(176, 120, 255, 255);    // sedes de grupos (roxo)
		const ImColor pinCol(64, 164, 255, 255);    // marcacoes salvas (azul)
		const ImColor text(236, 238, 244, 255);
		const ImColor mute(150, 156, 170, 255);
		const ImColor blue(30, 110, 232, 235);
		const ImColor blueD(24, 84, 180, 255);
		const ImColor btn(14, 16, 22, 175);
		const ImColor red(235, 70, 70, 255);
		const ImColor gps(255, 205, 40, 255);
		const ImColor route(60, 160, 255, 255);
		const ImColor routeOut(10, 30, 70, 230);
	}

	inline Ui::Layout Ui::MakeLayout() const
	{
		Layout L;
		L.scr = P->screen();
		L.k = std::max(0.5f, L.scr.y / 540.0f);
		const float k = L.k;
		L.pad = 10.0f * k;
		// mapa em tela quase cheia, sem painel preto: so uma margem fina e um contorno claro
		L.panA = ImVec2(L.scr.x * 0.012f, L.scr.y * 0.025f);
		L.panB = ImVec2(L.scr.x * 0.988f, L.scr.y * 0.975f);
		L.mapA = L.panA; L.mapB = L.panB;
		const float listW = Clampf(L.scr.x * 0.22f, 180.0f * k, 280.0f * k);
		L.barH = 34.0f * k;
		L.barY = L.panB.y - L.pad - L.barH;
		L.rowH = 28.0f * k;
		// lista flutuante sobre o mapa (canto direito): so cresce ate o que cabe, com o contador embaixo
		const float cntH = 20.0f * k;
		const float maxB = L.barY - cntH - 10.0f * k;
		L.listA = ImVec2(L.mapB.x - L.pad - listW, L.mapA.y + L.pad);
		L.hasFilt = P->hqCount() > 0;                                      // ha grupos: pilulas de filtro (local / categoria) no topo da lista
		L.rowsY = L.listA.y + (L.hasFilt ? L.rowH + 6.0f * k : 0.0f);
		const float want = L.rowsY + std::max(1.0f, (float)m_rows.size()) * L.rowH + 8.0f * k;
		L.listB = ImVec2(L.mapB.x - L.pad, std::min(maxB, want));
		L.filtA = ImVec2(L.listA.x + 6.0f * k, L.listA.y + 5.0f * k); L.filtB = ImVec2(L.listB.x - 6.0f * k, L.listA.y + 5.0f * k + L.rowH - 4.0f * k);
		L.cntA = ImVec2(L.listB.x - 70.0f * k, L.listB.y + 5.0f * k); L.cntB = ImVec2(L.listB.x, L.cntA.y + cntH - 4.0f * k);
		// botoes do menu: coluna a ESQUERDA da lista (olho = ver/ocultar, mao = mover). Menu oculto: so a coluna, encostada no canto, e ainda da para mover.
		{
			const float bs = 32.0f * k, gp = 6.0f * k;
			float bx0, bx1, by0, by1;
			if (!m_listHidden) { bx0 = L.listA.x - gp - bs; bx1 = L.listB.x; by0 = L.listA.y; by1 = L.cntB.y; }
			else { bx0 = L.listB.x - bs; bx1 = L.listB.x; by0 = L.listA.y; by1 = L.listA.y + 2.0f * bs + gp; }
			const float loX = L.mapA.x + L.pad - bx0, hiX = std::max(loX, L.mapB.x - L.pad - bx1);
			const float loY = L.mapA.y + L.pad - by0, hiY = std::max(loY, L.mapB.y - L.pad - by1);
			const float dx = Clampf(m_listDx, loX, hiX), dy = Clampf(m_listDy, loY, hiY);
			m_listDx = dx; m_listDy = dy;
			auto sh = [&](ImVec2& p) { p.x += dx; p.y += dy; };
			sh(L.listA); sh(L.listB); sh(L.filtA); sh(L.filtB); sh(L.cntA); sh(L.cntB); L.rowsY += dy;
			const float hx = bx0 + dx, hy = by0 + dy;
			L.eyeA = ImVec2(hx, hy);                  L.eyeB = ImVec2(hx + bs, hy + bs);
			L.moveA = ImVec2(hx, hy + bs + gp);       L.moveB = ImVec2(hx + bs, hy + 2.0f * bs + gp);
			if (m_listHidden) { L.listB = L.listA; L.hasFilt = false; L.rowsY = L.listA.y; }      // sem area: nada da lista recebe toque
		}
		// botoes em pilula, canto inferior esquerdo: FECHAR | - | + | O | LIMPAR GPS
		const float bx = L.mapA.x + L.pad, zs = L.barH, g = 6.0f * k;
		L.closeA = ImVec2(bx, L.barY);                     L.closeB = ImVec2(bx + 104.0f * k, L.barY + L.barH);
		L.zoutA = ImVec2(L.closeB.x + g, L.barY);          L.zoutB = ImVec2(L.zoutA.x + zs, L.barY + L.barH);
		L.zinA = ImVec2(L.zoutB.x + g, L.barY);            L.zinB = ImVec2(L.zinA.x + zs, L.barY + L.barH);
		L.ctrA = ImVec2(L.zinB.x + g, L.barY);             L.ctrB = ImVec2(L.ctrA.x + zs, L.barY + L.barH);
		L.clrA = ImVec2(L.ctrB.x + g, L.barY);             L.clrB = ImVec2(L.clrA.x + 124.0f * k, L.barY + L.barH);
		return L;
	}

	inline int Ui::ButtonAt(float x, float y) const
	{
		const Layout& L = m_L;
		if (In(x, y, L.closeA, L.closeB)) return BTN_CLOSE;
		if (In(x, y, L.eyeA, L.eyeB)) return BTN_LIST_EYE;
		if (In(x, y, L.moveA, L.moveB)) return BTN_LIST_MOVE;
		if (m_gps.on && In(x, y, L.clrA, L.clrB)) return BTN_CLEAR;
		if (In(x, y, L.zinA, L.zinB)) return BTN_ZIN;
		if (In(x, y, L.zoutA, L.zoutB)) return BTN_ZOUT;
		if (In(x, y, L.ctrA, L.ctrB)) return BTN_CENTER;
		return BTN_NONE;
	}

	// ---------------------------------------------------------------------------------------------
	//  Cidade nova: zona (mundo x cidade), lista de locais e imagem
	// ---------------------------------------------------------------------------------------------
	// Locais da zona atual: os do servidor que caem dentro dela + (na cidade) os do cliente (locais.txt, ids >= 60000)
	inline void Ui::RebuildLocs(int keepId)
	{
		m_locs.clear();
		for (const Location& l : m_locsSrv) if (ZoneOk(l.x, l.y)) m_locs.push_back(l);
		if (m_zoneCity)
		{
			const int n = P->cityPlaceCount();
			for (int i = 0; i < n && (int)m_locs.size() < kMaxLocations + 64; i++)
			{
				Location l; int icon = 0;
				if (!P->cityPlaceAt(i, icon, l.x, l.y, l.z, l.name) || !ZoneOk(l.x, l.y)) continue;
				l.id = kCityIdBase + i; l.icon = icon;
				m_locs.push_back(std::move(l));
			}
		}
		m_selLoc = -1;
		for (size_t i = 0; i < m_locs.size(); i++) if (m_locs[i].id == keepId) m_selLoc = (int)i;
	}

	// Troca de zona (entrou/saiu da cidade): limites e imagem do mapa, lista e GPS. O GPS de outra zona e' desligado (nao faz sentido atravessar).
	inline void Ui::UpdateZone(bool havePlayer, float plx, float ply, bool forceInit)
	{
		const City* c = P->city();
		m_city = (c && c->ok) ? *c : City();
		const bool want = m_city.ok && havePlayer && InsideCity(m_city, plx, ply);
		if (want == m_zoneCity && !forceInit) return;
		if (want != m_zoneCity)
		{
			if (m_gps.on && !(want ? InsideCity(m_city, m_gps.x, m_gps.y) : InsideWorld(m_gps.x, m_gps.y))) ClearGps(true);   // com a zona ANTIGA: avisa o servidor
			m_zoneCity = want;
			m_fArea.clear(); m_fCat.clear();       // os filtros Local/Cat valem so para a zona anterior
			if (want) SetBounds(m_view, m_city.minX, m_city.minY, m_city.maxX, m_city.maxY, true);
			else ResetBounds(m_view);
			RebuildLocs(-1);
			if (want) for (int cc = 2; cc < kCats; cc++) m_catOpen[cc] = true;      // cidade: categorias ja abertas (poucos locais)
			m_listScroll = 0;
			m_needInit.store(true);        // recentra no jogador, com o zoom certo da zona
		}
	}

	// Imagem da cidade: geral sempre; tiles de detalhe so com zoom alto (os que ainda nao carregaram deixam ver a geral por baixo)
	inline void Ui::DrawCityImage(ImGuiRenderer* r, const ImVec2& origin, int& drawn)
	{
		auto S = [&](float wx, float wy) { const Vec2 s = ToScreen(m_view, wx, wy); return ImVec2(origin.x + s.x, origin.y + s.y); };
		float ix0, iy0, ix1, iy1; P->cityImageRect(ix0, iy0, ix1, iy1);
		const ImTextureID ov = P->cityOverview();
		if (ov && ix1 > ix0 && iy1 > iy0) { r->drawImage(S(ix0, iy1), S(ix1, iy0), ov); drawn++; }
		if (m_view.scale < 0.40f) return;
		float gx0, gy1, tileM; int cols, rows; P->cityGrid(gx0, gy1, tileM, cols, rows);
		if (tileM <= 0.0f || cols <= 0 || rows <= 0) return;
		const float halfW = m_view.w * 0.5f / m_view.scale, halfH = m_view.h * 0.5f / m_view.scale;
		const int c0 = std::max(0, (int)std::floor((m_view.cx - halfW - gx0) / tileM)), c1 = std::min(cols - 1, (int)std::floor((m_view.cx + halfW - gx0) / tileM));
		const int r0 = std::max(0, (int)std::floor((gy1 - (m_view.cy + halfH)) / tileM)), r1 = std::min(rows - 1, (int)std::floor((gy1 - (m_view.cy - halfH)) / tileM));
		for (int rr = r0; rr <= r1; rr++)
			for (int cc = c0; cc <= c1; cc++)
			{
				const ImTextureID t = P->cityTile(cc, rr);
				if (!t) continue;
				const float tx0 = gx0 + cc * tileM, ty1 = gy1 - rr * tileM;
				ImVec2 a = S(tx0, ty1), b = S(tx0 + tileM, ty1 - tileM);
				a.x -= 0.5f; a.y -= 0.5f; b.x += 0.5f; b.y += 0.5f;
				r->drawImage(a, b, t);
			}
	}

	inline void Ui::ResetDrawState()
	{
		for (auto& f : m_f) f = Finger();
		m_pinching = false;
	}

	inline void Ui::DrainPending()
	{
		std::vector<Packet> q;
		{ std::lock_guard<std::mutex> l(m_mx); q.swap(m_pending); }
		for (Packet& p : q)
		{
			if (p.op == 1)
			{
				const int keepId = (m_selLoc >= 0 && m_selLoc < (int)m_locs.size()) ? m_locs[m_selLoc].id : -1;
				m_locsSrv = std::move(p.locs);
				RebuildLocs(keepId);
				if (m_gps.on && m_gps.loc >= 0) { /* mantem o GPS mesmo se o local sumiu da lista */ }
			}
			else if (p.op == 3)
			{
				if (!p.gpsOn) { ClearGps(false); }
				else
				{
					m_gps.on = true; m_gps.x = p.gx; m_gps.y = p.gy; m_gps.z = p.gz; m_gps.loc = p.gpsLoc;
					m_route.clear(); ClearLocalRoute();
					P->setGps(p.gx, p.gy, p.gz);
					m_selLoc = -1;
					for (size_t i = 0; i < m_locs.size(); i++) if (m_locs[i].id == p.gpsLoc) m_selLoc = (int)i;
				}
			}
			else if (p.op == 4)
			{
				m_route = std::move(p.route);
			}
		}
	}

	inline void Ui::ClearGps(bool notify)
	{
		if (m_gps.on) P->clearGps();
		P->clearDestMarker();
		m_gps = Gps();
		m_route.clear(); ClearLocalRoute();
		m_selLoc = -1;
		if (notify) P->sendCommand("/mapax clear");
	}

	// primeiros n caracteres (UTF-8) do texto
	inline std::string ClipUtf8(const std::string& t, int n)
	{
		int c = 0; size_t i = 0;
		while (i < t.size() && c < n) { i++; while (i < t.size() && ((unsigned char)t[i] & 0xC0) == 0x80) i++; c++; }
		return t.substr(0, i);
	}
	// nome do lugar no ponto (zona do GTA, sem a cidade: "Chinatown"); vazio = sem nome util
	inline std::string ZoneLabel(float x, float y)
	{
		char b[96]; ZoneNames::Describe(x, y, 0.0f, b, sizeof b);
		std::string t = b;
		const size_t bar = t.find(" | ");
		if (bar != std::string::npos) t = t.substr(bar + 3);
		return t == "San Andreas" ? std::string() : t;
	}

	inline void Ui::SelectLocation(int idx, bool fromList)
	{
		if (idx < 0 || idx >= (int)m_locs.size()) return;
		const Location& l = m_locs[idx];
		m_selLoc = idx;
		m_gps.on = true; m_gps.x = l.x; m_gps.y = l.y; m_gps.z = l.z; m_gps.loc = l.id;
		m_route.clear(); ClearLocalRoute();            // rota antiga nao vale mais; o servidor manda a nova (ou o GPS local calcula)
		P->setGps(l.x, l.y, l.z);
		if (fromList) CenterOn(m_view, l.x, l.y);
		Cmd("/mapax sel %d", l.id);
		P->setDestMarker(l.x, l.y, l.z, ClipUtf8(l.name, 24));                // marcacao 3D (unica) com o nome do local
		Toast("GPS: " + l.name);
	}

	inline int Ui::NearestPin(float rx, float ry, float radiusPx, float& wx, float& wy, std::string& label) const
	{
		int best = -1; float bestD = radiusPx;
		const int n = P->pinCount();
		for (int i = 0; i < n; i++)
		{
			float x = 0, y = 0; std::string nm;
			if (!P->pinAt(i, x, y, nm) || !ZoneOk(x, y)) continue;
			const Vec2 s = ToScreen(m_view, x, y);
			const float d = Dist(s.x, s.y, rx, ry - 12.0f * m_L.k);      // o pino fica acima do ponto: mede pela cabeca
			if (d <= bestD) { bestD = d; best = i; wx = x; wy = y; label = nm; }
		}
		return best;
	}

	inline int Ui::NearestHq(float rx, float ry, float radiusPx, float& wx, float& wy, std::string& name) const
	{
		int best = -1; float bestD = radiusPx;
		const int n = P->hqCount();
		for (int i = 0; i < n; i++)
		{
			float x = 0, y = 0, z = 0; std::string nm, cats, ar;
			if (!P->hqAt(i, x, y, z, nm, cats, ar) || !ZoneOk(x, y) || !HqPass(cats, ar)) continue;
			const Vec2 s = ToScreen(m_view, x, y);
			const float d = Dist(s.x, s.y, rx, ry);
			if (d <= bestD) { bestD = d; best = i; wx = x; wy = y; name = nm; }
		}
		return best;
	}

	// "Governamental;Saude" -> {"Governamental","Saude"}
	inline std::vector<std::string> Ui::SplitCats(const std::string& c)
	{
		std::vector<std::string> v; std::string cur;
		for (size_t i = 0; i <= c.size(); i++)
		{
			if (i == c.size() || c[i] == ';')
			{
				size_t a = 0, b = cur.size();
				while (a < b && cur[a] == ' ') a++;
				while (b > a && cur[b - 1] == ' ') b--;
				if (b > a) v.push_back(cur.substr(a, b - a));
				cur.clear();
			}
			else cur += c[i];
		}
		return v;
	}

	// filtros: local = o escolhido OU "Global" (grupo que atua no estado todo aparece em qualquer local); categoria = o grupo tem a categoria ("Sem categoria" = nenhuma)
	inline bool Ui::HqPass(const std::string& cats, const std::string& area) const
	{
		if (!m_fArea.empty() && area != m_fArea && area != "Global") return false;
		if (!m_fCat.empty())
		{
			const std::vector<std::string> cs = SplitCats(cats);
			if (cs.empty()) return m_fCat == "Sem categoria";
			bool has = false;
			for (const std::string& c : cs) if (c == m_fCat) has = true;
			if (!has) return false;
		}
		return true;
	}

	// valores possiveis de cada filtro (de TODOS os grupos, sem aplicar os filtros): local (Global por ultimo) / categoria (Governamental primeiro, "Sem categoria" por ultimo)
	inline void Ui::FilterOptions(bool area, std::vector<std::string>& out) const
	{
		out.clear();
		bool noCat = false, global = false;
		const int n = P->hqCount();
		for (int i = 0; i < n; i++)
		{
			float x, y, z; std::string nm, cats, ar;
			if (!P->hqAt(i, x, y, z, nm, cats, ar) || !ZoneOk(x, y)) continue;      // so as opcoes de grupos DESTA zona (a Cidade Nova nao aparece fora dela, nem os grupos de fora dentro dela)
			if (area) { if (ar == "Global") global = true; else if (std::find(out.begin(), out.end(), ar) == out.end()) out.push_back(ar); }
			else
			{
				const std::vector<std::string> cs = SplitCats(cats);
				if (cs.empty()) noCat = true;
				for (const std::string& c : cs) if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
			}
		}
		std::sort(out.begin(), out.end(), [area](const std::string& a, const std::string& b) {
			if (!area) { if (a == "Governamental" && b != "Governamental") return true; if (b == "Governamental" && a != "Governamental") return false; }
			return a < b; });
		if (area && global) out.push_back("Global");
		if (!area && noCat) out.push_back("Sem categoria");
	}

	// toque numa pilula de filtro: Todos -> primeiro valor -> ... -> ultimo -> Todos
	inline void Ui::CycleFilter(bool area)
	{
		std::vector<std::string> opts; FilterOptions(area, opts);
		std::string& cur = area ? m_fArea : m_fCat;
		if (opts.empty()) { cur.clear(); return; }
		if (cur.empty()) cur = opts[0];
		else
		{
			size_t k = 0; while (k < opts.size() && opts[k] != cur) k++;
			cur = (k + 1 >= opts.size()) ? std::string() : opts[k + 1];       // valor que sumiu: k = size -> volta para Todos
		}
		BuildRows();
		m_listScroll = 0;
	}

	// monta a lista em categorias: Meus locais (marcacoes salvas); GRUPOS (um cabecalho por categoria do grupo; o grupo aparece em todas as dele; filtros de local/categoria);
	// depois os locais do servidor por tipo. Categoria vazia nao aparece.
	inline void Ui::BuildRows()
	{
		// filtro que nao existe mais (grupo foi apagado/renomeado) volta para "Todos"
		{
			std::vector<std::string> o;
			FilterOptions(true, o);  if (!m_fArea.empty() && std::find(o.begin(), o.end(), m_fArea) == o.end()) m_fArea.clear();
			FilterOptions(false, o); if (!m_fCat.empty() && std::find(o.begin(), o.end(), m_fCat) == o.end()) m_fCat.clear();
		}
		std::vector<Row> items[kCats];
		const int np = P->pinCount();
		for (int i = 0; i < np; i++) { float px = 0, py = 0; std::string pn; if (P->pinAt(i, px, py, pn) && ZoneOk(px, py)) items[0].push_back({ 3, i, 0 }); }
		for (size_t i = 0; i < m_locs.size(); i++) { const int c = CatOfIcon(m_locs[i].icon); items[c].push_back({ 1, (int)i, c }); }
		// grupos por categoria
		std::vector<std::string> names; std::vector<std::vector<int>> members;
		const int nh = P->hqCount();
		m_hqShown = 0;
		for (int i = 0; i < nh; i++)
		{
			float x, y, z; std::string nm, cats, ar;
			if (!P->hqAt(i, x, y, z, nm, cats, ar) || !ZoneOk(x, y) || !HqPass(cats, ar)) continue;
			m_hqShown++;
			std::vector<std::string> cs = SplitCats(cats);
			if (cs.empty()) cs.push_back("Sem categoria");
			for (const std::string& c : cs)
			{
				if (!m_fCat.empty() && c != m_fCat) continue;                // filtrando por categoria: so o cabecalho dela
				size_t k = 0; while (k < names.size() && names[k] != c) k++;
				if (k == names.size()) { names.push_back(c); members.emplace_back(); }
				members[k].push_back(i);
			}
		}
		std::vector<size_t> ord(names.size());
		for (size_t k = 0; k < ord.size(); k++) ord[k] = k;
		std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
			const std::string &x = names[a], &y = names[b];
			const int ra = x == "Governamental" ? 0 : (x == "Sem categoria" ? 2 : 1), rb = y == "Governamental" ? 0 : (y == "Sem categoria" ? 2 : 1);
			return ra != rb ? ra < rb : x < y; });
		m_rows.clear();
		m_gcatNames.clear(); m_gcatCount.clear();
		if (!items[0].empty()) { m_rows.push_back({ 0, 0, 0 }); if (m_catOpen[0]) m_rows.insert(m_rows.end(), items[0].begin(), items[0].end()); }
		for (size_t o = 0; o < ord.size(); o++)
		{
			const size_t k = ord[o];
			const int hid = (int)m_gcatNames.size();
			m_gcatNames.push_back(names[k]); m_gcatCount.push_back((int)members[k].size());
			m_rows.push_back({ 0, 100 + hid, 100 + hid });
			if (std::find(m_gclosed.begin(), m_gclosed.end(), names[k]) == m_gclosed.end())
				for (int hi : members[k]) m_rows.push_back({ 2, hi, 100 + hid });
		}
		if (nh > 0 && m_hqShown == 0) m_rows.push_back({ 4, 0, 100 });      // filtros sem nenhum grupo
		for (int c = 2; c < kCats; c++)
		{
			if (items[c].empty()) continue;
			m_rows.push_back({ 0, c, c });
			if (m_catOpen[c]) m_rows.insert(m_rows.end(), items[c].begin(), items[c].end());
		}
	}

	inline bool Ui::RowPos(const Row& rw, float& x, float& y, std::string& name) const
	{
		float z = 0; std::string c, a;
		switch (rw.kind)
		{
			case 1: if (rw.idx < 0 || rw.idx >= (int)m_locs.size()) return false; x = m_locs[rw.idx].x; y = m_locs[rw.idx].y; name = m_locs[rw.idx].name; return true;
			case 2: return P->hqAt(rw.idx, x, y, z, name, c, a);
			case 3: return P->pinAt(rw.idx, x, y, name);
		}
		return false;
	}

	// toque numa linha da lista: cabecalho abre/fecha; local = GPS do servidor; sede = GPS + marcacao 3D; marcacao salva = GPS (ela ja tem o proprio 3D)
	inline void Ui::SelectRow(const Row& rw)
	{
		if (rw.kind == 4) return;
		if (rw.kind == 0)
		{
			if (rw.idx >= 100)
			{
				const int k = rw.idx - 100;
				if (k >= 0 && k < (int)m_gcatNames.size())
				{
					auto it = std::find(m_gclosed.begin(), m_gclosed.end(), m_gcatNames[k]);
					if (it == m_gclosed.end()) m_gclosed.push_back(m_gcatNames[k]); else m_gclosed.erase(it);
				}
			}
			else if (rw.idx >= 0 && rw.idx < kCats) m_catOpen[rw.idx] = !m_catOpen[rw.idx];
			BuildRows();
			return;
		}
		if (rw.kind == 1) { SelectLocation(rw.idx, true); return; }
		float x = 0, y = 0; std::string nm;
		if (!RowPos(rw, x, y, nm)) return;
		CenterOn(m_view, x, y);
		if (rw.kind == 2) SetFreeGps(x, y, true, ClipUtf8("SEDE " + nm, 24));
		else SetFreeGps(x, y, false, nm);
	}

	inline void Ui::SetFreeGps(float wx, float wy, bool create, const std::string& label)
	{
		wx = Clampf(wx, m_view.bx0, m_view.bx1);
		wy = Clampf(wy, m_view.by0, m_view.by1);
		m_gps.on = true; m_gps.x = wx; m_gps.y = wy; m_gps.z = 0; m_gps.loc = -1;
		m_route.clear(); ClearLocalRoute();
		m_selLoc = -1;
		P->setGps(wx, wy, 0.0f);
		Cmd("/mapax pos %d %d", (int)std::lround(wx), (int)std::lround(wy));
		std::string nm = label.empty() ? (m_zoneCity ? std::string("Cidade Nova") : ZoneLabel(wx, wy)) : label;
		if (create) P->setDestMarker(wx, wy, -1000.0f, ClipUtf8(nm.empty() ? std::string("Destino") : nm, 24));      // marcacao 3D (unica) com o nome do lugar clicado
		else P->clearDestMarker();                                                                                 // GPS numa marcacao salva: ela ja tem o proprio 3D
		Toast(nm.empty() ? std::string("GPS marcado") : "GPS marcado: " + nm);
	}

	// chegou ao destino (funciona com o mapa fechado)
	inline void Ui::TickGps()
	{
		if (!m_gps.on) return;
		float px, py, hd;
		if (!P->player(px, py, hd)) return;
		if (Dist(px, py, m_gps.x, m_gps.y) < 25.0f)
		{
			const int id = m_gps.loc;
			ClearGps(false);
			Cmd("/mapax arrived %d", id);
			Toast("Voce chegou ao destino");
		}
	}

	inline void Ui::OnTap(float x, float y)
	{
		const Layout& L = m_L;
		if (In(x, y, L.listA, L.listB))      // a lista flutua sobre o mapa: ela tem prioridade
		{
			if (L.hasFilt && y < L.rowsY)        // pilulas de filtro: esquerda = local, direita = categoria
			{
				const float mid = (L.filtA.x + L.filtB.x) * 0.5f;
				if (y >= L.filtA.y && y <= L.filtB.y) CycleFilter(x < mid);
				return;
			}
			const int i = (int)std::floor((y - L.rowsY + m_listScroll - 4.0f * L.k) / L.rowH);
			if (i >= 0 && i < (int)m_rows.size()) { const Row rw = m_rows[i]; SelectRow(rw); }
		}
		else if (In(x, y, L.mapA, L.mapB))
		{
			const float rx = x - L.mapA.x, ry = y - L.mapA.y;
			const int hit = NearestLocation(m_view, m_locs, rx, ry, 26.0f * L.k);
			if (hit >= 0) { SelectLocation(hit, false); return; }
			float pwx = 0, pwy = 0; std::string pnm;
			if (NearestHq(rx, ry, 24.0f * L.k, pwx, pwy, pnm) >= 0) { CenterOn(m_view, pwx, pwy); SetFreeGps(pwx, pwy, true, ClipUtf8("SEDE " + pnm, 24)); return; }       // toque numa sede de grupo
			if (NearestPin(rx, ry, 26.0f * L.k, pwx, pwy, pnm) >= 0) { SetFreeGps(pwx, pwy, false, pnm); return; }       // toque numa marcacao: GPS ate ela (sem criar outra)
			const Vec2 w = ToWorld(m_view, rx, ry);
			SetFreeGps(w.x, w.y);
		}
	}

	inline void Ui::ProcessEvents()
	{
		std::vector<TouchEv> ev;
		{ std::lock_guard<std::mutex> l(m_mx); ev.swap(m_events); }
		const Layout& L = m_L;
		const float slop = 10.0f * L.k;
		for (const TouchEv& e : ev)
		{
			Finger& f = m_f[e.num & 3];
			if (e.type == 2)   // push
			{
				f = Finger();
				f.down = true; f.sx = f.x = e.x; f.sy = f.y = e.y; f.t0 = P->nowMs();
				const int b = ButtonAt(e.x, e.y);
				if (b != BTN_NONE) { f.region = 3; f.btn = b; }
				else if (In(e.x, e.y, L.listA, L.listB)) { f.region = 2; f.scroll0 = m_listScroll; }
				else if (In(e.x, e.y, L.mapA, L.mapB)) { f.region = 1; }
				else f.region = 0;
				int mapFingers = 0;
				for (const Finger& o : m_f) if (o.down && o.region == 1) mapFingers++;
				if (mapFingers >= 2)
				{
					// pinca: guarda distancia e ponto medio de referencia
					const Finger* a = nullptr; const Finger* b2 = nullptr;
					for (const Finger& o : m_f) if (o.down && o.region == 1) { if (!a) a = &o; else if (!b2) b2 = &o; }
					m_prevPinchDist = Dist(a->x, a->y, b2->x, b2->y);
					m_prevMidX = (a->x + b2->x) * 0.5f; m_prevMidY = (a->y + b2->y) * 0.5f;
					m_pinching = true;
					for (Finger& o : m_f) if (o.down && o.region == 1) o.moved = true;   // nunca vira "toque"
				}
			}
			else if (e.type == 3)   // move
			{
				if (!f.down) continue;
				const float px = f.x, py = f.y;
				f.x = e.x; f.y = e.y;
				if (Dist(f.x, f.y, f.sx, f.sy) > slop) f.moved = true;
				if (f.region == 1)
				{
					int idx[2] = { -1, -1 }, c = 0;
					for (int i = 0; i < 4; i++) if (m_f[i].down && m_f[i].region == 1 && c < 2) idx[c++] = i;
					if (m_pinching && c >= 2)
					{
						const Finger& a = m_f[idx[0]]; const Finger& b = m_f[idx[1]];
						const float d = Dist(a.x, a.y, b.x, b.y);
						const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
						PanPx(m_view, mx - m_prevMidX, my - m_prevMidY);
						if (m_prevPinchDist > 8.0f && d > 8.0f)
							ZoomAt(m_view, mx - L.mapA.x, my - L.mapA.y, d / m_prevPinchDist);
						m_prevPinchDist = d; m_prevMidX = mx; m_prevMidY = my;
					}
					else if (f.moved && c == 1)
					{
						PanPx(m_view, f.x - px, f.y - py);
					}
				}
				else if (f.region == 3 && f.btn == BTN_LIST_MOVE)
				{
					m_listDx += f.x - px; m_listDy += f.y - py;      // MakeLayout limita ao mapa no proximo quadro
				}
				else if (f.region == 2 && f.moved)
				{
					const float total = (float)m_rows.size() * L.rowH + 8.0f * L.k, viewH = L.listB.y - L.rowsY;
					m_listScroll = ClampScroll(f.scroll0 - (f.y - f.sy), total, viewH);
				}
			}
			else if (e.type == 1)   // pop / cancel
			{
				if (!f.down) continue;
				const bool ok = !e.cancel;
				const int64_t dt = P->nowMs() - f.t0;
				if (ok && f.region == 3 && ButtonAt(e.x, e.y) == f.btn)
				{
					switch (f.btn)
					{
						case BTN_CLOSE: Close(true); break;
						case BTN_CLEAR: ClearGps(true); Toast("GPS removido"); break;
						case BTN_LIST_EYE: m_listHidden = !m_listHidden; break;
						case BTN_ZIN:   ZoomAt(m_view, m_view.w * 0.5f, m_view.h * 0.5f, 1.5f); break;
						case BTN_ZOUT:  ZoomAt(m_view, m_view.w * 0.5f, m_view.h * 0.5f, 1.0f / 1.5f); break;
						case BTN_CENTER:
						{
							float px, py, hd;
							if (P->player(px, py, hd)) CenterOn(m_view, px, py);
							break;
						}
					}
				}
				else if (ok && (f.region == 1 || f.region == 2) && !f.moved && IsTap(e.x - f.sx, e.y - f.sy, dt, slop))
				{
					OnTap(e.x, e.y);
				}
				f = Finger();
				int mapFingers = 0;
				for (const Finger& o : m_f) if (o.down && o.region == 1) mapFingers++;
				if (mapFingers < 2) m_pinching = false;
				if (m_pinching == false)
					for (Finger& o : m_f) if (o.down && o.region == 1) { o.sx = o.x; o.sy = o.y; }   // resto do arrasto continua com 1 dedo
				if (!m_open.load()) return;
			}
			if (!m_open.load()) return;   // FECHAR: ignora o resto da fila
		}
	}

	inline void Ui::DrawPin(ImGuiRenderer* r, const ImVec2& tip, float s, const ImColor& fill, const ImColor& edge) const
	{
		// gota apontando para baixo: ponta em 'tip'
		const ImVec2 c(tip.x, tip.y - s * 1.55f);
		ImVec2 tri[3] = { ImVec2(tip.x, tip.y), ImVec2(c.x - s * 0.82f, c.y + s * 0.45f), ImVec2(c.x + s * 0.82f, c.y + s * 0.45f) };
		r->drawConvexPolyFilled(tri, 3, edge);
		r->drawRoundedRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), edge, s, true);
		r->drawRoundedRect(ImVec2(c.x - s * 0.72f, c.y - s * 0.72f), ImVec2(c.x + s * 0.72f, c.y + s * 0.72f), fill, s * 0.72f, true);
		r->drawRoundedRect(ImVec2(c.x - s * 0.28f, c.y - s * 0.28f), ImVec2(c.x + s * 0.28f, c.y + s * 0.28f), edge, s * 0.28f, true);
	}

	// icones simples por id (0 pino, 1 casa, 2 hospital, 3 banco, 4 trabalho, 5 aviao, 6 caveira, 7 roupa, 8 estrela, 9 posto)
	inline void Ui::DrawIcon(ImGuiRenderer* r, int icon, const ImVec2& c, float s, const ImColor& col) const
	{
		const ImColor dark(20, 22, 28, 255);
		switch (icon)
		{
			case 1:   // casa
			{
				ImVec2 roof[3] = { ImVec2(c.x - s, c.y - s * 0.1f), ImVec2(c.x, c.y - s), ImVec2(c.x + s, c.y - s * 0.1f) };
				r->drawConvexPolyFilled(roof, 3, col);
				r->drawRect(ImVec2(c.x - s * 0.68f, c.y - s * 0.1f), ImVec2(c.x + s * 0.68f, c.y + s * 0.85f), col, true);
				r->drawRect(ImVec2(c.x - s * 0.18f, c.y + s * 0.3f), ImVec2(c.x + s * 0.18f, c.y + s * 0.85f), dark, true);
				break;
			}
			case 2:   // hospital (cruz)
				r->drawRect(ImVec2(c.x - s * 0.32f, c.y - s), ImVec2(c.x + s * 0.32f, c.y + s), col, true);
				r->drawRect(ImVec2(c.x - s, c.y - s * 0.32f), ImVec2(c.x + s, c.y + s * 0.32f), col, true);
				break;
			case 3:   // banco ($)
				r->drawRoundedRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, s, true);
				r->drawText(ImVec2(c.x - s * 0.38f, c.y - s * 0.62f), dark, "$", false, s * 1.3f);
				break;
			case 4:   // trabalho (maleta)
				r->drawRoundedRect(ImVec2(c.x - s, c.y - s * 0.55f), ImVec2(c.x + s, c.y + s * 0.8f), col, s * 0.18f, true);
				r->drawRect(ImVec2(c.x - s * 0.35f, c.y - s * 0.95f), ImVec2(c.x + s * 0.35f, c.y - s * 0.55f), col, false, 2.0f);
				break;
			case 5:   // aviao (seta)
			{
				ImVec2 tri[3] = { ImVec2(c.x, c.y - s), ImVec2(c.x - s * 0.8f, c.y + s * 0.8f), ImVec2(c.x + s * 0.8f, c.y + s * 0.8f) };
				r->drawConvexPolyFilled(tri, 3, col);
				break;
			}
			case 6:   // caveira / perigo
				r->drawRoundedRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s * 0.5f), col, s * 0.8f, true);
				r->drawRect(ImVec2(c.x - s * 0.5f, c.y + s * 0.3f), ImVec2(c.x + s * 0.5f, c.y + s), col, true);
				r->drawRect(ImVec2(c.x - s * 0.55f, c.y - s * 0.3f), ImVec2(c.x - s * 0.15f, c.y + s * 0.1f), dark, true);
				r->drawRect(ImVec2(c.x + s * 0.15f, c.y - s * 0.3f), ImVec2(c.x + s * 0.55f, c.y + s * 0.1f), dark, true);
				break;
			case 7:   // roupa (camiseta)
				r->drawRect(ImVec2(c.x - s * 0.55f, c.y - s * 0.7f), ImVec2(c.x + s * 0.55f, c.y + s * 0.9f), col, true);
				r->drawRect(ImVec2(c.x - s, c.y - s * 0.7f), ImVec2(c.x + s, c.y - s * 0.1f), col, true);
				break;
			case 8:   // estrela (losango)
			{
				ImVec2 d[4] = { ImVec2(c.x, c.y - s), ImVec2(c.x + s, c.y), ImVec2(c.x, c.y + s), ImVec2(c.x - s, c.y) };
				r->drawConvexPolyFilled(d, 4, col);
				break;
			}
			case 9:   // posto (gota)
				r->drawRoundedRect(ImVec2(c.x - s * 0.7f, c.y - s * 0.4f), ImVec2(c.x + s * 0.7f, c.y + s), col, s * 0.7f, true);
				{
					ImVec2 tri[3] = { ImVec2(c.x, c.y - s), ImVec2(c.x - s * 0.7f, c.y), ImVec2(c.x + s * 0.7f, c.y) };
					r->drawConvexPolyFilled(tri, 3, col);
				}
				break;
			default:  // pino
				r->drawRoundedRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, s, true);
				r->drawRoundedRect(ImVec2(c.x - s * 0.38f, c.y - s * 0.38f), ImVec2(c.x + s * 0.38f, c.y + s * 0.38f), dark, s * 0.38f, true);
				break;
		}
	}

	inline void Ui::DrawButton(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const char* label, const ImColor& bg, float fs, bool hot) const
	{
		r->drawRoundedRect(a, b, hot ? ui_colors::blueD : bg, (b.y - a.y) * 0.5f, true);      // pilula (circulo nos botoes quadrados)
		r->drawRoundedRect(a, b, ImColor(255, 255, 255, 60), (b.y - a.y) * 0.5f, false, 1.0f);
		const ImVec2 ts = r->calculateTextSize(label, fs);
		r->drawText(ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + ((b.y - a.y) - ts.y) * 0.5f), ui_colors::text, label, false, fs);
	}

	// ---------------------------------------------------------------------------------------------
	//  GPS pelas ruas + linhas
	// ---------------------------------------------------------------------------------------------
	// Sem rota do servidor: calcula A* entre o jogador e o destino no grafo de ruas. Recalcula quando o destino muda ou o
	// jogador sai da rota (mais que 'recalcular_a'); no maximo 1 calculo a cada 1,2 s. Falha = segue a linha reta tracejada.
	inline void Ui::UpdateLocalRoute(bool havePlayer, float plx, float ply)
	{
		if (m_zoneCity || !m_gps.on || m_route.size() >= 2 || !havePlayer) return;
		const Roads::Graph* g = P->roads();
		if (!g) return;
		const Lines::Styles& S = P->lineStyles();
		const bool sameTarget = m_localValid && m_localToX == m_gps.x && m_localToY == m_gps.y;
		bool need = !sameTarget;
		if (sameTarget && m_localRoute.size() >= 2)
			need = Lines::NearestOnPolyline(m_localRoute, { plx, ply }) > S.offRouteDist;
		else if (sameTarget)   // ja falhou: so tenta de novo se o jogador andou bastante
			need = Dist(plx, ply, m_localFromX, m_localFromY) > 120.0f;
		if (!need) return;
		const int64_t now = P->nowMs();
		if (now - m_localTryAt < 1200) return;
		m_localTryAt = now;
		Roads::RouteResult rr;
		std::vector<Vec2> out;
		if (m_search.Route(*g, plx, ply, m_gps.x, m_gps.y, S.snapMax, S.smooth, out, &rr))
		{
			m_localRoute = std::move(out); m_localLen = rr.length; m_localUndirected = rr.undirected; m_localWhy.clear();
		}
		else { m_localRoute.clear(); m_localWhy = rr.why; }
		m_localValid = true;
		m_localToX = m_gps.x; m_localToY = m_gps.y; m_localFromX = plx; m_localFromY = ply;
	}

	// rota que vale agora: a do servidor (com cantos arredondados) ou a local; vazia = nenhuma
	inline const std::vector<Vec2>& Ui::EffectiveRoute()
	{
		static const std::vector<Vec2> kEmpty;
		if (m_route.size() >= 2)
		{
			uint64_t h = 1469598103934665603ull ^ m_route.size();
			for (const Vec2& p : m_route)
			{
				uint32_t a, b; std::memcpy(&a, &p.x, 4); std::memcpy(&b, &p.y, 4);
				h = (h ^ a) * 1099511628211ull; h = (h ^ b) * 1099511628211ull;
			}
			if (h != m_routeDrawKey || m_routeDraw.empty())
			{
				m_routeDraw = P->lineStyles().smooth ? Lines::RoundCorners(m_route, 14.0f, 4) : m_route;
				m_routeDrawKey = h;
			}
			return m_routeDraw;
		}
		if (m_gps.on && m_localRoute.size() >= 2 && m_localValid && m_localToX == m_gps.x && m_localToY == m_gps.y) return m_localRoute;
		return kEmpty;
	}

	// ---------------------------------------------------------------------------------------------
	//  Minimapa: rota (mundo) a partir do jogador. O hook de CHud::DrawRadar (game/hooks.cpp) converte para pixels.
	// ---------------------------------------------------------------------------------------------
	inline void Ui::PublishMiniRoute()
	{
		float px = 0, py = 0, hd = 0;
		if (!MiniRoute::gEnabled.load() || !m_gps.on || !P->player(px, py, hd) || !Finite(px) || !Finite(py)) { MiniRoute::ClearWorld(); return; }
		UpdateLocalRoute(true, px, py);
		const std::vector<Vec2>& rt = EffectiveRoute();
		std::vector<Vec2> wp;
		bool straight = false;
		wp.push_back({ px, py });
		if (rt.size() >= 2)
		{
			size_t seg = 1;
			Lines::NearestOnPolyline(rt, { px, py }, &seg);
			for (size_t i = seg; i < rt.size(); i++) wp.push_back(rt[i]);       // so o que ainda falta percorrer
		}
		else { wp.push_back({ m_gps.x, m_gps.y }); straight = true; }          // sem rota/grafo: reta ate o destino
		MiniRoute::PublishWorld(wp, straight);
	}

	// Ligacao entre os dois mapas: so a "ponta". No mapa de SA aparece o comeco da rodovia (da borda oeste ate Bayside); no mapa da cidade
	// aparece o fim dela (continuacao alem da divisa x=-3000 ate Bayside). Coordenadas = eixo da rodovia de saida (MOBJ).
	inline void Ui::DrawExitLink(ImGuiRenderer* r, const ImVec2& origin, float k)
	{
		if (!m_city.ok) return;
		static const float kPts[][2] = {
			{ -3000.0f, 1967.09f }, { -2950.0f, 1967.09f }, { -2836.0f, 1967.09f }, { -2741.5f, 1968.5f }, { -2704.3f, 1982.1f },
			{ -2671.7f, 2004.7f }, { -2645.0f, 2034.2f }, { -2625.6f, 2068.9f }, { -2615.1f, 2107.4f }, { -2615.1f, 2276.0f } };
		const int n = (int)(sizeof(kPts) / sizeof(kPts[0]));
		std::vector<ImVec2> pts;
		pts.reserve(n);
		for (int i = 0; i < n; i++)
		{
			const Vec2 sp = ToScreen(m_view, kPts[i][0], kPts[i][1]);
			pts.push_back(ImVec2(origin.x + sp.x, origin.y + sp.y));
		}
		const float w = Clampf(14.0f * m_view.scale, 2.5f * k, 8.0f * k);
		r->drawPolyline(pts.data(), n, ImColor(255, 255, 255, 235), false, w + 2.5f * k);
		r->drawPolyline(pts.data(), n, ImColor(255, 205, 70, 255), false, w);
		const float fs = 12.0f * k;
		if (!m_zoneCity)
		{
			// SA: a rodovia sai pela borda oeste -> seta e nome da cidade nova
			const ImVec2 a = pts.front();
			r->drawText(ImVec2(a.x + 8.0f * k, a.y - 7.0f * k - fs), ImColor(255, 255, 255, 255), "< Cidade Nova", true, fs);
		}
		else
		{
			// Cidade: a rodovia chega em Bayside (San Fierro)
			const ImVec2 b = pts.back();
			r->drawText(ImVec2(b.x + 8.0f * k, b.y - fs * 0.5f), ImColor(255, 255, 255, 255), "San Fierro >", true, fs);
		}
	}

	inline void Ui::DrawMiniCity(ImGuiRenderer* r)
	{
		if (!m_zoneCity || !MiniCity::gEnabled.load()) return;
		MiniCity::Xf t;
		if (!MiniCity::GetXf(t, P->nowMs())) return;
		const float wpp = MiniCity::WorldPerPx(t);                              // metros por pixel na tela
		if (!(wpp > 0.0f) || !Finite(wpp)) return;
		std::vector<ImVec2> pts; std::vector<ImVec2> uvs;
		std::vector<MiniCity::V> quad, clipped;
		// retangulo do mundo [x0,x1] x [y0,y1] com a imagem tex (v=0 no NORTE) -> quadrilatero girado, recortado na mascara do radar
		auto drawRect = [&](float x0, float y0, float x1, float y1, ImTextureID tex)
		{
			quad.clear();
			quad.push_back({ MiniCity::Apply(t, x0, y1), { 0.0f, 0.0f } });
			quad.push_back({ MiniCity::Apply(t, x1, y1), { 1.0f, 0.0f } });
			quad.push_back({ MiniCity::Apply(t, x1, y0), { 1.0f, 1.0f } });
			quad.push_back({ MiniCity::Apply(t, x0, y0), { 0.0f, 1.0f } });
			for (const auto& q : quad) if (!Finite(q.p.x) || !Finite(q.p.y)) return;
			MiniCity::ClipTex(quad, t.poly, clipped);
			if (clipped.size() < 3) return;
			pts.clear(); uvs.clear();
			for (const auto& q : clipped) { pts.push_back(ImVec2(q.p.x, q.p.y)); uvs.push_back(ImVec2(q.uv.x, q.uv.y)); }
			r->drawTexPoly(pts.data(), uvs.data(), (int)pts.size(), tex);
		};
		// fundo: o disco inteiro azul-agua (cor do "fora do mundo" dos tiles), para a imagem nao flutuar sobre o vazio do jogo
		{
			std::vector<ImVec2> bg;
			for (const Vec2& p : t.poly) bg.push_back(ImVec2(p.x, p.y));
			r->drawConvexPolyFilled(bg.data(), (int)bg.size(), ImColor(143, 189, 240, 255));
		}
		float ix0, iy0, ix1, iy1; P->cityImageRect(ix0, iy0, ix1, iy1);
		const ImTextureID ov = P->cityOverview();
		if (ov && ix1 > ix0 && iy1 > iy0) drawRect(ix0, iy0, ix1, iy1, ov);
		// detalhe: os tiles (1,6 m/px) que tocam o disco visivel, so se o zoom do radar pede (senao a imagem geral basta)
		float gx0, gy1, tileM; int cols, rows; P->cityGrid(gx0, gy1, tileM, cols, rows);
		if (wpp < 3.2f && tileM > 0.0f && cols > 0 && rows > 0)
		{
			const float R = t.rad * wpp * 1.25f;
			const int c0 = std::max(0, (int)std::floor((t.wx - R - gx0) / tileM)), c1 = std::min(cols - 1, (int)std::floor((t.wx + R - gx0) / tileM));
			const int r0 = std::max(0, (int)std::floor((gy1 - (t.wy + R)) / tileM)), r1 = std::min(rows - 1, (int)std::floor((gy1 - (t.wy - R)) / tileM));
			int guard = 0;
			for (int rr = r0; rr <= r1 && guard < 9; rr++)
				for (int cc = c0; cc <= c1 && guard < 9; cc++)
				{
					const ImTextureID tex = P->cityTile(cc, rr);
					if (!tex) continue;
					guard++;
					const float tx0 = gx0 + cc * tileM, ty1 = gy1 - rr * tileM;
					drawRect(tx0, ty1 - tileM, tx0 + tileM, ty1, tex);
				}
		}
		// destino do GPS (a rota em si e a linha do DrawMiniRoute)
		if (m_gps.on)
		{
			const Vec2 g = MiniCity::Apply(t, m_gps.x, m_gps.y);
			if (MiniCity::InsideConvex(t.poly, g.x, g.y))
			{
				const float pr = Clampf(t.rad * 0.06f, 3.0f, 9.0f);
				r->drawRoundedRect(ImVec2(g.x - pr - 1.5f, g.y - pr - 1.5f), ImVec2(g.x + pr + 1.5f, g.y + pr + 1.5f), ImColor(255, 255, 255, 255), pr + 1.5f, true);
				r->drawRoundedRect(ImVec2(g.x - pr, g.y - pr), ImVec2(g.x + pr, g.y + pr), ImColor(20, 20, 20, 255), pr, true);
			}
		}
		// seta do jogador (o radar nativo a desenhou por baixo da imagem): ponta para o rumo do veiculo
		{
			float dx = t.hdx, dy = t.hdy;
			const float dl = std::hypot(dx, dy);
			if (dl > 0.001f) { dx /= dl; dy /= dl; } else { dx = 0.0f; dy = -1.0f; }   // sem rumo: aponta para cima
			const float sz = Clampf(t.rad * 0.11f, 6.0f, 16.0f);
			const ImVec2 tip(t.cx + dx * sz * 1.25f, t.cy + dy * sz * 1.25f);
			const ImVec2 l(t.cx - dx * sz * 0.8f - dy * sz * 0.75f, t.cy - dy * sz * 0.8f + dx * sz * 0.75f);
			const ImVec2 rr(t.cx - dx * sz * 0.8f + dy * sz * 0.75f, t.cy - dy * sz * 0.8f - dx * sz * 0.75f);
			const ImVec2 n(t.cx - dx * sz * 0.35f, t.cy - dy * sz * 0.35f);
			ImVec2 outer[4] = { tip, l, n, rr };
			r->drawConvexPolyFilled(outer, 4, ImColor(15, 15, 15, 255));
			const float k = 0.72f;
			const ImVec2 tip2(t.cx + dx * sz * 1.25f * k, t.cy + dy * sz * 1.25f * k);
			const ImVec2 l2(t.cx + (l.x - t.cx) * k, t.cy + (l.y - t.cy) * k), r2(t.cx + (rr.x - t.cx) * k, t.cy + (rr.y - t.cy) * k), n2(t.cx + (n.x - t.cx) * k, t.cy + (n.y - t.cy) * k);
			ImVec2 inner[4] = { tip2, l2, n2, r2 };
			r->drawConvexPolyFilled(inner, 4, ImColor(255, 255, 255, 255));
		}
	}

	inline void Ui::DrawMiniLink(ImGuiRenderer* r)
	{
		if (!m_city.ok || m_zoneCity) return;      // dentro da cidade a rodovia ja esta na imagem do minimapa
		std::vector<std::vector<Vec2>> runs;
		float rad = 0;
		if (!MiniRoute::GetLink(runs, rad, P->nowMs())) return;
		const float w = Clampf(rad * 0.05f, 2.2f, 7.0f);
		const float ow = w + 2.0f * Clampf(rad * 0.012f, 1.0f, 2.5f);
		std::vector<ImVec2> pts;
		for (const auto& run : runs)
		{
			pts.clear();
			for (const Vec2& p : run) pts.push_back(ImVec2(p.x, p.y));
			if (pts.size() < 2) continue;
			r->drawPolyline(pts.data(), (int)pts.size(), ImColor(255, 255, 255, 235), false, ow);
			r->drawPolyline(pts.data(), (int)pts.size(), ImColor(255, 205, 70, 255), false, w);
		}
	}

	inline void Ui::DrawMiniRoute(ImGuiRenderer* r)
	{
		std::vector<std::vector<Vec2>> runs;
		float rad = 0;
		bool straight = false;
		float rcx = 0, rcy = 0;
		if (!MiniRoute::GetScreen(runs, rad, P->nowMs(), straight, &rcx, &rcy)) return;
		const Lines::Style& rs = P->lineStyles().cat[Lines::kRoute];
		if (!rs.enabled) return;
		const float w = Clampf(rad * 0.055f, 2.5f, 8.0f);                       // espessura proporcional ao radar
		const float ow = w + 2.0f * Clampf(rad * 0.012f, 1.0f, 2.5f);
		const int a = straight ? (int)rs.main.a * 6 / 10 : (int)rs.main.a;
		const int ao = straight ? (int)rs.outline.a * 6 / 10 : (int)rs.outline.a;
		const ImColor cm(rs.main.r, rs.main.g, rs.main.b, a), co(rs.outline.r, rs.outline.g, rs.outline.b, ao);
		std::vector<ImVec2> pts;
		for (const auto& run : runs)
		{
			pts.clear();
			std::vector<Vec2> rn = run;
			// a run que nasce no centro (jogador) comeca um pouco depois, para nao cobrir a seta do radar
			if (std::hypot(rn[0].x - rcx, rn[0].y - rcy) < rad * 0.06f && !MiniRoute::TrimStart(rn, rad * 0.16f)) continue;
			for (const Vec2& p : rn) pts.push_back(ImVec2(p.x, p.y));
			if (pts.size() < 2) continue;
			r->drawPolyline(pts.data(), (int)pts.size(), co, false, ow);
			r->drawPolyline(pts.data(), (int)pts.size(), cm, false, w);
		}
	}

	// Linha da rota: contorno escuro + linha principal, espessura pelo zoom, so os trechos visiveis. A parte que o jogador
	// ja passou nao e desenhada (a linha sai da posicao dele). Pontas arredondadas.
	inline void Ui::DrawRouteLine(ImGuiRenderer* r, const std::vector<Vec2>& rt, const ImVec2& origin, float k, bool havePlayer, float plx, float ply)
	{
		const Lines::Style& rs = P->lineStyles().cat[Lines::kRoute];
		if (!rs.enabled || rt.size() < 2) return;
		std::vector<Vec2> wp;
		wp.reserve(rt.size() + 1);
		size_t from = 0;
		if (havePlayer)
		{
			size_t seg = 1;
			Lines::NearestOnPolyline(rt, { plx, ply }, &seg);
			from = seg;                                  // 1o ponto ainda a frente
			wp.push_back({ plx, ply });
		}
		for (size_t i = from; i < rt.size(); i++) wp.push_back(rt[i]);
		if (wp.size() < 2) return;

		const float halfW = m_view.w * 0.5f / m_view.scale + 40.0f, halfH = m_view.h * 0.5f / m_view.scale + 40.0f;
		static thread_local std::vector<std::pair<int, int>> runs;
		Lines::VisibleRuns(wp, m_view.cx - halfW, m_view.cy - halfH, m_view.cx + halfW, m_view.cy + halfH, runs);
		if (runs.empty()) return;

		const float w = Lines::WidthPx(rs, m_view.scale) * k;
		const float ow = w + 2.0f * rs.outlinePx * k;
		const ImColor cm(rs.main.r, rs.main.g, rs.main.b, rs.main.a), co(rs.outline.r, rs.outline.g, rs.outline.b, rs.outline.a);
		auto S = [&](const Vec2& p) { const Vec2 s = ToScreen(m_view, p.x, p.y); return ImVec2(origin.x + s.x, origin.y + s.y); };
		std::vector<ImVec2> pts;
		for (const auto& run : runs)
		{
			pts.clear();
			for (int i = run.first; i <= run.second; i++) pts.push_back(S(wp[(size_t)i]));
			if (pts.size() < 2) continue;
			r->drawPolyline(pts.data(), (int)pts.size(), co, false, ow);
			r->drawPolyline(pts.data(), (int)pts.size(), cm, false, w);
			// pontas arredondadas (so nas pontas reais da rota; corte pela tela fica reto)
			auto cap = [&](const ImVec2& c) {
				r->drawRoundedRect(ImVec2(c.x - ow * 0.5f, c.y - ow * 0.5f), ImVec2(c.x + ow * 0.5f, c.y + ow * 0.5f), co, ow * 0.5f, true);
				r->drawRoundedRect(ImVec2(c.x - w * 0.5f, c.y - w * 0.5f), ImVec2(c.x + w * 0.5f, c.y + w * 0.5f), cm, w * 0.5f, true);
			};
			if (run.first == 0) cap(pts.front());
			if (run.second == (int)wp.size() - 1) cap(pts.back());
		}
	}

	// Camada opcional com TODAS as ruas (nos + ligacoes reais do jogo). LOD: afastado so rodovias; meio rodovias+avenidas.
	// NOTA (conferido no projeto): ImDrawIdx = 16 bits (imconfig.h) e TODA a interface sai em UMA ImDrawList (GetBackgroundDrawList):
	// o teto e 65536 vertices por quadro. Cada linha ImGui com espessura > 1 custa 8 vertices (3 por ponto se fina, 6 num segmento).
	// Por isso: orcamento 'max_vertices' (padrao 24000), prioridade rodovia > avenida > rua, e rua sem contorno.
	inline void Ui::DrawRoads(ImGuiRenderer* r, const ImVec2& origin, float k)
	{
		const Lines::Styles& LS = P->lineStyles();
		if (!LS.roadLayer) return;
		const Roads::Graph* g = P->roads();
		if (!g || m_view.scale < 0.05f) return;
		const float halfW = m_view.w * 0.5f / m_view.scale, halfH = m_view.h * 0.5f / m_view.scale;
		int minCat = Lines::kStreet;
		if (m_view.scale < 0.15f) minCat = Lines::kHighway; else if (m_view.scale < 0.30f) minCat = Lines::kAvenue;
		m_segs.clear();
		int budget = LS.maxVertices, segLeft = LS.maxSegments;
		bool cut = false;
		for (int c = Lines::kHighway; c >= minCat && c >= Lines::kStreet; c--)
		{
			const Lines::Style& st = LS.cat[c];
			if (!st.enabled || !st.implemented) continue;
			const bool thick = Lines::WidthPx(st, m_view.scale) * k > 1.0f;
			const bool outline = c != Lines::kStreet && st.outlinePx > 0.0f;
			const int cost = (outline ? 8 : 0) + (thick ? 8 : 6);
			const int fit = std::min(segLeft, budget / cost);
			if (fit <= 0) { cut = true; break; }
			const int n = Roads::ForSegments(*g, LS, m_view.cx - halfW, m_view.cy - halfH, m_view.cx + halfW, m_view.cy + halfH, fit, c,
				[&](float ax, float ay, float bx, float by, Lines::Category cc) {
					const Vec2 a = ToScreen(m_view, ax, ay), b = ToScreen(m_view, bx, by);
					m_segs.push_back({ ImVec2(origin.x + a.x, origin.y + a.y), ImVec2(origin.x + b.x, origin.y + b.y), (uint8_t)cc });
				});
			budget -= n * cost; segLeft -= n;
			if (n >= fit) cut = true;
		}
		m_roadsCut = cut;                                   // true = a camada foi cortada pelo orcamento (a plataforma pode avisar)
		for (int pass = 0; pass < 2; pass++)                  // 0 = contornos, 1 = linhas
			for (int c = Lines::kStreet; c <= Lines::kHighway; c++)
			{
				const Lines::Style& st = LS.cat[c];
				if (!st.enabled || !st.implemented) continue;
				if (pass == 0 && (c == Lines::kStreet || st.outlinePx <= 0.0f)) continue;
				const float w = Lines::WidthPx(st, m_view.scale) * k;
				const ImColor col = pass == 0 ? ImColor(st.outline.r, st.outline.g, st.outline.b, st.outline.a) : ImColor(st.main.r, st.main.g, st.main.b, st.main.a);
				const float tw = pass == 0 ? w + 2.0f * st.outlinePx * k : w;
				for (const Seg& sg : m_segs) if (sg.c == c) r->drawLine(sg.a, sg.b, col, tw);
			}
	}

	inline void Ui::Draw(ImGuiRenderer* r)
	{
		using namespace ui_colors;
		if (m_resetReq.exchange(false))
		{
			if (m_gps.on) P->clearGps();
			m_gps = Gps(); m_locs.clear(); m_locsSrv.clear(); m_zoneCity = false; ResetBounds(m_view); m_rows.clear(); m_route.clear(); ClearLocalRoute(); m_selLoc = -1; m_listScroll = 0; ResetDrawState();
		}
		if (m_sendClose.exchange(false)) P->sendCommand("/mapax close");
		DrainPending();
		{ float zx = 0, zy = 0, zh = 0; const bool zp = P->player(zx, zy, zh); UpdateZone(zp, zx, zy, false); }
		MiniCity::gInCity.store(m_zoneCity);      // o hook do radar (hooks.cpp) e o nome do local (hud.cpp) leem isto
		TickGps();

		// animacao de TV: liga (sobe) quando abre, desliga (desce) quando fecha; o mapa segue desenhado ate apagar
		const int64_t nowA = P->nowMs();
		const float dtA = m_visT ? std::min(0.1f, std::max(0.0f, (float)(nowA - m_visT) / 1000.0f)) : 0.0f;
		m_visT = nowA;
		if (m_open.load()) m_vis = std::min(1.0f, m_vis + dtA / kTvOnSec);
		else               m_vis = std::max(0.0f, m_vis - dtA / kTvOffSec);
		bool closing = !m_open.load();
		MiniCity::gMapOpen.store(m_open.load() || m_vis > 0.0f);      // mapa grande aparecendo: o minimapa do HUD some (hooks.cpp / hud.cpp)

		if (closing && m_vis <= 0.0f)
		{
			PublishMiniRoute();     // mapa fechado: a rota aparece no minimapa (radar nativo)
			DrawMiniCity(r);        // cidade nova: imagem no minimapa (por baixo da rota)
			DrawMiniLink(r);        // rodovia de saida (continuacao nos dois lados)
			DrawMiniRoute(r);
			ResetDrawState();
			return;
		}
		MiniRoute::ClearWorld();    // mapa aberto (ou apagando): sem linha no minimapa

		BuildRows();
		m_L = MakeLayout();
		const Layout& L = m_L;
		const float k = L.k;
		m_view.w = L.mapB.x - L.mapA.x;
		m_view.h = L.mapB.y - L.mapA.y;

		if (m_needInit.exchange(false))
		{
			float px, py, hd;
			m_view.scale = m_view.w / 3000.0f;                    // ~ 3 km de largura
			if (P->player(px, py, hd)) { m_view.cx = px; m_view.cy = py; } else { m_view.cx = 0; m_view.cy = 0; m_view.scale = MinScale(m_view); }
			Clamp(m_view);
			m_listScroll = 0;
			ResetDrawState();
		}
		if (m_sendOpen.exchange(false)) P->sendCommand("/mapax open");

		if (!closing)
		{
			ProcessEvents();
			if (!m_open.load())   // fechou agora (FECHAR / voltar): termina este quadro e apaga em animacao
			{
				closing = true;
				if (m_sendClose.exchange(false)) P->sendCommand("/mapax close");
			}
		}
		Clamp(m_view);
		if (!m_listHidden) m_listScroll = ClampScroll(m_listScroll, (float)m_rows.size() * L.rowH + 8.0f * k, L.listB.y - L.rowsY);

		float plx = 0, ply = 0, plh = 0;
		const bool havePlayer = P->player(plx, ply, plh);

		// ---- efeito de TV: tudo abaixo e recortado na area "acesa" (linha que abre/fecha) ----
		const bool tv = m_vis < 0.999f;
		constexpr float kTvLine = 0.40f;                    // fatia da animacao em que so a linha horizontal cresce
		ImVec2 tvA(0, 0), tvB = L.scr;
		float tvFlash = 0.0f;                               // 0..1: brilho branco que some ao ligar
		if (tv)
		{
			const float pp = m_vis;
			float cw, ch;
			const float lineH = std::max(2.0f, 3.0f * k);
			if (pp < kTvLine)
			{
				const float u = pp / kTvLine, e = 1.0f - (1.0f - u) * (1.0f - u);
				cw = std::max(6.0f * k, L.scr.x * e); ch = lineH; tvFlash = 1.0f;
			}
			else
			{
				const float v = (pp - kTvLine) / (1.0f - kTvLine), e = 1.0f - (1.0f - v) * (1.0f - v) * (1.0f - v);
				cw = L.scr.x; ch = lineH + (L.scr.y - lineH) * e; tvFlash = (1.0f - v) * (1.0f - v);
			}
			const float cx = L.scr.x * 0.5f, cy = L.scr.y * 0.5f;
			tvA = ImVec2(cx - cw * 0.5f, cy - ch * 0.5f); tvB = ImVec2(cx + cw * 0.5f, cy + ch * 0.5f);
			r->pushClipRect(tvA, tvB, true);
		}

		// ---- fundo e painel ----
		r->drawRect(ImVec2(0, 0), L.scr, dim, true);      // sem painel preto: o jogo aparece escurecido por tras do mapa

		// ---- mapa ----
		r->pushClipRect(L.mapA, L.mapB, true);
		r->drawRect(L.mapA, L.mapB, mapBg, true);
		auto S = [&](float wx, float wy) { const Vec2 s = ToScreen(m_view, wx, wy); return ImVec2(L.mapA.x + s.x, L.mapA.y + s.y); };
		int tilesDrawn = 0;
		const ImTextureID full = m_zoneCity ? (ImTextureID)0 : P->fullMap();
		if (m_zoneCity) { DrawCityImage(r, L.mapA, tilesDrawn); }
		else if (full)
		{
			r->drawImage(S(kWorldMin, kWorldMax), S(kWorldMax, kWorldMin), full);
			tilesDrawn = 1;
		}
		else
		{
			for (int idx : VisibleTiles(m_view))
			{
				float x0, y0, x1, y1;
				TileWorldRect(idx, x0, y0, x1, y1);
				ImVec2 a = S(x0, y1), b = S(x1, y0);
				a.x -= 0.5f; a.y -= 0.5f; b.x += 0.5f; b.y += 0.5f;      // sem frestas entre tiles
				const ImTextureID t = P->tile(idx);
				if (t) { r->drawImage(a, b, t); tilesDrawn++; }
				else   { r->drawRect(a, b, ImColor(26, 30, 38, 255), true); r->drawRect(a, b, ImColor(255, 255, 255, 14), false); }
				if (debugGrid || !t)
				{
					char lb[12]; std::snprintf(lb, sizeof(lb), "%02d", idx);
					if (b.x - a.x > 28.0f * k) r->drawText(ImVec2(a.x + 4.0f * k, a.y + 3.0f * k), debugGrid ? ImColor(255, 220, 90, 255) : ImColor(255, 255, 255, 60), lb, false, 11.0f * k);
					if (debugGrid) r->drawRect(a, b, ImColor(255, 220, 90, 140), false, 1.0f);
				}
			}
		}
		if (P->mapDim() > 0) r->drawRect(L.mapA, L.mapB, ImColor(0, 0, 0, P->mapDim()), true);   // escurece (estilo do painel)
		r->drawRect(S(m_view.bx0, m_view.by1), S(m_view.bx1, m_view.by0), ImColor(255, 255, 255, 40), false, 1.5f);

		// ---- ruas (camada opcional) e caminho ----
		if (!m_zoneCity) DrawRoads(r, L.mapA, k);          // grafo de ruas (NODES) so cobre o mapa original
		DrawExitLink(r, L.mapA, k);                        // ponta da rodovia de saida (SA e cidade)
		UpdateLocalRoute(havePlayer, plx, ply);
		if (m_gps.on)
		{
			const std::vector<Vec2>& rt = EffectiveRoute();
			if (rt.size() >= 2)
			{
				DrawRouteLine(r, rt, L.mapA, k, havePlayer, plx, ply);
			}
			else if (havePlayer)
			{
				// sem rota (servidor nem ruas): linha reta tracejada ate o destino
				const ImVec2 a = S(plx, ply), b = S(m_gps.x, m_gps.y);
				const float len = Dist(a.x, a.y, b.x, b.y);
				const float dash = 11.0f * k, gap = 8.0f * k;
				if (len > 1.0f)
				{
					const float ux = (b.x - a.x) / len, uy = (b.y - a.y) / len;
					int guard = 0;
					for (float t = 0; t < len && guard < 1500; t += dash + gap, guard++)
					{
						const float t2 = std::min(len, t + dash);
						r->drawLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * t2, a.y + uy * t2), routeOut, 6.0f * k);
						r->drawLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * t2, a.y + uy * t2), route, 3.0f * k);
					}
				}
			}
		}

		// ---- locais + sedes dos grupos (roxo): icones sempre visiveis; nomes em pilula escura, sem sobreposicao ----
		// Na cidade nova os marcadores sao maiores (o mapa e' grande e o zoom de 1 km deixava tudo minusculo). Os nomes sao
		// colocados por prioridade (selecionado > locais > grupos) e cada um tenta direita, esquerda, acima, abaixo: se nenhuma
		// posicao esta livre o nome fica de fora (o icone continua) -> no zoom afastado nao vira uma "sopa" de letras.
		{
			const bool labels = m_view.scale >= (m_zoneCity ? 0.05f : 0.22f);
			struct Mk { ImVec2 c; float s; int icon; ImColor col; std::string name; bool sel; int prio; };
			std::vector<Mk> mk;
			const float mul = m_zoneCity ? 1.45f : 1.0f;
			const float fsz = (m_zoneCity ? 13.5f : 11.0f) * k;
			auto inView = [&](const ImVec2& c) { return !(c.x < L.mapA.x - 20 || c.x > L.mapB.x + 20 || c.y < L.mapA.y - 20 || c.y > L.mapB.y + 20); };
			for (size_t i = 0; i < m_locs.size(); i++)
			{
				const Location& l = m_locs[i];
				const ImVec2 c = S(l.x, l.y);
				if (!inView(c)) continue;
				const bool sel = (int)i == m_selLoc;
				mk.push_back({ c, (sel ? 9.5f : 7.5f) * k * mul, l.icon, text, l.name, sel, sel ? 0 : 1 });
			}
			const int nh = P->hqCount();
			for (int i = 0; i < nh; i++)
			{
				float wx = 0, wy = 0, wz = 0; std::string nm, hc, ha;
				if (!P->hqAt(i, wx, wy, wz, nm, hc, ha) || !ZoneOk(wx, wy) || !HqPass(hc, ha)) continue;       // os filtros da lista valem tambem para os icones
				const ImVec2 c = S(wx, wy);
				if (!inView(c)) continue;
				const bool sel = m_gps.on && Dist(m_gps.x, m_gps.y, wx, wy) < 2.0f;
				mk.push_back({ c, (sel ? 9.0f : 7.0f) * k * mul, 1, hqCol, ClipUtf8(nm, 24), sel, sel ? 0 : 2 });
			}
			// icones (o selecionado por cima)
			for (int pass = 0; pass < 2; pass++)
				for (const Mk& m : mk)
				{
					if ((pass == 1) != m.sel) continue;
					r->drawRoundedRect(ImVec2(m.c.x - m.s - 2 * k, m.c.y - m.s - 2 * k), ImVec2(m.c.x + m.s + 2 * k, m.c.y + m.s + 2 * k), m.sel ? gps : ImColor(10, 12, 16, 235), m.s + 2 * k, true);
					DrawIcon(r, m.icon, m.c, m.s * 0.8f, m.col);
				}
			if (labels || m_selLoc >= 0)
			{
				std::vector<int> ord(mk.size());
				for (size_t i = 0; i < ord.size(); i++) ord[i] = (int)i;
				std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return mk[x].prio < mk[y].prio; });
				struct Rc { float x0, y0, x1, y1; };
				std::vector<Rc> used;
				for (const Mk& m : mk) used.push_back({ m.c.x - m.s - 2 * k, m.c.y - m.s - 2 * k, m.c.x + m.s + 2 * k, m.c.y + m.s + 2 * k });   // os proprios icones tambem bloqueiam nomes
				const float padX = 5.0f * k, padY = 2.5f * k;
				for (int oi : ord)
				{
					const Mk& m = mk[oi];
					if (!labels && !m.sel) continue;
					const ImVec2 ts = r->calculateTextSize(m.name, fsz);
					const float w = ts.x + padX * 2, h = ts.y + padY * 2, g = m.s + 5.0f * k;
					const Rc cand[4] = {
						{ m.c.x + g, m.c.y - h * 0.5f, m.c.x + g + w, m.c.y + h * 0.5f },                 // direita
						{ m.c.x - g - w, m.c.y - h * 0.5f, m.c.x - g, m.c.y + h * 0.5f },                 // esquerda
						{ m.c.x - w * 0.5f, m.c.y - g - h, m.c.x + w * 0.5f, m.c.y - g },                 // acima
						{ m.c.x - w * 0.5f, m.c.y + g, m.c.x + w * 0.5f, m.c.y + g + h } };               // abaixo
					int pick = -1;
					for (int ci = 0; ci < 4 && pick < 0; ci++)
					{
						const Rc& q = cand[ci];
						if (q.x0 < L.mapA.x + 2 || q.x1 > L.mapB.x - 2 || q.y0 < L.mapA.y + 2 || q.y1 > L.mapB.y - 2) continue;       // nunca fora do mapa
						bool ok = true;
						for (const Rc& u : used) if (!(q.x1 <= u.x0 || q.x0 >= u.x1 || q.y1 <= u.y0 || q.y0 >= u.y1)) { ok = false; break; }
						if (ok) pick = ci;
					}
					if (pick < 0) { if (!m.sel) continue; pick = 0; }       // o selecionado sempre tem nome
					const Rc& q = cand[pick];
					r->drawRoundedRect(ImVec2(q.x0, q.y0), ImVec2(q.x1, q.y1), ImColor(12, 14, 20, 215), 5.0f * k, true);
					r->drawText(ImVec2(q.x0 + padX, q.y0 + padY), text, m.name, false, fsz);
					used.push_back({ q.x0 - 1.0f * k, q.y0 - 1.0f * k, q.x1 + 1.0f * k, q.y1 + 1.0f * k });
				}
			}
		}

		// ---- marcacoes 3D do jogador: pino azul + nome ----
		{
			const int np = P->pinCount();
			for (int i = 0; i < np; i++)
			{
				float wx = 0, wy = 0; std::string nm;
				if (!P->pinAt(i, wx, wy, nm) || !ZoneOk(wx, wy)) continue;
				const ImVec2 c = S(wx, wy);
				if (c.x < L.mapA.x - 20 || c.x > L.mapB.x + 20 || c.y < L.mapA.y - 20 || c.y > L.mapB.y + 20) continue;
				const float ps = 6.0f * k;
				DrawPin(r, c, ps, ImColor(64, 164, 255, 255), ImColor(255, 255, 255, 255));
				r->drawText(ImVec2(c.x + ps + 4.0f * k, c.y - ps * 1.55f - 6.5f * k), text, nm, true, 11.0f * k);
			}
		}

		// ---- destino (GPS) e jogador ----
		if (m_gps.on)
		{
			const ImVec2 c = S(m_gps.x, m_gps.y);
			DrawPin(r, c, 7.0f * k, gps, ImColor(20, 22, 28, 255));
		}
		if (havePlayer)
		{
			const ImVec2 c = S(plx, ply);
			// seta de direcao (heading em graus; 0 = norte, anti-horario como no jogo)
			const float rad = plh * 3.14159265f / 180.0f;
			const float fx = -std::sin(rad), fy = -std::cos(rad);       // vetor na tela
			ImVec2 tri[3] = { ImVec2(c.x + fx * 17.0f * k, c.y + fy * 17.0f * k),
							  ImVec2(c.x - fy * 6.0f * k - fx * 4.0f * k, c.y + fx * 6.0f * k - fy * 4.0f * k),
							  ImVec2(c.x + fy * 6.0f * k - fx * 4.0f * k, c.y - fx * 6.0f * k - fy * 4.0f * k) };
			r->drawConvexPolyFilled(tri, 3, ImColor(255, 255, 255, 220));
			DrawPin(r, c, 6.5f * k, red, ImColor(255, 255, 255, 255));
		}

		// ---- aviso/distancia ----
		{
			std::string head;
			if (m_gps.on && havePlayer)
			{
				const std::vector<Vec2>& rt = EffectiveRoute();
				const float d = rt.size() >= 2 ? RouteRemaining(rt, plx, ply) : Dist(plx, ply, m_gps.x, m_gps.y);
				head = "GPS  " + FormatDistance(d) + (rt.size() >= 2 ? "  (pela rua)" : "  (reta)");
			}
			if (!head.empty())
			{
				const ImVec2 ts = r->calculateTextSize(head, 12.0f * k);
				r->drawRoundedRect(ImVec2(L.mapA.x + 8 * k, L.mapA.y + 8 * k), ImVec2(L.mapA.x + 8 * k + ts.x + 16 * k, L.mapA.y + 8 * k + ts.y + 10 * k), ImColor(10, 12, 16, 215), 5 * k, true);
				r->drawText(ImVec2(L.mapA.x + 16 * k, L.mapA.y + 13 * k), gps, head, false, 12.0f * k);
			}
			if (tilesDrawn == 0)
			{
				const char* msg = m_zoneCity ? "Imagem da cidade indisponivel (SAMP/radar/cidade/overview.png - veja [CIDADE] no samp_log.txt)" : "Imagem do mapa indisponivel (tiles do radar nao carregaram - veja [MAPA] no samp_log.txt)";
				const ImVec2 ts = r->calculateTextSize(msg, 11.0f * k);
				r->drawText(ImVec2(L.mapA.x + (m_view.w - ts.x) * 0.5f, L.mapB.y - 24.0f * k), ImColor(255, 190, 90, 255), msg, true, 11.0f * k);
			}
		}
		// zoom / centralizar
		{
			const bool hz = m_f[0].down || m_f[1].down;   // so para o "pressionado"
			(void)hz;
			auto hot = [&](int id) { for (const Finger& f : m_f) if (f.down && f.region == 3 && f.btn == id) return true; return false; };
			DrawButton(r, L.zinA, L.zinB, "+", btn, 18.0f * k, hot(BTN_ZIN));
			DrawButton(r, L.zoutA, L.zoutB, "-", btn, 18.0f * k, hot(BTN_ZOUT));
			DrawButton(r, L.ctrA, L.ctrB, "O", btn, 14.0f * k, hot(BTN_CENTER));
		}
		// toast
		if (!m_toast.empty() && P->nowMs() < m_toastUntil)
		{
			const ImVec2 ts = r->calculateTextSize(m_toast, 12.0f * k);
			const float tx = L.mapA.x + (m_view.w - ts.x) * 0.5f, ty = L.mapB.y - 56.0f * k;
			r->drawRoundedRect(ImVec2(tx - 10 * k, ty - 5 * k), ImVec2(tx + ts.x + 10 * k, ty + ts.y + 5 * k), ImColor(10, 12, 16, 230), 6 * k, true);
			r->drawText(ImVec2(tx, ty), text, m_toast, false, 12.0f * k);
		}
		r->popClipRect();
		r->drawRect(L.mapA, L.mapB, border, false, 1.0f);

		// ---- lista em categorias (vidro escuro flutuante; nome alinhado a direita + icone; selecionado = pilula branca) ----
		if (!m_listHidden)
		{
			r->drawRoundedRect(L.listA, L.listB, glass, 9.0f * k, true);
			if (L.hasFilt)
			{
				// pilulas de filtro: "Local: Todos" | "Cat: Todas" (toque troca o valor, ciclando)
				const float mid = (L.filtA.x + L.filtB.x) * 0.5f, gp = 2.0f * k, fsz = 10.5f * k;
				for (int side = 0; side < 2; side++)
				{
					const bool isArea = side == 0;
					const ImVec2 a(isArea ? L.filtA.x : mid + gp, L.filtA.y), b(isArea ? mid - gp : L.filtB.x, L.filtB.y);
					const std::string& cur = isArea ? m_fArea : m_fCat;
					const bool active = !cur.empty();
					r->drawRoundedRect(a, b, active ? ImColor(244, 246, 250, 235) : ImColor(255, 255, 255, 28), (b.y - a.y) * 0.5f, true);
					std::string lb = std::string(isArea ? "Local: " : "Cat: ") + (active ? cur : std::string(isArea ? "Todos" : "Todas"));
					const float maxW = (b.x - a.x) - 12.0f * k;
					while (lb.size() > 4 && r->calculateTextSize(lb, fsz).x > maxW) lb.resize(lb.size() - 1);
					const ImVec2 ts0 = r->calculateTextSize(lb, fsz);
					r->drawText(ImVec2(a.x + ((b.x - a.x) - ts0.x) * 0.5f, a.y + ((b.y - a.y) - ts0.y) * 0.5f), active ? ImColor(20, 22, 28, 255) : text, lb, false, fsz);
				}
			}
			r->pushClipRect(ImVec2(L.listA.x, L.rowsY), L.listB, true);
			const float fs = 12.5f * k;
			const float off = 4.0f * k;
			int selNo = 0, itemNo = 0;
			for (size_t i = 0; i < m_rows.size(); i++)
			{
				const Row& rw = m_rows[i];
				const float y = L.rowsY + off + (float)i * L.rowH - m_listScroll;
				if (rw.kind != 0 && rw.kind != 4) itemNo++;
				if (y + L.rowH < L.rowsY || y > L.listB.y) { float x0, y0; std::string n0; if (rw.kind != 0 && RowPos(rw, x0, y0, n0) && m_gps.on && Dist(m_gps.x, m_gps.y, x0, y0) < 2.0f) selNo = itemNo; continue; }
				const float ax = L.listA.x + 5.0f * k, bx2 = L.listB.x - 5.0f * k, ay = y + 2.0f * k, by = y + L.rowH - 2.0f * k;
				if (rw.kind == 0)
				{
					// cabecalho da categoria: seta (aberta/fechada) + NOME + quantidade
					const bool isG = rw.idx >= 100;                         // categoria de GRUPO (roxo): "GRUPOS: GOVERNAMENTAL"
					const int gk = rw.idx - 100;
					int cnt = 0; bool open = true;
					if (isG) { if (gk >= 0 && gk < (int)m_gcatCount.size()) { cnt = m_gcatCount[gk]; open = std::find(m_gclosed.begin(), m_gclosed.end(), m_gcatNames[gk]) == m_gclosed.end(); } }
					else
					{
						open = m_catOpen[rw.idx];
						if (rw.idx == 0) cnt = P->pinCount();
						else { for (const Location& l : m_locs) if (CatOfIcon(l.icon) == rw.idx) cnt++; }
					}
					const ImColor hcol = isG ? hqCol : mute;
					const float cy = y + L.rowH * 0.5f, ts2 = 5.0f * k, cx = ax + 10.0f * k;
					ImVec2 tri[3];
					if (open) { tri[0] = ImVec2(cx - ts2, cy - ts2 * 0.6f); tri[1] = ImVec2(cx + ts2, cy - ts2 * 0.6f); tri[2] = ImVec2(cx, cy + ts2 * 0.7f); }
					else                   { tri[0] = ImVec2(cx - ts2 * 0.6f, cy - ts2); tri[1] = ImVec2(cx - ts2 * 0.6f, cy + ts2); tri[2] = ImVec2(cx + ts2 * 0.8f, cy); }
					r->drawConvexPolyFilled(tri, 3, hcol);
					std::string nm = isG ? (gk >= 0 && gk < (int)m_gcatNames.size() ? "Grupos: " + m_gcatNames[gk] : std::string("Grupos")) : std::string(CatName(rw.idx));
					for (char& ch : nm) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
					const float hmax = (bx2 - ax) - 56.0f * k;
					while (nm.size() > 4 && r->calculateTextSize(nm, 11.5f * k).x > hmax) nm.resize(nm.size() - 1);
					const ImVec2 tsz = r->calculateTextSize(nm, 11.5f * k);
					r->drawText(ImVec2(cx + 12.0f * k, y + (L.rowH - tsz.y) * 0.5f), hcol, nm, false, 11.5f * k);
					char cb[16]; std::snprintf(cb, sizeof(cb), "%d", cnt);
					const ImVec2 csz = r->calculateTextSize(cb, 11.0f * k);
					r->drawText(ImVec2(bx2 - 6.0f * k - csz.x, y + (L.rowH - csz.y) * 0.5f), hcol, cb, false, 11.0f * k);
					continue;
				}
				if (rw.kind == 4)
				{
					const char* msg = "Nenhum grupo neste filtro";
					const ImVec2 msz = r->calculateTextSize(msg, 11.0f * k);
					r->drawText(ImVec2(bx2 - 8.0f * k - msz.x, y + (L.rowH - msz.y) * 0.5f), mute, msg, false, 11.0f * k);
					continue;
				}
				float wx = 0, wy = 0; std::string nm;
				if (!RowPos(rw, wx, wy, nm)) continue;
				const bool sel = m_gps.on && (rw.kind == 1 ? (int)rw.idx == m_selLoc : Dist(m_gps.x, m_gps.y, wx, wy) < 2.0f);
				if (sel) { selNo = itemNo; r->drawRoundedRect(ImVec2(ax, ay), ImVec2(bx2, by), ImColor(244, 246, 250, 235), (by - ay) * 0.5f, true); }
				const float is = 7.5f * k;
				const ImVec2 ic(bx2 - 14.0f * k, y + L.rowH * 0.5f);
				const ImColor icol = rw.kind == 2 ? hqCol : (rw.kind == 3 ? pinCol : text);
				DrawIcon(r, rw.kind == 1 ? m_locs[rw.idx].icon : (rw.kind == 2 ? 1 : 0), ic, is, sel ? ImColor(20, 22, 28, 255) : icol);
				float maxW = (bx2 - ax) - 44.0f * k;
				std::string tag;
				if (rw.kind == 2)      // local de atuacao em letra pequena, a esquerda (some se faltar espaco)
				{
					float tx_, ty_, tz_; std::string n1, c1, a1;
					if (P->hqAt(rw.idx, tx_, ty_, tz_, n1, c1, a1)) tag = a1;
					if (!tag.empty()) { const float tw = r->calculateTextSize(tag, 9.5f * k).x; if (r->calculateTextSize(nm, fs).x + tw + 12.0f * k <= maxW) { maxW -= tw + 8.0f * k; } else tag.clear(); }
				}
				while (nm.size() > 3 && r->calculateTextSize(nm, fs).x > maxW) nm.resize(nm.size() - 1);
				const ImVec2 ts = r->calculateTextSize(nm, fs);
				r->drawText(ImVec2(ic.x - is - 8.0f * k - ts.x, y + (L.rowH - ts.y) * 0.5f), sel ? ImColor(20, 22, 28, 255) : text, nm, false, fs);
				if (!tag.empty()) { const ImVec2 tsz2 = r->calculateTextSize(tag, 9.5f * k); r->drawText(ImVec2(ax + 8.0f * k, y + (L.rowH - tsz2.y) * 0.5f), sel ? ImColor(60, 66, 80, 255) : mute, tag, false, 9.5f * k); }
			}
			if (m_rows.empty())
				r->drawText(ImVec2(L.listA.x + 12 * k, L.rowsY + 10 * k), mute, "Sem locais", false, 12.0f * k);
			const float total = (float)m_rows.size() * L.rowH + 8.0f * k, viewH = L.listB.y - L.rowsY;
			if (total > viewH)
			{
				const float th = std::max(24.0f * k, viewH * viewH / total);
				const float ty = L.rowsY + (viewH - th) * (m_listScroll / (total - viewH));
				r->drawRect(ImVec2(L.listB.x - 3 * k, L.rowsY + 6 * k), ImVec2(L.listB.x - k, L.listB.y - 6 * k), ImColor(255, 255, 255, 22), true);
				r->drawRect(ImVec2(L.listB.x - 3 * k, ty), ImVec2(L.listB.x - k, ty + th), ImColor(255, 255, 255, 140), true);
			}
			r->popClipRect();
			// contador "selecionado/total" (como "33/39" do GTA V)
			const int totalItems = P->pinCount() + m_hqShown + (int)m_locs.size();
			char cb[24];
			if (m_gps.on && selNo > 0) std::snprintf(cb, sizeof(cb), "%d/%d", selNo, totalItems); else std::snprintf(cb, sizeof(cb), "%d", totalItems);
			const ImVec2 csz = r->calculateTextSize(cb, 11.0f * k);
			r->drawRoundedRect(ImVec2(L.listB.x - csz.x - 16.0f * k, L.cntA.y), ImVec2(L.listB.x, L.cntB.y), glass, (L.cntB.y - L.cntA.y) * 0.5f, true);
			r->drawText(ImVec2(L.listB.x - csz.x - 8.0f * k, L.cntA.y + ((L.cntB.y - L.cntA.y) - csz.y) * 0.5f), text, cb, false, 11.0f * k);
		}

		// ---- botoes do menu: olho (ver/ocultar) e mao (mover); ficam mesmo com o menu oculto ----
		{
			auto hot = [&](int id) { for (const Finger& f : m_f) if (f.down && f.region == 3 && f.btn == id) return true; return false; };
			const ImColor bgc(14, 16, 22, 200), lineC(255, 255, 255, 70), white(255, 255, 255, 255);
			auto circle = [&](const ImVec2& a, const ImVec2& b, bool on) {
				r->drawRoundedRect(a, b, on ? ui_colors::blueD : bgc, (b.y - a.y) * 0.5f, true);
				r->drawRoundedRect(a, b, lineC, (b.y - a.y) * 0.5f, false, 1.0f);
			};
			// olho: menu visivel -> mostra o olho CORTADO (toque oculta); oculto -> olho aberto (toque mostra)
			circle(L.eyeA, L.eyeB, hot(BTN_LIST_EYE));
			const float is = (L.eyeB.x - L.eyeA.x) * 0.60f;
			auto icon = [&](int id, const ImVec2& a, const ImVec2& b) {
				const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
				const ImTextureID t = P->uiIcon(id);
				if (t) { r->drawImage(ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), ImVec2(c.x + is * 0.5f, c.y + is * 0.5f), t, white); return; }
				// sem textura: formas simples
				if (id == 0)   // setas laterais + mao (punho)
				{
					r->drawLine(ImVec2(c.x - is * 0.5f, c.y), ImVec2(c.x + is * 0.5f, c.y), white, 2.0f * k);
					ImVec2 l[3] = { ImVec2(c.x - is * 0.5f, c.y), ImVec2(c.x - is * 0.28f, c.y - is * 0.18f), ImVec2(c.x - is * 0.28f, c.y + is * 0.18f) };
					ImVec2 rr[3] = { ImVec2(c.x + is * 0.5f, c.y), ImVec2(c.x + is * 0.28f, c.y - is * 0.18f), ImVec2(c.x + is * 0.28f, c.y + is * 0.18f) };
					r->drawConvexPolyFilled(l, 3, white); r->drawConvexPolyFilled(rr, 3, white);
					r->drawRoundedRect(ImVec2(c.x - is * 0.2f, c.y - is * 0.26f), ImVec2(c.x + is * 0.2f, c.y + is * 0.3f), white, 3.0f * k, false, 2.0f * k);
				}
				else           // olho: amendoa + pupila (+ corte)
				{
					r->drawRoundedRect(ImVec2(c.x - is * 0.5f, c.y - is * 0.2f), ImVec2(c.x + is * 0.5f, c.y + is * 0.2f), white, is * 0.2f, false, 2.0f * k);
					r->drawRoundedRect(ImVec2(c.x - is * 0.14f, c.y - is * 0.14f), ImVec2(c.x + is * 0.14f, c.y + is * 0.14f), white, is * 0.14f, true);
					if (id == 2) r->drawLine(ImVec2(c.x - is * 0.45f, c.y + is * 0.4f), ImVec2(c.x + is * 0.45f, c.y - is * 0.4f), white, 2.4f * k);
				}
			};
			icon(m_listHidden ? 1 : 2, L.eyeA, L.eyeB);
			circle(L.moveA, L.moveB, hot(BTN_LIST_MOVE));
			icon(0, L.moveA, L.moveB);
		}

		// ---- barra inferior: botoes em pilula + escala e nome da zona (estilo do mapa do GTA V) ----
		{
			auto hot = [&](int id) { for (const Finger& f : m_f) if (f.down && f.region == 3 && f.btn == id) return true; return false; };
			DrawButton(r, L.closeA, L.closeB, "FECHAR", blue, 13.0f * k, hot(BTN_CLOSE));
			if (m_gps.on) DrawButton(r, L.clrA, L.clrB, "LIMPAR GPS", btn, 12.0f * k, hot(BTN_CLEAR));
			// escala: barra com o maior valor "redondo" que cabe em ~110 px
			static const float nice[] = { 50, 100, 200, 500, 1000, 2000, 5000 };
			float meters = nice[0];
			for (float nv : nice) if (nv * m_view.scale <= 110.0f * k) meters = nv;
			const float wpx = meters * m_view.scale;
			const float sx = L.mapA.x + L.pad + 2.0f * k, sy = L.barY - 12.0f * k;
			r->drawLine(ImVec2(sx, sy), ImVec2(sx + wpx, sy), ImColor(0, 0, 0, 200), 4.0f * k);
			r->drawLine(ImVec2(sx, sy), ImVec2(sx + wpx, sy), ImColor(255, 255, 255, 235), 2.0f * k);
			r->drawLine(ImVec2(sx, sy - 4.0f * k), ImVec2(sx, sy + 4.0f * k), ImColor(255, 255, 255, 235), 2.0f * k);
			r->drawLine(ImVec2(sx + wpx, sy - 4.0f * k), ImVec2(sx + wpx, sy + 4.0f * k), ImColor(255, 255, 255, 235), 2.0f * k);
			char sb[24]; if (meters >= 1000.0f) std::snprintf(sb, sizeof(sb), "%d km", (int)(meters / 1000.0f)); else std::snprintf(sb, sizeof(sb), "%d m", (int)meters);
			r->drawText(ImVec2(sx + wpx + 8.0f * k, sy - 7.0f * k), text, sb, true, 11.0f * k);
			// nome da zona no centro do mapa (ex.: "Chinatown")
			const std::string zn = m_zoneCity ? std::string("Cidade Nova") : ZoneLabel(m_view.cx, m_view.cy);
			if (!zn.empty()) r->drawText(ImVec2(sx, sy - 28.0f * k), text, zn, true, 16.0f * k);
		}

		// ---- TV: brilho + linhas de varredura sobre a area acesa, depois solta o recorte ----
		if (tv)
		{
			const int fa = (int)std::lround(235.0f * std::clamp(tvFlash, 0.0f, 1.0f));
			if (fa > 0) r->drawRect(tvA, tvB, ImColor(255, 255, 255, fa), true);
			const int sa = (int)std::lround(80.0f * std::clamp(tvFlash, 0.0f, 1.0f));
			if (sa > 2)
				for (float y = std::floor(tvA.y / (4.0f * k)) * 4.0f * k; y < tvB.y; y += 4.0f * k)
					r->drawRect(ImVec2(tvA.x, y), ImVec2(tvB.x, y + 1.6f * k), ImColor(0, 0, 0, sa), true);
			const int ea = (int)std::lround(200.0f * std::clamp(tvFlash * 1.2f, 0.0f, 1.0f));
			if (ea > 0)    // borda quente nas bordas horizontais da tela ligando
			{
				r->drawRect(ImVec2(tvA.x, tvA.y), ImVec2(tvB.x, tvA.y + 2.0f * k), ImColor(255, 255, 255, ea), true);
				r->drawRect(ImVec2(tvA.x, tvB.y - 2.0f * k), ImVec2(tvB.x, tvB.y), ImColor(255, 255, 255, ea), true);
			}
			r->popClipRect();
		}
	}
}
