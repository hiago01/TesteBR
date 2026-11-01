#pragma once
// =====================================================================================
//  RoadGraph - grafo de RUAS a partir de NODES0..NODES63.DAT + GPS por A*  (LOGICA PURA: so STL/stdio; testada no PC)
//
//  FORMATO DE CADA NODESn.DAT (conferido no desassembly do libGTASA 2.10 arm64, CPathFind::LoadPathFindData(RwStream*,int)
//  @ 0x3db4ec: cinco RwStreamReadInt32 seguidos, depois leituras de tamanho fixo):
//      cabecalho   5 x int32 : nos, nos_de_veiculo, nos_de_pedestre, nos_de_navegacao, links
//      nos         nos x 28 bytes
//                    +0 u32 (endereco em memoria, ignorado)   +4 u32 (zero)
//                    +8 i16 x   +10 i16 y   +12 i16 z      (mundo = valor / 8)
//                    +14 i16 (0x7FFE)   +16 u16 baseLinkId   +18 u16 areaId   +20 u16 nodeId
//                    +22 u8 largura do caminho   +23 u8 flood-fill   +24 u32 flags (bits 0..3 = numero de links)
//      navegacao   navi x 14 bytes  (pulado: so para IA de pedestres/carros)
//      links       links x 4 bytes {u16 areaId, u16 nodeId}  (+ 0x300 bytes de enchimento)  -> vizinhos de cada no
//      (depois: u16 x links, u8 x (links+192) comprimentos, u8 x (links+192) flags de cruzamento - nao usados aqui)
//  Os nos de VEICULO vem primeiro (indices 0..veiculo-1). So eles entram no grafo (GPS de carro).
//  ONDE ESTAO: o jogo os trata como recursos de streaming "DAT" (RESOURCE_ID_DAT = 25511, 64 ids) dentro dos IMG
//  (texdb/*.IMG, entradas "nodesN.dat"). Tambem aceita arquivo solto SAMP/paths/nodesN.dat (tem prioridade).
//
//  SEGURANCA: tudo e validado (tamanhos, limites, indices). Arquivo ruim = area ignorada + log; nunca derruba o jogo.
//  O grafo guarda so os nos de veiculo em int16 (x8): ~12 mil nos ~ algumas centenas de KB.
// =====================================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <queue>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "MapLines.h"
#include "WorldMapCore.h"

namespace WorldMap
{
namespace Roads
{
	constexpr int   kAreas = 64;
	constexpr int   kAreaGrid = 8;
	constexpr float kAreaSize = 750.0f;
	constexpr uint32_t kMaxNodesPerArea = 40000;     // limite de sanidade (o jogo original tem < 3000 por area)

	// ---------------------------------------------------------------- 1) um arquivo NODESn.DAT
	struct AreaVeh
	{
		int area = -1;
		uint32_t nodesTotal = 0, vehNodes = 0, pedNodes = 0, naviNodes = 0, linkCount = 0;
		std::vector<int16_t> x, y;          // mundo * 8, so nos de veiculo
		std::vector<uint8_t> width;
		struct Edge { uint32_t from; uint16_t toArea, toNode; };
		std::vector<Edge> edges;
		uint32_t idMismatch = 0;            // areaId/nodeId do no diferente do esperado (so informativo)
		uint32_t outsideArea = 0;           // no fora do quadrado da area (so informativo)
		uint32_t badNodes = 0, badLinks = 0;
	};

	inline uint16_t U16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
	inline int16_t  I16(const uint8_t* p) { int16_t v;  std::memcpy(&v, p, 2); return v; }
	inline uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

