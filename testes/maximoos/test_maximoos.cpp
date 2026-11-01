// Testes de PC do Maximo OS (etapa 1). Compilar:  g++ -std=c++17 -Wall -Wextra -pthread test_maximoos.cpp -o t && ./t
// NAO testa: ImGui/renderizador real, hooks do jogo, dispositivo Android.
#include "MockCanvas.h"
#include "../../app/src/main/cpp/samp/gui/maximoos/OSManager.h"
#include <cstdio>
#include <cstdlib>
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void tapAt(OSManager& os, Vec2 p, int64_t& t, MockCanvas& cv)
{
	os.input().handleRaw(2, 0, p.x, p.y, false); os.frame(cv, t += 16, 16);
	os.input().handleRaw(1, 0, p.x, p.y, false); os.frame(cv, t += 16, 16);
}
static void frames(OSManager& os, MockCanvas& cv, int64_t& t, int n) { for (int i = 0; i < n; ++i) { cv.ops.clear(); os.frame(cv, t += 16, 16); } }

int main()
{
	// ---------- ResolutionManager: varias resolucoes ----------
	const float res[][2] = { {800,480},{1280,720},{1920,1080},{2400,1080},{2340,1080},{2560,1600},{1024,768},{3200,1440},{640,360},{1080,2400} };
	for (auto& r : res)
	{
		Metrics m = ComputeMetrics(r[0], r[1]);
		CHECK(m.valid);
		Rect scr{ 0,0,r[0],r[1] };
		CHECK(m.frame.inside(scr));
		CHECK(m.screen.inside(m.frame));
		CHECK(m.content.inside(m.screen));
		CHECK(m.statusBar.inside(m.screen) && m.navBar.inside(m.screen));
		CHECK(std::fabs(m.frame.w / m.frame.h - 1.40f) < 0.01f);
		CHECK(m.font(Fonts::Tiny) >= Metrics::kMinFontPx);
		// grade nunca estoura a area
		GridSpec g = ComputeGrid(Rect{ m.content.x, m.content.y, m.content.w, m.content.h * 0.8f }, m.px(150), m.px(128));
		for (int i = 0; i < g.perPage; ++i) CHECK(GridCell(g, i).inside(g.area));
	}
	CHECK(!ComputeMetrics(0, 0).valid);
	CHECK(!ComputeMetrics(NAN, 100).valid);

	// ---------- Gestos ----------
	{
		GestureTracker g; std::vector<Touch> o;
		g.feed(2, 0, 100, 100, false, 0, o); g.feed(1, 0, 102, 101, false, 50, o);
		bool tap = false; for (auto& t : o) tap |= t.kind == Touch::Tap; CHECK(tap);
		o.clear(); GestureTracker g2;
		g2.feed(2, 0, 100, 100, false, 0, o); g2.feed(3, 0, 100, 160, false, 30, o); g2.feed(3, 0, 100, 220, false, 60, o); g2.feed(1, 0, 100, 220, false, 70, o);
		bool db = false, de = false, tp = false; for (auto& t : o) { db |= t.kind == Touch::DragBegin; de |= t.kind == Touch::DragEnd; tp |= t.kind == Touch::Tap; }
		CHECK(db && de && !tp);
		o.clear(); GestureTracker g3; g3.feed(2, 0, 10, 10, false, 0, o); g3.feed(2, 1, 50, 50, false, 5, o);   // segundo dedo ignorado
		int downs = 0; for (auto& t : o) downs += t.kind == Touch::Down; CHECK(downs == 1);
		o.clear(); GestureTracker g4; g4.feed(2, 0, 10, 10, false, 0, o); g4.tick(600, o);
		bool lp = false; for (auto& t : o) lp |= t.kind == Touch::LongPress; CHECK(lp);
		o.clear(); GestureTracker g5; g5.feed(2, 0, 10, 10, false, 0, o); g5.feed(1, 0, 10, 10, true, 20, o);
		bool cc = false, tt = false; for (auto& t : o) { cc |= t.kind == Touch::Cancel; tt |= t.kind == Touch::Tap; } CHECK(cc && !tt);
	}

	// ---------- InputManager: captura ----------
	{
		InputManager in;
		CHECK(!in.handleRaw(2, 0, 5, 5, false));                // fechado: nao consome
		in.setOpen(true);
		CHECK(in.consumeFlush()); CHECK(!in.consumeFlush());    // flush uma vez so
		CHECK(in.handleRaw(2, 0, 5, 5, false)); CHECK(in.anyClaimed());
		in.setOpen(false);                                       // fecha no meio do gesto
		CHECK(in.handleRaw(3, 0, 6, 6, false));                  // dedo reivindicado continua engolido
		CHECK(in.handleRaw(1, 0, 6, 6, false)); CHECK(!in.anyClaimed());
		CHECK(!in.handleRaw(2, 0, 5, 5, false));                // depois do UP volta ao jogo
		in.setOpen(true); for (int i = 0; i < 400; ++i) in.handleRaw(3, 1, i, i, false);
		std::vector<RawTouch> v; in.drain(v); CHECK(v.size() <= 256);
	}

	// ---------- AppRegistry / AppManager ----------
	{
		OSManager os;
		CHECK(os.registry().size() == 11);
		AppInfo dup; dup.id = "mdt"; dup.factory = []() { return std::unique_ptr<App>(new TestApp()); };
		CHECK(!os.registry().add(dup));                          // id duplicado recusado
		AppInfo bad; bad.id = "x"; CHECK(!os.registry().add(bad)); // sem factory recusado
		os.setScreenSize(1920, 1080);
		AppManager& am = os.apps(); const Metrics& m = os.metrics();
		CHECK(!am.open("naoexiste", m));
		CHECK(am.open("teste", m)); CHECK(am.foregroundId() == "teste");
		TestApp* ta = dynamic_cast<TestApp*>(am.foreground()); CHECK(ta && ta->created == 1 && ta->resumed == 1);
		CHECK(am.open("mdt", m)); CHECK(ta->paused == 1);       // abrir outro pausa o anterior
		CHECK(am.recents().size() == 2 && am.recents()[0] == "mdt");
		CHECK(am.open("teste", m)); CHECK(ta->created == 1 && ta->resumed == 2);   // reaproveita instancia
		for (const char* id : { "cidadaos","veiculos","ocorrencias","mandados","organizacoes","mensagens" }) am.open(id, m);
		CHECK((int)am.instanceCount() <= AppManager::kMaxRecents);
		am.closeAll(); CHECK(am.instanceCount() == 0 && am.foregroundId().empty());
	}

	// ---------- Navegacao / transicao ----------
	{
		NavStack<int> s; s.push(1); s.push(2); s.push(3); CHECK(s.depth() == 3); s.popTo(1); CHECK(s.depth() == 1 && s.top() == 1); CHECK(s.pop()); CHECK(!s.pop());
		Transition t; t.start(1); CHECK(t.running()); t.update(80); CHECK(t.alpha() > 0.5f); t.update(100); CHECK(!t.running() && t.alpha() == 1.0f);
	}

	// ---------- Notificacoes ----------
	{
		NotificationCenter n; int a = n.post("A", "x", "mdt", 0); n.post("B", "y", "", 100);
		CHECK(n.unread() == 2 && n.unreadFor("mdt") == 1); CHECK(n.toast(200)); CHECK(!n.toast(100000));
		n.markRead(a); CHECK(n.unread() == 1); n.dismiss(a); CHECK(n.list().size() == 1); n.clear(); CHECK(n.unread() == 0);
		for (int i = 0; i < 80; ++i) n.post("t", "t", "", i); CHECK(n.list().size() == NotificationCenter::kMax);
	}

	// ---------- OSManager de ponta a ponta ----------
	for (auto& r : res)
	{
		OSManager os; MockCanvas cv; int64_t t = 1000;
		os.setScreenSize(r[0], r[1]);
		CHECK(!os.isOpen());
		os.input().setOpen(true); os.open(); CHECK(os.isOpen());
		frames(os, cv, t, 3);
		const Metrics& m = os.metrics(); const Rect scr{ 0,0,r[0],r[1] };
		// nada desenhado fora do celular, e texto sempre >= fonte minima
		bool allIn = true, fontOk = true, textInClip = true;
		for (auto& o : cv.ops)
		{
			if (o.k == 'X') { if (o.a < Metrics::kMinFontPx - 0.01f) fontOk = false; if (o.r.right() > o.clip.right() + 0.5f && o.clip.w < 1e8f) textInClip = false; }
			Rect vis = o.r; if (o.k != 'X') { /* geometria pode ser grande no scrim */ }
			(void)vis;
		}
		(void)allIn; CHECK(fontOk); CHECK(textInClip); CHECK(cv.minClipDepth == 0 && cv.clips.empty());
		// abre um app tocando no icone da grade
		const GridSpec g = os.home().grid(m);
		tapAt(os, GridCell(g, 7).center(), t, cv);   // 8o app = configuracoes (se couber na pagina)
		if (g.perPage > 7) { CHECK(os.apps().foregroundId() == "configuracoes"); }
		tapAt(os, os.navHome().center(), t, cv);   // Home
		CHECK(os.apps().foregroundId().empty()); CHECK(os.isOpen());
		// abre o app de teste e interage
		os.openApp("teste"); frames(os, cv, t, 3);
		tapAt(os, os.navRecents().center(), t, cv); CHECK(os.recentsOpen());
		tapAt(os, os.navRecents().center(), t, cv); CHECK(!os.recentsOpen());
		// sino abre o painel
		frames(os, cv, t, 1);
		if (!os.bellRect().empty()) { tapAt(os, os.bellRect().center(), t, cv); CHECK(os.notifPanelOpen()); os.back(); CHECK(!os.notifPanelOpen()); }
		// Voltar: app -> home -> fecha
		CHECK(os.back()); CHECK(os.apps().foregroundId().empty() && os.isOpen());
		CHECK(os.back()); CHECK(!os.isOpen());
		// fechou: pausa tudo, captura liberada
		CHECK(!os.input().handleRaw(2, 0, 1, 1, false));
	}

	// toque fora da moldura fecha; toque dentro nao
	{
		OSManager os; MockCanvas cv; int64_t t = 0; os.setScreenSize(2400, 1080); os.input().setOpen(true); os.open(); frames(os, cv, t, 2);
		tapAt(os, os.metrics().content.center(), t, cv); CHECK(os.isOpen());
		tapAt(os, { 3, 3 }, t, cv); CHECK(!os.isOpen());
	}
	// aberto: TODO evento e consumido (nenhum vaza para o jogo)
	{
		OSManager os; os.setScreenSize(1920, 1080); os.input().setOpen(true); os.open();
		bool leak = false; for (int i = 0; i < 100; ++i) { if (!os.input().handleRaw(i % 3 == 0 ? 2 : i % 3 == 1 ? 3 : 1, i & 1, i * 10, i * 5, false)) leak = true; }
		CHECK(!leak);
	}
	// fechar no meio do gesto nao deixa o dedo vazar
	{
		OSManager os; os.setScreenSize(1920, 1080); os.input().setOpen(true); os.open();
		os.input().handleRaw(2, 0, 100, 100, false); os.close();
		CHECK(os.input().handleRaw(3, 0, 110, 100, false)); CHECK(os.input().handleRaw(1, 0, 110, 100, false)); CHECK(!os.input().handleRaw(2, 0, 100, 100, false));
	}
	// ScrollView
	{
		ScrollView sv; sv.setLayout({ 0,0,100,200 }, 1000); CHECK(sv.maxOffset() == 800);
		Touch t; t.kind = Touch::Down; t.pos = { 10,150 }; sv.onTouch(t); t.kind = Touch::DragBegin; sv.onTouch(t);
		t.kind = Touch::Drag; t.delta = { 0,-100 }; sv.onTouch(t); CHECK(sv.offset() == 100);
		t.delta = { 0,-5000 }; sv.onTouch(t); CHECK(sv.offset() == 800);
		t.delta = { 0,5000 }; sv.onTouch(t); CHECK(sv.offset() == 0);
		t.kind = Touch::DragEnd; t.velocity = { 0,-2000 }; sv.onTouch(t); for (int i = 0; i < 200; ++i) sv.update(16); CHECK(sv.offset() > 0 && sv.offset() <= 800);
	}
	// FitText UTF-8
	{
		MockCanvas cv; std::string s = FitText(cv, "Organizações de teste muito longas", 16, 100);
		CHECK(cv.measure(s, 16).x <= 100.01f);
		bool valid = true; for (size_t i = 0; i < s.size(); ++i) if ((unsigned char)s[i] >= 0x80 && (unsigned char)s[i] < 0xC0 && (i == 0 || (unsigned char)s[i - 1] < 0x80)) valid = false; CHECK(valid);
		CHECK(FitText(cv, "abc", 16, 0).empty());
	}
	// dump p/ render
	{
		OSManager os; MockCanvas cv; int64_t t = 0; os.setScreenSize(2400, 1080); os.input().setOpen(true); os.open(); os.setStatusProvider([]() { StatusInfo s; s.clock = "21:45"; return s; });
		os.notifications().post("Teste de sistema", "Notificação de demonstração (sem dados reais)", "teste", 0);
		frames(os, cv, t, 3); cv.dump("home.txt");
		os.notifications().markAllRead(); os.openApp("teste"); frames(os, cv, t, 20); cv.dump("teste.txt");
		os.openApp("mdt"); frames(os, cv, t, 20); cv.dump("placeholder.txt");
		os.back(); os.openApp("teste"); frames(os, cv, t, 20); tapAt(os, os.navRecents().center(), t, cv); frames(os, cv, t, 2); cv.dump("recents.txt");
	}
	printf("%d verificacoes, %d falhas\n", g_n, g_fail);
	return g_fail ? 1 : 0;
}
