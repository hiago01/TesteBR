// Teste no PC da logica pura do mapa aberto (WorldMapCore.h). Sem jogo, sem celular.
#include "WorldMapCore.h"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace WorldMap;

static int g_ok = 0, g_fail = 0;
static void r(const char* name, bool ok, const char* extra = "")
{
	printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, extra);
	ok ? g_ok++ : g_fail++;
}
static bool near(float a, float b, float e = 0.01f) { return std::fabs(a - b) <= e; }

// leitor/escritor falsos com a mesma interface do RakNet::BitStream (Read(T&) e Read(char*, n))
struct W { std::vector<unsigned char> b;
	template <class T> W& put(T v) { const unsigned char* p = (const unsigned char*)&v; b.insert(b.end(), p, p + sizeof(T)); return *this; }
	W& str(const std::string& s) { put<uint8_t>((uint8_t)s.size()); b.insert(b.end(), s.begin(), s.end()); return *this; } };
struct R { const std::vector<unsigned char>& b; size_t p = 0; R(const std::vector<unsigned char>& v) : b(v) {}
	template <class T> bool Read(T& v) { if (p + sizeof(T) > b.size()) return false; std::memcpy(&v, &b[p], sizeof(T)); p += sizeof(T); return true; }
	bool Read(char* d, int n) { if (p + n > b.size()) return false; std::memcpy(d, &b[p], n); p += n; return true; } };