	inline bool ParseArea(const uint8_t* d, size_t n, int area, AreaVeh& out, std::string& err)
	{
		AreaVeh a;
		a.area = area;
		if (!d || n < 20) { err = "arquivo menor que o cabecalho"; return false; }
		a.nodesTotal = U32(d); a.vehNodes = U32(d + 4); a.pedNodes = U32(d + 8); a.naviNodes = U32(d + 12); a.linkCount = U32(d + 16);
		if (a.nodesTotal > kMaxNodesPerArea || a.vehNodes > a.nodesTotal || a.pedNodes > a.nodesTotal ||
			a.naviNodes > kMaxNodesPerArea * 2u || a.linkCount > kMaxNodesPerArea * 8u)
		{ err = "contagens absurdas no cabecalho"; return false; }
		const uint64_t nodesOff = 20;
		const uint64_t naviOff  = nodesOff + 28ull * a.nodesTotal;
		const uint64_t linksOff = naviOff + 14ull * a.naviNodes;
		const uint64_t need     = linksOff + 4ull * a.linkCount;
		if (need > n) { err = "arquivo truncado (precisa " + std::to_string(need) + ", tem " + std::to_string(n) + ")"; return false; }

		const int col = area % kAreaGrid, row = area / kAreaGrid;
		const float ax0 = kWorldMin + col * kAreaSize, ay0 = kWorldMin + row * kAreaSize;
		a.x.resize(a.vehNodes); a.y.resize(a.vehNodes); a.width.resize(a.vehNodes);
		for (uint32_t i = 0; i < a.vehNodes; i++)
		{
			const uint8_t* r = d + nodesOff + 28ull * i;
			const int16_t x = I16(r + 8), y = I16(r + 10);
			const uint16_t base = U16(r + 16), aid = U16(r + 18), nid = U16(r + 20);
			const uint32_t flags = U32(r + 24);
			const float wx = x * 0.125f, wy = y * 0.125f;
			a.x[i] = x; a.y[i] = y; a.width[i] = r[22];
			if (!Finite(wx) || !Finite(wy) || std::fabs(wx) > 3200.0f || std::fabs(wy) > 3200.0f) { a.badNodes++; a.x[i] = 0; a.y[i] = 0; continue; }
			if (aid != area || nid != i) a.idMismatch++;
			if (wx < ax0 - 120.0f || wx > ax0 + kAreaSize + 120.0f || wy < ay0 - 120.0f || wy > ay0 + kAreaSize + 120.0f) a.outsideArea++;
			const uint32_t cnt = flags & 0xF;
			if ((uint64_t)base + cnt > a.linkCount) { a.badNodes++; continue; }
			for (uint32_t k = 0; k < cnt; k++)
			{
				const uint8_t* l = d + linksOff + 4ull * (base + k);
				const uint16_t la = U16(l), ln = U16(l + 2);
				if (la >= kAreas) { a.badLinks++; continue; }
				a.edges.push_back({ i, la, ln });
			}
		}
		// mais de 2% de nos ruins = arquivo nao e o que pensamos
		if (a.vehNodes > 0 && a.badNodes * 50u > a.vehNodes) { err = "muitos nos invalidos (" + std::to_string(a.badNodes) + "/" + std::to_string(a.vehNodes) + ")"; return false; }
		out = std::move(a);
		return true;
	}

	// ---------------------------------------------------------------- 2) o grafo
	struct Stats
	{
		int areasLoaded = 0, areasMissing = 0, areasBad = 0;
		uint32_t nodes = 0, directedEdges = 0, undirectedEdges = 0, unresolved = 0;
		uint32_t idMismatch = 0, outsideArea = 0, badNodes = 0, badLinks = 0;
		uint32_t widthHist[256] = {};
	};

	class Graph
	{
	public:
		uint32_t Size() const { return (uint32_t)px.size(); }
		bool     Ready() const { return px.size() >= 500; }   // menos que isso = dados incompletos demais
		float    X(uint32_t i) const { return px[i] * 0.125f; }
		float    Y(uint32_t i) const { return py[i] * 0.125f; }
		float    Width(uint32_t i) const { return (float)wd[i]; }
		// vizinhos de i (CSR). und = grafo nao dirigido (ignora mao unica)
		const uint32_t* Begin(uint32_t i, bool und) const { return (und ? adj2.data() : adj.data()) + (und ? off2[i] : off[i]); }
		const uint32_t* End(uint32_t i, bool und) const { return (und ? adj2.data() : adj.data()) + (und ? off2[i + 1] : off[i + 1]); }
		bool HasEdge(uint32_t a, uint32_t b) const { return std::binary_search(Begin(a, false), End(a, false), b); }
		size_t MemoryBytes() const
		{
			return px.capacity() * 2 + py.capacity() * 2 + wd.capacity() + (off.capacity() + adj.capacity() + off2.capacity() + adj2.capacity() +
				   gridOff.capacity() + gridIds.capacity()) * 4;
		}
		const Stats& stats() const { return st; }
		Stats& mutableStats() { return st; }     // so o carregador preenche contadores de areas

