#pragma once
// =====================================================================================
//  MapLines - estilo e geometria das LINHAS do mapa (LOGICA PURA: sem ImGui, sem jogo; testada no PC).
//    - categorias de linha (rota GPS, rua, avenida, rodovia + categorias ainda SEM implementacao, so configuraveis)
//    - estilo por categoria (cor, contorno, espessura minima/maxima) lido de SAMP/radar/linhas.ini
//    - geometria: simplificar (Douglas-Peucker), suavizar (Chaikin), espessura pelo zoom, trechos visiveis,
//      ponto mais proximo da rota
//  Usado por WorldMapUI.h (desenho) e RoadGraph.h (classificacao).
// =====================================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "WorldMapCore.h"

namespace WorldMap
{
namespace Lines
{
	enum Category : int
	{
		kRoute = 0,     // linha do GPS (implementada)
		kStreet,        // rua           (camada de ruas: implementada, classificacao por largura = heuristica)
		kAvenue,        // avenida       (idem)
		kHighway,       // rodovia       (idem)
		kDirt,          // estrada de terra      - SEM implementacao: so configuracao
		kPedPath,       // caminho de pedestres  - SEM implementacao
		kWater,         // rota maritima         - SEM implementacao
		kAir,           // rota aerea            - SEM implementacao
		kInterior,      // interiores            - SEM implementacao
		kCount
	};

	struct Rgba { uint8_t r = 255, g = 255, b = 255, a = 255; };

	struct Style
	{
		const char* key = "";      // nome da secao no .ini
		Rgba  main;
		Rgba  outline;
		float minPx = 3.0f;        // espessura da linha principal com o mapa todo visivel (px, antes da escala da interface)
		float maxPx = 8.0f;        // espessura com zoom maximo
		float outlinePx = 1.6f;    // contorno de cada lado
		bool  enabled = true;
		bool  implemented = false; // false = categoria preparada para configuracao posterior (nunca e desenhada)
	};

	struct Styles
	{
		Style cat[kCount];
		bool  roadLayer = false;          // camada com TODAS as ruas (desligada por padrao)
		int   maxSegments = 2500;         // teto de segmentos da camada de ruas por quadro
		int   maxVertices = 24000;        // orcamento de vertices ImGui da camada de ruas (o projeto usa ImDrawIdx de 16 bits: 65536 vertices
		                                  // para a interface INTEIRA num quadro; ver NOTA em WorldMapUI.h DrawRoads)
		float streetMaxWidth = 8.0f;      // largura do no (unidades do jogo) ate a qual e "rua"
		float avenueMaxWidth = 14.0f;     // acima de streetMax e ate aqui = "avenida"; acima = "rodovia"
		float snapMax = 600.0f;           // distancia maxima (unidades) para prender origem/destino a uma rua
		float offRouteDist = 45.0f;       // se o jogador sair da rota mais que isso, recalcula
		bool  smooth = true;              // curvas suaves (Chaikin)
	};

	inline Styles Defaults()
	{
		Styles s;
		struct D { const char* key; Rgba main, out; float mn, mx, op; bool impl; };
		const D d[kCount] = {
			{ "rota",       { 60, 160, 255, 255 }, { 10, 30, 70, 230 },  3.0f, 8.0f, 1.8f, true  },
			{ "rua",        { 210, 215, 225, 200 }, { 20, 24, 34, 150 }, 1.0f, 3.0f, 0.8f, true  },
			{ "avenida",    { 255, 225, 140, 220 }, { 40, 30, 10, 170 }, 1.6f, 4.5f, 1.0f, true  },
			{ "rodovia",    { 255, 170, 90, 235 },  { 50, 20, 5, 190 },  2.2f, 6.0f, 1.2f, true  },
			{ "terra",      { 190, 150, 100, 200 }, { 30, 20, 10, 150 }, 1.0f, 3.0f, 0.8f, false },
			{ "pedestre",   { 150, 220, 150, 180 }, { 10, 40, 10, 140 }, 1.0f, 2.5f, 0.6f, false },
			{ "agua",       { 90, 170, 255, 160 },  { 5, 25, 60, 130 },  1.0f, 3.0f, 0.8f, false },
			{ "ar",         { 230, 230, 255, 140 }, { 30, 30, 60, 120 }, 1.0f, 2.5f, 0.6f, false },
			{ "interior",   { 255, 200, 255, 160 }, { 40, 10, 40, 130 }, 1.0f, 2.5f, 0.6f, false },
		};
		for (int i = 0; i < kCount; i++)
		{
			Style& c = s.cat[i];
			c.key = d[i].key; c.main = d[i].main; c.outline = d[i].out;
			c.minPx = d[i].mn; c.maxPx = d[i].mx; c.outlinePx = d[i].op;
			c.implemented = d[i].impl; c.enabled = true;
		}
		return s;
	}

