#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/actorpool.h"
#include "../../net/playerpool.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../game/actor.h"
#include "../../game/IconTexDb.h"
#include "jobs.h"
#include "maximomenu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern UI*       pUI;
extern CNetGame* pNetGame;
extern volatile bool g_bTouchCancel;     // game/multitouch.cpp

namespace
{
	struct TouchEv { int type; int num; float x; float y; bool cancel; };
	struct Job  { int id = 0; std::string name, descr, image; uint32_t smin = 0, smax = 0; int minLvl = 0, vac = 0, used = 0; };
	struct Mine { bool has = false; int id = 0; std::string name, date; int lvl = 1, xp = 0, done = 0; uint32_t earned = 0; bool duty = false; };
	struct Snap { int plLevel = 0; Mine mine; std::vector<Job> jobs; };

	std::atomic<bool> g_open{false}, g_flush{false}, g_resetView{false};
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::atomic<uint32_t> g_ver{0};
	std::atomic<int>  g_resultCode{-1};
	std::mutex        g_evMutex, g_snapMutex;
	std::vector<TouchEv> g_events;
	Snap              g_snap;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- estado da tela (so a thread de desenho) ----
	Snap     g_s;                  // copia local
	uint32_t g_seenVer = ~0u;
	int      g_tab = 0;            // 0 vagas | 1 meu emprego | 2 informacoes
	int      g_sel = -1;           // indice em g_s.jobs
	int      g_page = 0;           // pagina de cartoes (3 por pagina)
	int      g_confirm = 0;        // 0 nada | 1 assinar | 2 demissao
	std::string g_toast; int64_t g_toastUntil = 0; bool g_toastBad = false;

	// toques de mundo
	struct WorldTap { bool valid = false; float x = 0, y = 0; int64_t t = 0; };
	WorldTap g_wt;                 // dedo 0 em andamento (thread do jogo)
	std::atomic<bool>  g_tapReq{false};
	std::atomic<float> g_tapX{0}, g_tapY{0};
	int64_t g_lastTapSent = 0;

	// ---- leitura do pacote ----
	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], (unsigned)n)) return false;
		return true;
	}

	void Send(int op, int arg = -1)
	{
		if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED || !pNetGame->GetRakClient()) return;
		RakNet::BitStream bs;
		bs.Write((uint8_t)op);
		if (op == 1 || op == 2) bs.Write((uint16_t)arg);
		pNetGame->GetRakClient()->RPC(&RPC_JobsRequest, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}

	const char* ResultText(int c, bool& bad)
	{
		bad = true;
		switch (c)
		{
		case 0: bad = false; return "Carteira assinada! Você foi contratado.";
		case 1: bad = false; return "Você pediu demissão.";
		case 2: return "Seu nível é insuficiente para esta vaga.";
		case 3: return "Esta vaga está desativada.";
		case 4: return "Você já possui um emprego. Peça demissão primeiro.";
		case 5: return "Não há vagas disponíveis.";
		case 6: return "Este emprego não existe mais.";
		case 8: return "Faça login para usar a agência.";
		default: return "Erro no servidor. Tente de novo.";
		}
	}

	std::string Money(uint32_t v)
	{
		std::string s = std::to_string(v), o;
		for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return "US$ " + o;
	}

	// pinta o icone/imagem do emprego (textura do banco texdb/icones) ou uma letra no lugar
	void DrawJobImage(ImGuiRenderer* r, const Job& j, const ImVec2& a, const ImVec2& b, float k)
	{
		RwTexture* t = j.image.empty() ? nullptr : IconTexDb::Texture(j.image);
		if (t && t->raster)
		{
			const float cw = b.x - a.x, ch = b.y - a.y;
			const float iw = (float)(t->raster->width > 0 ? t->raster->width : 1), ih = (float)(t->raster->height > 0 ? t->raster->height : 1);
			const float s = std::min(cw / iw, ch / ih), dw = iw * s, dh = ih * s;
			const ImVec2 a2(a.x + (cw - dw) * 0.5f, a.y + (ch - dh) * 0.5f);
			r->drawImage(a2, ImVec2(a2.x + dw, a2.y + dh), (ImTextureID)t->raster, ImColor(255, 255, 255, 255), ImVec2(0, 0), ImVec2(1, 1));
			return;
		}
		r->drawRoundedRect(a, b, ImColor(40, 52, 74, 255), 8.0f * k, true);
		std::string ch = j.name.empty() ? "?" : j.name.substr(0, 1);
		const float fs = (b.y - a.y) * 0.5f;
		const ImVec2 ts = r->calculateTextSize(ch, fs);
		r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ImColor(120, 170, 240, 255), ch, false, fs);
	}

	enum Kind { K_NONE = 0, K_CLOSE, K_TAB, K_CARD, K_SIGN, K_QUIT, K_YES, K_NO, K_PANEL, K_PREV, K_NEXT };
	struct Hit { ImVec2 a, b; int kind; int idx; };
	struct Drag { int finger = -1; Hit hit{ImVec2(), ImVec2(), K_NONE, 0}; float sx = 0, sy = 0, dx = 0; bool moved = false; };
	Drag g_drag;
}

