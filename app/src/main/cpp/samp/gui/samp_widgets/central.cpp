#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/playerpool.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/encoding/encoding.h"
#include "central.h"
#include "central_logic.h"
#include "avatar.h"
#include "central_icons.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern UI*       pUI;
extern CNetGame* pNetGame;
extern volatile bool g_bTouchCancel;    // game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL

namespace CL = CenLogic;

namespace
{
	std::atomic<bool> g_open{false};
	std::atomic<bool> g_askOpen{false};      // servidor mandou abrir (RPC 253 op 3)
	std::mutex        g_m;
	CL::List          g_list;
	std::atomic<bool> g_claimed[4];
	struct Ev { int type, num; float x, y; bool cancel; };
	std::vector<Ev>   g_ev;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	bool Connected() { return pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED && pNetGame->GetRakClient(); }

	// cliente -> servidor pelo comando oculto "/cenx ..." (SendChatCommand), o MESMO caminho do "/invx" do inventario (comprovado).
	// op 1 = pedir a lista | op 2 = escolher o modelo de fundo (arg). O RPC 254 do servidor continua aceito, mas o cliente nao o usa.
	void SendReq(uint8_t op, int arg = -1)
	{
		if (!Connected()) return;
		char cmd[32];
		if (op == 2 && arg >= 0) snprintf(cmd, sizeof(cmd), "/cenx style %d", arg);
		else snprintf(cmd, sizeof(cmd), "/cenx req");
		pNetGame->SendChatCommand(cmd);
	}

	// ---- estado da tela (thread de desenho) ----
	bool    g_settings = false;      // painel de modelos de fundo aberto
	float   g_scroll = 0.0f;
	int     g_orgSel = 0;                // filtro da lista: 0 todos | 1 PM | 2 MEC | 3 SAMU
	int64_t g_lastReq = 0;
	int64_t g_openT = 0;                 // quando abriu (para avisar se o servidor nao responde)
	std::string g_query;                 // texto da pesquisa (bytes cp1251 do teclado); protegido por g_m
	std::string g_lastQuery;
	CentralUI*  s_self = nullptr;        // o teclado (Keyboard::show) devolve o texto por keyboardEvent do widget
	struct Fing { int num = -1; float sx = 0, sy = 0, s0 = 0; int kind = 0; bool moved = false; } g_f;   // kind: 1 lista, 2 botao/celula, 3 fora, 4 painel

	ImColor C(const uint8_t* c, int a = 255) { return ImColor((int)c[0], (int)c[1], (int)c[2], a); }