int main()
{
	// ---- tiles
	{
		float x0, y0, x1, y1;
		TileWorldRect(0, x0, y0, x1, y1);
		r("tile 0 = noroeste", near(x0, -3000) && near(x1, -2500) && near(y1, 3000) && near(y0, 2500));
		TileWorldRect(143, x0, y0, x1, y1);
		r("tile 143 = sudeste", near(x0, 2500) && near(x1, 3000) && near(y1, -2500) && near(y0, -3000));
		TileWorldRect(66, x0, y0, x1, y1);   // linha 5, coluna 6: centro-sul do meio
		r("tile 66 = linha 5 coluna 6", near(x0, 0) && near(y1, 500));
		r("TileAt(2488,-1666) = linha 9 coluna 10 = 118", TileAt(2488, -1666) == 118);
		r("TileAt(0,0) = linha 6 coluna 6 = 78", TileAt(0, 0) == 78);
		r("TileAt fora do mundo = -1", TileAt(-3500, 0) == -1 && TileAt(0, 4000) == -1);
		bool all = true;
		for (int i = 0; i < 144; i++) { TileWorldRect(i, x0, y0, x1, y1); if (TileAt((x0 + x1) / 2, (y0 + y1) / 2) != i) all = false; }
		r("TileAt(centro do tile i) == i para os 144", all);
	}
	// ---- vista
	{
		View v; v.w = 1000; v.h = 600; v.cx = 0; v.cy = 0; v.scale = 0.5f;
		Vec2 s = ToScreen(v, 0, 0);
		r("centro do mundo vai ao centro da area", near(s.x, 500) && near(s.y, 300));
		s = ToScreen(v, 100, 100);
		r("norte (+y) sobe na tela; leste (+x) vai a direita", s.x > 500 && s.y < 300);
		Vec2 w = ToWorld(v, s.x, s.y);
		r("ToWorld inverte ToScreen", near(w.x, 100) && near(w.y, 100));
		View z = v; z.scale = 0.0001f; Clamp(z);
		r("zoom minimo cobre o mundo", near(z.scale, 1000.0f / 6000.0f, 0.0001f));
		z.scale = 100.0f; Clamp(z);
		r("zoom maximo limitado", near(z.scale, 1.6f, 0.0001f));
		View p = v; p.scale = 1.0f; p.cx = 2900; p.cy = -2900; Clamp(p);
		r("nao sai do mundo (canto sudeste)", p.cx + 500 <= 3000.01f && p.cy - 300 >= -3000.01f);
		View q = v; q.scale = 0.5f; q.cx = 300; q.cy = 400;
		Vec2 before = ToWorld(q, 700, 200);
		ZoomAt(q, 700, 200, 1.5f);
		Vec2 after = ToWorld(q, 700, 200);
		r("ZoomAt mantem o ponto sob o dedo parado", near(before.x, after.x, 0.5f) && near(before.y, after.y, 0.5f) && q.scale > 0.5f);
		View d = v; d.cx = 0; d.cy = 0;
		PanPx(d, 50, 0);
		r("arrastar para a direita move o mapa para o oeste (cx diminui)", d.cx < 0);
		PanPx(d, 0, 50);
		r("arrastar para baixo mostra o norte (cy aumenta)", d.cy > 0);
		View big; big.w = 100000; big.h = 100000; big.scale = 1.0f; Clamp(big);
		r("vista maior que o mundo: centraliza sem NaN", std::isfinite(big.cx) && std::isfinite(big.cy) && big.cx == 0 && big.cy == 0);
		auto vt = VisibleTiles(v);
		bool inRange = !vt.empty();
		for (int t : vt) if (t < 0 || t > 143) inRange = false;
		r("tiles visiveis validos (0..143)", inRange);
		View whole; whole.w = 800; whole.h = 800; whole.scale = MinScale(whole); Clamp(whole);
		r("area quadrada com o mundo inteiro: 144 tiles", VisibleTiles(whole).size() == 144);
		View tiny = v; tiny.scale = 1.6f; tiny.cx = 0; tiny.cy = 0; Clamp(tiny);
		r("bem perto: poucos tiles", VisibleTiles(tiny).size() <= 4);
	}
	// ---- gestos
	{
		r("toque curto e parado = tap", IsTap(2, 3, 120, 10));
		r("arrasto longo nao e tap", !IsTap(40, 0, 100, 10));
		r("segurar muito tempo nao e tap", !IsTap(1, 1, 900, 10));
	}
	// ---- locais
	{
		View v; v.w = 800; v.h = 600; v.scale = 0.5f; v.cx = 0; v.cy = 0;
		std::vector<Location> ls(3);
		ls[0].id = 1; ls[0].x = 0;   ls[0].y = 0;
		ls[1].id = 2; ls[1].x = 60;  ls[1].y = 0;      // 30 px a direita do centro
		ls[2].id = 3; ls[2].x = 900; ls[2].y = 900;
		r("toque no marcador acha o mais perto", NearestLocation(v, ls, 400, 300, 20) == 0);
		r("toque entre dois pega o mais perto", NearestLocation(v, ls, 420, 300, 30) == 1);
		r("toque longe de tudo = -1", NearestLocation(v, ls, 100, 100, 20) == -1);
		r("local fora da tela nao e pego", NearestLocation(v, ls, 400, 300, 5) == 0);
	}
	// ---- lista
	{
		r("scroll nao passa do fim", near(ClampScroll(500, 300, 200), 100));
		r("scroll nao fica negativo", near(ClampScroll(-30, 300, 200), 0));
		r("conteudo menor que a janela: 0", near(ClampScroll(50, 100, 200), 0));
	}
	// ---- rota
	{
		std::vector<Vec2> rt = { {0, 0}, {100, 0}, {100, 100} };
		r("comprimento da rota", near(RouteLength(rt), 200));
		r("restante no inicio = total", near(RouteRemaining(rt, 0, 0), 200));
		r("restante no meio do 1o trecho", near(RouteRemaining(rt, 50, 5), 150));
		r("restante perto do fim", near(RouteRemaining(rt, 100, 90), 10));
		r("rota vazia = 0", near(RouteRemaining({}, 1, 1), 0));
		r("formato de distancia: metros", FormatDistance(850) == "850 m");
		r("formato de distancia: km", FormatDistance(1234) == "1.2 km");
	}
	// ---- pacote
	{
		W w; w.put<uint8_t>(1).put<uint16_t>(2)
			.put<uint16_t>(7).put<uint8_t>(3).put<float>(10.5f).put<float>(-20.f).put<float>(5.f).str("Banco Central")
			.put<uint16_t>(9).put<uint8_t>(1).put<float>(1.f).put<float>(2.f).put<float>(3.f).str("Favela\x01X");
		R rd(w.b); Packet p;
		bool ok = ParsePacket(rd, p);
		r("op1 locais: 2 locais lidos", ok && p.locs.size() == 2 && p.locs[0].id == 7 && p.locs[0].icon == 3 && p.locs[0].name == "Banco Central" && near(p.locs[0].x, 10.5f));
		r("op1: caractere de controle no nome vira espaco", ok && p.locs[1].name == "Favela X");
		W e; e.put<uint8_t>(1).put<uint16_t>(0);
		R re(e.b); Packet pe;
		r("op1 vazio limpa", ParsePacket(re, pe) && pe.locs.empty() && pe.op == 1);
		W big; big.put<uint8_t>(1).put<uint16_t>(500);
		R rb(big.b); Packet pb;
		r("op1 com 500 locais rejeitado", !ParsePacket(rb, pb));
		W tr; tr.put<uint8_t>(1).put<uint16_t>(1).put<uint16_t>(1).put<uint8_t>(0).put<float>(1.f);
		R rt(tr.b); Packet pt;
		r("pacote truncado rejeitado (sem alterar a saida)", !ParsePacket(rt, pt) && pt.locs.empty());
		W nan; nan.put<uint8_t>(1).put<uint16_t>(1).put<uint16_t>(1).put<uint8_t>(0).put<float>(NAN).put<float>(0.f).put<float>(0.f).str("x");
		R rn(nan.b); Packet pn;
		r("coordenada NaN rejeitada", !ParsePacket(rn, pn));
		W o; o.put<uint8_t>(2).put<uint8_t>(1);
		R ro(o.b); Packet po;
		r("op2 abrir", ParsePacket(ro, po) && po.open);
		W g; g.put<uint8_t>(3).put<uint8_t>(1).put<float>(100.f).put<float>(-200.f).put<float>(12.f).put<uint16_t>(0xFFFF);
		R rg(g.b); Packet pg;
		r("op3 gps ponto livre", ParsePacket(rg, pg) && pg.gpsOn && pg.gpsLoc == -1 && near(pg.gy, -200));
		W g2; g2.put<uint8_t>(3).put<uint8_t>(1).put<float>(1.f).put<float>(2.f).put<float>(3.f).put<uint16_t>(42);
		R rg2(g2.b); Packet pg2;
		r("op3 gps de um local", ParsePacket(rg2, pg2) && pg2.gpsLoc == 42);
		W rt4; rt4.put<uint8_t>(4).put<uint16_t>(3).put<float>(0.f).put<float>(0.f).put<float>(10.f).put<float>(0.f).put<float>(10.f).put<float>(10.f);
		R rr(rt4.b); Packet pr;
		r("op4 rota com 3 pontos", ParsePacket(rr, pr) && pr.route.size() == 3 && near(RouteLength(pr.route), 20));
		W rt5; rt5.put<uint8_t>(4).put<uint16_t>(600);
		R r5(rt5.b); Packet p5;
		r("op4 com 600 pontos rejeitado", !ParsePacket(r5, p5));
		W bad; bad.put<uint8_t>(99);
		R rbad(bad.b); Packet pbad;
		r("op desconhecido rejeitado", !ParsePacket(rbad, pbad));
	}
	printf("%d ok, %d falhas\n", g_ok, g_fail);
	return g_fail ? 1 : 0;
}