		bool Build(std::vector<AreaVeh>& areas)
		{
			Clear();
			if ((int)areas.size() != kAreas) return false;
			uint32_t base[kAreas + 1] = {};
			for (int a = 0; a < kAreas; a++) base[a + 1] = base[a] + (uint32_t)areas[a].x.size();
			const uint32_t N = base[kAreas];
			px.reserve(N); py.reserve(N); wd.reserve(N);
			for (int a = 0; a < kAreas; a++)
			{
				px.insert(px.end(), areas[a].x.begin(), areas[a].x.end());
				py.insert(py.end(), areas[a].y.begin(), areas[a].y.end());
				wd.insert(wd.end(), areas[a].width.begin(), areas[a].width.end());
				for (uint8_t w : areas[a].width) st.widthHist[w]++;
				st.idMismatch += areas[a].idMismatch; st.outsideArea += areas[a].outsideArea;
				st.badNodes += areas[a].badNodes; st.badLinks += areas[a].badLinks;
			}
			st.nodes = N;
			std::vector<uint64_t> dir;
			for (int a = 0; a < kAreas; a++)
			{
				for (const AreaVeh::Edge& e : areas[a].edges)
				{
					if (e.toNode >= areas[e.toArea].x.size()) { st.unresolved++; continue; }
					const uint32_t from = base[a] + e.from, to = base[e.toArea] + e.toNode;
					if (from == to) continue;
					dir.push_back(((uint64_t)from << 32) | to);
				}
				std::vector<AreaVeh::Edge>().swap(areas[a].edges);   // libera ja
			}
			std::sort(dir.begin(), dir.end());
			dir.erase(std::unique(dir.begin(), dir.end()), dir.end());
			Csr(dir, N, off, adj);
			st.directedEdges = (uint32_t)dir.size();
			std::vector<uint64_t> und;
			und.reserve(dir.size() * 2);
			for (uint64_t k : dir) { und.push_back(k); und.push_back((k << 32) | (k >> 32)); }
			std::vector<uint64_t>().swap(dir);
			std::sort(und.begin(), und.end());
			und.erase(std::unique(und.begin(), und.end()), und.end());
			Csr(und, N, off2, adj2);
			st.undirectedEdges = (uint32_t)und.size() / 2;
			BuildGrid();
			return N > 0;
		}

		// no mais perto de (x,y) dentro de maxDist (unidades do jogo); -1 se nao houver
		int Nearest(float x, float y, float maxDist) const
		{
			if (px.empty() || gridOff.empty()) return -1;
			const int cx = CellOf(x), cy = CellOf(y);
			const int maxRing = (int)std::ceil(maxDist / kCell) + 1;
			float best = maxDist * maxDist; int bi = -1;
			for (int r = 0; r <= maxRing && r < kGrid; r++)
			{
				for (int gy = cy - r; gy <= cy + r; gy++)
					for (int gx = cx - r; gx <= cx + r; gx++)
					{
						if (std::max(std::abs(gx - cx), std::abs(gy - cy)) != r) continue;   // so o anel
						if (gx < 0 || gy < 0 || gx >= kGrid || gy >= kGrid) continue;
						const int c = gy * kGrid + gx;
						for (uint32_t k = gridOff[c]; k < gridOff[c + 1]; k++)
						{
							const uint32_t i = gridIds[k];
							const float dx = X(i) - x, dy = Y(i) - y, d2 = dx * dx + dy * dy;
							if (d2 < best) { best = d2; bi = (int)i; }
						}
					}
				if (bi >= 0 && std::sqrt(best) <= (float)r * kCell) break;    // nenhum anel seguinte pode ser mais perto
			}
			return bi;
		}