	// fundo de uma linha com o modelo e o efeito (todos os efeitos sao desenhados por primitivas)
	void DrawStyleBg(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, int idx, float u, double t, float rad = 0.0f)
	{
		const CL::StyleDef& s = CL::Style(idx);
		const float w = b.x - a.x, h = b.y - a.y;
		ImColor c1 = C(s.c1, 240), c2 = C(s.c2, 240);
		if (s.effect == CL::E_Wave)       // fogo: o laranja respira
		{
			const float m = 0.5f + 0.5f * (float)std::sin(t * 2.3);
			c2 = ImColor((int)std::min(255.0f, s.c2[0] + 15.0f * m), (int)std::min(255.0f, s.c2[1] + 50.0f * m), (int)s.c2[2], 240);
		}
		if (rad > 0.0f)     // cantos arredondados: miolo em degrade + duas pontas de cor solida (mesma cor das extremidades do degrade)
		{
			r->drawRoundedRect(a, ImVec2(a.x + rad * 2.0f, b.y), c1, rad, true);
			r->drawRoundedRect(ImVec2(b.x - rad * 2.0f, a.y), b, c2, rad, true);
			r->drawRectFilledMulticolor(ImVec2(a.x + rad, a.y), ImVec2(b.x - rad, b.y), c1, c2, c2, c1);
		}
		else r->drawRectFilledMulticolor(a, b, c1, c2, c2, c1);

		r->pushClipRect(a, b, true);
		if (s.effect == CL::E_Shimmer || s.effect == CL::E_Wave)
		{
			const bool fire = s.effect == CL::E_Wave;
			const float ph = (float)std::fmod(t * (fire ? 0.75 : 0.42), 1.8);
			const float cx = a.x + (ph - 0.4f) * w, bw = w * (fire ? 0.30f : 0.16f), sk = h * 0.6f;
			ImVec2 p[4] = { ImVec2(cx, a.y), ImVec2(cx + bw, a.y), ImVec2(cx + bw - sk, b.y), ImVec2(cx - sk, b.y) };
			r->drawConvexPolyFilled(p, 4, fire ? ImColor(255, 190, 70, 80) : ImColor(255, 245, 200, 85));
			ImVec2 q[4] = { ImVec2(cx + bw * 0.35f, a.y), ImVec2(cx + bw * 0.65f, a.y), ImVec2(cx + bw * 0.65f - sk, b.y), ImVec2(cx + bw * 0.35f - sk, b.y) };
			r->drawConvexPolyFilled(q, 4, fire ? ImColor(255, 240, 160, 70) : ImColor(255, 255, 255, 110));
		}
		if (s.effect == CL::E_Lights)     // pisca-pisca nas bordas de cima e de baixo
		{
			static const int col[3][3] = { { 255, 80, 80 }, { 110, 255, 140 }, { 255, 220, 100 } };
			const float gap = 15.0f * u, ins = rad;
			const int n = std::max(2, (int)((w - 2.0f * ins) / gap));
			const int ph = (int)(t * 2.4);
			for (int i = 0; i < n; i++)
			{
				const float x = a.x + ins + (i + 0.5f) * ((w - 2.0f * ins) / (float)n);
				const bool on = ((ph + i) & 1) == 0;
				const int* c = col[i % 3];
				for (int side = 0; side < 2; side++)
				{
					const float y = side ? b.y - 2.6f * u : a.y + 2.6f * u;
					ImVec2 d[8];
					for (int k = 0; k < 8; k++) { const float an = 6.2831853f * k / 8.0f; d[k] = ImVec2(x + std::cos(an) * 2.0f * u, y + std::sin(an) * 2.0f * u); }
					r->drawConvexPolyFilled(d, 8, ImColor(c[0], c[1], c[2], on ? 255 : 60));
				}
			}
		}
		r->popClipRect();

		const float lw = 1.3f * u;
		if (s.effect == CL::E_Pulse)
		{
			const float p = 0.5f + 0.5f * (float)std::sin(t * 2.8);
			if (rad > 0.0f)
			{
				r->drawRoundedRect(ImVec2(a.x - 1.2f * u, a.y - 1.2f * u), ImVec2(b.x + 1.2f * u, b.y + 1.2f * u), C(s.edge, (int)(30 + 60 * p)), rad + 1.0f * u, false, 2.6f * u);
				r->drawRoundedRect(a, b, C(s.edge, (int)(110 + 145 * p)), rad, false, lw);
			}
			else
			{
				r->drawRect(ImVec2(a.x - 1.2f * u, a.y - 1.2f * u), ImVec2(b.x + 1.2f * u, b.y + 1.2f * u), C(s.edge, (int)(30 + 60 * p)), false, 2.6f * u);
				r->drawRect(a, b, C(s.edge, (int)(110 + 145 * p)), false, lw);
			}
		}
		else if (s.effect == CL::E_Rainbow)
		{
			constexpr int N = 36;
			for (int i = 0; i < N; i++)
			{
				float hh = (float)i / (float)N + (float)(t * 0.3); hh -= std::floor(hh);
				const ImColor col = ImColor::HSV(hh, 0.8f, 1.0f);
				const float x0 = a.x + rad + (w - 2.0f * rad) * i / N, x1 = a.x + rad + (w - 2.0f * rad) * (i + 1) / N;
				r->drawLine(ImVec2(x0, a.y), ImVec2(x1, a.y), col, lw * 1.4f);
				r->drawLine(ImVec2(x0, b.y), ImVec2(x1, b.y), col, lw * 1.4f);
			}
			if (rad <= 0.0f)
			{
				r->drawLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y), ImColor::HSV((float)std::fmod(t * 0.3, 1.0), 0.8f, 1.0f), lw * 1.4f);
				r->drawLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), ImColor::HSV((float)std::fmod(t * 0.3 + 0.5, 1.0), 0.8f, 1.0f), lw * 1.4f);
			}
		}
		else if (rad > 0.0f) r->drawRoundedRect(a, b, C(s.edge, 210), rad, false, lw);
		else r->drawRect(a, b, C(s.edge, 210), false, lw);
		(void)h;
	}

	void Disc(ImGuiRenderer* r, float x, float y, float rr, const ImColor& col)
	{
		constexpr int N = 20; ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(x + std::cos(an) * rr, y + std::sin(an) * rr); }
		r->drawConvexPolyFilled(p, N, col);
	}
	void Circ(ImGuiRenderer* r, float x, float y, float rr, const ImColor& col, float th)
	{
		constexpr int N = 24; ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(x + std::cos(an) * rr, y + std::sin(an) * rr); }
		r->drawPolyline(p, N, col, true, th);
	}
	// icones das organizacoes e da pesquisa, so com primitivas (s = meio-tamanho)
	void IconShield(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col, float th)      // PM
	{
		ImVec2 p[5] = { ImVec2(cx - s * 0.8f, cy - s * 0.85f), ImVec2(cx + s * 0.8f, cy - s * 0.85f), ImVec2(cx + s * 0.8f, cy + s * 0.1f), ImVec2(cx, cy + s * 1.0f), ImVec2(cx - s * 0.8f, cy + s * 0.1f) };
		r->drawPolyline(p, 5, col, true, th);
		Disc(r, cx, cy - s * 0.1f, s * 0.22f, col);
	}
	void IconTools(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col, float th)       // MEC: chave e martelo cruzados
	{
		r->drawLine(ImVec2(cx - s * 0.8f, cy + s * 0.8f), ImVec2(cx + s * 0.7f, cy - s * 0.7f), col, th * 1.2f);
		r->drawLine(ImVec2(cx - s * 0.8f, cy - s * 0.8f), ImVec2(cx + s * 0.7f, cy + s * 0.7f), col, th * 1.2f);
		Disc(r, cx + s * 0.78f, cy - s * 0.78f, s * 0.3f, col);
		r->drawRect(ImVec2(cx - s * 1.0f, cy - s * 1.0f), ImVec2(cx - s * 0.55f, cy - s * 0.55f), col, true);
	}
	void IconMed(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col, float th)         // SAMU: maleta com cruz
	{
		r->drawRoundedRect(ImVec2(cx - s, cy - s * 0.8f), ImVec2(cx + s, cy + s * 0.9f), col, s * 0.25f, false, th);
		r->drawLine(ImVec2(cx - s * 0.45f, cy + s * 0.05f), ImVec2(cx + s * 0.45f, cy + s * 0.05f), col, th);
		r->drawLine(ImVec2(cx, cy - s * 0.4f), ImVec2(cx, cy + s * 0.5f), col, th);
		r->drawLine(ImVec2(cx - s * 0.35f, cy - s * 0.8f), ImVec2(cx - s * 0.35f, cy - s * 1.05f), col, th);
		r->drawLine(ImVec2(cx + s * 0.35f, cy - s * 0.8f), ImVec2(cx + s * 0.35f, cy - s * 1.05f), col, th);
	}
	void IconPeople(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col)                // jogadores online
	{
		Disc(r, cx - s * 0.4f, cy - s * 0.35f, s * 0.33f, col);
		Disc(r, cx - s * 0.4f, cy + s * 0.7f, s * 0.55f, col);
		Disc(r, cx + s * 0.5f, cy - s * 0.2f, s * 0.28f, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * 0.75f));
		Disc(r, cx + s * 0.5f, cy + s * 0.75f, s * 0.48f, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * 0.75f));
	}
	void IconSearch(ImGuiRenderer* r, float cx, float cy, float s, const ImColor& col, float th)
	{
		Circ(r, cx - s * 0.15f, cy - s * 0.15f, s * 0.62f, col, th);
		r->drawLine(ImVec2(cx + s * 0.3f, cy + s * 0.3f), ImVec2(cx + s * 0.85f, cy + s * 0.85f), col, th * 1.2f);
	}

	void Icon(ImGuiRenderer* r, float cx, float cy, float rad, const ImColor& col)     // silhueta simples (cabeca + ombros)
	{
		auto disc = [&](float x, float y, float rr) { constexpr int N = 20; ImVec2 p[N]; for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(x + std::cos(an) * rr, y + std::sin(an) * rr); } r->drawConvexPolyFilled(p, N, col); };
		disc(cx, cy - rad * 0.28f, rad * 0.32f);
		disc(cx, cy + rad * 0.55f, rad * 0.5f);
	}
}

