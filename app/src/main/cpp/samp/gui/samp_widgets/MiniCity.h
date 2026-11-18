#pragma once
// =====================================================================================
//  Minimapa da CIDADE NOVA (HUD). Logica PURA (sem ImGui/jogo): testada no host (g++, sem ImGui).
//
//  Dentro da cidade (fora do mapa original +-3000) o radar nativo mostra so o "fora do mundo" azul. Aqui a imagem da cidade
//  (SAMP/radar/cidade) e desenhada POR CIMA do radar, no mesmo formato/rotacao/zoom dele:
//    1) Ui::Draw (WorldMapUI.h) diz se o jogador esta na cidade                       -> gInCity
//    2) game/hooks.cpp, logo depois de CHud::DrawRadar(): le a posicao do jogador e, com as funcoes do PROPRIO jogo
//       (TransformRealWorldPointToRadarSpace/TransformRadarPointToScreenSpace), mede como o mundo vira pixels
//       (a transformacao do radar e AFIM: rotacao da camera + zoom + deslocamento)    -> PublishXf()
//    3) Ui::DrawMiniCity desenha os tiles/imagem como quadrilateros girados, recortados no poligono da mascara do radar
//  Como a imagem cobre a seta do jogador (desenhada pelo radar nativo), a seta e redesenhada aqui (rumo do veiculo).
// =====================================================================================
#include "WorldMapCore.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MiniCity
{
	using WorldMap::Vec2;
	constexpr int64_t kFreshMs = 300;

	inline std::atomic<bool> gEnabled{ true };    // /mapa minimapa cidade on|off
	inline std::atomic<bool> gInCity{ false };    // jogador dentro da cidade (publicado pela interface)

	struct Xf
	{
		bool ok = false;
		float wx = 0, wy = 0;          // ponto do mundo (jogador) usado como origem
		float sx = 0, sy = 0;          // onde ele cai na tela (pixels)
		float ax = 0, ay = 0;          // pixels por unidade de mundo em +X
		float bx = 0, by = 0;          // pixels por unidade de mundo em +Y
		float cx = 0, cy = 0, rad = 0; // centro e raio do disco do radar (pixels)
		float hdx = 0, hdy = 0;        // direcao do veiculo/jogador em pixels (vetor unitario); 0,0 = desconhecida
		std::vector<Vec2> poly;        // mascara do radar (convexo, mesmo sentido do MiniRoute::MaskPolygon)
		int64_t stamp = -1000000;
	};

	inline std::mutex gMx;
	inline Xf gXf;

	inline void PublishXf(Xf&& x, int64_t now) { std::lock_guard<std::mutex> l(gMx); gXf = std::move(x); gXf.stamp = now; gXf.ok = true; }
	inline void ClearXf() { std::lock_guard<std::mutex> l(gMx); gXf.ok = false; }
	inline bool GetXf(Xf& out, int64_t now)
	{
		std::lock_guard<std::mutex> l(gMx);
		if (!gXf.ok || now - gXf.stamp > kFreshMs || gXf.poly.size() < 3) return false;
		out = gXf;
		return true;
	}

	// mundo -> pixels
	inline Vec2 Apply(const Xf& t, float wx, float wy) { return { t.sx + (wx - t.wx) * t.ax + (wy - t.wy) * t.bx, t.sy + (wx - t.wx) * t.ay + (wy - t.wy) * t.by }; }
	// metros por pixel (transformacao com escala uniforme); 0 = invalida
	inline float WorldPerPx(const Xf& t) { const float s = std::hypot(t.ax, t.ay); return s > 1e-9f ? 1.0f / s : 0.0f; }

	// Monta Xf a partir de 3 pontos medidos pelo jogo: P0 e P0 + (d,0) e P0 + (0,d) ja em pixels.
	inline Xf Make(float wx, float wy, Vec2 s0, Vec2 sx, Vec2 sy, float d)
	{
		Xf t;
		t.wx = wx; t.wy = wy; t.sx = s0.x; t.sy = s0.y;
		t.ax = (sx.x - s0.x) / d; t.ay = (sx.y - s0.y) / d;
		t.bx = (sy.x - s0.x) / d; t.by = (sy.y - s0.y) / d;
		return t;
	}

	// ---- recorte de poligono TEXTURIZADO num poligono convexo (Sutherland-Hodgman). Dentro = lado esquerdo da aresta (como MiniRoute::Cross >= 0).
	struct V { Vec2 p; Vec2 uv; };
	inline float Side(const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
	inline void ClipTex(const std::vector<V>& in, const std::vector<Vec2>& clip, std::vector<V>& out)
	{
		std::vector<V> cur = in, nxt;
		for (size_t i = 0; i < clip.size() && !cur.empty(); i++)
		{
			const Vec2& a = clip[i]; const Vec2& b = clip[(i + 1) % clip.size()];
			nxt.clear();
			for (size_t j = 0; j < cur.size(); j++)
			{
				const V& s = cur[j]; const V& e = cur[(j + 1) % cur.size()];
				const float ds = Side(a, b, s.p), de = Side(a, b, e.p);
				const bool sin = ds >= 0.0f, ein = de >= 0.0f;
				if (sin != ein)
				{
					const float t = ds / (ds - de);
					nxt.push_back({ { s.p.x + (e.p.x - s.p.x) * t, s.p.y + (e.p.y - s.p.y) * t }, { s.uv.x + (e.uv.x - s.uv.x) * t, s.uv.y + (e.uv.y - s.uv.y) * t } });
				}
				if (ein) nxt.push_back(e);
			}
			cur.swap(nxt);
		}
		out = cur;
	}

	inline bool InsideConvex(const std::vector<Vec2>& poly, float x, float y)
	{
		for (size_t i = 0; i < poly.size(); i++) if (Side(poly[i], poly[(i + 1) % poly.size()], { x, y }) < 0.0f) return false;
		return poly.size() >= 3;
	}
}