		// percorre os nos dentro do retangulo do mundo (+margem); f(i)
		template <class F> void ForNodesInRect(float x0, float y0, float x1, float y1, F f) const
		{
			if (gridOff.empty()) return;
			const int c0 = CellOf(x0), c1 = CellOf(x1), r0 = CellOf(y0), r1 = CellOf(y1);
			for (int gy = r0; gy <= r1; gy++)
				for (int gx = c0; gx <= c1; gx++)
				{
					const int c = gy * kGrid + gx;
					for (uint32_t k = gridOff[c]; k < gridOff[c + 1]; k++) f(gridIds[k]);
				}
		}

	private:
		static constexpr int   kGrid = 60;
		static constexpr float kCell = kWorldSize / kGrid;   // 100
		std::vector<int16_t> px, py;
		std::vector<uint8_t> wd;
		std::vector<uint32_t> off, adj, off2, adj2, gridOff, gridIds;
		Stats st;

		static int CellOf(float v) { int c = (int)std::floor((v - kWorldMin) / kCell); return c < 0 ? 0 : (c >= kGrid ? kGrid - 1 : c); }
		void Clear()
		{
			px.clear(); py.clear(); wd.clear(); off.clear(); adj.clear(); off2.clear(); adj2.clear(); gridOff.clear(); gridIds.clear(); st = Stats();
		}
		static void Csr(const std::vector<uint64_t>& e, uint32_t N, std::vector<uint32_t>& o, std::vector<uint32_t>& a)
		{
			o.assign(N + 1, 0); a.resize(e.size());
			for (uint64_t k : e) o[(uint32_t)(k >> 32) + 1]++;
			for (uint32_t i = 0; i < N; i++) o[i + 1] += o[i];
			std::vector<uint32_t> fill(o.begin(), o.end() - 1);
			for (uint64_t k : e) a[fill[(uint32_t)(k >> 32)]++] = (uint32_t)k;   // ja ordenado: cada lista sai ordenada
		}
		void BuildGrid()
		{
			const uint32_t N = Size();
			gridOff.assign(kGrid * kGrid + 1, 0);
			for (uint32_t i = 0; i < N; i++) gridOff[CellOf(X(i)) + CellOf(Y(i)) * kGrid + 1]++;
			for (int c = 0; c < kGrid * kGrid; c++) gridOff[c + 1] += gridOff[c];
			gridIds.resize(N);
			std::vector<uint32_t> fill(gridOff.begin(), gridOff.end() - 1);
			for (uint32_t i = 0; i < N; i++) gridIds[fill[CellOf(X(i)) + CellOf(Y(i)) * kGrid]++] = i;
		}
	};

	// ---------------------------------------------------------------- 3) A*
	struct RouteResult
	{
		bool ok = false;
		bool undirected = false;     // so achou caminho ignorando mao unica
		uint32_t startNode = 0, goalNode = 0, expanded = 0;
		float length = 0.0f;         // comprimento da polyline final (unidades do jogo ~ metros)
		const char* why = "";
	};