CentralUI::CentralUI() : Widget() { s_self = this; }

// texto da pesquisa vindo do teclado (cp1251/Latin-1); texto vazio limpa
void CentralUI::keyboardEvent(const std::string& input)
{
	std::lock_guard<std::mutex> l(g_m);
	g_query = CL::Trim(input).substr(0, 24);
}

bool CentralUI::IsOpen() { return g_open.load(); }

void CentralUI::Open()
{
	if (g_open.load()) return;
	{ std::lock_guard<std::mutex> l(g_m); g_ev.clear(); }
	for (auto& c : g_claimed) c.store(false);
	g_f = Fing(); g_scroll = 0.0f; g_orgSel = 0; g_settings = false; g_lastReq = 0; g_openT = NowMs();
	{ std::lock_guard<std::mutex> l(g_m); g_query.clear(); g_lastQuery.clear(); }
	g_open.store(true);
}

void CentralUI::Close()
{
	g_open.store(false);
	std::lock_guard<std::mutex> l(g_m);
	g_ev.clear();
}

void CentralUI::Reset()
{
	g_open.store(false); g_askOpen.store(false);
	std::lock_guard<std::mutex> l(g_m);
	g_ev.clear(); g_list = CL::List();
}

bool CentralUI::OnBackPressed()
{
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 400) return true;       // onKeyDown + onBackPressed no mesmo toque
	if (!g_open.load()) return false;
	s_last = now;
	Close();
	return true;
}

bool CentralUI::HandleRawTouch(int type, int num, int x, int y)
{
	const int n = num & 3;
	const bool open = g_open.load();
	if (!open && !g_claimed[n].load()) return false;
	if (open)
	{
		g_claimed[n].store(true);
		std::lock_guard<std::mutex> l(g_m);
		if (g_ev.size() < 256) g_ev.push_back({ type, n, (float)x, (float)y, g_bTouchCancel });
	}
	if (type == 1) g_claimed[n].store(false);
	return true;
}

