#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/playerpool.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "central.h"
#include "central_logic.h"
#include "avatar.h"

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

	void SendReq(uint8_t op, int arg = -1)
	{
		if (!Connected()) return;
		RakNet::BitStream bs;
		bs.Write(op);
		if (arg >= 0) bs.Write((uint8_t)arg);
		pNetGame->GetRakClient()->RPC(&RPC_CentralOut, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}

	// ---- estado da tela (thread de desenho) ----
	bool    g_settings = false;      // painel de modelos de fundo aberto
	float   g_scroll = 0.0f;
	int64_t g_lastReq = 0;
	struct Fing { int num = -1; float sx = 0, sy = 0, s0 = 0; int kind = 0; bool moved = false; } g_f;   // kind: 1 lista, 2 botao/celula, 3 fora, 4 painel

	ImColor C(const uint8_t* c, int a = 255) { return ImColor((int)c[0], (int)c[1], (int)c[2], a); }

	// fundo de uma linha com o modelo e o efeito (todos os efeitos sao desenhados por primitivas)
	void DrawStyleBg(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, int idx, float u, double t)
	{
		const CL::StyleDef& s = CL::Style(idx);
		const float w = b.x - a.x, h = b.y - a.y;
		ImColor c1 = C(s.c1, 240), c2 = C(s.c2, 240);
		if (s.effect == CL::E_Wave)       // fogo: o laranja respira
		{
			const float m = 0.5f + 0.5f * (float)std::sin(t * 2.3);
			c2 = ImColor((int)std::min(255.0f, s.c2[0] + 15.0f * m), (int)std::min(255.0f, s.c2[1] + 50.0f * m), (int)s.c2[2], 240);
		}
		r->drawRectFilledMulticolor(a, b, c1, c2, c2, c1);

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
			const float gap = 15.0f * u;
			const int n = std::max(2, (int)(w / gap));
			const int ph = (int)(t * 2.4);
			for (int i = 0; i < n; i++)
			{
				const float x = a.x + (i + 0.5f) * (w / (float)n);
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
			r->drawRect(ImVec2(a.x - 1.2f * u, a.y - 1.2f * u), ImVec2(b.x + 1.2f * u, b.y + 1.2f * u), C(s.edge, (int)(30 + 60 * p)), false, 2.6f * u);
			r->drawRect(a, b, C(s.edge, (int)(110 + 145 * p)), false, lw);
		}
		else if (s.effect == CL::E_Rainbow)
		{
			constexpr int N = 36;
			for (int i = 0; i < N; i++)
			{
				float hh = (float)i / (float)N + (float)(t * 0.3); hh -= std::floor(hh);
				const ImColor col = ImColor::HSV(hh, 0.8f, 1.0f);
				const float x0 = a.x + w * i / N, x1 = a.x + w * (i + 1) / N;
				r->drawLine(ImVec2(x0, a.y), ImVec2(x1, a.y), col, lw * 1.4f);
				r->drawLine(ImVec2(x0, b.y), ImVec2(x1, b.y), col, lw * 1.4f);
			}
			r->drawLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y), ImColor::HSV((float)std::fmod(t * 0.3, 1.0), 0.8f, 1.0f), lw * 1.4f);
			r->drawLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), ImColor::HSV((float)std::fmod(t * 0.3 + 0.5, 1.0), 0.8f, 1.0f), lw * 1.4f);
		}
		else r->drawRect(a, b, C(s.edge, 210), false, lw);
		(void)h;
	}

	void Icon(ImGuiRenderer* r, float cx, float cy, float rad, const ImColor& col)     // silhueta simples (cabeca + ombros)
	{
		auto disc = [&](float x, float y, float rr) { constexpr int N = 20; ImVec2 p[N]; for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(x + std::cos(an) * rr, y + std::sin(an) * rr); } r->drawConvexPolyFilled(p, N, col); };
		disc(cx, cy - rad * 0.28f, rad * 0.32f);
		disc(cx, cy + rad * 0.55f, rad * 0.5f);
	}
}