	class Search
	{
	public:
		// Rota (sx,sy) -> (tx,ty) pelas ruas. out = polyline (origem, nos..., destino), ja simplificada e suavizada.
		bool Route(const Graph& g, float sx, float sy, float tx, float ty, float snapMax, bool smooth,
				   std::vector<Vec2>& out, RouteResult* rr = nullptr)
		{
			RouteResult res;
			out.clear();
			if (!g.Ready()) { res.why = "grafo indisponivel"; if (rr) *rr = res; return false; }
			const int s = g.Nearest(sx, sy, snapMax), t = g.Nearest(tx, ty, snapMax);
			if (s < 0) { res.why = "origem longe de qualquer rua"; if (rr) *rr = res; return false; }
			if (t < 0) { res.why = "destino longe de qualquer rua"; if (rr) *rr = res; return false; }
			res.startNode = (uint32_t)s; res.goalNode = (uint32_t)t;
			std::vector<uint32_t> path;
			if (!Astar(g, (uint32_t)s, (uint32_t)t, false, path, res.expanded))
			{
				if (!Astar(g, (uint32_t)s, (uint32_t)t, true, path, res.expanded)) { res.why = "sem caminho entre as ruas"; if (rr) *rr = res; return false; }
				res.undirected = true;
			}
			std::vector<Vec2> pts;
			pts.reserve(path.size() + 2);
			pts.push_back({ sx, sy });
			for (uint32_t n : path) pts.push_back({ g.X(n), g.Y(n) });
			pts.push_back({ tx, ty });
			// sem "voltinha": se a origem ja esta alem do 1o no (em direcao ao 2o), descarta o 1o; idem no fim
			if (pts.size() >= 4)
			{
				float tt = 0.0f;
				Lines::PointSegDist(pts[0], pts[1], pts[2], &tt);
				if (tt > 0.0f && tt < 1.0f) pts.erase(pts.begin() + 1);
			}
			if (pts.size() >= 4)
			{
				const size_t n = pts.size();
				float tt = 0.0f;
				Lines::PointSegDist(pts[n - 1], pts[n - 3], pts[n - 2], &tt);
				if (tt > 0.0f && tt < 1.0f) pts.erase(pts.end() - 2);
			}
			pts = Lines::Simplify(pts, 1.2f);
			if (smooth) { pts = Lines::RoundCorners(pts, 14.0f, 4, 3000); pts = Lines::Simplify(pts, 0.5f); }   // simplifica de novo: menos vertices ImGui
			out = std::move(pts);
			res.length = RouteLength(out);
			res.ok = true;
			if (rr) *rr = res;
			return true;
		}

	private:
		std::vector<float> gs;
		std::vector<uint32_t> prev, stamp;
		uint32_t gen = 0;

		bool Astar(const Graph& g, uint32_t s, uint32_t t, bool und, std::vector<uint32_t>& path, uint32_t& expanded)
		{
			const uint32_t N = g.Size();
			if (gs.size() != N) { gs.assign(N, 0.0f); prev.assign(N, 0); stamp.assign(N, 0); gen = 0; }
			if (++gen == 0) { std::fill(stamp.begin(), stamp.end(), 0); gen = 1; }
			using QE = std::pair<float, uint32_t>;
			std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
			auto H = [&](uint32_t i) { return Dist(g.X(i), g.Y(i), g.X(t), g.Y(t)); };
			gs[s] = 0.0f; stamp[s] = gen; prev[s] = s;
			pq.push({ H(s), s });
			uint32_t exp = 0;
			while (!pq.empty())
			{
				const QE top = pq.top(); pq.pop();
				const uint32_t u = top.second;
				if (top.first > gs[u] + H(u) + 0.01f) continue;      // entrada velha
				exp++;
				if (u == t)
				{
					path.clear();
					for (uint32_t c = t;; c = prev[c]) { path.push_back(c); if (c == s) break; if (path.size() > N) return false; }
					std::reverse(path.begin(), path.end());
					expanded += exp;
					return true;
				}
				if (exp > N + 8) break;                               // seguranca
				for (const uint32_t* p = g.Begin(u, und); p != g.End(u, und); ++p)
				{
					const uint32_t v = *p;
					const float ng = gs[u] + Dist(g.X(u), g.Y(u), g.X(v), g.Y(v));
					if (stamp[v] != gen || ng < gs[v]) { stamp[v] = gen; gs[v] = ng; prev[v] = u; pq.push({ ng + H(v), v }); }
				}
			}
			expanded += exp;
			return false;
		}
	};

	// ---------------------------------------------------------------- 4) segmentos para a camada de ruas
	// Chama f(ax,ay,bx,by,categoria) para cada rua visivel (uma vez por trecho; mao unica tambem). Para em maxSeg.
	// onlyCat >= 0 = so essa categoria (permite dar prioridade: rodovias primeiro).
	// Retorna quantos foram emitidos.
	template <class F>
	inline int ForSegments(const Graph& g, const Lines::Styles& styles, float x0, float y0, float x1, float y1, int maxSeg, int onlyCat, F f)
	{
		int n = 0;
		const float m = 200.0f;   // margem: trechos longos cujo no de origem esta fora da tela
		g.ForNodesInRect(x0 - m, y0 - m, x1 + m, y1 + m, [&](uint32_t i) {
			if (n >= maxSeg) return;
			for (const uint32_t* p = g.Begin(i, false); p != g.End(i, false); ++p)
			{
				const uint32_t j = *p;
				if (j < i && g.HasEdge(j, i)) continue;     // o par (j,i) ja foi (ou sera) emitido por j
				const Lines::Category c = Lines::ClassifyWidth(styles, std::max(g.Width(i), g.Width(j)));
				if (onlyCat >= 0 && (int)c != onlyCat) continue;
				if (!styles.cat[c].enabled || !styles.cat[c].implemented) continue;
				f(g.X(i), g.Y(i), g.X(j), g.Y(j), c);
				if (++n >= maxSeg) return;
			}
		});
		return n;
	}