	// ---------------------------------------------------------------- .ini (linhas.ini)
	inline std::string Trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
		return s.substr(a, b - a);
	}
	inline bool ParseRgba(const std::string& v, Rgba& out)
	{
		int c[4] = { 0, 0, 0, 255 };
		int n = std::sscanf(v.c_str(), " %d , %d , %d , %d", &c[0], &c[1], &c[2], &c[3]);
		if (n < 3) return false;
		auto cl = [](int x) { return (uint8_t)std::min(255, std::max(0, x)); };
		out.r = cl(c[0]); out.g = cl(c[1]); out.b = cl(c[2]); out.a = cl(c[3]);
		return true;
	}
	inline bool ParseBool(const std::string& v) { return v == "1" || v == "on" || v == "sim" || v == "true"; }

	// Formato:  [secao] / chave=valor  (# ou ; comeca comentario). Chave ou valor invalido = aviso, o resto segue.
	//   [geral]   camada_ruas=0|1  max_segmentos=N  max_vertices=N  rua_ate=8  avenida_ate=14  prender_max=600  recalcular_a=45  suavizar=0|1
	//   [rota] [rua] [avenida] [rodovia] [terra] [pedestre] [agua] [ar] [interior]
	//             ativo=0|1  cor=r,g,b[,a]  contorno=r,g,b[,a]  largura_min=N  largura_max=N  contorno_px=N
	inline bool ParseIni(const std::string& text, Styles& st, std::vector<std::string>* warn = nullptr)
	{
		auto W = [&](const std::string& m) { if (warn && warn->size() < 40) warn->push_back(m); };
		int sec = -2;   // -2 nenhuma, -1 geral, 0.. categoria
		size_t pos = 0; int ln = 0;
		while (pos <= text.size())
		{
			size_t e = text.find('\n', pos);
			if (e == std::string::npos) e = text.size();
			std::string line = Trim(text.substr(pos, e - pos));
			pos = e + 1; ln++;
			if (line.empty() || line[0] == '#' || line[0] == ';') continue;
			if (line[0] == '[')
			{
				const size_t c = line.find(']');
				const std::string name = Trim(line.substr(1, c == std::string::npos ? std::string::npos : c - 1));
				sec = -2;
				if (name == "geral") sec = -1;
				else for (int i = 0; i < kCount; i++) if (name == st.cat[i].key) sec = i;
				if (sec == -2) W("linha " + std::to_string(ln) + ": secao desconhecida [" + name + "]");
				continue;
			}
			const size_t eq = line.find('=');
			if (eq == std::string::npos) { W("linha " + std::to_string(ln) + ": falta '='"); continue; }
			const std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			if (sec == -2) { W("linha " + std::to_string(ln) + ": chave fora de secao"); continue; }
			const float fv = (float)std::atof(v.c_str());
			if (sec == -1)
			{
				if (k == "camada_ruas") st.roadLayer = ParseBool(v);
				else if (k == "max_segmentos") st.maxSegments = std::min(8000, std::max(200, (int)fv));
				else if (k == "max_vertices") st.maxVertices = std::min(40000, std::max(4000, (int)fv));
				else if (k == "rua_ate") st.streetMaxWidth = std::max(0.0f, fv);
				else if (k == "avenida_ate") st.avenueMaxWidth = std::max(st.streetMaxWidth, fv);
				else if (k == "prender_max") st.snapMax = std::min(3000.0f, std::max(50.0f, fv));
				else if (k == "recalcular_a") st.offRouteDist = std::min(500.0f, std::max(10.0f, fv));
				else if (k == "suavizar") st.smooth = ParseBool(v);
				else W("linha " + std::to_string(ln) + ": chave desconhecida '" + k + "' em [geral]");
				continue;
			}
			Style& c = st.cat[sec];
			if (k == "ativo") c.enabled = ParseBool(v);
			else if (k == "cor") { if (!ParseRgba(v, c.main)) W("linha " + std::to_string(ln) + ": cor invalida"); }
			else if (k == "contorno") { if (!ParseRgba(v, c.outline)) W("linha " + std::to_string(ln) + ": contorno invalido"); }
			else if (k == "largura_min") c.minPx = std::min(40.0f, std::max(0.5f, fv));
			else if (k == "largura_max") c.maxPx = std::min(40.0f, std::max(0.5f, fv));
			else if (k == "contorno_px") c.outlinePx = std::min(10.0f, std::max(0.0f, fv));
			else W("linha " + std::to_string(ln) + ": chave desconhecida '" + k + "' em [" + c.key + "]");
		}
		for (int i = 0; i < kCount; i++) if (st.cat[i].maxPx < st.cat[i].minPx) st.cat[i].maxPx = st.cat[i].minPx;
		return true;
	}