CentralUI::CentralUI() : Widget() {}

bool CentralUI::IsOpen() { return g_open.load(); }

void CentralUI::Open()
{
	if (g_open.load()) return;
	{ std::lock_guard<std::mutex> l(g_m); g_ev.clear(); }
	for (auto& c : g_claimed) c.store(false);
	g_f = Fing(); g_scroll = 0.0f; g_settings = false; g_lastReq = 0;
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
		uint16_t tot = 0, sl = 0, mins = 0, adm = 0; uint8_t st = 0;
		if (!bs.Read(tot) || !bs.Read(sl) || !bs.Read(mins) || !bs.Read(st) || !bs.Read(adm)) return;
		g_list.Header(tot, sl, mins, st, adm);
	}
	else if (op == 2)
	{
		uint8_t n = 0;
		if (!bs.Read(n) || !g_list.have) return;
		for (int i = 0; i < n; i++)
		{
			CL::Row row; uint8_t nl = 0;
			if (!bs.Read(row.pid) || !bs.Read(row.acc) || !bs.Read(row.style) || !bs.Read(row.border) || !bs.Read(row.flags) || !bs.Read(row.ping) || !bs.Read(nl)) return;
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

	CL::List L;
	std::vector<Ev> evs;
	{ std::lock_guard<std::mutex> l(g_m); L = g_list; evs.swap(g_ev); }

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float u  = std::min(scr.y / 720.0f, scr.x / 1100.0f);
	const float ox = scr.x * 0.5f - 640.0f * u, oy = (scr.y - 720.0f * u) * 0.5f;
	auto PX = [&](float v) { return ox + v * u; };
	auto PY = [&](float v) { return oy + v * u; };
	const ImVec2 pa(PX(430.0f), PY(36.0f)), pb(PX(850.0f), PY(684.0f));
	const ImVec2 xa(PX(806.0f), PY(46.0f)), xb(PX(838.0f), PY(74.0f));            // X fechar
	const ImVec2 ga(PX(796.0f), PY(104.0f)), gb(PX(834.0f), PY(142.0f));          // engrenagem
	const ImVec2 va(PX(446.0f), PY(214.0f)), vb(PX(834.0f), PY(672.0f));          // area da lista
	const float rowH = 44.0f * u, pitch = 50.0f * u;
	const float content = (float)L.rows.size() * pitch;
	auto In = [](float x, float y, const ImVec2& a, const ImVec2& b) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; };
	auto cellA = [&](int i) { return ImVec2(PX(446.0f + (i % 3) * 130.0f), PY(262.0f + (i / 3) * 70.0f)); };
	auto cellB = [&](int i) { const ImVec2 a = cellA(i); return ImVec2(a.x + 124.0f * u, a.y + 54.0f * u); };
	auto cellAt = [&](float x, float y) { for (int i = 0; i < CL::kStyles; i++) if (In(x, y, cellA(i), cellB(i))) return i; return -1; };

	g_scroll = CL::ClampScroll(g_scroll, content, vb.y - va.y);

	// ---- toques ----
	for (const Ev& e : evs)
	{
		if (e.type == 2 && g_f.num < 0)
		{
			g_f = Fing(); g_f.num = e.num; g_f.sx = e.x; g_f.sy = e.y; g_f.s0 = g_scroll;
			if (!In(e.x, e.y, pa, pb)) g_f.kind = 3;
			else if (In(e.x, e.y, xa, xb) || In(e.x, e.y, ga, gb) || (g_settings && cellAt(e.x, e.y) >= 0)) g_f.kind = 2;
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
				const int c0 = cellAt(f.sx, f.sy), c1 = cellAt(e.x, e.y);
				if (g_settings && c0 >= 0 && c0 == c1) { SendReq(2, c0); g_lastReq = 0; }   // escolhe o modelo; a lista volta com ele aplicado
			}
		}
	}

	// ---- painel ----
	r->drawRect(ImVec2(0, 0), scr, ImColor(0, 0, 0, 110), true);
	r->drawRoundedRect(pa, pb, ImColor(22, 22, 28, 240), 8.0f * u, true);
	r->drawRoundedRect(pa, pb, ImColor(70, 70, 82, 255), 8.0f * u, false, 1.0f);
	const ImColor cPink(236, 40, 96, 255), cText(238, 238, 244, 255), cMute(150, 150, 164, 255);

	// cabecalho
	r->drawRoundedRect(ImVec2(PX(446.0f), PY(46.0f)), ImVec2(PX(474.0f), PY(74.0f)), cPink, 5.0f * u, true);
	Icon(r, PX(460.0f), PY(60.0f), 10.0f * u, ImColor(255, 255, 255, 255));
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
		r->drawRoundedRect(ga, gb, pr || g_settings ? ImColor(236, 40, 96, 90) : ImColor(255, 255, 255, 22), 6.0f * u, true);
		r->drawRoundedRect(ga, gb, g_settings ? cPink : ImColor(255, 255, 255, 50), 6.0f * u, false, 1.0f);
		const float gx = (ga.x + gb.x) * 0.5f, gy = (ga.y + gb.y) * 0.5f;       // engrenagem: anel + 8 dentes
		for (int i = 0; i < 8; i++) { const float an = 0.785398f * i; r->drawLine(ImVec2(gx + std::cos(an) * 7.0f * u, gy + std::sin(an) * 7.0f * u), ImVec2(gx + std::cos(an) * 10.0f * u, gy + std::sin(an) * 10.0f * u), cText, 2.4f * u); }
		{ constexpr int N = 24; ImVec2 p[N]; for (int i = 0; i < N; i++) { const float an = 6.2831853f * i / N; p[i] = ImVec2(gx + std::cos(an) * 7.0f * u, gy + std::sin(an) * 7.0f * u); } r->drawPolyline(p, N, cText, true, 2.0f * u); }
	}

	// faixa de numeros: online, admins e populacao
	{
		char buf[48];
		const ImVec2 a1(PX(446.0f), PY(172.0f)), b1(PX(548.0f), PY(200.0f)), a2(PX(554.0f), PY(172.0f)), b2(PX(646.0f), PY(200.0f));
		r->drawRoundedRect(a1, b1, ImColor(255, 255, 255, 14), 5.0f * u, true);
		snprintf(buf, sizeof(buf), "%d online", (int)L.total);
		ImVec2 ts = r->calculateTextSize(buf, 11.0f * u);
		r->drawText(ImVec2((a1.x + b1.x - ts.x) * 0.5f, (a1.y + b1.y - ts.y) * 0.5f), cText, buf, false, 11.0f * u);
		r->drawRoundedRect(a2, b2, ImColor(255, 255, 255, 14), 5.0f * u, true);
		snprintf(buf, sizeof(buf), "%d admin", (int)L.admins);
		ts = r->calculateTextSize(buf, 11.0f * u);
		r->drawText(ImVec2((a2.x + b2.x - ts.x) * 0.5f, (a2.y + b2.y - ts.y) * 0.5f), cText, buf, false, 11.0f * u);
		const ImVec2 a3(PX(654.0f), PY(172.0f)), b3(PX(834.0f), PY(200.0f));
		r->drawRoundedRect(a3, b3, ImColor(255, 255, 255, 14), 5.0f * u, true);
		const float fill = CL::PopFill(L.total, L.slots);
		if (fill > 0.0f) r->drawRoundedRect(a3, ImVec2(a3.x + (b3.x - a3.x) * fill, b3.y), cPink, 5.0f * u, true);
		snprintf(buf, sizeof(buf), "%d/%d", (int)L.total, (int)L.slots);
		ts = r->calculateTextSize(buf, 11.0f * u);
		r->drawText(ImVec2((a3.x + b3.x - ts.x) * 0.5f, (a3.y + b3.y - ts.y) * 0.5f), cText, buf, true, 11.0f * u);
	}
	r->drawLine(ImVec2(PX(446.0f), PY(208.0f)), ImVec2(PX(834.0f), PY(208.0f)), ImColor(255, 255, 255, 0), 1.0f);

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
	if (L.rows.empty())
	{
		const char* msg = L.have ? "Ninguem online" : "Carregando...";
		const ImVec2 ts = r->calculateTextSize(msg, 12.0f * u);
		r->drawText(ImVec2((va.x + vb.x - ts.x) * 0.5f, va.y + 40.0f * u), cMute, msg, false, 12.0f * u);
		return;
	}
	const int localId = (pNetGame && pNetGame->GetPlayerPool()) ? (int)pNetGame->GetPlayerPool()->GetLocalPlayerID() : -1;
	r->pushClipRect(va, vb, true);
	for (size_t i = 0; i < L.rows.size(); i++)
	{
		const float y = va.y + (float)i * pitch - g_scroll;
		if (y + rowH < va.y || y > vb.y) continue;
		const CL::Row& row = L.rows[i];
		const ImVec2 a(va.x, y), b(vb.x - 8.0f * u, y + rowH);
		DrawStyleBg(r, a, b, row.style, u, t);
		const bool self = (int)row.pid == localId || (Avatar::AccountId() && row.acc == Avatar::AccountId());
		if (self) r->drawRect(ImVec2(a.x - 1.5f * u, a.y - 1.5f * u), ImVec2(b.x + 1.5f * u, b.y + 1.5f * u), ImColor(255, 255, 255, 200), false, 1.4f * u);
		char buf[64]; snprintf(buf, sizeof(buf), "%s", row.name.c_str());
		const float ty = a.y + (rowH - 13.0f * u) * 0.5f;
		r->drawText(ImVec2(a.x + 12.0f * u, ty), ImColor(255, 255, 255, 255), buf, true, 12.0f * u);
		const ImVec2 nsz = r->calculateTextSize(buf, 12.0f * u);
		snprintf(buf, sizeof(buf), "(%u)", (unsigned)row.acc);
		r->drawText(ImVec2(a.x + 16.0f * u + nsz.x, ty + 1.0f * u), ImColor(235, 235, 245, 190), buf, true, 10.5f * u);
		float rx = b.x - 10.0f * u;
		if (row.flags & 1)       // admin
		{
			const char* tg = "ADMIN"; const ImVec2 ts = r->calculateTextSize(tg, 10.0f * u);
			const ImVec2 ta(rx - ts.x - 12.0f * u, a.y + (rowH - 18.0f * u) * 0.5f), tb(rx, ta.y + 18.0f * u);
			r->drawRoundedRect(ta, tb, ImColor(0, 0, 0, 130), 4.0f * u, true);
			r->drawText(ImVec2(ta.x + 6.0f * u, ta.y + (18.0f * u - ts.y) * 0.5f), ImColor(255, 226, 120, 255), tg, false, 10.0f * u);
			rx = ta.x - 8.0f * u;
		}
		snprintf(buf, sizeof(buf), "%dms", (int)row.ping);
		const ImVec2 ps = r->calculateTextSize(buf, 9.5f * u);
		r->drawText(ImVec2(rx - ps.x, a.y + (rowH - ps.y) * 0.5f), ImColor(235, 235, 245, 170), buf, true, 9.5f * u);
	}
	r->popClipRect();
	if (content > vb.y - va.y)     // barra de rolagem
	{
		const float vh = vb.y - va.y, th = std::max(24.0f * u, vh * vh / content), tp = va.y + (vh - th) * (g_scroll / (content - vh));
		r->drawRoundedRect(ImVec2(vb.x - 4.0f * u, tp), ImVec2(vb.x - 1.5f * u, tp + th), ImColor(255, 255, 255, 90), 2.0f * u, true);
	}
}
