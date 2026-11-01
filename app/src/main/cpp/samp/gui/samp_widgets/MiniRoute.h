#pragma once
// =====================================================================================
//  Linha da rota no MINIMAPA (radar nativo). Logica PURA (sem ImGui/jogo): testada no host (testes/test_miniroute.cpp).
//
//  Fluxo (todo quadro com o GPS ligado e o mapa aberto FECHADO):
//    1) Ui::Draw (WorldMapUI.h) publica a rota em coordenadas do MUNDO  -> PublishWorld()
//    2) game/hooks.cpp, logo depois de CHud::DrawRadar() (ainda com o retangulo/transformacao do radar do projeto ativos),
//       converte cada ponto com as funcoes do PROPRIO jogo (CRadar::TransformRealWorldPointToRadarSpace e
//       CRadar::TransformRadarPointToScreenSpace), recorta no circulo do radar e guarda o resultado -> Build()/PublishScreen()
//    3) Ui::Draw desenha as polilinhas ja em pixels (ImGui) por cima do radar -> GetScreen()
//  Espaco do radar = disco de raio 1 centrado no jogador (como em CRadar). Nada aqui depende do jogo.
// =====================================================================================
#include "WorldMapCore.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MiniRoute
{
	using WorldMap::Vec2;

	constexpr float  kClipRadius = 0.965f;   // raio de recorte no espaco do radar (deixa a borda do disco livre)
	constexpr size_t kMaxPoints  = 1500;     // pontos da rota processados por quadro
	constexpr int64_t kFreshMs   = 300;      // o desenho do radar so vale se foi feito ha menos disso

	inline std::atomic<bool> gEnabled{ true };   // /mapa minimapa on|off

	// ------------------------------------------------------------------ recorte no circulo
	// Recorta a polilinha 'in' (espaco do radar) no disco de raio R. Cada trecho continuo dentro do disco vira uma "run".
	inline void ClipToCircle(const std::vector<Vec2>& in, float R, std::vector<std::vector<Vec2>>& out)
	{
		out.clear();
		if (in.size() < 2 || !(R > 0.0f)) return;
		std::vector<Vec2> cur;
		auto flush = [&]() { if (cur.size() >= 2) out.push_back(cur); cur.clear(); };
		const float R2 = R * R;
		for (size_t i = 0; i + 1 < in.size(); i++)
		{
			const Vec2 a = in[i], b = in[i + 1];
			if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y)) { flush(); continue; }
			const float dx = b.x - a.x, dy = b.y - a.y;
			const float A = dx * dx + dy * dy;
			if (A < 1e-12f)   // segmento de comprimento ~0
			{
				if (a.x * a.x + a.y * a.y > R2) flush();
				continue;
			}
			const float B = 2.0f * (a.x * dx + a.y * dy), C = a.x * a.x + a.y * a.y - R2;
			const float disc = B * B - 4.0f * A * C;
			if (disc <= 0.0f) { flush(); continue; }              // reta fora do disco (ou tangente)
			const float sq = std::sqrt(disc);
			float t0 = (-B - sq) / (2.0f * A), t1 = (-B + sq) / (2.0f * A);
			if (t0 < 0.0f) t0 = 0.0f;
			if (t1 > 1.0f) t1 = 1.0f;
			if (t0 >= t1) { flush(); continue; }                   // intervalo dentro do disco nao toca o segmento
			const Vec2 p0 = { a.x + dx * t0, a.y + dy * t0 }, p1 = { a.x + dx * t1, a.y + dy * t1 };
			if (t0 > 0.0f) flush();                                // entrou no disco agora: nova run
			if (cur.empty()) cur.push_back(p0);
			cur.push_back(p1);
			if (t1 < 1.0f) flush();                                // saiu do disco
		}
		flush();
	}

	// ------------------------------------------------------------------ recorte em poligono CONVEXO (mascara real do minimapa)
	// O minimapa do projeto NAO e um circulo: DrawRadarMask e deformado para um "quadrado arredondado" (hooks.cpp,
	// CSprite2d__SetMaskVertices_hook, 8 vertices). A rota precisa ser recortada nesse formato, senao a linha vaza dos cantos.
	inline float Cross(const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

	// Casco convexo (Andrew). Devolve em sentido anti-horario (no sistema de coordenadas dado); a ordem de entrada nao importa.
	inline std::vector<Vec2> ConvexHull(std::vector<Vec2> p)
	{
		std::vector<Vec2> h;
		if (p.size() < 3) return h;
		std::sort(p.begin(), p.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
		p.erase(std::unique(p.begin(), p.end(), [](const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }), p.end());
		if (p.size() < 3) return h;
		h.resize(2 * p.size());
		size_t k = 0;
		for (size_t i = 0; i < p.size(); i++) { while (k >= 2 && Cross(h[k - 2], h[k - 1], p[i]) <= 0) k--; h[k++] = p[i]; }
		for (size_t i = p.size() - 1, t = k + 1; i > 0; i--) { while (k >= t && Cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) k--; h[k++] = p[i - 1]; }
		h.resize(k - 1);
		return h.size() >= 3 ? h : std::vector<Vec2>();
	}

	// Aproxima o poligono do centroide (k < 1 encolhe): margem para a linha nao encostar na moldura do radar.
	inline std::vector<Vec2> ShrinkPoly(const std::vector<Vec2>& poly, float k)
	{
		Vec2 c{ 0, 0 };
		for (const Vec2& v : poly) { c.x += v.x; c.y += v.y; }
		c.x /= (float)poly.size(); c.y /= (float)poly.size();
		std::vector<Vec2> o;
		for (const Vec2& v : poly) o.push_back({ c.x + (v.x - c.x) * k, c.y + (v.y - c.y) * k });
		return o;
	}

	// Cyrus-Beck: parte do segmento a->b dentro do poligono convexo CCW. false = nada dentro.
	inline bool ClipSegConvex(const Vec2& a, const Vec2& b, const std::vector<Vec2>& poly, float& t0, float& t1)
	{
		t0 = 0.0f; t1 = 1.0f;
		const float dx = b.x - a.x, dy = b.y - a.y;
		for (size_t i = 0; i < poly.size(); i++)
		{
			const Vec2& v = poly[i]; const Vec2& w = poly[(i + 1) % poly.size()];
			const float ex = w.x - v.x, ey = w.y - v.y;
			const float num = ex * (a.y - v.y) - ey * (a.x - v.x);     // >= 0 = dentro (lado esquerdo da aresta, CCW)
			const float den = ex * dy - ey * dx;
			if (den == 0.0f) { if (num < 0.0f) return false; continue; }
			const float t = -num / den;
			if (den > 0.0f) { if (t > t0) t0 = t; }
			else { if (t < t1) t1 = t; }
			if (t0 >= t1) return false;
		}
		return t0 < t1;
	}

	inline void ClipToConvex(const std::vector<Vec2>& in, const std::vector<Vec2>& poly, std::vector<std::vector<Vec2>>& out)
	{
		out.clear();
		if (in.size() < 2 || poly.size() < 3) return;
		std::vector<Vec2> cur;
		auto flush = [&]() { if (cur.size() >= 2) out.push_back(cur); cur.clear(); };
		for (size_t i = 0; i + 1 < in.size(); i++)
		{
			const Vec2 a = in[i], b = in[i + 1];
			if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y)) { flush(); continue; }
			float t0, t1;
			if (!ClipSegConvex(a, b, poly, t0, t1)) { flush(); continue; }
			const Vec2 p0 = { a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0 }, p1 = { a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1 };
			if (t0 > 0.0f) flush();
			if (cur.empty()) cur.push_back(p0);
			cur.push_back(p1);
			if (t1 < 1.0f) flush();
		}
		flush();
	}

	// Rota mundo -> pixels recortada no poligono da mascara (poligono ja em pixels, convexo CCW).
	template <class FR, class FS>
	inline void BuildPoly(const std::vector<Vec2>& world, FR toRadar, FS toScreen, const std::vector<Vec2>& poly, std::vector<std::vector<Vec2>>& runs)
	{
		runs.clear();
		if (world.size() < 2 || poly.size() < 3) return;
		const size_t n = world.size() < kMaxPoints ? world.size() : kMaxPoints;
		std::vector<Vec2> sp;
		sp.reserve(n);
		for (size_t i = 0; i < n; i++) sp.push_back(toScreen(toRadar(world[i])));
		ClipToConvex(sp, poly, runs);
	}

	// ------------------------------------------------------------------ rota mundo -> pixels
	// toRadar(Vec2 mundo) -> Vec2 espaco do radar ; toScreen(Vec2 radar) -> Vec2 pixels. Ambos vem do jogo (hooks.cpp).
	template <class FR, class FS>
	inline void Build(const std::vector<Vec2>& world, FR toRadar, FS toScreen, float R, std::vector<std::vector<Vec2>>& runs)
	{
		runs.clear();
		if (world.size() < 2) return;
		std::vector<Vec2> rp;
		const size_t n = world.size() < kMaxPoints ? world.size() : kMaxPoints;
		rp.reserve(n);
		for (size_t i = 0; i < n; i++) rp.push_back(toRadar(world[i]));
		ClipToCircle(rp, R, runs);
		for (auto& run : runs)
			for (Vec2& p : run) p = toScreen(p);
	}

	// ------------------------------------------------------------------ estado compartilhado entre threads
	inline std::mutex gMx;
	inline std::vector<Vec2> gWorld;                      // rota em coordenadas do mundo (a partir do jogador)
	inline bool gStraight = false;                        // true = reta jogador->destino (sem grafo de ruas)
	inline std::vector<std::vector<Vec2>> gScreen;        // polilinhas em pixels
	inline float gCx = 0, gCy = 0, gRadiusPx = 0;         // disco do radar em pixels (centro e raio)
	inline int64_t gScreenStamp = -1000000;

	// Mascara do radar (8 vertices em pixels, gravados por hooks.cpp em CSprite2d__SetMaskVertices_hook, ja deformados)
	inline std::vector<Vec2> gMask;
	inline void SetMask(const float* v, int count)
	{
		std::vector<Vec2> p;
		for (int i = 0; i < count; i++) p.push_back({ v[i * 2], v[i * 2 + 1] });
		std::lock_guard<std::mutex> l(gMx);
		gMask = std::move(p);
	}
	inline std::vector<Vec2> GetMask() { std::lock_guard<std::mutex> l(gMx); return gMask; }

	// Poligono de recorte a partir dos vertices da mascara: valida (dentro do esperado para o disco do radar) e
	// devolve o casco convexo encolhido por 'shrink'. Vazio = usar o circulo.
	inline std::vector<Vec2> MaskPolygon(const std::vector<Vec2>& mask, float cx, float cy, float radiusPx, float shrink)
	{
		if (mask.size() < 4 || !(radiusPx > 1.0f)) return {};
		for (const Vec2& v : mask) if (!std::isfinite(v.x) || !std::isfinite(v.y)) return {};
		std::vector<Vec2> h = ConvexHull(mask);
		if (h.size() < 3) return {};
		Vec2 c{ 0, 0 };
		for (const Vec2& v : h) { c.x += v.x; c.y += v.y; }
		c.x /= (float)h.size(); c.y /= (float)h.size();
		if (std::hypot(c.x - cx, c.y - cy) > 0.25f * radiusPx) return {};     // outro espaco de coordenadas: nao confiar
		float far = 0.0f, nearest = 1e30f;
		for (const Vec2& v : h) { const float d = std::hypot(v.x - c.x, v.y - c.y); far = std::max(far, d); nearest = std::min(nearest, d); }
		if (far > 1.6f * radiusPx || nearest < 0.6f * radiusPx) return {};
		return ShrinkPoly(h, shrink);
	}

	inline void PublishWorld(const std::vector<Vec2>& pts, bool straight)
	{
		std::lock_guard<std::mutex> l(gMx);
		gWorld = pts; gStraight = straight;
	}
	inline void ClearWorld()
	{
		std::lock_guard<std::mutex> l(gMx);
		gWorld.clear(); gStraight = false; gScreen.clear();
	}
	inline bool GetWorld(std::vector<Vec2>& out, bool& straight)
	{
		std::lock_guard<std::mutex> l(gMx);
		if (gWorld.size() < 2) return false;
		out = gWorld; straight = gStraight;
		return true;
	}
	inline void PublishScreen(std::vector<std::vector<Vec2>>&& runs, float cx, float cy, float radiusPx, int64_t now)
	{
		std::lock_guard<std::mutex> l(gMx);
		gScreen = std::move(runs); gCx = cx; gCy = cy; gRadiusPx = radiusPx; gScreenStamp = now;
	}
	inline void ClearScreen() { std::lock_guard<std::mutex> l(gMx); gScreen.clear(); }
	// false = nada para desenhar (radar nao foi desenhado neste quadro, rota vazia...)
	inline bool GetScreen(std::vector<std::vector<Vec2>>& out, float& radiusPx, int64_t now, bool& straight, float* cx = nullptr, float* cy = nullptr)
	{
		std::lock_guard<std::mutex> l(gMx);
		if (gScreen.empty() || now - gScreenStamp > kFreshMs || !(gRadiusPx > 1.0f)) return false;
		out = gScreen; radiusPx = gRadiusPx; straight = gStraight;
		if (cx) *cx = gCx;
		if (cy) *cy = gCy;
		return true;
	}

	// Remove os primeiros 'dist' pixels da polilinha (a linha nao cobre a seta do jogador no centro do radar).
	// Devolve false se sobrar menos de 2 pontos.
	inline bool TrimStart(std::vector<Vec2>& pts, float dist)
	{
		if (pts.size() < 2) return false;
		float left = dist;
		size_t i = 0;
		while (i + 1 < pts.size())
		{
			const float seg = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
			if (seg > left)
			{
				const float t = seg > 0 ? left / seg : 0.0f;
				pts[i] = { pts[i].x + (pts[i + 1].x - pts[i].x) * t, pts[i].y + (pts[i + 1].y - pts[i].y) * t };
				pts.erase(pts.begin(), pts.begin() + (std::ptrdiff_t)i);
				return pts.size() >= 2;
			}
			left -= seg;
			i++;
		}
		return false;     // a polilinha inteira e mais curta que 'dist'
	}
}