	// Texto do linhas.ini padrao (gerado quando o arquivo nao existe): todas as categorias, inclusive as ainda sem implementacao.
	inline std::string TemplateText(const Styles& s)
	{
		std::string t;
		t += "# Linhas do mapa Maximo RP. Linhas com # ou ; sao comentarios. Cores: r,g,b[,a] (0-255).\n";
		t += "# Categorias marcadas SEM IMPLEMENTACAO sao lidas mas ainda nao desenhadas (preparadas para configuracao posterior).\n\n";
		char b[256];
		t += "[geral]\n";
		std::snprintf(b, sizeof b, "camada_ruas=%d\nmax_segmentos=%d\nmax_vertices=%d\nrua_ate=%g\navenida_ate=%g\nprender_max=%g\nrecalcular_a=%g\nsuavizar=%d\n\n",
					  s.roadLayer ? 1 : 0, s.maxSegments, s.maxVertices, s.streetMaxWidth, s.avenueMaxWidth, s.snapMax, s.offRouteDist, s.smooth ? 1 : 0);
		t += b;
		for (int i = 0; i < kCount; i++)
		{
			const Style& c = s.cat[i];
			t += std::string("[") + c.key + "]" + (c.implemented ? "" : "   # SEM IMPLEMENTACAO") + "\n";
			std::snprintf(b, sizeof b, "ativo=%d\ncor=%d,%d,%d,%d\ncontorno=%d,%d,%d,%d\nlargura_min=%g\nlargura_max=%g\ncontorno_px=%g\n\n",
						  c.enabled ? 1 : 0, c.main.r, c.main.g, c.main.b, c.main.a, c.outline.r, c.outline.g, c.outline.b, c.outline.a, c.minPx, c.maxPx, c.outlinePx);
			t += b;
		}
		return t;
	}

	// ---------------------------------------------------------------- classificacao por largura do no
	inline Category ClassifyWidth(const Styles& s, float width)
	{
		if (width <= s.streetMaxWidth) return kStreet;
		if (width <= s.avenueMaxWidth) return kAvenue;
		return kHighway;
	}

	// ---------------------------------------------------------------- espessura pelo zoom
	// scale = pixels por unidade de mundo (View::scale). Interpola em escala suave entre o mapa todo e o zoom maximo.
	inline float WidthPx(const Style& st, float scale)
	{
		const float lo = 0.06f, hi = 1.2f;
		float t = (scale - lo) / (hi - lo);
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		t = std::sqrt(t);   // cresce mais rapido no inicio: ja legivel com pouco zoom
		return st.minPx + (st.maxPx - st.minPx) * t;
	}

	// ---------------------------------------------------------------- geometria
	inline float PointSegDist(const Vec2& p, const Vec2& a, const Vec2& b, float* tOut = nullptr)
	{
		const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
		float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0f;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		if (tOut) *tOut = t;
		return Dist(p.x, p.y, a.x + dx * t, a.y + dy * t);
	}

	// Douglas-Peucker iterativo (sem recursao). Mantem primeiro e ultimo ponto.
	inline std::vector<Vec2> Simplify(const std::vector<Vec2>& in, float tol)
	{
		const size_t n = in.size();
		if (n < 3 || tol <= 0.0f) return in;
		std::vector<char> keep(n, 0);
		keep[0] = keep[n - 1] = 1;
		std::vector<std::pair<size_t, size_t>> st;
		st.push_back({ 0, n - 1 });
		while (!st.empty())
		{
			const auto [a, b] = st.back(); st.pop_back();
			if (b <= a + 1) continue;
			float md = -1.0f; size_t mi = a;
			for (size_t i = a + 1; i < b; i++)
			{
				const float d = PointSegDist(in[i], in[a], in[b]);
				if (d > md) { md = d; mi = i; }
			}
			if (md > tol) { keep[mi] = 1; st.push_back({ a, mi }); st.push_back({ mi, b }); }
		}
		std::vector<Vec2> out;
		out.reserve(n);
		for (size_t i = 0; i < n; i++) if (keep[i]) out.push_back(in[i]);
		return out;
	}

