// Marcador no mundo: base visual do recurso PlayNow_markers (MTA), autoria de "theking" - creditos mantidos.
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
#include "../../game/World.h"
#include "../../game/Camera.h"
#include "../../vendor/armhook/patch.h"
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
extern CGame*    pGame;
extern volatile bool g_bTouchCancel;     // game/multitouch.cpp

namespace
{
	struct TouchEv { int type; int num; float x; float y; bool cancel; };
	struct Job  { int id = 0; std::string name, descr, image; uint32_t smin = 0, smax = 0; int minLvl = 0, vac = 0, used = 0; };
	struct Mine { bool has = false; int id = 0; std::string name, date; int lvl = 1, xp = 0, done = 0; uint32_t earned = 0; bool duty = false; };
	struct Snap { int plLevel = 0; Mine mine; std::vector<Job> jobs; };
	// marcador desenhado no mundo (RPC 245 op 4 = empregos, op 6 = patinete): anel no chao + "(NPC) nome" + botao tocavel ao lado do NPC
	// kind 1 agencia | 2 ponto de trabalho | 3 aluguel de patinete (anel + icone) | 4 botao de acao fixo na tela (montar/dobrar patinete; sem anel)
	struct Mk { int kind = 1, state = 0; float x = 0, y = 0, z = 0, ax = 0, ay = 0, az = 0, radius = 1.3f; uint32_t rgb = 0xFFFFFF; std::string text, name; };
	struct MkHit { ImVec2 a, b; int kind; };

