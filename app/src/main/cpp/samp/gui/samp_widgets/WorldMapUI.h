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

#include <atomic>
#include <cstdio>
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
		virtual ImTextureID tile(int idx) = 0;     // textura do tile radar%02d (0 = indisponivel ainda)
		virtual ImTextureID fullMap() = 0;         // imagem unica opcional do mundo todo (0 = nenhuma)
		virtual void   setGps(float x, float y, float z) = 0;   // blip no radar do jogo
		virtual void   clearGps() = 0;
		virtual void   sendCommand(const char* cmd) = 0;        // comando oculto /mapax ...
		virtual int64_t nowMs() = 0;
		virtual int    tilesLoaded() { return 0; }
		virtual int    mapDim() { return 70; }   // escurecimento (0-255) por cima do mapa; 0 para tiles claros (Google Maps)
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
			ImVec2 scr, panA, panB, mapA, mapB, listA, listB;
			float rowH = 30, barY = 0, barH = 34;
			ImVec2 closeA, closeB, clrA, clrB, zinA, zinB, zoutA, zoutB, ctrA, ctrB;
		};
		enum { BTN_NONE = 0, BTN_CLOSE, BTN_CLEAR, BTN_ZIN, BTN_ZOUT, BTN_CENTER };

		IPlatform* P;
		std::atomic<bool> m_open{ false }, m_flush{ false }, m_sendOpen{ false }, m_sendClose{ false }, m_needInit{ false }, m_resetReq{ false };
		std::atomic<bool> m_claimed[4] = { { false }, { false }, { false }, { false } };
		std::mutex m_mx;
		std::vector<TouchEv> m_events;
		std::vector<Packet>  m_pending;
		int64_t m_lastBack = 0;

		// estado (so a thread de desenho)
		std::vector<Location> m_locs;
		std::vector<Vec2> m_route;
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
		void ResetDrawState();
		void DrainPending();
		void ProcessEvents();
		void OnTap(float x, float y);
		void SelectLocation(int idx, bool fromList);
		void SetFreeGps(float wx, float wy);
		void ClearGps(bool notify);
		void TickGps();
		void Cmd(const char* fmt, int a = 0, int b = 0)
		{
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
		const ImColor dim(0, 0, 0, 125);
		const ImColor panel(20, 22, 28, 232);
		const ImColor mapBg(16, 18, 22, 255);
		const ImColor border(70, 78, 92, 255);
		const ImColor text(236, 238, 244, 255);
		const ImColor mute(150, 156, 170, 255);
		const ImColor blue(30, 110, 232, 255);
		const ImColor blueD(24, 84, 180, 255);
		const ImColor btn(44, 48, 58, 240);
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
		L.panA = ImVec2(L.scr.x * 0.04f, L.scr.y * 0.06f);
		L.panB = ImVec2(L.scr.x * 0.96f, L.scr.y * 0.94f);
		const float listW = Clampf(L.scr.x * 0.21f, 170.0f * k, 270.0f * k);
		L.barH = 34.0f * k;
		L.barY = L.panB.y - L.pad - L.barH;
		L.mapA = ImVec2(L.panA.x + L.pad, L.panA.y + L.pad);
		L.mapB = ImVec2(L.panB.x - L.pad * 2 - listW, L.barY - L.pad * 0.6f);
		L.listA = ImVec2(L.mapB.x + L.pad, L.mapA.y);
		L.listB = ImVec2(L.panB.x - L.pad, L.mapB.y);
		L.rowH = 30.0f * k;
		const float bx = L.panA.x + L.pad;
		L.closeA = ImVec2(bx, L.barY);                    L.closeB = ImVec2(bx + 110.0f * k, L.barY + L.barH);
		L.clrA = ImVec2(L.closeB.x + 8.0f * k, L.barY);   L.clrB = ImVec2(L.clrA.x + 130.0f * k, L.barY + L.barH);
		const float zs = 34.0f * k, zx = L.mapB.x - zs - 8.0f * k;
		L.zinA = ImVec2(zx, L.mapA.y + 8.0f * k);                   L.zinB = ImVec2(zx + zs, L.zinA.y + zs);
		L.zoutA = ImVec2(zx, L.zinB.y + 6.0f * k);                  L.zoutB = ImVec2(zx + zs, L.zoutA.y + zs);
		L.ctrA = ImVec2(zx, L.zoutB.y + 6.0f * k);                  L.ctrB = ImVec2(zx + zs, L.ctrA.y + zs);
		return L;
	}

	inline int Ui::ButtonAt(float x, float y) const
	{
		const Layout& L = m_L;
		if (In(x, y, L.closeA, L.closeB)) return BTN_CLOSE;
		if (m_gps.on && In(x, y, L.clrA, L.clrB)) return BTN_CLEAR;
		if (In(x, y, L.zinA, L.zinB)) return BTN_ZIN;
		if (In(x, y, L.zoutA, L.zoutB)) return BTN_ZOUT;
		if (In(x, y, L.ctrA, L.ctrB)) return BTN_CENTER;
		return BTN_NONE;
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
				m_locs = std::move(p.locs);
				m_selLoc = -1;
				for (size_t i = 0; i < m_locs.size(); i++) if (m_locs[i].id == keepId) m_selLoc = (int)i;
				if (m_gps.on && m_gps.loc >= 0) { /* mantem o GPS mesmo se o local sumiu da lista */ }
			}
			else if (p.op == 3)
			{
				if (!p.gpsOn) { ClearGps(false); }
				else
				{
					m_gps.on = true; m_gps.x = p.gx; m_gps.y = p.gy; m_gps.z = p.gz; m_gps.loc = p.gpsLoc;
					m_route.clear();
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
		m_gps = Gps();
		m_route.clear();
		m_selLoc = -1;
		if (notify) P->sendCommand("/mapax clear");
	}

	inline void Ui::SelectLocation(int idx, bool fromList)
	{
		if (idx < 0 || idx >= (int)m_locs.size()) return;
		const Location& l = m_locs[idx];
		m_selLoc = idx;
		m_gps.on = true; m_gps.x = l.x; m_gps.y = l.y; m_gps.z = l.z; m_gps.loc = l.id;
		m_route.clear();                               // rota antiga nao vale mais; o servidor manda a nova
		P->setGps(l.x, l.y, l.z);
		if (fromList) CenterOn(m_view, l.x, l.y);
		Cmd("/mapax sel %d", l.id);
		Toast("GPS: " + l.name);
	}

	inline void Ui::SetFreeGps(float wx, float wy)
	{
		wx = Clampf(wx, kWorldMin, kWorldMax);
		wy = Clampf(wy, kWorldMin, kWorldMax);
		m_gps.on = true; m_gps.x = wx; m_gps.y = wy; m_gps.z = 0; m_gps.loc = -1;
		m_route.clear();
		m_selLoc = -1;
		P->setGps(wx, wy, 0.0f);
		Cmd("/mapax pos %d %d", (int)std::lround(wx), (int)std::lround(wy));
		Toast("GPS marcado");
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
		if (In(x, y, L.mapA, L.mapB))
		{
			const float rx = x - L.mapA.x, ry = y - L.mapA.y;
			const int hit = NearestLocation(m_view, m_locs, rx, ry, 26.0f * L.k);
			if (hit >= 0) { SelectLocation(hit, false); return; }
			const Vec2 w = ToWorld(m_view, rx, ry);
			SetFreeGps(w.x, w.y);
		}
		else if (In(x, y, L.listA, L.listB))
		{
			const int i = (int)std::floor((y - L.listA.y + m_listScroll) / L.rowH);
			if (i >= 0 && i < (int)m_locs.size()) SelectLocation(i, true);
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
				else if (In(e.x, e.y, L.mapA, L.mapB)) { f.region = 1; }
				else if (In(e.x, e.y, L.listA, L.listB)) { f.region = 2; f.scroll0 = m_listScroll; }
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
				else if (f.region == 2 && f.moved)
				{
					const float total = (float)m_locs.size() * L.rowH, viewH = L.listB.y - L.listA.y;
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
		r->drawRoundedRect(a, b, hot ? ui_colors::blueD : bg, 5.0f * m_L.k, true);
		const ImVec2 ts = r->calculateTextSize(label, fs);
		r->drawText(ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + ((b.y - a.y) - ts.y) * 0.5f), ui_colors::text, label, false, fs);
	}

	inline void Ui::Draw(ImGuiRenderer* r)
	{
		using namespace ui_colors;
		if (m_resetReq.exchange(false))
		{
			if (m_gps.on) P->clearGps();
			m_gps = Gps(); m_locs.clear(); m_route.clear(); m_selLoc = -1; m_listScroll = 0; ResetDrawState();
		}
		if (m_sendClose.exchange(false)) P->sendCommand("/mapax close");
		DrainPending();
		TickGps();

		if (!m_open.load()) { ResetDrawState(); return; }

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

		ProcessEvents();
		if (!m_open.load()) { ResetDrawState(); if (m_sendClose.exchange(false)) P->sendCommand("/mapax close"); return; }
		Clamp(m_view);
		m_listScroll = ClampScroll(m_listScroll, (float)m_locs.size() * L.rowH, L.listB.y - L.listA.y);

		float plx = 0, ply = 0, plh = 0;
		const bool havePlayer = P->player(plx, ply, plh);

		// ---- fundo e painel ----
		r->drawRect(ImVec2(0, 0), L.scr, dim, true);
		r->drawRoundedRect(L.panA, L.panB, panel, 10.0f * k, true);

		// ---- mapa ----
		r->pushClipRect(L.mapA, L.mapB, true);
		r->drawRect(L.mapA, L.mapB, mapBg, true);
		auto S = [&](float wx, float wy) { const Vec2 s = ToScreen(m_view, wx, wy); return ImVec2(L.mapA.x + s.x, L.mapA.y + s.y); };
		int tilesDrawn = 0;
		const ImTextureID full = P->fullMap();
		if (full)
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
		r->drawRect(S(kWorldMin, kWorldMax), S(kWorldMax, kWorldMin), ImColor(255, 255, 255, 40), false, 1.5f);

		// ---- caminho ----
		if (m_gps.on)
		{
			if (m_route.size() >= 2)
			{
				std::vector<ImVec2> pts;
				pts.reserve(m_route.size() + 2);
				if (havePlayer) pts.push_back(S(plx, ply));
				for (const Vec2& p : m_route) pts.push_back(S(p.x, p.y));
				r->drawPolyline(pts.data(), (int)pts.size(), routeOut, false, 7.0f * k);
				r->drawPolyline(pts.data(), (int)pts.size(), route, false, 3.6f * k);
			}
			else if (havePlayer)
			{
				// sem rota do servidor: linha reta tracejada ate o destino
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

		// ---- locais ----
		const bool labels = m_view.scale >= 0.22f;
		for (size_t i = 0; i < m_locs.size(); i++)
		{
			const Location& l = m_locs[i];
			const ImVec2 c = S(l.x, l.y);
			if (c.x < L.mapA.x - 20 || c.x > L.mapB.x + 20 || c.y < L.mapA.y - 20 || c.y > L.mapB.y + 20) continue;
			const bool sel = (int)i == m_selLoc;
			const float s = (sel ? 9.5f : 7.5f) * k;
			r->drawRoundedRect(ImVec2(c.x - s - 2 * k, c.y - s - 2 * k), ImVec2(c.x + s + 2 * k, c.y + s + 2 * k), sel ? gps : ImColor(10, 12, 16, 230), s + 2 * k, true);
			DrawIcon(r, l.icon, c, s * 0.8f, text);
			if (labels || sel)
				r->drawText(ImVec2(c.x + s + 5 * k, c.y - 6.5f * k), text, l.name, true, 11.0f * k);
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
				const float d = m_route.size() >= 2 ? RouteRemaining(m_route, plx, ply) + Dist(plx, ply, m_route.front().x, m_route.front().y) * 0.0f
													 : Dist(plx, ply, m_gps.x, m_gps.y);
				head = "GPS  " + FormatDistance(d) + (m_route.size() >= 2 ? "  (pela rua)" : "  (reta)");
			}
			if (!head.empty())
			{
				const ImVec2 ts = r->calculateTextSize(head, 12.0f * k);
				r->drawRoundedRect(ImVec2(L.mapA.x + 8 * k, L.mapA.y + 8 * k), ImVec2(L.mapA.x + 8 * k + ts.x + 16 * k, L.mapA.y + 8 * k + ts.y + 10 * k), ImColor(10, 12, 16, 215), 5 * k, true);
				r->drawText(ImVec2(L.mapA.x + 16 * k, L.mapA.y + 13 * k), gps, head, false, 12.0f * k);
			}
			if (tilesDrawn == 0)
			{
				const char* msg = "Imagem do mapa indisponivel (tiles do radar nao carregaram - veja [MAPA] no samp_log.txt)";
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

		// ---- lista de locais (nome alinhado a direita + icone) ----
		{
			r->pushClipRect(L.listA, L.listB, true);
			r->drawRect(L.listA, L.listB, ImColor(14, 16, 20, 200), true);
			const float fs = 12.5f * k;
			for (size_t i = 0; i < m_locs.size(); i++)
			{
				const float y = L.listA.y + (float)i * L.rowH - m_listScroll;
				if (y + L.rowH < L.listA.y || y > L.listB.y) continue;
				const bool sel = (int)i == m_selLoc;
				if (sel) r->drawRect(ImVec2(L.listA.x, y), ImVec2(L.listB.x, y + L.rowH), ImColor(30, 110, 232, 110), true);
				else if (i % 2 == 0) r->drawRect(ImVec2(L.listA.x, y), ImVec2(L.listB.x, y + L.rowH), ImColor(255, 255, 255, 8), true);
				const float is = 8.0f * k;
				const ImVec2 ic(L.listB.x - 16.0f * k, y + L.rowH * 0.5f);
				DrawIcon(r, m_locs[i].icon, ic, is, sel ? gps : text);
				std::string nm = m_locs[i].name;
				const float maxW = (L.listB.x - L.listA.x) - 44.0f * k;
				while (nm.size() > 3 && r->calculateTextSize(nm, fs).x > maxW) nm.resize(nm.size() - 1);
				const ImVec2 ts = r->calculateTextSize(nm, fs);
				r->drawText(ImVec2(ic.x - is - 8.0f * k - ts.x, y + (L.rowH - ts.y) * 0.5f), text, nm, false, fs);
			}
			if (m_locs.empty())
				r->drawText(ImVec2(L.listA.x + 12 * k, L.listA.y + 14 * k), mute, "Sem locais", false, 12.0f * k);
			const float total = (float)m_locs.size() * L.rowH, viewH = L.listB.y - L.listA.y;
			if (total > viewH)
			{
				const float th = std::max(24.0f * k, viewH * viewH / total);
				const float ty = L.listA.y + (viewH - th) * (m_listScroll / (total - viewH));
				r->drawRect(ImVec2(L.listB.x - 3 * k, L.listA.y), ImVec2(L.listB.x, L.listB.y), ImColor(255, 255, 255, 22), true);
				r->drawRect(ImVec2(L.listB.x - 3 * k, ty), ImVec2(L.listB.x, ty + th), ImColor(255, 255, 255, 140), true);
			}
			r->popClipRect();
			r->drawRect(L.listA, L.listB, border, false, 1.0f);
		}

		// ---- barra inferior ----
		{
			auto hot = [&](int id) { for (const Finger& f : m_f) if (f.down && f.region == 3 && f.btn == id) return true; return false; };
			DrawButton(r, L.closeA, L.closeB, "FECHAR", blue, 13.0f * k, hot(BTN_CLOSE));
			if (m_gps.on) DrawButton(r, L.clrA, L.clrB, "LIMPAR GPS", btn, 12.0f * k, hot(BTN_CLEAR));
			const char* hint = "Arraste para mover | pinca para zoom | toque para marcar o GPS";
			const ImVec2 ts = r->calculateTextSize(hint, 11.0f * k);
			r->drawText(ImVec2(L.panB.x - L.pad - ts.x, L.barY + (L.barH - ts.y) * 0.5f), mute, hint, false, 11.0f * k);
		}
	}
}