// =====================================================================================
//  API
// =====================================================================================
bool JobsUI::IsOpen() { return g_open.load(); }
void JobsUI::Close(bool notifyServer)
{
	if (!g_open.exchange(false)) return;
	g_confirm = 0; g_drag = Drag();
	if (notifyServer) Send(4);
}
void JobsUI::Reset()
{
	g_open.store(false); g_confirm = 0; g_drag = Drag(); g_tapReq.store(false); g_wt.valid = false;
	{ std::lock_guard<std::mutex> l(g_snapMutex); g_snap = Snap(); }
	g_ver.fetch_add(1);
}
bool JobsUI::ConsumeFlushRequest() { return g_flush.exchange(false); }
bool JobsUI::OnBackPressed()
{
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 400) return g_open.load();
	if (!g_open.load()) return false;
	s_last = now;
	Close(true);
	return true;
}
bool JobsUI::HandleRawTouch(int type, int num, int x, int y)
{
	const int n = num & 3;
	const bool open = g_open.load();
	if (!open && !g_claimed[n].load()) return false;
	if (open)
	{
		g_claimed[n].store(true);
		std::lock_guard<std::mutex> l(g_evMutex);
		if (g_events.size() < 256) g_events.push_back({type, n, (float)x, (float)y, g_bTouchCancel});
	}
	if (type == 1) g_claimed[n].store(false);
	return true;
}

void JobsUI::ObserveWorldTouch(int type, int num, int x, int y)
{
	if (g_open.load() || (num & 3) != 0) return;
	const int64_t now = NowMs();
	if (type == 2) { g_wt.valid = true; g_wt.x = (float)x; g_wt.y = (float)y; g_wt.t = now; }
	else if (type == 3)
	{
		if (g_wt.valid && (std::fabs((float)x - g_wt.x) > 28.0f || std::fabs((float)y - g_wt.y) > 28.0f)) g_wt.valid = false;
	}
	else if (type == 1)
	{
		if (g_wt.valid && !g_bTouchCancel && now - g_wt.t < 380) { g_tapX.store(g_wt.x); g_tapY.store(g_wt.y); g_tapReq.store(true); }
		g_wt.valid = false;
	}
}

void JobsUI::OnServerPacket(const unsigned char* data, int bytes)
{
	if (!data || bytes <= 0 || bytes > 16384) return;
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	uint8_t op = 0;
	if (!bs.Read(op)) return;
	if (op == 3) { Close(false); return; }
	if (op == 2) { uint8_t c = 0; if (bs.Read(c)) g_resultCode.store(c); return; }
	if (op != 1) return;

	Snap s;
	uint16_t lvl = 0; uint8_t has = 0, n = 0;
	if (!bs.Read(lvl) || !bs.Read(has)) return;
	s.plLevel = lvl;
	if (has)
	{
		uint16_t id = 0, xp = 0, dn = 0; uint8_t jl = 0, duty = 0; uint32_t earned = 0;
		if (!bs.Read(id) || !RdStr(bs, s.mine.name) || !RdStr(bs, s.mine.date) || !bs.Read(jl) || !bs.Read(xp) || !bs.Read(dn) || !bs.Read(earned) || !bs.Read(duty)) return;
		s.mine.has = true; s.mine.id = id; s.mine.lvl = jl; s.mine.xp = xp; s.mine.done = dn; s.mine.earned = earned; s.mine.duty = duty != 0;
	}
	if (!bs.Read(n)) return;
	for (int i = 0; i < n; i++)
	{
		Job j; uint16_t id = 0, ml = 0, vac = 0, used = 0;
		if (!bs.Read(id) || !RdStr(bs, j.name) || !RdStr(bs, j.descr) || !RdStr(bs, j.image) || !bs.Read(j.smin) || !bs.Read(j.smax) || !bs.Read(ml) || !bs.Read(vac) || !bs.Read(used)) return;
		j.id = id; j.minLvl = ml; j.vac = vac; j.used = used;
		s.jobs.push_back(std::move(j));
	}
	{ std::lock_guard<std::mutex> l(g_snapMutex); g_snap = std::move(s); }
	g_ver.fetch_add(1);
	if (!g_open.exchange(true)) { g_flush.store(true); g_resetView.store(true); }
}