	std::atomic<bool> g_open{false}, g_flush{false}, g_resetView{false};
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::atomic<uint32_t> g_ver{0}, g_mkVer{0};
	std::atomic<uint32_t> g_mkLos{0xFFFFFFFFu};      // bit i = marcador i visivel da camera (calculado na thread do jogo, JobsUI_RenderWorld)
	std::atomic<int>  g_resultCode{-1};
	std::mutex        g_evMutex, g_snapMutex, g_mkMutex;
	std::vector<TouchEv> g_events;
	Snap              g_snap;
	std::vector<Mk>   g_mkNetA, g_mkNetB;            // recebido do servidor: A = empregos (op 4), B = patinete (op 6); protegido por g_mkMutex
	std::vector<Mk>   g_mkNet;                       // A + B (o que o desenho le)
	void MergeMk() { g_mkNet = g_mkNetA; g_mkNet.insert(g_mkNet.end(), g_mkNetB.begin(), g_mkNetB.end()); }   // chamar com g_mkMutex

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- estado da tela (so a thread de desenho) ----
	Snap     g_s;                  // copia local
	uint32_t g_seenVer = ~0u, g_seenMkVer = ~0u;
	std::vector<Mk>    g_mk;                 // copia local dos marcadores
	std::vector<MkHit> g_mkHit;              // areas tocaveis dos circulos (do quadro anterior)
	int      g_sel = -1;           // indice em g_s.jobs
	int      g_page = 0;           // pagina de cartoes (3 por pagina)
	int      g_confirm = 0;        // 0 nada | 1 assinar | 2 demissao | 3 informacoes da vaga
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
		case 7: return "Agência indisponível: o serviço de empregos não respondeu.";
		case 8: return "Faça login para usar a agência.";
		default: return "Erro no servidor. Tente de novo.";
		}
	}

	std::string Money(uint32_t v)
	{
		std::string s = std::to_string(v), o;
		for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return "R$ " + o;
	}

	// pinta a imagem do emprego (textura do banco texdb/icones) preenchendo o retangulo (corta o excesso) ou uma letra no lugar
	void DrawJobImage(ImGuiRenderer* r, const Job& j, const ImVec2& a, const ImVec2& b, float u)
	{
		RwTexture* t = j.image.empty() ? nullptr : IconTexDb::Texture(j.image);
		if (t && t->raster)
		{
			const float cw = b.x - a.x, ch = b.y - a.y;
			const float iw = (float)(t->raster->width > 0 ? t->raster->width : 1), ih = (float)(t->raster->height > 0 ? t->raster->height : 1);
			ImVec2 uv0(0, 0), uv1(1, 1);
			if (iw / ih > cw / ch) { const float f = (cw / ch) / (iw / ih); uv0.x = (1.0f - f) * 0.5f; uv1.x = 1.0f - uv0.x; }
			else                   { const float f = (iw / ih) / (cw / ch); uv0.y = (1.0f - f) * 0.5f; uv1.y = 1.0f - uv0.y; }
			r->drawImage(a, b, (ImTextureID)t->raster, ImColor(190, 190, 190, 255), uv0, uv1);
			return;
		}
		r->drawRoundedRect(a, b, ImColor(26, 26, 30, 255), 5.0f * u, true);
		std::string ch = j.name.empty() ? "?" : j.name.substr(0, 1);
		const float fs = (b.y - a.y) * 0.5f;
		const ImVec2 ts = r->calculateTextSize(ch, fs);
		r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ImColor(236, 40, 96, 200), ch, false, fs);
	}

	bool Proj(float x, float y, float z, ImVec2& o, float& depth)
	{
		CVector w(x, y, z), sc;
		((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&w, &sc, 0, 0, 0, 0);   // CSprite::CalcScreenCoors
		if (sc.z < 1.0f) return false;
		o = ImVec2(sc.x, sc.y); depth = sc.z;
		return true;
	}

	enum Kind { K_NONE = 0, K_CLOSE, K_CARD, K_SIGN, K_QUIT, K_YES, K_NO, K_PANEL, K_PREV, K_NEXT, K_INFO };
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
	{ std::lock_guard<std::mutex> l(g_mkMutex); g_mkNetA.clear(); g_mkNetB.clear(); g_mkNet.clear(); }
	g_mkVer.fetch_add(1);
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
	if (op == 4 || op == 6)
	{
		uint8_t n = 0;
		if (!bs.Read(n) || n > 8) return;
		std::vector<Mk> v;
		for (int i = 0; i < n; i++)
		{
			Mk m; uint8_t kind = 0, st = 0;
			if (!bs.Read(kind) || !bs.Read(m.x) || !bs.Read(m.y) || !bs.Read(m.z) || !bs.Read(m.ax) || !bs.Read(m.ay) || !bs.Read(m.az) || !bs.Read(m.radius) || !bs.Read(m.rgb) || !RdStr(bs, m.text) || !RdStr(bs, m.name) || !bs.Read(st)) return;
			if (!(m.radius >= 0.3f && m.radius <= 6.0f)) m.radius = 1.3f;
			m.kind = (kind >= 1 && kind <= 4) ? (int)kind : 1; m.state = st ? 1 : 0; m.rgb &= 0xFFFFFF;
			v.push_back(std::move(m));
		}
		{ std::lock_guard<std::mutex> l(g_mkMutex); (op == 4 ? g_mkNetA : g_mkNetB) = std::move(v); MergeMk(); }
		g_mkVer.fetch_add(1);
		return;
	}
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

// Marcador no estilo PlayNow_markers (MTA) de "theking" (creditos mantidos). Anel e icone sao desenhados NA CENA 3D (JobsUI_RenderWorld); aqui no overlay ficam so "(NPC) nome" sobre a cabeca + botao tocavel ao lado do NPC (tudo e por toque: o Android nao tem tecla E)
static void DrawMarkers(ImGuiRenderer* r, float k, int64_t now)
{
	g_mkHit.clear();
	{
		const uint32_t v = g_mkVer.load();
		if (v != g_seenMkVer) { std::lock_guard<std::mutex> l(g_mkMutex); g_mk = g_mkNet; g_seenMkVer = v; }
	}
	if (g_mk.empty()) return;
	const float t = (float)(now % 100000) * 0.001f;
	const uint32_t los = g_mkLos.load();
	for (size_t mi = 0; mi < g_mk.size(); mi++)
	{
		const Mk& m = g_mk[mi];
		if (mi < 32 && !(los & (1u << mi))) continue;      // atras de parede/carro: sem nome nem botao
		const std::string label = !m.text.empty() ? m.text : (m.kind == 4 ? std::string("Patinete") : m.kind == 3 ? std::string("Alugar patinete") : m.kind == 2 ? std::string(m.state ? "Preparar pedido" : "Iniciar serviço") : std::string("Abrir agência"));
		const float fs = 11.5f * k, h = 24.0f * k;
		const ImVec2 ts = r->calculateTextSize(label, fs);
		ImVec2 a, b;
		if (m.kind == 4)
		{
			// botao fixo na tela (nao e de mundo): lado direito, abaixo do topo
			const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
			b = ImVec2(scr.x - 20.0f * k, scr.y * 0.32f + h);
			a = ImVec2(b.x - (ts.x + 34.0f * k), scr.y * 0.32f);
		}
		else
		{
			// ---- nome do NPC (acima da cabeca) e botao (ao lado, na altura do peito) ----
			ImVec2 head, chest; float dh = 0.0f, dc = 0.0f;
			if (!Proj(m.ax, m.ay, m.az + 1.05f, head, dh) || !Proj(m.ax, m.ay, m.az + 0.25f, chest, dc)) continue;
			if (dc > (m.kind == 2 ? 7.0f : 12.0f)) continue;           // so perto: o servidor tambem confere a distancia
			if (!m.name.empty())
			{
				const std::string nm = "(NPC) " + m.name;
				const float fn = 12.0f * k;
				const ImVec2 tn = r->calculateTextSize(nm, fn);
				r->drawText(ImVec2(head.x - tn.x * 0.5f, head.y - tn.y), ImColor(255, 255, 255, 255), nm, true, fn);
			}
			a = ImVec2(chest.x + 22.0f * k, chest.y - h * 0.5f); b = ImVec2(a.x + ts.x + 34.0f * k, a.y + h);
		}
		const float pulse = 0.5f + 0.5f * std::sin(t * 4.0f);
		r->drawRoundedRect(a, b, ImColor(22, 22, 26, 215), 6.0f * k, true);
		r->drawRoundedRect(a, b, ImColor(255, 255, 255, 60 + (int)(60.0f * pulse)), 6.0f * k, false, 1.2f);
		const ImVec2 dc2(a.x + 13.0f * k, a.y + h * 0.5f);
		r->drawRoundedRect(ImVec2(dc2.x - 5.5f * k, dc2.y - 5.5f * k), ImVec2(dc2.x + 5.5f * k, dc2.y + 5.5f * k), ImColor(236, 40, 96, 255), 5.5f * k, true);
		r->drawText(ImVec2(a.x + 26.0f * k, a.y + (h - ts.y) * 0.5f), ImColor(235, 235, 240, 255), label, false, fs);
		const float pad = 8.0f * k;
		g_mkHit.push_back({ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad), m.kind});
	}
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

	// ---- marcadores no mundo (anel no chao, circulo com icone, seta, texto sobre o NPC) ----
	if (!g_open.load()) DrawMarkers(r, k, now);
	else g_mkHit.clear();

	// ---- toque de mundo: primeiro nos circulos, depois na propria recepcionista (tela fechada) ----
	bool tapped = false;
	if (g_tapReq.load() && !g_open.load() && now - g_lastTapSent > 1200)
	{
		const float tx = g_tapX.load(), ty = g_tapY.load();
		for (const MkHit& h : g_mkHit)
			if (tx >= h.a.x && tx <= h.b.x && ty >= h.a.y && ty <= h.b.y)
			{
				g_tapReq.store(false); g_lastTapSent = now; tapped = true;
				Send(h.kind == 4 ? 9 : h.kind == 3 ? 8 : h.kind == 2 ? 7 : 6);
				break;
			}
	}
	if (!tapped && g_tapReq.exchange(false) && !g_open.load() && pNetGame && pNetGame->GetActorPool() && pNetGame->GetPlayerPool() && now - g_lastTapSent > 1200)
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

	if (!g_open.load())
	{
		g_drag = Drag();
		const int rc = g_resultCode.exchange(-1);       // erro/aviso do servidor com a tela fechada (ex.: servico fora do ar): mostra no topo da tela
		if (rc >= 0) { g_toast = ResultText(rc, g_toastBad); g_toastUntil = now + 4200; }
		if (now < g_toastUntil)
		{
			const float fs = 14.0f * k;
			const ImVec2 ts = r->calculateTextSize(g_toast, fs);
			const ImVec2 a((scr.x - ts.x) * 0.5f - 16.0f * k, 70.0f * k), b((scr.x + ts.x) * 0.5f + 16.0f * k, 70.0f * k + ts.y + 16.0f * k);
			r->drawRoundedRect(a, b, g_toastBad ? ImColor(120, 36, 40, 245) : ImColor(24, 100, 62, 245), 8.0f * k, true);
			r->drawText(ImVec2(a.x + 16.0f * k, a.y + 8.0f * k), ImColor(244, 244, 247, 255), g_toast, false, fs);
		}
		return;
	}

	if (g_resetView.exchange(false)) { g_sel = -1; g_page = 0; g_confirm = 0; g_drag = Drag(); g_toastUntil = 0; g_seenVer = ~0u; }
	{
		const uint32_t v = g_ver.load();
		if (v != g_seenVer)
		{
			std::lock_guard<std::mutex> l(g_snapMutex);
			g_s = g_snap; g_seenVer = v;
			if (g_sel >= (int)g_s.jobs.size()) g_sel = -1;
			if (g_sel < 0 && g_s.mine.has)
				for (int i = 0; i < (int)g_s.jobs.size(); i++) if (g_s.jobs[(size_t)i].id == g_s.mine.id) { g_sel = i; g_page = i / 3; break; }
			g_confirm = 0;
		}
	}
	{
		const int rc = g_resultCode.exchange(-1);
		if (rc >= 0) { g_toast = ResultText(rc, g_toastBad); g_toastUntil = now + 3200; g_confirm = 0; }
	}

	// ---- layout identico ao da referencia (coordenadas em "pixels de referencia": painel 797 x 448) ----
	const ImColor cDim(0, 0, 0, 120), cPanel(30, 30, 33, 236), cCard(46, 46, 50, 228), cLine(82, 82, 90, 255), cText(244, 244, 247, 255), cSub(160, 160, 170, 255);
	const ImColor cPink(236, 40, 96, 255), cGreen(60, 200, 120, 255), cRed(229, 72, 77, 255), cOff(58, 58, 64, 255), cDark(22, 22, 25, 255);

	std::vector<Hit> hits;
	r->drawRect(ImVec2(0, 0), scr, cDim, true);

	const float u = std::min(0.74f * scr.y / 448.0f, 0.95f * scr.x / 797.0f);
	const ImVec2 P0((scr.x - 797.0f * u) * 0.5f, (scr.y - 448.0f * u) * 0.5f), P1(P0.x + 797.0f * u, P0.y + 448.0f * u);
	auto V   = [&](float x, float y) { return ImVec2(P0.x + x * u, P0.y + y * u); };
	auto txt = [&](float x, float y, const ImColor& c, const std::string& s, float fs) { r->drawText(V(x, y), c, s, false, fs * u); };
	auto txtR = [&](float xr, float y, const ImColor& c, const std::string& s, float fs)
	{ const ImVec2 ts = r->calculateTextSize(s, fs * u); r->drawText(ImVec2(P0.x + xr * u - ts.x, P0.y + y * u), c, s, false, fs * u); };
	auto wrap = [&](float x, float y, const std::string& text, const ImColor& col, float fs, float maxW, int maxLines, float lh) -> int
	{
		std::string line, word; int lines = 0; const std::string t = text + " ";
		for (char ch : t)
		{
			if (ch != ' ') { word += ch; continue; }
			const std::string t2 = line.empty() ? word : line + " " + word;
			if (r->calculateTextSize(t2, fs * u).x > maxW * u && !line.empty()) { if (lines < maxLines) txt(x, y + lines * lh, col, line, fs); lines++; line = word; }
			else line = t2;
			word.clear();
		}
		if (!line.empty() && lines < maxLines) { txt(x, y + lines * lh, col, line, fs); lines++; }
		return lines < maxLines ? lines : maxLines;
	};
	auto button = [&](float x0, float y0, float x1, float y1, const char* text, const ImColor& col, bool enabled, int kind, int idx, float fs)
	{
		const ImVec2 a2 = V(x0, y0), b2 = V(x1, y1);
		r->drawRoundedRect(a2, b2, enabled ? col : cOff, 5.0f * u, true);
		if (col.Value.x != cPink.Value.x || !enabled) r->drawRoundedRect(a2, b2, cLine, 5.0f * u, false, 1.0f);
		const ImVec2 ts = r->calculateTextSize(text, fs * u);
		r->drawText(ImVec2((a2.x + b2.x - ts.x) * 0.5f, (a2.y + b2.y - ts.y) * 0.5f), enabled ? cText : cSub, text, false, fs * u);
		if (enabled) hits.push_back({a2, b2, kind, idx});
	};
	auto briefcase = [&](float x, float y, float s, const ImColor& c)
	{
		r->drawRoundedRect(V(x + s * 0.30f, y + s * 0.10f), V(x + s * 0.70f, y + s * 0.38f), c, 1.5f * u, false, 1.4f);
		r->drawRoundedRect(V(x + s * 0.05f, y + s * 0.30f), V(x + s * 0.95f, y + s * 0.90f), c, 2.0f * u, true);
	};
	auto dot = [&](float cx, float cy, float rad, const ImColor& c) { r->drawRoundedRect(V(cx - rad, cy - rad), V(cx + rad, cy + rad), c, rad * u, true); };

	r->drawRoundedRect(P0, P1, cPanel, 8.0f * u, true);
	r->drawRoundedRect(P0, P1, cLine, 8.0f * u, false, 1.2f);
	hits.push_back({P0, P1, K_PANEL, 0});

	// ---- cabecalho ----
	r->drawRoundedRect(V(15, 15), V(41, 41), cPink, 6.0f * u, true);
	briefcase(20.0f, 20.0f, 16.0f, cText);
	txt(50, 20, cText, "Agência de Empregos", 12.5f);
	r->drawRoundedRect(V(756, 16), V(782, 42), cOff, 6.0f * u, true);
	r->drawLine(V(765, 25), V(773, 33), cText, 2.0f);
	r->drawLine(V(773, 25), V(765, 33), cText, 2.0f);
	{ const ImVec2 xa = V(748, 8), xb = V(790, 50); hits.push_back({xa, xb, K_CLOSE, 0}); }

	const bool employed = g_s.mine.has;
	const int nJobs = (int)g_s.jobs.size();
	int curIdx = -1;
	if (employed) for (int i = 0; i < nJobs; i++) if (g_s.jobs[(size_t)i].id == g_s.mine.id) { curIdx = i; break; }

	// ---- barra lateral ----
	txt(15, 58, cSub, "NAVEGAÇÃO", 8.0f);
	r->drawRoundedRect(V(15, 73), V(200, 110), cPink, 6.0f * u, true);
	briefcase(25.0f, 82.0f, 18.0f, cText);
	txt(52, 85, cText, "AGÊNCIA", 10.0f);
	txtR(190, 84, cText, ">", 12.0f);

	{   // EMPREGO ATUAL
		r->drawRoundedRect(V(15, 123), V(200, 312), cCard, 7.0f * u, true);
		txt(25, 131, cSub, "EMPREGO ATUAL", 8.0f);
		Job cur; cur.name = g_s.mine.name;
		if (curIdx >= 0) cur = g_s.jobs[(size_t)curIdx];
		if (employed)
		{
			DrawJobImage(r, cur, V(20, 147), V(195, 226), u);
			char lv[24]; snprintf(lv, sizeof lv, "LVL %d", g_s.mine.lvl);
			const ImVec2 ts = r->calculateTextSize(lv, 7.5f * u);
			r->drawRoundedRect(V(26, 207), ImVec2(V(26, 207).x + ts.x + 10.0f * u, V(26, 221).y), cPink, 3.0f * u, true);
			txt(31, 208.5f, cText, lv, 7.5f);
			txt(25, 233, cText, g_s.mine.name, 11.5f);
			dot(30, 258, 3.0f, cPink);
			txt(38, 253, cText, curIdx >= 0 ? Money(cur.smin) + " até " + Money(cur.smax) : "", 8.0f);
			button(25, 277, 190, 302, "DEMITIR-SE", cOff, true, K_QUIT, 0, 9.0f);
		}
		else
		{
			r->drawRoundedRect(V(20, 147), V(195, 226), cDark, 5.0f * u, true);
			txt(30, 175, cSub, "Desempregado", 11.0f);
			txt(30, 195, cSub, "Escolha uma vaga ao lado.", 8.0f);
		}
	}
	{   // nivel + XP
		r->drawRoundedRect(V(15, 322), V(200, 388), cCard, 7.0f * u, true);
		char b1[64]; const int lvl = employed ? g_s.mine.lvl : 0;
		snprintf(b1, sizeof b1, "Nível %d", employed ? lvl : g_s.plLevel);
		txt(25, 331, cText, b1, 11.0f);
		float frac = 0.0f;
		if (employed)
		{
			const int into = lvl >= 5 ? 60 : g_s.mine.xp - (lvl - 1) * 60;
			frac = lvl >= 5 ? 1.0f : std::clamp(into / 60.0f, 0.0f, 1.0f);
			snprintf(b1, sizeof b1, "%d XP - %d%%", g_s.mine.xp, (int)std::lround(frac * 100.0f));
			txtR(190, 334, cPink, b1, 8.0f);
		}
		r->drawRoundedRect(V(25, 350), V(190, 355), cOff, 2.5f * u, true);
		if (frac > 0.01f) r->drawRoundedRect(V(25, 350), V(25 + 165.0f * frac, 355), cPink, 2.5f * u, true);
		wrap(25, 362, "Complete trabalhos para subir de nível e desbloquear novas profissões", cSub, 7.5f, 165, 2, 10.0f);
	}
	{   // jogador
		dot(33, 413, 10.0f, cOff);
		dot(33, 410, 3.2f, cText);
		r->drawRoundedRect(V(27.5f, 415), V(38.5f, 420), cText, 2.5f * u, true);
		CPlayerPool* pp = (pNetGame && pNetGame->GetPlayerPool()) ? pNetGame->GetPlayerPool() : nullptr;
		const char* nm = pp ? pp->GetLocalPlayerName() : nullptr;
		txt(50, 404, cText, nm ? nm : "", 8.5f);
		if (pp) { char b1[24]; snprintf(b1, sizeof b1, "ID: %d", (int)pp->GetLocalPlayerID()); txt(50, 417, cPink, b1, 7.5f); }
	}

	// ---- vagas ----
	const int perPage = 3;
	const int pages = std::max(1, (nJobs + perPage - 1) / perPage);
	g_page = std::clamp(g_page, 0, pages - 1);
	txt(232, 54, cText, "VAGAS DISPONÍVEIS", 15.0f);
	txt(232, 74, cSub, "Selecione uma vaga para iniciar o serviço", 8.0f);
	button(719, 56, 747, 82, "<", cOff, g_page > 0, K_PREV, 0, 12.0f);
	button(754, 56, 782, 82, ">", cOff, g_page < pages - 1, K_NEXT, 0, 12.0f);
	if (nJobs == 0) txt(236, 120, cSub, "Nenhuma vaga aberta no momento.", 11.0f);
	for (int c = 0; c < perPage; c++)
	{
		const int i = g_page * perPage + c; if (i >= nJobs) break;
		const Job& j = g_s.jobs[(size_t)i];
		const float cx = 232.0f + c * 188.0f, cy = 97.0f, cw = 176.0f, ch = 336.0f;
		const bool sel = (i == g_sel), cur = (i == curIdx);
		const bool full = j.vac > 0 && j.used >= j.vac;
		const bool lowLvl = g_s.plLevel < j.minLvl;
		if (sel) for (int gI = 4; gI >= 1; gI--)
			r->drawRoundedRect(V(cx - gI * 1.6f, cy - gI * 1.6f), V(cx + cw + gI * 1.6f, cy + ch + gI * 1.6f), ImColor(236, 40, 96, 80 - gI * 16), (7.0f + gI) * u, false, 1.6f);
		r->drawRoundedRect(V(cx, cy), V(cx + cw, cy + ch), cCard, 7.0f * u, true);
		r->drawRoundedRect(V(cx, cy), V(cx + cw, cy + ch), sel ? cPink : cLine, 7.0f * u, false, sel ? 2.0f : 1.0f);
		hits.push_back({V(cx, cy), V(cx + cw, cy + ch), K_CARD, i});
		DrawJobImage(r, j, V(cx + 4, cy + 4), V(cx + cw - 4, cy + 112), u);
		{   // selo de nivel
			char lv[24]; snprintf(lv, sizeof lv, "LVL %d", j.minLvl);
			const ImVec2 ts = r->calculateTextSize(lv, 7.5f * u);
			const ImVec2 la = V(cx + 10, cy + 90);
			r->drawRoundedRect(la, ImVec2(la.x + ts.x + 10.0f * u, V(0, cy + 104).y), lowLvl ? cRed : cPink, 3.0f * u, true);
			txt(cx + 15, cy + 91.5f, cText, lv, 7.5f);
		}
		{   // icone de livro = informacoes completas
			r->drawRoundedRect(V(cx + cw - 28, cy + 10), V(cx + cw - 10, cy + 28), ImColor(0, 0, 0, 140), 4.0f * u, true);
			r->drawRoundedRect(V(cx + cw - 25, cy + 14), V(cx + cw - 13, cy + 24), cText, 1.5f * u, false, 1.3f);
			r->drawLine(V(cx + cw - 19, cy + 14), V(cx + cw - 19, cy + 24), cText, 1.3f);
			hits.push_back({V(cx + cw - 34, cy + 6), V(cx + cw - 6, cy + 32), K_INFO, i});
		}
		txt(cx + 12, cy + 121, cText, j.name, 12.5f);
		dot(cx + 16, cy + 151, 3.0f, cPink);
		txt(cx + 24, cy + 146, cText, Money(j.smin) + " até " + Money(j.smax), 8.5f);
		wrap(cx + 12, cy + 166, j.descr.empty() ? "Sem descrição." : j.descr, cSub, 8.5f, cw - 24, sel ? 3 : 4, 11.0f);
		if (sel)
		{
			char bf[80];
			txt(cx + 12, cy + 212, cText, "Requisitos", 8.5f);
			snprintf(bf, sizeof bf, "• Nível %d ou mais (você: %d)", j.minLvl, g_s.plLevel);
			txt(cx + 12, cy + 225, lowLvl ? cRed : cGreen, bf, 8.0f);
			txt(cx + 12, cy + 237, cSub, "• Não ter outro emprego", 8.0f);
			txt(cx + 12, cy + 251, cText, "Remuneração", 8.5f);
			txt(cx + 12, cy + 264, cGreen, Money(j.smin) + " – " + Money(j.smax) + " por tarefa", 8.0f);
		}
		const std::string st = full ? "Vagas esgotadas" : (j.vac > 0 ? "Vagas: " + std::to_string(j.vac - j.used) + " de " + std::to_string(j.vac) : "Vagas abertas");
		txt(cx + 12, cy + 282, full ? cRed : cGreen, st, 8.0f);
		if (cur) button(cx + 10, cy + 300, cx + cw - 10, cy + 324, "DEMITIR-SE", cOff, true, K_QUIT, 0, 9.0f);
		else
		{
			const char* label = employed ? "JÁ POSSUI EMPREGO" : (full ? "VAGAS ESGOTADAS" : (lowLvl ? "NÍVEL INSUFICIENTE" : "ASSINAR CARTEIRA"));
			button(cx + 10, cy + 300, cx + cw - 10, cy + 324, label, sel ? cPink : cOff, !employed && !full && !lowLvl, K_SIGN, i, 9.0f);
		}
	}

	// ---- confirmacao / informacoes ----
	if (g_confirm != 0)
	{
		r->drawRect(P0, P1, ImColor(0, 0, 0, 150), true);
		hits.clear();
		hits.push_back({P0, P1, K_PANEL, 0});
		const bool info = (g_confirm == 3);
		const float mw = info ? 400.0f : 330.0f, mh = info ? 250.0f : 140.0f, mx = 398.5f - mw * 0.5f, my = 224.0f - mh * 0.5f;
		r->drawRoundedRect(V(mx, my), V(mx + mw, my + mh), ImColor(36, 36, 40, 250), 8.0f * u, true);
		r->drawRoundedRect(V(mx, my), V(mx + mw, my + mh), cPink, 8.0f * u, false, 2.0f);
		const bool okJob = g_sel >= 0 && g_sel < nJobs;
		if (info && okJob)
		{
			const Job& j = g_s.jobs[(size_t)g_sel];
			char bf[96];
			txt(mx + 16, my + 14, cText, j.name, 14.0f);
			wrap(mx + 16, my + 36, j.descr.empty() ? "Sem descrição." : j.descr, cSub, 9.0f, mw - 32, 3, 12.0f);
			snprintf(bf, sizeof bf, "Requisito: nível %d ou mais (você: %d)", j.minLvl, g_s.plLevel);
			txt(mx + 16, my + 82, g_s.plLevel < j.minLvl ? cRed : cGreen, bf, 9.0f);
			txt(mx + 16, my + 98, cText, "Remuneração: " + Money(j.smin) + " – " + Money(j.smax) + " por tarefa", 9.0f);
			txt(mx + 16, my + 114, cSub, "O pagamento sobe com o nível do emprego (1 a 5).", 8.5f);
			wrap(mx + 16, my + 136, "Depois de assinar a carteira, vá ao local de trabalho e toque em Iniciar serviço. Você só pode ter um emprego por vez e ele fica salvo na sua conta.", cSub, 8.5f, mw - 32, 4, 12.0f);
			button(mx + 16, my + mh - 40, mx + mw - 16, my + mh - 14, "FECHAR", cOff, true, K_NO, 0, 9.5f);
		}
		else
		{
			std::string t1, t2;
			if (g_confirm == 1 && okJob) { t1 = "Assinar carteira?"; t2 = "Contratação: " + g_s.jobs[(size_t)g_sel].name; }
			else { t1 = "Pedir demissão?"; t2 = "Você perde o vínculo com " + g_s.mine.name + "."; }
			txt(mx + 16, my + 14, cText, t1, 14.0f);
			txt(mx + 16, my + 44, cSub, t2, 9.5f);
			button(mx + 16, my + mh - 42, mx + mw * 0.5f - 5, my + mh - 14, "CONFIRMAR", g_confirm == 1 ? cGreen : cRed, true, K_YES, 0, 9.5f);
			button(mx + mw * 0.5f + 5, my + mh - 42, mx + mw - 16, my + mh - 14, "CANCELAR", cOff, true, K_NO, 0, 9.5f);
		}
	}

	// ---- aviso ----
	if (now < g_toastUntil)
	{
		const float fs = 10.0f * u;
		const ImVec2 ts = r->calculateTextSize(g_toast, fs);
		const ImVec2 a((scr.x - ts.x) * 0.5f - 14.0f * u, P0.y + 8.0f * u), b((scr.x + ts.x) * 0.5f + 14.0f * u, P0.y + 38.0f * u);
		r->drawRoundedRect(a, b, g_toastBad ? ImColor(120, 36, 40, 245) : ImColor(24, 100, 62, 245), 7.0f * u, true);
		r->drawText(ImVec2(a.x + 14.0f * u, a.y + (b.y - a.y - ts.y) * 0.5f), cText, g_toast, false, fs);
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
		case K_PREV: if (g_page > 0) g_page--; break;
		case K_NEXT: g_page++; break;
		case K_CARD: g_sel = h.idx; break;
		case K_SIGN: if (!employed) { g_sel = h.idx; g_confirm = 1; } break;
		case K_INFO: g_sel = h.idx; g_confirm = 3; break;
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
				if (g_confirm == 0 && std::fabs(d.dx) > 70.0f * k && std::fabs(e.y - d.sy) < std::fabs(d.dx)) g_page += (d.dx < 0) ? 1 : -1;
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

// =====================================================================================
//  Marcador NA CENA 3D (RwIm3D, chamado 1x por frame em hooks.cpp RenderEffects, junto do IdCard3D_Render)
//  Base visual: recurso PlayNow_markers (MTA), autoria de "theking" - creditos mantidos.
//   - anel: faixa externa (25% do raio, como o floor.png) COLADA NO CHAO (cada vertice segue o terreno), pulsando em tamanho e
//     transparencia (200 +- 35); com teste de profundidade -> paredes, carros e objetos escondem o anel de verdade;
//   - icone: quad branco voltado para a camera, 0,8 m a 1,2 m do chao, balancando 0,1 m; tambem com teste de profundidade.
// =====================================================================================
void JobsUI_RenderWorld()
{
	if (!pNetGame || !pGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED) return;
	static std::vector<Mk> mk;
	static uint32_t seen = ~0u;
	{
		const uint32_t v = g_mkVer.load();
		if (v != seen)
		{
			{ std::lock_guard<std::mutex> l(g_mkMutex); mk = g_mkNet; seen = v; }
			FLog("[JOBS3D] %d marcador(es) recebido(s) do servidor", (int)mk.size());
		}
	}
	if (mk.empty()) return;

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector cam = TheCamera.GetPosition();
	float px = cam.x, py = cam.y, pz = cam.z;
	bool havePl = false;
	if (CPlayerPool* pool = pNetGame->GetPlayerPool())
	{
		CLocalPlayer* lp = pool->GetLocalPlayer();
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		if (pp && pp->m_pPed) { const CVector pv = pp->m_pPed->GetPosition(); px = pv.x; py = pv.y; pz = pv.z; havePl = true; }
	}

	{
		// linha de visao do nome/botao: so para NPCs a menos de 14 m (o botao so aparece a ate 12 m) e so a cada 150 ms (a consulta de colisao e cara)
		static int64_t nextLos = 0;
		const int64_t nowMs = NowMs();
		if (nowMs >= nextLos)
		{
			nextLos = nowMs + 150;
			uint32_t bits = 0;
			for (size_t i = 0; i < mk.size() && i < 32; i++)
			{
				if (mk[i].kind == 4) { bits |= 1u << i; continue; }            // botao fixo na tela: sem linha de visao
				const float dx = mk[i].ax - cam.x, dy = mk[i].ay - cam.y, dz = mk[i].az - cam.z;
				if (dx * dx + dy * dy + dz * dz > 14.0f * 14.0f) continue;
				const CVector npc(mk[i].ax, mk[i].ay, mk[i].az + 0.8f);
				if (CWorld::GetIsLineOfSightClear(npc, cam, true, true, false, true, false, false, false)) bits |= 1u << i;
			}
			g_mkLos.store(bits);
		}
	}

	const float t = (float)(NowMs() % 100000) * 0.001f;
	const float ph = t * 2.0f, sn = std::sin(ph);          // MARKER_ANIMATION_SPEED 0.002/ms = 2 rad/s

	struct Job3 { const Mk* m; float d; };
	static std::vector<Job3> order; order.clear();
	for (const Mk& m : mk)
	{
		const float dx = m.x - cam.x, dy = m.y - cam.y, dz = m.z - cam.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (m.kind != 4 && d < 70.0f) order.push_back({&m, d});
	}
	if (order.empty()) return;
	static bool firstDraw = true;
	if (firstDraw) { firstDraw = false; FLog("[JOBS3D] primeiro desenho: %d marcador(es) perto", (int)order.size()); }
	std::sort(order.begin(), order.end(), [](const Job3& a, const Job3& b) { return a.d > b.d; });   // translucido: de tras pra frente

	CHook::CallFunction<void>(g_libGTASA + 0x6F4EC4);   // DefinedState(), igual ao Hose.cpp / IdCard3D.cpp
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
	RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
	RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(FALSE));
	RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
	RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERLINEAR));
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS,    RWRSTATE(rwTEXTUREADDRESSCLAMP));

	// relevo do chao: guardado por marcador e refeito so a cada 4 s (colisao e cara; o chao nao muda)
	constexpr int N = 32;
	struct Ground { float z[N]; int64_t t = 0; uint32_t ver = ~0u; };
	static std::vector<Ground> gcache;
	if (gcache.size() != mk.size()) gcache.assign(mk.size(), Ground());
	const int64_t nowG = NowMs();

	for (const Job3& o : order)
	{
		const Mk& m = *o.m;
		Ground& g = gcache[(size_t)(o.m - mk.data())];
		if (g.ver != seen || nowG - g.t > 4000)
		{
			g.ver = seen; g.t = nowG;
			for (int i = 0; i < N; i++)
			{
				const float ang = (float)i * (6.2831853f / (float)N);
				float gz = CWorld::FindGroundZForCoord(m.x + std::cos(ang) * m.radius * 0.87f, m.y + std::sin(ang) * m.radius * 0.87f);   // meio da faixa do anel
				if (!(std::fabs(gz - m.z) < 1.5f)) gz = m.z;               // leitura invalida: usa a altura do servidor
				g.z[i] = gz;
			}
		}
		const bool inside = havePl && std::fabs(pz - m.z) < 2.5f && (px - m.x) * (px - m.x) + (py - m.y) * (py - m.y) <= m.radius * m.radius;
		int rr = (int)((m.rgb >> 16) & 255), gg = (int)((m.rgb >> 8) & 255), bb = (int)(m.rgb & 255);
		int aa = (int)(200.0f + std::sin(ph / 3.0f) * 35.0f);
		if (inside) { rr = (int)(190 + sn * 23); gg = (int)(200 + sn * 20); bb = (int)std::min(255.0f, 216 + sn * 30); aa = 255; }
		const float fade = std::clamp((70.0f - o.d) / 20.0f, 0.0f, 1.0f);
		const uint32_t col = ((uint32_t)(aa * fade) << 24) | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;

		// ---- anel colado no chao ----
		{
			static RxObjSpace3DVertex v[N * 2];
			static uint16_t idx[N * 6];
			const float ro = m.radius * (1.0f - sn * 0.07f), ri = ro * 0.74f;
			for (int i = 0; i < N; i++)
			{
				const float ang = (float)i * (6.2831853f / (float)N), c = std::cos(ang), s2 = std::sin(ang);
				for (int k = 0; k < 2; k++)
				{
					const float rad = k == 0 ? ro : ri, x = m.x + c * rad, y = m.y + s2 * rad;
					const float gz = g.z[i];
					RxObjSpace3DVertex& w = v[i * 2 + k];
					w.objVertex = { x, y, gz + 0.04f };
					w.objNormal = { 0.0f, 0.0f, 1.0f };
					w.color = col; w.u = 0.0f; w.v = 0.0f;
				}
				const int j = (i + 1) % N, a0 = i * 2, a1 = j * 2;
				uint16_t* q = &idx[i * 6];
				q[0] = (uint16_t)a0; q[1] = (uint16_t)a1; q[2] = (uint16_t)(a1 + 1);
				q[3] = (uint16_t)a0; q[4] = (uint16_t)(a1 + 1); q[5] = (uint16_t)(a0 + 1);
			}
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(NULL));
			if (RwIm3DTransform(v, N * 2, nullptr, 0))
			{
				RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, reinterpret_cast<RwImVertexIndex*>(idx), N * 6);
				RwIm3DEnd();
			}
		}

		// ---- icone flutuante (billboard) ----
		if (o.d < 60.0f)
		{
			static RwTexture* texCache[4] = {nullptr, nullptr, nullptr, nullptr};          // evita procurar o icone pelo nome a cada quadro
			const int ti = m.kind == 3 ? 3 : (m.kind == 2 ? (m.state ? 2 : 1) : 0);
			if (!texCache[ti]) texCache[ti] = IconTexDb::Texture(ti == 3 ? "mk_alugar" : (ti == 2 ? "mk_cozinha" : (ti == 1 ? "mk_servico" : "mk_jobs")));
			RwTexture* tex = texCache[ti];
			if (tex && tex->raster)
			{
				const float cx = m.x, cy = m.y, cz = m.z + 1.2f + sn * 0.1f;
				float fx = cx - cam.x, fy = cy - cam.y; const float fl = std::sqrt(fx * fx + fy * fy);
				if (fl > 0.01f)
				{
					fx /= fl; fy /= fl;
					const float rx = fy, ry = -fx, h = 0.4f;               // direita na tela (eixo vertical = Z do mundo)
					RxObjSpace3DVertex q[4];
					const float px4[4] = { cx - rx * h, cx + rx * h, cx + rx * h, cx - rx * h };
					const float py4[4] = { cy - ry * h, cy + ry * h, cy + ry * h, cy - ry * h };
					const float pz4[4] = { cz - h, cz - h, cz + h, cz + h };
					const float u4[4] = { 0, 1, 1, 0 }, v4[4] = { 1, 1, 0, 0 };
					const uint32_t ic = ((uint32_t)(255.0f * fade) << 24) | 0x00FFFFFFu;
					for (int i = 0; i < 4; i++)
					{
						q[i].objVertex = { px4[i], py4[i], pz4[i] };
						q[i].objNormal = { -fx, -fy, 0.0f };
						q[i].color = ic; q[i].u = u4[i]; q[i].v = v4[i];
					}
					static const uint16_t qi[6] = { 0, 1, 2, 0, 2, 3 };
					RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(tex->raster));
					if (RwIm3DTransform(q, 4, nullptr, rwIM3D_VERTEXUV))
					{
						RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, reinterpret_cast<RwImVertexIndex*>(const_cast<uint16_t*>(qi)), 6);
						RwIm3DEnd();
					}
				}
			}
		}
	}
}