// ---- RPC 253 ----
void CentralUI::OnServerPacket(const unsigned char* data, int bytes)
{
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	uint8_t op = 0;
	if (!bs.Read(op)) return;
	if (op == 3) { g_askOpen.store(true); return; }
	std::lock_guard<std::mutex> l(g_m);
	if (op == 1)
	{
		uint16_t tot = 0, sl = 0, mins = 0, adm = 0, pm = 0, mec = 0, sa = 0; uint8_t st = 0;
		if (!bs.Read(tot) || !bs.Read(sl) || !bs.Read(mins) || !bs.Read(st) || !bs.Read(adm)) return;
		if (!bs.Read(pm) || !bs.Read(mec) || !bs.Read(sa)) { pm = mec = sa = 0; }     // servidor antigo: sem contagem de organizacoes
		g_list.Header(tot, sl, mins, st, adm, pm, mec, sa);
		FLog("[CENTRAL] lista: %u online, %u slots, pm=%u mec=%u samu=%u", (unsigned)tot, (unsigned)sl, (unsigned)pm, (unsigned)mec, (unsigned)sa);
	}
	else if (op == 2)
	{
		uint8_t n = 0;
		if (!bs.Read(n) || !g_list.have) return;
		for (int i = 0; i < n; i++)
		{
			CL::Row row; uint8_t nl = 0;
			if (!bs.Read(row.pid) || !bs.Read(row.acc) || !bs.Read(row.style) || !bs.Read(row.border) || !bs.Read(row.flags) || !bs.Read(row.org) || !bs.Read(row.ping) || !bs.Read(nl)) return;
			if (nl > 24) return;
			char nm[25]; for (int k = 0; k < nl; k++) { uint8_t ch = 0; if (!bs.Read(ch)) return; nm[k] = (char)ch; } nm[nl] = 0;
			row.name = nm;
			g_list.Add(row);
		}
	}
}