	// ---------------------------------------------------------------- 5) IMG VER2 e carga
	struct ImgEntry { uint32_t off = 0; uint32_t sectors = 0; };

	inline bool NodesNameArea(const char* name, int& area)
	{
		// "nodes<N>.dat" (sem diferenciar maiusculas), N = 0..63, sem zeros a esquerda
		if (strncasecmp(name, "nodes", 5) != 0) return false;
		const char* p = name + 5;
		if (*p < '0' || *p > '9') return false;
		int v = 0, digits = 0;
		while (*p >= '0' && *p <= '9' && digits < 3) { v = v * 10 + (*p - '0'); p++; digits++; }
		if (strncasecmp(p, ".dat", 4) != 0 || (p[4] != 0)) return false;
		if (digits > 1 && name[5] == '0') return false;
		if (v < 0 || v >= kAreas) return false;
		area = v;
		return true;
	}

	// Le o diretorio de um IMG VER2 e anota as entradas nodesN.dat. Retorna quantas achou (-1 = nao e VER2/ilegivel).
	inline int ImgFindNodes(FILE* f, ImgEntry out[kAreas], bool have[kAreas])
	{
		char magic[4]; uint32_t cnt = 0;
		if (std::fread(magic, 1, 4, f) != 4 || std::fread(&cnt, 4, 1, f) != 1) return -1;
		if (std::memcmp(magic, "VER2", 4) != 0 || cnt == 0 || cnt > 400000) return -1;
		int found = 0;
		for (uint32_t i = 0; i < cnt; i++)
		{
			uint8_t e[32];
			if (std::fread(e, 1, 32, f) != 32) break;
			char name[25]; std::memcpy(name, e + 8, 24); name[24] = 0;
			int a;
			if (!NodesNameArea(name, a) || have[a]) continue;
			const uint16_t s1 = U16(e + 4), s2 = U16(e + 6);
			out[a].off = U32(e); out[a].sectors = s2 ? s2 : s1;       // igual ao jogo: +6 se nao-zero, senao +4
			have[a] = true; found++;
		}
		return found;
	}

	inline bool ReadAll(const std::string& path, std::vector<uint8_t>& buf, size_t maxBytes)
	{
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) return false;
		std::fseek(f, 0, SEEK_END);
		const long sz = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (sz <= 0 || (size_t)sz > maxBytes) { std::fclose(f); return false; }
		buf.resize((size_t)sz);
		const bool ok = std::fread(buf.data(), 1, buf.size(), f) == buf.size();
		std::fclose(f);
		return ok;
	}

