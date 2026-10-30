#pragma once
// =====================================================================================
//  Mapa aberto (Maximo RP) - LOGICA PURA (sem ImGui, sem jogo): testada no PC.
//    - geometria: mundo (-3000..3000) <-> tela, zoom/arrasto/pinca
//    - tiles do radar nativo (12 x 12, 500 unidades cada; radar00 = noroeste, radar143 = sudeste)
//    - toque: toque simples x arrasto, pinca, selecao de marcador
//    - rota (linha do caminho) e distancia
//    - pacote do servidor (RPC 204) lido por um leitor generico (BitStream real ou falso nos testes)
//  Protocolo servidor -> cliente (RPC 204), 1o byte = op:
//    op 1  LOCAIS     uint16 n | n x [ uint16 id | uint8 icone | float x | float y | float z | uint8 len | char nome[len] ]
//                     (substitui TODA a lista; n = 0 limpa)
//    op 2  ABRIR      uint8 (1 abre, 0 fecha)
//    op 3  GPS        uint8 ligado | float x | float y | float z | uint16 idLocal (0xFFFF = ponto livre)
//    op 4  ROTA       uint16 n | n x [ float x | float y ]   (n = 0 apaga; ate 512 pontos)
//  Cliente -> servidor (comando oculto, mesmo padrao do /invx):
//    /mapax open | close | sel <id> | pos <x> <y> | clear | arrived <id>
// =====================================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace WorldMap
{
	constexpr float kWorldMin = -3000.0f;
	constexpr float kWorldMax = 3000.0f;
	constexpr float kWorldSize = 6000.0f;
	constexpr int   kTiles = 12;               // por lado
	constexpr float kTileSize = 500.0f;
	constexpr int   kMaxLocations = 200;
	constexpr int   kMaxRoutePoints = 512;
	constexpr uint16_t kNoLocation = 0xFFFF;

	struct Vec2 { float x = 0, y = 0; };

	inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline bool  Finite(float v) { return std::isfinite(v) && std::fabs(v) < 100000.0f; }

	// ---------------------------------------------------------------- tiles do radar
	// radarNN: NN = linha * 12 + coluna; linha 0 = norte (y = +3000), coluna 0 = oeste (x = -3000).
	inline void TileWorldRect(int idx, float& x0, float& y0, float& x1, float& y1)
	{
		const int col = idx % kTiles, row = idx / kTiles;
		x0 = kWorldMin + col * kTileSize;
		x1 = x0 + kTileSize;
		y1 = kWorldMax - row * kTileSize;
		y0 = y1 - kTileSize;
	}
	inline int TileAt(float wx, float wy)
	{
		if (wx < kWorldMin || wx >= kWorldMax || wy <= kWorldMin || wy > kWorldMax) return -1;
		const int col = (int)std::floor((wx - kWorldMin) / kTileSize);
		const int row = (int)std::floor((kWorldMax - wy) / kTileSize);
		if (col < 0 || col >= kTiles || row < 0 || row >= kTiles) return -1;
		return row * kTiles + col;
	}

	// ---------------------------------------------------------------- vista (zoom/arrasto)
	struct View
	{
		float cx = 0.0f, cy = 0.0f;   // centro (mundo)
		float scale = 0.2f;           // pixels de tela por unidade de mundo
		float w = 800.0f, h = 600.0f; // area do mapa (pixels)
	};
	inline float MinScale(const View& v) { return std::max(v.w, v.h) / kWorldSize; }   // o mundo cobre toda a area
	inline float MaxScale(const View&)  { return 1.6f; }                               // ~ 500 m na tela
	inline void  Clamp(View& v)
	{
		v.scale = Clampf(v.scale, MinScale(v), MaxScale(v));
		const float halfW = v.w * 0.5f / v.scale, halfH = v.h * 0.5f / v.scale;
		// nao deixa sair do mundo (se a vista for maior que o mundo, centraliza)
		const float minCx = kWorldMin + halfW, maxCx = kWorldMax - halfW;
		const float minCy = kWorldMin + halfH, maxCy = kWorldMax - halfH;
		v.cx = minCx > maxCx ? 0.0f : Clampf(v.cx, minCx, maxCx);
		v.cy = minCy > maxCy ? 0.0f : Clampf(v.cy, minCy, maxCy);
	}
	// p = posicao relativa ao canto superior esquerdo da area do mapa
	inline Vec2 ToScreen(const View& v, float wx, float wy)
	{
		return { v.w * 0.5f + (wx - v.cx) * v.scale, v.h * 0.5f - (wy - v.cy) * v.scale };
	}
	inline Vec2 ToWorld(const View& v, float px, float py)
	{
		return { v.cx + (px - v.w * 0.5f) / v.scale, v.cy - (py - v.h * 0.5f) / v.scale };
	}
	inline void PanPx(View& v, float dx, float dy) { v.cx -= dx / v.scale; v.cy += dy / v.scale; Clamp(v); }
	// zoom mantendo o ponto do mundo sob 'pivot' (px relativo a area) parado
	inline void ZoomAt(View& v, float pivotX, float pivotY, float factor)
	{
		const Vec2 before = ToWorld(v, pivotX, pivotY);
		v.scale = Clampf(v.scale * factor, MinScale(v), MaxScale(v));
		v.cx = before.x - (pivotX - v.w * 0.5f) / v.scale;
		v.cy = before.y + (pivotY - v.h * 0.5f) / v.scale;
		Clamp(v);
	}
	inline void CenterOn(View& v, float wx, float wy) { v.cx = wx; v.cy = wy; Clamp(v); }
	// lista de tiles visiveis (indices 0..143)
	inline std::vector<int> VisibleTiles(const View& v)
	{
		std::vector<int> out;
		const float halfW = v.w * 0.5f / v.scale, halfH = v.h * 0.5f / v.scale;
		const float x0 = v.cx - halfW, x1 = v.cx + halfW, y0 = v.cy - halfH, y1 = v.cy + halfH;
		const int c0 = std::max(0, (int)std::floor((x0 - kWorldMin) / kTileSize));
		const int c1 = std::min(kTiles - 1, (int)std::floor((x1 - kWorldMin) / kTileSize));
		const int r0 = std::max(0, (int)std::floor((kWorldMax - y1) / kTileSize));
		const int r1 = std::min(kTiles - 1, (int)std::floor((kWorldMax - y0) / kTileSize));
		for (int r = r0; r <= r1; r++)
			for (int c = c0; c <= c1; c++) out.push_back(r * kTiles + c);
		return out;
	}

	// ---------------------------------------------------------------- gestos
	// toque simples: quase parado e curto
	inline bool IsTap(float dxPx, float dyPx, long long dtMs, float slopPx)
	{
		return dtMs <= 450 && (dxPx * dxPx + dyPx * dyPx) <= slopPx * slopPx;
	}
	inline float Dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

	// ---------------------------------------------------------------- locais
	struct Location
	{
		int id = 0;
		int icon = 0;
		float x = 0, y = 0, z = 0;
		std::string name;
	};
	// indice do local mais perto do toque (em pixels de tela) dentro do raio; -1 = nenhum
	inline int NearestLocation(const View& v, const std::vector<Location>& locs, float px, float py, float radiusPx)
	{
		int best = -1;
		float bestD = radiusPx;
		for (size_t i = 0; i < locs.size(); i++)
		{
			const Vec2 s = ToScreen(v, locs[i].x, locs[i].y);
			const float d = Dist(s.x, s.y, px, py);
			if (d <= bestD) { bestD = d; best = (int)i; }
		}
		return best;
	}

	// ---------------------------------------------------------------- lista rolavel
	inline float ClampScroll(float scroll, float contentH, float viewH)
	{
		return Clampf(scroll, 0.0f, std::max(0.0f, contentH - viewH));
	}

	// ---------------------------------------------------------------- rota
	inline float RouteLength(const std::vector<Vec2>& r)
	{
		float t = 0;
		for (size_t i = 1; i < r.size(); i++) t += Dist(r[i - 1].x, r[i - 1].y, r[i].x, r[i].y);
		return t;
	}
	// comprimento restante da rota a partir do ponto mais proximo de (px,py)
	inline float RouteRemaining(const std::vector<Vec2>& r, float px, float py)
	{
		if (r.size() < 2) return 0.0f;
		size_t bestSeg = 0;
		float bestD = 1e30f, bestT = 0;
		for (size_t i = 1; i < r.size(); i++)
		{
			const float ax = r[i - 1].x, ay = r[i - 1].y, bx = r[i].x, by = r[i].y;
			const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
			float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
			t = Clampf(t, 0.0f, 1.0f);
			const float d = Dist(px, py, ax + dx * t, ay + dy * t);
			if (d < bestD) { bestD = d; bestSeg = i; bestT = t; }
		}
		float rem = Dist(r[bestSeg - 1].x, r[bestSeg - 1].y, r[bestSeg].x, r[bestSeg].y) * (1.0f - bestT);
		for (size_t i = bestSeg + 1; i < r.size(); i++) rem += Dist(r[i - 1].x, r[i - 1].y, r[i].x, r[i].y);
		return rem;
	}
	inline std::string FormatDistance(float meters)
	{
		char b[24];
		if (meters < 1000.0f) std::snprintf(b, sizeof(b), "%d m", (int)(meters + 0.5f));
		else                  std::snprintf(b, sizeof(b), "%.1f km", meters / 1000.0f);
		return b;
	}

	// ---------------------------------------------------------------- pacote do servidor (RPC 204)
	struct Packet
	{
		uint8_t op = 0;
		std::vector<Location> locs;     // op 1
		bool open = false;              // op 2
		bool gpsOn = false;             // op 3
		float gx = 0, gy = 0, gz = 0;
		int   gpsLoc = -1;
		std::vector<Vec2> route;        // op 4
	};

	template <class BS>
	inline bool ReadStr(BS& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], n)) return false;
		return true;
	}

	// Rejeita pacote truncado, contagem absurda e coordenada fora do mundo/infinita: devolve false e nao muda nada.
	template <class BS>
	inline bool ParsePacket(BS& bs, Packet& out)
	{
		Packet p;
		if (!bs.Read(p.op)) return false;
		if (p.op == 1)
		{
			uint16_t n = 0;
			if (!bs.Read(n) || n > kMaxLocations) return false;
			for (int i = 0; i < n; i++)
			{
				Location l;
				uint16_t id = 0; uint8_t icon = 0;
				if (!bs.Read(id) || !bs.Read(icon) || !bs.Read(l.x) || !bs.Read(l.y) || !bs.Read(l.z) || !ReadStr(bs, l.name)) return false;
				if (!Finite(l.x) || !Finite(l.y) || !Finite(l.z)) return false;
				if (l.name.size() > 40) l.name.resize(40);
				for (char& c : l.name) if ((unsigned char)c < 32) c = ' ';
				l.id = id; l.icon = icon;
				p.locs.push_back(std::move(l));
			}
		}
		else if (p.op == 2)
		{
			uint8_t o = 0;
			if (!bs.Read(o)) return false;
			p.open = o != 0;
		}
		else if (p.op == 3)
		{
			uint8_t on = 0; uint16_t loc = kNoLocation;
			if (!bs.Read(on) || !bs.Read(p.gx) || !bs.Read(p.gy) || !bs.Read(p.gz) || !bs.Read(loc)) return false;
			if (on && (!Finite(p.gx) || !Finite(p.gy) || !Finite(p.gz))) return false;
			p.gpsOn = on != 0;
			p.gpsLoc = loc == kNoLocation ? -1 : (int)loc;
		}
		else if (p.op == 4)
		{
			uint16_t n = 0;
			if (!bs.Read(n) || n > kMaxRoutePoints) return false;
			for (int i = 0; i < n; i++)
			{
				Vec2 v;
				if (!bs.Read(v.x) || !bs.Read(v.y) || !Finite(v.x) || !Finite(v.y)) return false;
				p.route.push_back(v);
			}
		}
		else return false;
		out = std::move(p);
		return true;
	}
}