void CentralUI::draw(ImGuiRenderer* r)
{
	if (g_askOpen.exchange(false)) Open();
	if (!g_open.load()) { g_f = Fing(); return; }
	if (!Connected()) { Close(); return; }
	Avatar::Tick();

	const int64_t now = NowMs();
	const double  t = (double)now / 1000.0;
	if (g_lastReq == 0 || now - g_lastReq > 3000) { g_lastReq = now; SendReq(1); }

	CL::List L; std::string query;
	std::vector<Ev> evs;
	{ std::lock_guard<std::mutex> l(g_m); L = g_list; query = g_query; evs.swap(g_ev); }
	if (query != g_lastQuery) { g_lastQuery = query; g_scroll = 0.0f; }
	static int s_lastOrg = 0; if (s_lastOrg != g_orgSel) { s_lastOrg = g_orgSel; g_scroll = 0.0f; }
	const std::vector<int> shown = CL::Filter(L, query, g_orgSel);

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float u  = std::min(scr.y / 720.0f, scr.x / 1100.0f);
	const float ox = scr.x * 0.5f - 640.0f * u, oy = (scr.y - 720.0f * u) * 0.5f;
	auto PX = [&](float v) { return ox + v * u; };
	auto PY = [&](float v) { return oy + v * u; };
	const ImVec2 pa(PX(430.0f), PY(36.0f)), pb(PX(850.0f), PY(684.0f));
	const ImVec2 xa(PX(806.0f), PY(46.0f)), xb(PX(838.0f), PY(74.0f));            // X fechar
	const ImVec2 ga(PX(796.0f), PY(104.0f)), gb(PX(834.0f), PY(142.0f));          // engrenagem
	const ImVec2 oa(PX(446.0f), PY(172.0f)), ob(PX(640.0f), PY(204.0f));         // faixa dos chips PM | MEC | SAMU
	const ImVec2 sa(PX(446.0f), PY(212.0f)), sb(PX(834.0f), PY(244.0f));          // caixa de pesquisa
	const ImVec2 sxa(PX(804.0f), PY(212.0f)), sxb(PX(834.0f), PY(244.0f));        // "x" que limpa a pesquisa
	const ImVec2 va(PX(446.0f), PY(254.0f)), vb(PX(834.0f), PY(672.0f));          // area da lista
	const float rowH = 58.0f * u, pitch = 66.0f * u, rowR = 10.0f * u;
	const float content = (float)shown.size() * pitch;
	auto In = [](float x, float y, const ImVec2& a, const ImVec2& b) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; };
	auto cellA = [&](int i) { return ImVec2(PX(446.0f + (i % 3) * 130.0f), PY(262.0f + (i / 3) * 70.0f)); };
	auto cellB = [&](int i) { const ImVec2 a = cellA(i); return ImVec2(a.x + 124.0f * u, a.y + 54.0f * u); };
	auto orgAt = [&](float x, float y) { if (!In(x, y, oa, ob)) return 0; const int i = (int)((x - oa.x) / ((ob.x - oa.x) / 3.0f)); return 1 + std::min(2, std::max(0, i)); };
	auto cellAt = [&](float x, float y) { for (int i = 0; i < CL::kStyles; i++) if (In(x, y, cellA(i), cellB(i))) return i; return -1; };

	g_scroll = CL::ClampScroll(g_scroll, content, vb.y - va.y);

	// ---- toques ----
	for (const Ev& e : evs)
	{
		if (e.type == 2 && g_f.num < 0)
		{
			g_f = Fing(); g_f.num = e.num; g_f.sx = e.x; g_f.sy = e.y; g_f.s0 = g_scroll;
			if (!In(e.x, e.y, pa, pb)) g_f.kind = 3;
			else if (In(e.x, e.y, xa, xb) || In(e.x, e.y, ga, gb) || (g_settings && cellAt(e.x, e.y) >= 0) || (!g_settings && orgAt(e.x, e.y) > 0) || (!g_settings && In(e.x, e.y, sa, sb))) g_f.kind = 2;
			else if (!g_settings && In(e.x, e.y, va, vb)) g_f.kind = 1;
			else g_f.kind = 4;
		}
		else if (e.type == 3 && e.num == g_f.num)
		{
			if (g_f.kind == 1)
			{
				if (std::fabs(e.y - g_f.sy) > 9.0f * u) g_f.moved = true;
				if (g_f.moved) g_scroll = CL::ClampScroll(g_f.s0 - (e.y - g_f.sy), content, vb.y - va.y);
			}
		}
		else if (e.type == 1 && e.num == g_f.num)
		{
			const Fing f = g_f; g_f = Fing();
			if (e.cancel) continue;
			if (f.kind == 3) { Close(); return; }
			if (f.kind == 2)
			{
				if (In(e.x, e.y, xa, xb) && In(f.sx, f.sy, xa, xb)) { Close(); return; }
				if (In(e.x, e.y, ga, gb) && In(f.sx, f.sy, ga, gb)) { g_settings = !g_settings; g_scroll = 0.0f; continue; }
				if (!g_settings && In(e.x, e.y, sa, sb) && In(f.sx, f.sy, sa, sb))
				{
					if (!query.empty() && In(e.x, e.y, sxa, sxb)) { std::lock_guard<std::mutex> l(g_m); g_query.clear(); }       // "x": limpa
					else if (pUI && pUI->keyboard() && s_self) pUI->keyboard()->show(s_self);                                      // abre o teclado (o texto volta em keyboardEvent)
					continue;
				}
				{ const int o0 = g_settings ? 0 : orgAt(f.sx, f.sy), o1 = g_settings ? 0 : orgAt(e.x, e.y); if (o0 > 0 && o0 == o1) { g_orgSel = (g_orgSel == o0) ? 0 : o0; continue; } }       // chip: filtra pela org (toque de novo = todos)
				const int c0 = cellAt(f.sx, f.sy), c1 = cellAt(e.x, e.y);
				if (g_settings && c0 >= 0 && c0 == c1) { SendReq(2, c0); g_lastReq = 0; }   // escolhe o modelo; a lista volta com ele aplicado
			}
		}
	}

	// ---- painel ----
	r->drawRect(ImVec2(0, 0), scr, ImColor(0, 0, 0, 110), true);
	r->drawRoundedRect(pa, pb, ImColor(22, 22, 28, 240), 8.0f * u, true);
	r->drawRoundedRect(pa, pb, ImColor(70, 70, 82, 255), 8.0f * u, false, 1.0f);
	const ImColor cPink(46, 128, 255, 255), cText(238, 238, 244, 255), cMute(150, 150, 164, 255);

	// cabecalho
	{
		static RwRaster* s_globe = nullptr; static bool s_gf = false;
		if (!s_globe && !s_gf) { s_globe = Avatar::PngRaster(kIconGlobePng, kIconGlobePngSize); if (!s_globe) s_gf = true; }
		if (s_globe) r->drawImage(ImVec2(PX(444.0f), PY(44.0f)), ImVec2(PX(476.0f), PY(76.0f)), (ImTextureID)s_globe, ImColor(255, 255, 255, 255), ImVec2(0, 0), ImVec2(1, 1));
		else { r->drawRoundedRect(ImVec2(PX(446.0f), PY(46.0f)), ImVec2(PX(474.0f), PY(74.0f)), cPink, 5.0f * u, true); Icon(r, PX(460.0f), PY(60.0f), 10.0f * u, ImColor(255, 255, 255, 255)); }
	}
	r->drawText(ImVec2(PX(484.0f), PY(52.0f)), cText, "Central da Cidade", false, 14.0f * u);
	{
		const bool pr = g_f.num >= 0 && g_f.kind == 2 && In(g_f.sx, g_f.sy, xa, xb);
		r->drawLine(ImVec2(xa.x + 9.0f * u, xa.y + 7.0f * u), ImVec2(xb.x - 9.0f * u, xb.y - 7.0f * u), pr ? cPink : cMute, 1.8f * u);
		r->drawLine(ImVec2(xb.x - 9.0f * u, xa.y + 7.0f * u), ImVec2(xa.x + 9.0f * u, xb.y - 7.0f * u), pr ? cPink : cMute, 1.8f * u);
	}
	r->drawLine(ImVec2(PX(446.0f), PY(84.0f)), ImVec2(PX(834.0f), PY(84.0f)), ImColor(255, 255, 255, 22), 1.0f);

	// perfil: avatar (foto + borda escolhida), nome, ID da conta e tempo online
	{
		const float cx = PX(484.0f), cy = PY(126.0f), ar = 28.0f * u;
		Avatar::DrawPhoto(r, cx, cy, ar);
		Avatar::DrawBorder(r, Avatar::Border(), cx, cy, ar, u * 0.8f, t);
		const char* nm = (pNetGame && pNetGame->GetPlayerPool()) ? pNetGame->GetPlayerPool()->GetLocalPlayerName() : nullptr;
		char line[96]; snprintf(line, sizeof(line), "%s (%u)", (nm && *nm) ? nm : "Jogador", (unsigned)Avatar::AccountId());
		r->drawText(ImVec2(PX(534.0f), PY(108.0f)), cText, line, false, 13.0f * u);
		snprintf(line, sizeof(line), "Online ha %d minuto(s)", (int)L.myMin);
		r->drawText(ImVec2(PX(534.0f), PY(128.0f)), cMute, line, false, 10.5f * u);
		const bool pr = g_f.num >= 0 && g_f.kind == 2 && In(g_f.sx, g_f.sy, ga, gb);
		r->drawRoundedRect(ga, gb, pr || g_settings ? ImColor(46, 128, 255, 90) : ImColor(255, 255, 255, 22), 6.0f * u, true);
		r->drawRoundedRect(ga, gb, g_settings ? cPink : ImColor(255, 255, 255, 50), 6.0f * u, false, 1.0f);
		const float gx = (ga.x + gb.x) * 0.5f, gy = (ga.y + gb.y) * 0.5f;       // engrenagem: anel + 8 dentes
		for (int i = 0; i < 8; i++) { const float an = 0.785398f * i; r->drawLine(ImVec2(gx + std::cos(an) * 7.0f * u, gy + std::sin(an) * 7.0f * u), ImVec2(gx + std::cos(an) * 10.0f * u, gy + std::sin(an) * 10.0f * u), cText, 2.4f * u); }
		{ constexpr int N = 24; ImVec2 p[N]; for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(gx + std::cos(an) * 7.0f * u, gy + std::sin(an) * 7.0f * u); } r->drawPolyline(p, N, cText, true, 2.0f * u); }
	}

	// faixa de numeros: organizacoes em servico (PM | MEC | SAMU) e populacao real (online/slots)
	{
		char buf[48];
		const ImVec2 a1(PX(446.0f), PY(172.0f)), b1(PX(640.0f), PY(204.0f));
		r->drawRoundedRect(a1, b1, ImColor(255, 255, 255, 14), 6.0f * u, true);
		const float cw = (b1.x - a1.x) / 3.0f, cyy = (a1.y + b1.y) * 0.5f;
		const int   cnt[3] = { (int)L.pm, (int)L.mec, (int)L.samu };
		const ImColor ic[3] = { ImColor(96, 170, 255, 255), ImColor(255, 172, 72, 255), ImColor(255, 92, 104, 255) };
		for (int i = 0; i < 3; i++)
		{
			if (g_orgSel == i + 1) r->drawRoundedRect(ImVec2(a1.x + cw * i + 2.0f * u, a1.y + 2.0f * u), ImVec2(a1.x + cw * (i + 1) - 2.0f * u, b1.y - 2.0f * u), ImColor(ic[i].Value.x, ic[i].Value.y, ic[i].Value.z, 0.28f), 5.0f * u, true);
			const float ix = a1.x + cw * i + 16.0f * u;
			if (i == 0) IconShield(r, ix, cyy, 7.5f * u, ic[0], 1.5f * u);
			else if (i == 1) IconTools(r, ix, cyy, 7.5f * u, ic[1], 1.6f * u);
			else IconMed(r, ix, cyy, 7.5f * u, ic[2], 1.5f * u);
			snprintf(buf, sizeof(buf), "%d", cnt[i]);
			const ImVec2 ts = r->calculateTextSize(buf, 12.0f * u);
			r->drawText(ImVec2(ix + 12.0f * u, cyy - ts.y * 0.5f), cText, buf, false, 12.0f * u);
			if (i < 2) r->drawLine(ImVec2(a1.x + cw * (i + 1), a1.y + 8.0f * u), ImVec2(a1.x + cw * (i + 1), b1.y - 8.0f * u), ImColor(255, 255, 255, 40), 1.0f);
		}
		// jogadores: barra rosa proporcional + "online/slots" (valores reais do servidor)
		const ImVec2 a3(PX(650.0f), PY(172.0f)), b3(PX(834.0f), PY(204.0f));
		r->drawRoundedRect(a3, b3, ImColor(255, 255, 255, 14), 6.0f * u, true);
		const float fillW = std::max(34.0f * u, (b3.x - a3.x) * CL::PopFill(L.total, L.slots));
		r->drawRoundedRect(a3, ImVec2(a3.x + fillW, b3.y), cPink, 6.0f * u, true);
		{
			static RwRaster* s_pl = nullptr; static bool s_pf = false;
			if (!s_pl && !s_pf) { s_pl = Avatar::PngRaster(kIconPlayersPng, kIconPlayersPngSize); if (!s_pl) s_pf = true; }
			const float ic = 10.0f * u, icx = a3.x + 17.0f * u, icy = (a3.y + b3.y) * 0.5f;
			if (s_pl) r->drawImage(ImVec2(icx - ic, icy - ic), ImVec2(icx + ic, icy + ic), (ImTextureID)s_pl, ImColor(255, 255, 255, 255), ImVec2(0, 0), ImVec2(1, 1));
			else IconPeople(r, icx, icy, 7.5f * u, ImColor(255, 255, 255, 255));
		}
		char sn[16], ss[16]; snprintf(sn, sizeof(sn), "%d", (int)L.total); snprintf(ss, sizeof(ss), "/%d", (int)L.slots);
		const ImVec2 t1 = r->calculateTextSize(sn, 12.0f * u), t2 = r->calculateTextSize(ss, 11.0f * u);
		const float tx = b3.x - 10.0f * u - t1.x - t2.x, ty = (a3.y + b3.y) * 0.5f;
		r->drawLine(ImVec2(tx - 8.0f * u, a3.y + 9.0f * u), ImVec2(tx - 8.0f * u, b3.y - 9.0f * u), ImColor(255, 255, 255, 70), 1.0f);
		r->drawText(ImVec2(tx, ty - t1.y * 0.5f), ImColor(255, 255, 255, 255), sn, true, 12.0f * u);
		r->drawText(ImVec2(tx + t1.x, ty - t2.y * 0.5f), ImColor(235, 235, 245, 170), ss, true, 11.0f * u);
	}

	// caixa de pesquisa (toque abre o teclado; filtra por nome ou ID da conta)
	if (!g_settings)
	{
		const bool pr = g_f.num >= 0 && g_f.kind == 2 && In(g_f.sx, g_f.sy, sa, sb);
		r->drawRoundedRect(sa, sb, pr ? ImColor(46, 128, 255, 50) : ImColor(255, 255, 255, 14), 6.0f * u, true);
		r->drawRoundedRect(sa, sb, ImColor(255, 255, 255, 46), 6.0f * u, false, 1.0f);
		IconSearch(r, sa.x + 16.0f * u, (sa.y + sb.y) * 0.5f, 7.0f * u, cMute, 1.5f * u);
		const std::string shownTxt = query.empty() ? std::string("Pesquisar...") : Encoding::cp2utf(query);
		const ImVec2 ts = r->calculateTextSize(shownTxt, 11.5f * u);
		r->drawText(ImVec2(sa.x + 32.0f * u, (sa.y + sb.y - ts.y) * 0.5f), query.empty() ? cMute : cText, shownTxt, false, 11.5f * u);
		if (!query.empty())
		{
			const float xx = (sxa.x + sxb.x) * 0.5f, xy = (sxa.y + sxb.y) * 0.5f, q = 4.5f * u;
			r->drawLine(ImVec2(xx - q, xy - q), ImVec2(xx + q, xy + q), cMute, 1.6f * u);
			r->drawLine(ImVec2(xx + q, xy - q), ImVec2(xx - q, xy + q), cMute, 1.6f * u);
		}
	}

	if (g_settings)
	{
		r->drawText(ImVec2(PX(446.0f), PY(218.0f)), cText, "Modelo de fundo da sua linha", false, 12.0f * u);
		r->drawText(ImVec2(PX(446.0f), PY(236.0f)), cMute, "Todos na Central veem o modelo que voce escolher.", false, 9.5f * u);
		for (int i = 0; i < CL::kStyles; i++)
		{
			const ImVec2 a = cellA(i), b = cellB(i);
			const bool sel = (int)L.myStyle == i;
			DrawStyleBg(r, a, b, i, u, t);
			if (sel) r->drawRect(ImVec2(a.x - 2.5f * u, a.y - 2.5f * u), ImVec2(b.x + 2.5f * u, b.y + 2.5f * u), cPink, false, 2.0f * u);
			const char* nm = CL::Style(i).name;
			const ImVec2 ts = r->calculateTextSize(nm, 11.0f * u);
			r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ImColor(255, 255, 255, 255), nm, true, 11.0f * u);
		}
		return;
	}

	// lista
	if (shown.empty())
	{
		const bool slow = !L.have && (now - g_openT) > 4500;
		const char* msg = !L.have ? (slow ? "Sem resposta do servidor. Atualize o servidor (maximo_central.inc)." : "Carregando...")
		                          : (L.rows.empty() ? "Ninguem online" : (g_orgSel > 0 && query.empty() ? "Nenhum membro dessa organizacao online" : "Nenhum jogador encontrado"));
		const ImVec2 ts = r->calculateTextSize(msg, 11.5f * u);
		r->drawText(ImVec2((va.x + vb.x - ts.x) * 0.5f, va.y + 40.0f * u), cMute, msg, false, 11.5f * u);
		return;
	}
	const int localId = (pNetGame && pNetGame->GetPlayerPool()) ? (int)pNetGame->GetPlayerPool()->GetLocalPlayerID() : -1;
	r->drawRoundedRect(ImVec2(va.x - 4.0f * u, va.y - 2.0f * u), ImVec2(vb.x + 2.0f * u, vb.y + 2.0f * u), ImColor(255, 255, 255, 10), 12.0f * u, true);     // moldura arredondada da lista
	r->pushClipRect(va, vb, true);
	for (size_t i = 0; i < shown.size(); i++)
	{
		const float y = va.y + (float)i * pitch - g_scroll;
		if (y + rowH < va.y || y > vb.y) continue;
		const CL::Row& row = L.rows[shown[i]];
		const ImVec2 a(va.x, y), b(vb.x - 8.0f * u, y + rowH);
		DrawStyleBg(r, a, b, row.style, u, t, rowR);
		const bool self = (int)row.pid == localId || (Avatar::AccountId() && row.acc == Avatar::AccountId());
		if (self) r->drawRoundedRect(ImVec2(a.x - 1.5f * u, a.y - 1.5f * u), ImVec2(b.x + 1.5f * u, b.y + 1.5f * u), ImColor(255, 255, 255, 220), rowR + 1.5f * u, false, 1.6f * u);
		char buf[64]; snprintf(buf, sizeof(buf), "%s", row.name.c_str());
		const float nfs = 15.5f * u;
		const ImVec2 nsz = r->calculateTextSize(buf, nfs);
		const float tx0 = a.x + 16.0f * u, ty = a.y + (rowH - nsz.y) * 0.5f;
		// nome nitido: sombra escura deslocada + traco duplo (peso) em branco puro
		r->drawText(ImVec2(tx0 + 1.4f * u, ty + 1.4f * u), ImColor(0, 0, 0, 200), buf, false, nfs);
		r->drawText(ImVec2(tx0, ty), ImColor(255, 255, 255, 255), buf, true, nfs);
		r->drawText(ImVec2(tx0 + 0.6f * u, ty), ImColor(255, 255, 255, 255), buf, false, nfs);
		snprintf(buf, sizeof(buf), "(%u)", (unsigned)row.acc);
		const ImVec2 isz = r->calculateTextSize(buf, 12.5f * u);
		r->drawText(ImVec2(tx0 + nsz.x + 8.0f * u + 1.0f * u, a.y + (rowH - isz.y) * 0.5f + 1.0f * u), ImColor(0, 0, 0, 170), buf, false, 12.5f * u);
		r->drawText(ImVec2(tx0 + nsz.x + 8.0f * u, a.y + (rowH - isz.y) * 0.5f), ImColor(240, 240, 250, 235), buf, true, 12.5f * u);
		float rx = b.x - 14.0f * u;
		if (row.flags & 1)       // admin
		{
			const char* tg = "ADMIN"; const ImVec2 ts = r->calculateTextSize(tg, 11.0f * u);
			const ImVec2 ta(rx - ts.x - 14.0f * u, a.y + (rowH - 22.0f * u) * 0.5f), tb(rx, ta.y + 22.0f * u);
			r->drawRoundedRect(ta, tb, ImColor(0, 0, 0, 150), 6.0f * u, true);
			r->drawText(ImVec2(ta.x + 7.0f * u, ta.y + (22.0f * u - ts.y) * 0.5f), ImColor(255, 226, 120, 255), tg, false, 11.0f * u);
			rx = ta.x - 10.0f * u;
		}
		snprintf(buf, sizeof(buf), "%dms", (int)row.ping);
		const ImVec2 ps = r->calculateTextSize(buf, 11.0f * u);
		r->drawText(ImVec2(rx - ps.x + 1.0f * u, a.y + (rowH - ps.y) * 0.5f + 1.0f * u), ImColor(0, 0, 0, 170), buf, false, 11.0f * u);
		r->drawText(ImVec2(rx - ps.x, a.y + (rowH - ps.y) * 0.5f), ImColor(240, 240, 250, 220), buf, true, 11.0f * u);
	}
	r->popClipRect();
	if (content > vb.y - va.y)     // barra de rolagem
	{
		const float vh = vb.y - va.y, th = std::max(24.0f * u, vh * vh / content), tp = va.y + (vh - th) * (g_scroll / (content - vh));
		r->drawRoundedRect(ImVec2(vb.x - 4.0f * u, tp), ImVec2(vb.x - 1.5f * u, tp + th), ImColor(255, 255, 255, 90), 2.0f * u, true);
	}
}