	inline bool HasImgExt(const std::string& n)
	{
		return n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".img") == 0;
	}

	// root = pasta de dados do jogo (g_pszStorage, termina em '/'). Procura SAMP/paths/nodesN.dat solto e texdb/*.img.
	// log recebe linhas de diagnostico. Retorna true se o grafo ficou utilizavel.
	inline bool LoadAll(const std::string& root, Graph& g, std::vector<std::string>& log)
	{
		auto L = [&](const std::string& s) { if (log.size() < 80) log.push_back(s); };
		std::vector<AreaVeh> areas(kAreas);
		std::vector<std::string> imgs;            // IMGs candidatos (gta3 primeiro)
		if (DIR* d = opendir((root + "texdb").c_str()))
		{
			std::vector<std::string> names;
			while (dirent* e = readdir(d)) { const std::string n = e->d_name; if (HasImgExt(n)) names.push_back(n); }
			closedir(d);
			std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
				const bool ga = strncasecmp(a.c_str(), "gta3", 4) == 0, gb = strncasecmp(b.c_str(), "gta3", 4) == 0;
				if (ga != gb) return ga;
				return a < b;
			});
			for (const std::string& n : names) imgs.push_back(root + "texdb/" + n);
		}
		ImgEntry ent[kAreas]; int entImg[kAreas]; bool have[kAreas] = {};
		for (int a = 0; a < kAreas; a++) entImg[a] = -1;
		for (size_t k = 0; k < imgs.size(); k++)
		{
			FILE* f = std::fopen(imgs[k].c_str(), "rb");
			if (!f) continue;
			bool before[kAreas]; std::memcpy(before, have, sizeof(have));
			const int n = ImgFindNodes(f, ent, have);
			std::fclose(f);
			for (int a = 0; a < kAreas; a++) if (have[a] && !before[a]) entImg[a] = (int)k;
			if (n > 0) L("[NODES] " + imgs[k].substr(root.size()) + ": " + std::to_string(n) + " arquivos nodesN.dat no diretorio");
		}
		std::vector<uint8_t> buf;
		int loose = 0, missing = 0, bad = 0, ok = 0;
		std::string missList;
		for (int a = 0; a < kAreas; a++)
		{
			buf.clear();
			bool got = false;
			const std::string lp = root + "SAMP/paths/nodes" + std::to_string(a) + ".dat";
			if (ReadAll(lp, buf, 8u << 20)) { got = true; loose++; }
			else if (entImg[a] >= 0)
			{
				FILE* f = std::fopen(imgs[entImg[a]].c_str(), "rb");
				if (f)
				{
					const size_t bytes = (size_t)ent[a].sectors * 2048u;
					if (bytes > 0 && bytes <= (8u << 20) && std::fseek(f, (long)((uint64_t)ent[a].off * 2048u), SEEK_SET) == 0)
					{
						buf.resize(bytes);
						const size_t r = std::fread(buf.data(), 1, bytes, f);
						buf.resize(r);          // ultimo arquivo do IMG pode vir cortado: o parser valida o tamanho
						got = r > 0;
					}
					std::fclose(f);
				}
			}
			if (!got) { missing++; if (missList.size() < 80) missList += std::to_string(a) + " "; continue; }
			std::string err;
			if (!ParseArea(buf.data(), buf.size(), a, areas[a], err)) { bad++; L("[NODES] area " + std::to_string(a) + " rejeitada: " + err); areas[a] = AreaVeh(); continue; }
			ok++;
		}
		const bool built = g.Build(areas);
		Stats& s = g.mutableStats();
		s.areasLoaded = ok; s.areasMissing = missing; s.areasBad = bad;
		L("[NODES] areas ok=" + std::to_string(ok) + " (soltas=" + std::to_string(loose) + ") faltando=" + std::to_string(missing) +
		  (missList.empty() ? "" : " [" + missList + "]") + " rejeitadas=" + std::to_string(bad));
		L("[NODES] nos de veiculo=" + std::to_string(s.nodes) + " ligacoes=" + std::to_string(s.directedEdges) + " (sem mao unica=" +
		  std::to_string(s.undirectedEdges) + ") sem_destino=" + std::to_string(s.unresolved) + " memoria~" + std::to_string(g.MemoryBytes() / 1024) + " KB");
		L("[NODES] conferencia: id!=esperado=" + std::to_string(s.idMismatch) + " fora_da_area=" + std::to_string(s.outsideArea) +
		  " nos_ruins=" + std::to_string(s.badNodes) + " links_ruins=" + std::to_string(s.badLinks));
		{   // histograma das larguras (para ajustar rua_ate/avenida_ate no linhas.ini)
			std::string h = "[NODES] largura do caminho (valor:qtd):";
			int shown = 0;
			for (int w = 0; w < 256 && shown < 12; w++) if (s.widthHist[w] > 0) { h += " " + std::to_string(w) + ":" + std::to_string(s.widthHist[w]); shown++; }
			L(h);
		}
		return built && g.Ready();
	}
}
}