	// Chaikin (corte de cantos): curva suave que passa perto dos pontos; extremos preservados. iters 1..3.
	inline std::vector<Vec2> Chaikin(const std::vector<Vec2>& in, int iters, size_t maxPoints = 4096)
	{
		std::vector<Vec2> cur = in;
		for (int it = 0; it < iters; it++)
		{
			if (cur.size() < 3 || cur.size() * 2 > maxPoints) break;
			std::vector<Vec2> nx;
			nx.reserve(cur.size() * 2);
			nx.push_back(cur.front());
			for (size_t i = 0; i + 1 < cur.size(); i++)
			{
				const Vec2& p = cur[i]; const Vec2& q = cur[i + 1];
				if (i > 0) nx.push_back({ p.x * 0.75f + q.x * 0.25f, p.y * 0.75f + q.y * 0.25f });
				if (i + 2 < cur.size()) nx.push_back({ p.x * 0.25f + q.x * 0.75f, p.y * 0.25f + q.y * 0.75f });
			}
			nx.push_back(cur.back());
			cur.swap(nx);
		}
		return cur;
	}

	// Arredonda os CANTOS com curvas de Bezier quadratica, com desvio LIMITADO: a curva nunca se afasta do vertice original mais
	// que ~0,35 * maxCut (para 90 graus). Em ruas isso mantem a linha em cima da pista (o Chaikin comum cortava cantos sem limite).
	// Cada canto usa no maximo metade de cada segmento vizinho (cantos seguidos nao se sobrepoem). Extremos preservados.
	inline std::vector<Vec2> RoundCorners(const std::vector<Vec2>& in, float maxCut, int samples = 4, size_t maxPoints = 4096)
	{
		const size_t n = in.size();
		if (n < 3 || maxCut <= 0.0f || samples < 2) return in;
		std::vector<Vec2> out;
		out.reserve(n * 2);
		out.push_back(in[0]);
		for (size_t i = 1; i + 1 < n; i++)
		{
			const Vec2& a = in[i - 1]; const Vec2& v = in[i]; const Vec2& b = in[i + 1];
			const float l1 = Dist(a.x, a.y, v.x, v.y), l2 = Dist(v.x, v.y, b.x, b.y);
			if (l1 < 1e-3f || l2 < 1e-3f) { out.push_back(v); continue; }
			const float ux = (a.x - v.x) / l1, uy = (a.y - v.y) / l1, wx = (b.x - v.x) / l2, wy = (b.y - v.y) / l2;
			const float cosA = ux * wx + uy * wy;                  // -1 = reta; +1 = volta total
			if (cosA < -0.9986f) { out.push_back(v); continue; }   // ~3 graus: nao precisa arredondar
			const float c = std::min(maxCut, std::min(0.5f * l1, 0.5f * l2));
			const Vec2 p0 = { v.x + ux * c, v.y + uy * c }, p2 = { v.x + wx * c, v.y + wy * c };
			if (out.size() + (size_t)samples + 1 > maxPoints) { out.push_back(v); continue; }
			for (int k = 0; k <= samples; k++)
			{
				const float t = (float)k / (float)samples, m = 1.0f - t;
				out.push_back({ m * m * p0.x + 2.0f * m * t * v.x + t * t * p2.x, m * m * p0.y + 2.0f * m * t * v.y + t * t * p2.y });
			}
		}
		out.push_back(in[n - 1]);
		return out;
	}

	// indice do segmento (i-1,i) mais perto de p; devolve a distancia
	inline float NearestOnPolyline(const std::vector<Vec2>& r, const Vec2& p, size_t* seg = nullptr, float* t = nullptr)
	{
		float best = 1e30f; size_t bs = 1; float bt = 0.0f;
		for (size_t i = 1; i < r.size(); i++)
		{
			float tt = 0.0f;
			const float d = PointSegDist(p, r[i - 1], r[i], &tt);
			if (d < best) { best = d; bs = i; bt = tt; }
		}
		if (seg) *seg = bs;
		if (t) *t = bt;
		return best;
	}

	// Trechos [i0,i1] (inclusivos, em indices de ponto) cujos segmentos tocam o retangulo do MUNDO [x0,x1] x [y0,y1].
	// Evita mandar milhares de pontos fora da tela para o ImGui. Segmentos consecutivos visiveis viram um so trecho.
	inline void VisibleRuns(const std::vector<Vec2>& r, float x0, float y0, float x1, float y1,
							std::vector<std::pair<int, int>>& runs)
	{
		runs.clear();
		int start = -1;
		for (size_t i = 1; i < r.size(); i++)
		{
			const Vec2& a = r[i - 1]; const Vec2& b = r[i];
			const bool vis = !(std::max(a.x, b.x) < x0 || std::min(a.x, b.x) > x1 ||
							   std::max(a.y, b.y) < y0 || std::min(a.y, b.y) > y1);
			if (vis) { if (start < 0) start = (int)i - 1; }
			else if (start >= 0) { runs.push_back({ start, (int)i - 1 }); start = -1; }
		}
		if (start >= 0) runs.push_back({ start, (int)r.size() - 1 });
	}
}
}