JobsUI::JobsUI() : Widget() {}

// =====================================================================================
//  Desenho + toques
// =====================================================================================
void JobsUI::draw(ImGuiRenderer* r)
{
	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float  k   = scr.y / 540.0f;
	const int64_t now = NowMs();

	// ---- toque de mundo na recepcionista (tela fechada) ----
	if (g_tapReq.exchange(false) && !g_open.load() && pNetGame && pNetGame->GetActorPool() && pNetGame->GetPlayerPool() && now - g_lastTapSent > 1200)
	{
		const float tx = g_tapX.load(), ty = g_tapY.load();
		CActorPool* ap = pNetGame->GetActorPool();
		for (PLAYERID id = 0; id < MAX_ACTORS; id++)
		{
			CActor* a = ap->GetAt(id);
			if (!a || !a->m_pPed) continue;
			RwMatrix m = a->m_pPed->GetMatrix().ToRwMatrix();
			CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
			if (!lp || !lp->GetPlayerPed() || !lp->GetPlayerPed()->m_pPed) break;
			if (lp->GetPlayerPed()->m_pPed->GetDistanceFromPoint(m.pos.x, m.pos.y, m.pos.z) > 14.0f) continue;
			CVector feet(m.pos.x, m.pos.y, m.pos.z - 1.0f), head(m.pos.x, m.pos.y, m.pos.z + 0.85f), sf, sh;
			// CSprite::CalcScreenCoors
			((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&feet, &sf, 0, 0, 0, 0);
			((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&head, &sh, 0, 0, 0, 0);
			if (sf.z < 1.0f || sh.z < 1.0f) continue;
			const float h = std::fabs(sf.y - sh.y), w = h * 0.55f, cx = (sf.x + sh.x) * 0.5f, pad = std::max(14.0f * k, h * 0.12f);
			if (tx >= cx - w * 0.5f - pad && tx <= cx + w * 0.5f + pad && ty >= std::min(sf.y, sh.y) - pad && ty <= std::max(sf.y, sh.y) + pad)
			{
				g_lastTapSent = now;
				Send(1, (int)id);
				break;
			}
		}
	}

	if (!g_open.load()) { g_drag = Drag(); return; }

	if (g_resetView.exchange(false)) { g_tab = 0; g_sel = -1; g_page = 0; g_confirm = 0; g_drag = Drag(); g_toastUntil = 0; g_seenVer = ~0u; }
	{
		const uint32_t v = g_ver.load();
		if (v != g_seenVer)
		{
			std::lock_guard<std::mutex> l(g_snapMutex);
			g_s = g_snap; g_seenVer = v;
			if (g_sel >= (int)g_s.jobs.size()) g_sel = -1;
			g_confirm = 0;
		}
	}
	{
		const int rc = g_resultCode.exchange(-1);
		if (rc >= 0) { g_toast = ResultText(rc, g_toastBad); g_toastUntil = now + 3200; g_confirm = 0; if (rc == 0) g_tab = 1; }
	}

	// paleta Maximo RP (mesma familia do inventario: rosa 0xEC2860 sobre grafite)
	const ImColor cDim(0, 0, 0, 150), cPanel(17, 17, 20, 240), cCard(34, 34, 38, 240), cLine(62, 62, 70, 255), cText(240, 240, 244, 255), cSub(150, 150, 160, 255);
	const ImColor cPink(236, 40, 96, 255), cPinkBg(236, 40, 96, 40), cGreen(47, 191, 113, 255), cRed(229, 72, 77, 255), cOff(44, 44, 50, 255), cAmber(240, 176, 56, 255);

	std::vector<Hit> hits;
	r->drawRect(ImVec2(0, 0), scr, cDim, true);

	const float pw = std::min(scr.x - 24.0f * k, 860.0f * k), ph = std::min(scr.y - 20.0f * k, 440.0f * k);
	const ImVec2 P0((scr.x - pw) * 0.5f, (scr.y - ph) * 0.5f), P1(P0.x + pw, P0.y + ph);
	r->drawRoundedRect(P0, P1, cPanel, 12.0f * k, true);
	r->drawRoundedRect(P0, P1, cLine, 12.0f * k, false, 1.5f);
	hits.push_back({P0, P1, K_PANEL, 0});

	auto button = [&](const ImVec2& a2, const ImVec2& b2, const char* text, const ImColor& col, bool enabled, int kind, int idx, float fs = 13.0f)
	{
		r->drawRoundedRect(a2, b2, enabled ? col : cOff, 7.0f * k, true);
		const ImVec2 ts = r->calculateTextSize(text, fs * k);
		r->drawText(ImVec2((a2.x + b2.x - ts.x) * 0.5f, (a2.y + b2.y - ts.y) * 0.5f), enabled ? cText : cSub, text, false, fs * k);
		if (enabled) hits.push_back({a2, b2, kind, idx});
	};

	// ---- cabecalho ----
	{
		const ImVec2 ia(P0.x + 14.0f * k, P0.y + 12.0f * k), ib(ia.x + 30.0f * k, ia.y + 30.0f * k);
		r->drawRoundedRect(ia, ib, cPink, 8.0f * k, true);
		r->drawRoundedRect(ImVec2(ia.x + 8.0f * k, ia.y + 12.0f * k), ImVec2(ib.x - 8.0f * k, ib.y - 7.0f * k), cText, 2.0f * k, true);     // maleta
		r->drawRoundedRect(ImVec2(ia.x + 12.0f * k, ia.y + 7.0f * k), ImVec2(ib.x - 12.0f * k, ia.y + 13.0f * k), cText, 1.5f * k, false, 1.5f);
		r->drawText(ImVec2(ib.x + 10.0f * k, P0.y + 10.0f * k), cText, "Agência de Empregos", false, 17.0f * k);
		r->drawText(ImVec2(ib.x + 10.0f * k, P0.y + 30.0f * k), cPink, "MÁXIMO RP", false, 11.0f * k);
		const ImVec2 xa(P1.x - 46.0f * k, P0.y + 10.0f * k), xb(P1.x - 14.0f * k, P0.y + 42.0f * k);
		r->drawRoundedRect(xa, xb, cOff, 8.0f * k, true);
		const float m = 10.0f * k;
		r->drawLine(ImVec2(xa.x + m, xa.y + m), ImVec2(xb.x - m, xb.y - m), cText, 2.2f);
		r->drawLine(ImVec2(xb.x - m, xa.y + m), ImVec2(xa.x + m, xb.y - m), cText, 2.2f);
		hits.push_back({ImVec2(xa.x - 8 * k, xa.y - 8 * k), ImVec2(xb.x + 8 * k, xb.y + 8 * k), K_CLOSE, 0});
	}
	const float top = P0.y + 54.0f * k, bot = P1.y - 14.0f * k;
	r->drawRect(ImVec2(P0.x + 1.0f, top - 4.0f * k), ImVec2(P1.x - 1.0f, top - 3.0f * k), cLine, true);

	// ---- barra lateral: navegacao + emprego atual + nivel ----
	const float sx0 = P0.x + 14.0f * k, sx1 = sx0 + 196.0f * k;
	r->drawText(ImVec2(sx0, top + 2.0f * k), cSub, "NAVEGAÇÃO", false, 10.5f * k);
	static const char* kTabs[4] = { "Empregos disponíveis", "Meu emprego atual", "Informações", "Sair" };
	float ny = top + 18.0f * k;
	for (int i = 0; i < 4; i++)
	{
		const ImVec2 a2(sx0, ny), b2(sx1, ny + 30.0f * k);
		const bool act = (i == g_tab) && i < 3;
		r->drawRoundedRect(a2, b2, act ? cPink : (i == 3 ? ImColor(92, 30, 40, 255) : cCard), 7.0f * k, true);
		if (!act) r->drawRoundedRect(a2, b2, cLine, 7.0f * k, false);
		r->drawText(ImVec2(a2.x + 12.0f * k, a2.y + 8.0f * k), cText, kTabs[i], false, 12.5f * k);
		if (i < 3) r->drawText(ImVec2(b2.x - 18.0f * k, a2.y + 6.0f * k), cText, ">", false, 14.0f * k);
		hits.push_back({a2, b2, K_TAB, i});
		ny += 36.0f * k;
	}
	const bool employed = g_s.mine.has;
	{   // cartao EMPREGO ATUAL
		const ImVec2 a2(sx0, ny + 4.0f * k), b2(sx1, bot - 52.0f * k);
		r->drawRoundedRect(a2, b2, cCard, 8.0f * k, true);
		r->drawRoundedRect(a2, b2, cLine, 8.0f * k, false);
		r->drawText(ImVec2(a2.x + 10.0f * k, a2.y + 8.0f * k), cSub, "EMPREGO ATUAL", false, 10.5f * k);
		if (!employed) r->drawText(ImVec2(a2.x + 10.0f * k, a2.y + 28.0f * k), cText, "Desempregado", false, 14.0f * k);
		else
		{
			Job cur; cur.name = g_s.mine.name;
			for (const Job& j : g_s.jobs) if (j.id == g_s.mine.id) { cur = j; break; }
			DrawJobImage(r, cur, ImVec2(a2.x + 10.0f * k, a2.y + 26.0f * k), ImVec2(a2.x + 54.0f * k, a2.y + 70.0f * k), k);
			r->drawText(ImVec2(a2.x + 62.0f * k, a2.y + 28.0f * k), cText, g_s.mine.name, false, 14.0f * k);
			char b1[48]; snprintf(b1, sizeof b1, "LVL %d", g_s.mine.lvl);
			r->drawText(ImVec2(a2.x + 62.0f * k, a2.y + 48.0f * k), cPink, b1, false, 11.5f * k);
			button(ImVec2(a2.x + 10.0f * k, b2.y - 34.0f * k), ImVec2(b2.x - 10.0f * k, b2.y - 8.0f * k), "DEMITIR-SE", cOff, true, K_QUIT, 0, 11.5f);
			r->drawRoundedRect(ImVec2(a2.x + 10.0f * k, b2.y - 34.0f * k), ImVec2(b2.x - 10.0f * k, b2.y - 8.0f * k), cLine, 7.0f * k, false);
		}
	}
	{   // nivel + XP
		const ImVec2 a2(sx0, bot - 44.0f * k), b2(sx1, bot);
		char b1[64]; const int lvl = employed ? g_s.mine.lvl : 0;
		snprintf(b1, sizeof b1, employed ? "Nível %d" : "Nível do jogador: %d", employed ? lvl : g_s.plLevel);
		r->drawText(ImVec2(a2.x, a2.y), cText, b1, false, 13.0f * k);
		if (employed)
		{
			snprintf(b1, sizeof b1, "%d XP", g_s.mine.xp);
			const ImVec2 ts = r->calculateTextSize(b1, 11.0f * k);
			r->drawText(ImVec2(b2.x - ts.x, a2.y + 1.0f * k), cPink, b1, false, 11.0f * k);
			const int into = lvl >= 5 ? 60 : g_s.mine.xp - (lvl - 1) * 60;
			const float frac = lvl >= 5 ? 1.0f : std::clamp(into / 60.0f, 0.0f, 1.0f);
			const ImVec2 ba(a2.x, a2.y + 20.0f * k), bb(b2.x, a2.y + 26.0f * k);
			r->drawRoundedRect(ba, bb, cOff, 3.0f * k, true);
			if (frac > 0.01f) r->drawRoundedRect(ba, ImVec2(ba.x + (bb.x - ba.x) * frac, bb.y), cPink, 3.0f * k, true);
			r->drawText(ImVec2(a2.x, a2.y + 29.0f * k), cSub, "Complete tarefas para subir de nível.", false, 10.0f * k);
		}
	}

	// ---- area principal ----
	const float mx0 = sx1 + 18.0f * k, mx1 = P1.x - 14.0f * k, my0 = top + 2.0f * k;
	float listMax = 0.0f;
	const int perPage = 3;
	const int nJobs = (int)g_s.jobs.size();
	const int pages = std::max(1, (nJobs + perPage - 1) / perPage);
	if (g_tab == 0)
	{
		g_page = std::clamp(g_page, 0, pages - 1);
		r->drawText(ImVec2(mx0, my0), cText, "VAGAS DISPONÍVEIS", false, 17.0f * k);
		r->drawText(ImVec2(mx0, my0 + 22.0f * k), cSub, "Selecione uma vaga para ver os detalhes e assinar a carteira", false, 11.5f * k);
		if (pages > 1)
		{
			const ImVec2 la(mx1 - 70.0f * k, my0 + 2.0f * k), lb(la.x + 30.0f * k, la.y + 28.0f * k), ra(lb.x + 10.0f * k, la.y), rb(ra.x + 30.0f * k, lb.y);
			button(la, lb, "<", cOff, g_page > 0, K_PREV, 0, 14.0f); button(ra, rb, ">", cOff, g_page < pages - 1, K_NEXT, 0, 14.0f);
		}
		const float cy0 = my0 + 44.0f * k, cy1 = bot, gap = 12.0f * k;
		const float cw = (mx1 - mx0 - gap * (perPage - 1)) / perPage;
		if (nJobs == 0) r->drawText(ImVec2(mx0 + 4.0f * k, cy0 + 12.0f * k), cSub, "Nenhuma vaga aberta no momento.", false, 14.0f * k);
		for (int c = 0; c < perPage; c++)
		{
			const int i = g_page * perPage + c; if (i >= nJobs) break;
			const Job& j = g_s.jobs[(size_t)i];
			const ImVec2 a2(mx0 + c * (cw + gap), cy0), b2(a2.x + cw, cy1);
			const bool sel = (i == g_sel);
			const bool full = j.vac > 0 && j.used >= j.vac;
			const bool lowLvl = g_s.plLevel < j.minLvl;
			r->drawRoundedRect(a2, b2, sel ? ImColor(44, 30, 36, 245) : cCard, 10.0f * k, true);
			r->drawRoundedRect(a2, b2, sel ? cPink : cLine, 10.0f * k, false, sel ? 2.2f : 1.0f);
			const float ih = (cy1 - cy0) * (sel ? 0.30f : 0.36f);
			DrawJobImage(r, j, ImVec2(a2.x + 8.0f * k, a2.y + 8.0f * k), ImVec2(b2.x - 8.0f * k, a2.y + ih), k);
			{   // selo de nivel
				char lv[24]; snprintf(lv, sizeof lv, "LVL %d", j.minLvl);
				const ImVec2 ts = r->calculateTextSize(lv, 10.0f * k);
				const ImVec2 la2(a2.x + 14.0f * k, a2.y + ih - 24.0f * k), lb2(la2.x + ts.x + 14.0f * k, la2.y + 17.0f * k);
				r->drawRoundedRect(la2, lb2, lowLvl ? cRed : cPink, 4.0f * k, true);
				r->drawText(ImVec2(la2.x + 7.0f * k, la2.y + 2.5f * k), cText, lv, false, 10.0f * k);
			}
			float y = a2.y + ih + 8.0f * k; const float px = a2.x + 12.0f * k, maxW = cw - 24.0f * k;
			r->drawText(ImVec2(px, y), cText, j.name, false, 15.5f * k); y += 22.0f * k;
			r->drawText(ImVec2(px, y), cGreen, Money(j.smin) + " até " + Money(j.smax), false, 11.5f * k); y += 19.0f * k;
			{   // descricao (quebra por largura)
				const float fs = 11.0f * k; const int maxLines = sel ? 5 : 3;
				std::string line, word, text = (j.descr.empty() ? "Sem descrição." : j.descr) + " ";
				int lines = 0;
				for (char ch : text)
				{
					if (ch != ' ') { word += ch; continue; }
					std::string t2 = line.empty() ? word : line + " " + word;
					if (r->calculateTextSize(t2, fs).x > maxW && !line.empty()) { if (lines < maxLines) r->drawText(ImVec2(px, y), cSub, line, false, fs); y += 14.0f * k; lines++; line = word; }
					else line = t2;
					word.clear();
				}
				if (!line.empty() && lines < maxLines) { r->drawText(ImVec2(px, y), cSub, line, false, fs); y += 14.0f * k; }
			}
			if (sel)
			{
				y += 4.0f * k;
				char bf[80];
				r->drawText(ImVec2(px, y), cText, "Requisitos", false, 11.5f * k); y += 15.0f * k;
				snprintf(bf, sizeof bf, "• Nível %d ou mais (você: %d)", j.minLvl, g_s.plLevel);
				r->drawText(ImVec2(px, y), lowLvl ? cRed : cGreen, bf, false, 10.5f * k); y += 14.0f * k;
				r->drawText(ImVec2(px, y), cSub, "• Não ter outro emprego", false, 10.5f * k); y += 18.0f * k;
				r->drawText(ImVec2(px, y), cText, "Remuneração", false, 11.5f * k); y += 15.0f * k;
				r->drawText(ImVec2(px, y), cGreen, Money(j.smin) + " – " + Money(j.smax) + " por tarefa", false, 10.5f * k);
			}
			std::string st = full ? "Vagas esgotadas" : (j.vac > 0 ? "Vagas: " + std::to_string(j.vac - j.used) + " de " + std::to_string(j.vac) : "Vagas abertas");
			r->drawText(ImVec2(px, b2.y - 62.0f * k), full ? cRed : cGreen, st, false, 10.5f * k);
			const ImVec2 ba(a2.x + 10.0f * k, b2.y - 40.0f * k), bb(b2.x - 10.0f * k, b2.y - 10.0f * k);
			if (!sel) button(ba, bb, "SELECIONAR", cOff, true, K_CARD, i, 12.0f);
			else
			{
				const char* label = employed ? "VOCÊ JÁ TEM EMPREGO" : (full ? "VAGAS ESGOTADAS" : (lowLvl ? "NÍVEL INSUFICIENTE" : "ASSINAR CARTEIRA"));
				button(ba, bb, label, cPink, !employed && !full && !lowLvl, K_SIGN, i, 12.0f);
				hits.push_back({a2, ImVec2(b2.x, ba.y - 2.0f * k), K_CARD, i});
			}
			if (!sel) hits.push_back({a2, b2, K_CARD, i});
		}
	}
	else if (g_tab == 1)
	{
		if (!employed)
		{
			r->drawText(ImVec2(mx0, my0 + 6.0f * k), cText, "Você está desempregado.", false, 20.0f * k);
			r->drawText(ImVec2(mx0, my0 + 36.0f * k), cSub, "Abra \"Empregos disponíveis\", escolha uma vaga e assine sua carteira.", false, 13.0f * k);
		}
		else
		{
			const Mine& m = g_s.mine;
			r->drawText(ImVec2(mx0, my0), cSub, "EMPREGO ATUAL", false, 11.0f * k);
			r->drawText(ImVec2(mx0, my0 + 16.0f * k), cText, m.name, false, 26.0f * k);
			char b1[96]; float y = my0 + 62.0f * k;
			auto row = [&](const char* label, const std::string& val, const ImColor& col)
			{
				r->drawText(ImVec2(mx0, y), cSub, label, false, 13.0f * k);
				r->drawText(ImVec2(mx0 + 190.0f * k, y), col, val, false, 14.5f * k);
				y += 25.0f * k;
			};
			row("Contratado em", m.date, cText);
			snprintf(b1, sizeof b1, "%d de 5", m.lvl); row("Nível do emprego", b1, cText);
			snprintf(b1, sizeof b1, "%d tarefas", m.done); row("Tarefas concluídas", b1, cText);
			row("Total recebido", Money(m.earned), cGreen);
			row("Expediente", m.duty ? "Em serviço" : "Fora de serviço (use /servico)", m.duty ? cGreen : cAmber);
			button(ImVec2(mx1 - 200.0f * k, bot - 40.0f * k), ImVec2(mx1, bot), "PEDIR DEMISSÃO", cRed, true, K_QUIT, 0, 13.5f);
		}
	}
	else
	{
		float y = my0 + 2.0f * k;
		const char* lines[] = {
			"Como funciona", "",
			"1. Escolha uma vaga em \"Empregos disponíveis\" e toque em ASSINAR CARTEIRA.",
			"2. O servidor confere seu nível, as vagas e se você já tem emprego.",
			"3. Cozinheiro: use /servico para iniciar o expediente, vá ao balcão da cozinha",
			"    e use a tecla de ação (ou /cozinhar) para preparar pedidos. Cada pedido paga",
			"    entre o salário mínimo e o máximo, e o pagamento sobe com o nível do emprego.",
			"4. Você só pode ter um emprego por vez. Para trocar, peça demissão.",
			"5. Seu emprego fica salvo na sua conta: continua valendo quando você volta." };
		for (int i = 0; i < 9; i++)
		{
			r->drawText(ImVec2(mx0, y), i == 0 ? cPink : cSub, lines[i], false, i == 0 ? 18.0f * k : 12.5f * k);
			y += (i == 0 ? 28.0f : 20.0f) * k;
		}
	}
	(void)listMax;

	// ---- confirmacao ----
	if (g_confirm != 0)
	{
		r->drawRect(P0, P1, ImColor(0, 0, 0, 150), true);
		const float cw2 = 380.0f * k, ch2 = 150.0f * k;
		const ImVec2 a((scr.x - cw2) * 0.5f, (scr.y - ch2) * 0.5f), b(a.x + cw2, a.y + ch2);
		r->drawRoundedRect(a, b, cCard, 12.0f * k, true);
		r->drawRoundedRect(a, b, cPink, 12.0f * k, false, 2.0f);
		std::string t1, t2;
		if (g_confirm == 1 && g_sel >= 0 && g_sel < (int)g_s.jobs.size()) { t1 = "Assinar carteira?"; t2 = "Contratação: " + g_s.jobs[(size_t)g_sel].name; }
		else { t1 = "Pedir demissão?"; t2 = "Você perde o vínculo com " + g_s.mine.name + "."; }
		r->drawText(ImVec2(a.x + 18.0f * k, a.y + 14.0f * k), cText, t1, false, 20.0f * k);
		r->drawText(ImVec2(a.x + 18.0f * k, a.y + 46.0f * k), cSub, t2, false, 13.0f * k);
		hits.clear();
		hits.push_back({P0, P1, K_PANEL, 0});
		button(ImVec2(a.x + 18.0f * k, b.y - 52.0f * k), ImVec2(a.x + cw2 * 0.5f - 6.0f * k, b.y - 14.0f * k), "CONFIRMAR", g_confirm == 1 ? cGreen : cRed, true, K_YES, 0, 13.0f);
		button(ImVec2(a.x + cw2 * 0.5f + 6.0f * k, b.y - 52.0f * k), ImVec2(b.x - 18.0f * k, b.y - 14.0f * k), "CANCELAR", cOff, true, K_NO, 0, 13.0f);
	}

	// ---- aviso ----
	if (now < g_toastUntil)
	{
		const float fs = 14.5f * k;
		const ImVec2 ts = r->calculateTextSize(g_toast, fs);
		const ImVec2 a((scr.x - ts.x) * 0.5f - 16.0f * k, P1.y - 66.0f * k), b((scr.x + ts.x) * 0.5f + 16.0f * k, P1.y - 30.0f * k);
		r->drawRoundedRect(a, b, g_toastBad ? ImColor(120, 36, 40, 245) : ImColor(24, 100, 62, 245), 9.0f * k, true);
		r->drawText(ImVec2(a.x + 16.0f * k, a.y + (b.y - a.y - ts.y) * 0.5f), cText, g_toast, false, fs);
	}

	// ---- toques ----
	auto hitTest = [&](float x, float y) -> Hit
	{
		for (int i = (int)hits.size() - 1; i >= 0; i--)
			if (x >= hits[(size_t)i].a.x && x <= hits[(size_t)i].b.x && y >= hits[(size_t)i].a.y && y <= hits[(size_t)i].b.y) return hits[(size_t)i];
		return Hit{ImVec2(), ImVec2(), K_NONE, 0};
	};
	auto activate = [&](const Hit& h)
	{
		switch (h.kind)
		{
		case K_CLOSE: JobsUI::Close(true); break;
		case K_TAB:
			if (h.idx == 3) { JobsUI::Close(true); break; }
			g_tab = h.idx; break;
		case K_PREV: if (g_page > 0) g_page--; break;
		case K_NEXT: g_page++; break;
		case K_CARD: g_sel = h.idx; break;
		case K_SIGN: if (!employed && g_sel >= 0) g_confirm = 1; break;
		case K_QUIT: if (employed) g_confirm = 2; break;
		case K_NO: g_confirm = 0; break;
		case K_YES:
			if (g_confirm == 1 && g_sel >= 0 && g_sel < (int)g_s.jobs.size()) Send(2, g_s.jobs[(size_t)g_sel].id);
			else if (g_confirm == 2) Send(3);
			g_confirm = 0;
			break;
		default: break;
		}
	};

	std::vector<TouchEv> evs;
	{ std::lock_guard<std::mutex> l(g_evMutex); evs.swap(g_events); }
	for (const TouchEv& e : evs)
	{
		if (!g_open.load()) break;
		if (e.type == 2 && g_drag.finger < 0)
		{
			g_drag = Drag(); g_drag.finger = e.num; g_drag.hit = hitTest(e.x, e.y); g_drag.sx = e.x; g_drag.sy = e.y;
		}
		else if (e.num == g_drag.finger && e.type == 3)
		{
			g_drag.dx = e.x - g_drag.sx;
			if (std::fabs(e.x - g_drag.sx) > 12.0f * k || std::fabs(e.y - g_drag.sy) > 12.0f * k) g_drag.moved = true;
		}
		else if (e.num == g_drag.finger && e.type == 1)
		{
			const Drag d = g_drag; g_drag = Drag();
			if (e.cancel) continue;
			if (d.moved)
			{   // arrastar para os lados troca a pagina de vagas
				if (g_tab == 0 && g_confirm == 0 && std::fabs(d.dx) > 70.0f * k && std::fabs(e.y - d.sy) < std::fabs(d.dx)) g_page += (d.dx < 0) ? 1 : -1;
				continue;
			}
			if (d.hit.kind != K_NONE && d.hit.kind != K_PANEL)
			{
				const Hit h1 = hitTest(e.x, e.y);
				if (h1.kind == d.hit.kind && h1.idx == d.hit.idx) activate(d.hit);
			}
		}
	}
}
