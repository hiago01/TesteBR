#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "missoespanel.h"
#include "../PanelAlpha.h"          // transparencia compartilhada (Menu jogador + Gerenciador de grupo)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

// Missoes (ver missoespanel.h). Medidas em "unidades de referencia": a tela de referencia tem 1280 x 720; tudo e multiplicado por u e centralizado.

namespace
{
	constexpr int kCats = 5;
	struct Rw { int type = 0; int32_t arg = 0; uint32_t qty = 0; };
	struct Row
	{
		int id = 0; std::string name, desc; uint32_t target = 1, prog = 0; int status = 0, rep = 1, repDone = 0, dest = 0; uint32_t secs = 0; int icon = 0; bool bonus = false;
		std::vector<Rw> rw;
	};
	struct ListState { bool have = false, loading = false, stale = false; int64_t reqT = 0, recvT = 0; std::vector<Row> rows; };
	struct Info
	{
		int seq = 0; bool hasS = false; std::string sname, grand; int lvl = 1, lmax = 50, week = 1; uint32_t into = 0, need = 1, secs = 0; int ready[kCats] = {0, 0, 0, 0, 0}; int64_t recvT = 0;
	};

	// ---- dados que chegam da rede (thread de rede) ----
	std::mutex g_mtx;
	Info       g_netInfo;
	unsigned   g_infoVer = 0;
	struct NetList { int seq; int cat; ListState d; };
	struct NetClaim { int seq; int id; int res; std::vector<Rw> rw; };
	std::vector<NetList>  g_netLists;
	std::vector<NetClaim> g_netClaims;
	std::atomic<int> g_seq{0};

	// ---- estado da thread de desenho ----
	Info       g_i;
	unsigned   g_seen = ~0u;
	int64_t    g_t0 = 0;
	int        g_cat = 0;
	ListState  g_ls[kCats];
	float      g_scroll[kCats] = {0, 0, 0, 0, 0};
	int        g_claimBusyId = 0; int64_t g_claimBusyT = 0;

	// ---- toque ----
	enum { K_NONE = 0, K_CLOSE, K_CAT, K_TAB, K_CLAIM, K_GO, K_BONUS };
	struct Target { int kind = K_NONE; int idx = 0; };
	struct HitR { float x0, y0, x1, y1; Target t; };
	std::vector<HitR> g_hits;
	float g_listR[4] = {0, 0, 0, 0};
	int    g_finger = -1;
	Target g_down;
	float  g_sx = 0, g_sy = 0, g_lx = 0, g_ly = 0;
	bool   g_moved = false, g_inList = false;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}
	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], (unsigned)n)) return false;
		for (char& c : out) if ((unsigned char)c < 32) c = ' ';
		return true;
	}
	bool RdRewards(RakNet::BitStream& bs, std::vector<Rw>& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n) || n > 8) return false;
		out.clear();
		for (int i = 0; i < n; i++)
		{
			uint8_t t = 0; int32_t a = 0; uint32_t q = 0;
			if (!bs.Read(t) || !bs.Read(a) || !bs.Read(q)) return false;
			Rw w; w.type = t; w.arg = a; w.qty = q; out.push_back(w);
		}
		return true;
	}
	std::string Dots(uint64_t v)
	{
		const std::string s = std::to_string(v);
		std::string o;
		for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return o;
	}
	std::string RwText(const Rw& w)
	{
		switch (w.type)
		{
		case 0: return "R$ " + Dots(w.qty);
		case 1: return "MB " + Dots(w.qty);
		case 2: return Dots(w.qty) + (w.qty == 1 ? " VIP" : " VIPs");
		case 3: return "EXP " + Dots(w.qty);
		case 4: return "Moldura";
		case 5: return "Linha";
		case 6: return "Item x" + Dots(w.qty);
		case 7: return "Caixa x" + Dots(w.qty);
		}
		return "?";
	}
	void RwColor(int type, int& r, int& g, int& b)
	{
		switch (type)
		{
		case 0: r = 96; g = 210; b = 120; break;
		case 1: r = 236; g = 120; b = 190; break;
		case 2: r = 250; g = 200; b = 70; break;
		case 3: r = 98; g = 200; b = 250; break;
		case 4: case 5: r = 190; g = 140; b = 250; break;
		default: r = 240; g = 170; b = 90; break;
		}
	}
	std::string Timer(int64_t secs)
	{
		if (secs < 0) secs = 0;
		char b[48];
		if (secs >= 86400) std::snprintf(b, sizeof b, "%lldd %lldh", (long long)(secs / 86400), (long long)((secs % 86400) / 3600));
		else if (secs >= 3600) std::snprintf(b, sizeof b, "%lldh %02lldm", (long long)(secs / 3600), (long long)((secs % 3600) / 60));
		else std::snprintf(b, sizeof b, "%lldm %02llds", (long long)(secs / 60), (long long)(secs % 60));
		return b;
	}

	struct CatDef { const char* name; const char* kind; };
	const CatDef kCat[kCats] = { { "DIÁRIO", "Diária" }, { "SEMANAL", "Semanal" }, { "TEMPORADA", "Temporada" }, { "ESPECIAL", "Especial" }, { "EVENTOS", "Evento" } };

	// icones desenhados por codigo (c = centro, s = tamanho)
	void Circle(ImGuiRenderer* r, float cx, float cy, float rad, const ImColor& col, bool fill, float th)
	{
		ImVec2 p[28];
		for (int i = 0; i < 28; i++) { const float a = (float)i / 28.0f * 6.2831853f; p[i] = ImVec2(cx + std::cos(a) * rad, cy + std::sin(a) * rad); }
		if (fill) r->drawConvexPolyFilled(p, 28, col); else r->drawPolyline(p, 28, col, true, th);
	}
	void Glyph(ImGuiRenderer* r, int icon, float cx, float cy, float s, const ImColor& col)
	{
		const float th = std::max(1.5f, s * 0.12f);
		switch (icon)
		{
		case 1: { Circle(r, cx, cy, s * 0.8f, col, false, th); ImVec2 p[3] = { ImVec2(cx - s * 0.38f, cy), ImVec2(cx - s * 0.08f, cy + s * 0.32f), ImVec2(cx + s * 0.42f, cy - s * 0.3f) }; r->drawPolyline(p, 3, col, false, th); break; }
		case 2: { Circle(r, cx, cy, s * 0.8f, col, false, th); r->drawLine(ImVec2(cx, cy), ImVec2(cx, cy - s * 0.5f), col, th); r->drawLine(ImVec2(cx, cy), ImVec2(cx + s * 0.38f, cy + s * 0.14f), col, th); break; }
		case 3: { r->drawRoundedRect(ImVec2(cx - s * 0.75f, cy - s * 0.6f), ImVec2(cx + s * 0.75f, cy + s * 0.65f), col, s * 0.1f, false, th); r->drawLine(ImVec2(cx - s * 0.75f, cy - s * 0.12f), ImVec2(cx + s * 0.75f, cy - s * 0.12f), col, th); r->drawLine(ImVec2(cx, cy - s * 0.6f), ImVec2(cx, cy - s * 0.12f), col, th); break; }
		case 4: { ImVec2 p[4] = { ImVec2(cx - s * 0.7f, cy + s * 0.6f), ImVec2(cx - s * 0.5f, cy - s * 0.5f), ImVec2(cx + s * 0.7f, cy - s * 0.65f), ImVec2(cx + s * 0.45f, cy + s * 0.5f) }; r->drawPolyline(p, 4, col, true, th); r->drawLine(ImVec2(cx - s * 0.7f, cy + s * 0.7f), ImVec2(cx + s * 0.2f, cy - s * 0.15f), col, th); break; }
		case 5: { Circle(r, cx, cy - s * 0.35f, s * 0.32f, col, false, th); r->drawRoundedRect(ImVec2(cx - s * 0.65f, cy + s * 0.12f), ImVec2(cx + s * 0.65f, cy + s * 0.78f), col, s * 0.3f, false, th); break; }
		case 7: { r->drawRoundedRect(ImVec2(cx - s * 0.75f, cy - s * 0.6f), ImVec2(cx + s * 0.75f, cy + s * 0.7f), col, s * 0.12f, false, th); r->drawLine(ImVec2(cx - s * 0.75f, cy - s * 0.2f), ImVec2(cx + s * 0.75f, cy - s * 0.2f), col, th); r->drawLine(ImVec2(cx - s * 0.35f, cy - s * 0.8f), ImVec2(cx - s * 0.35f, cy - s * 0.4f), col, th); r->drawLine(ImVec2(cx + s * 0.35f, cy - s * 0.8f), ImVec2(cx + s * 0.35f, cy - s * 0.4f), col, th); break; }
		default:   // 6 e demais: estrela
		{
			ImVec2 p[10];
			for (int i = 0; i < 10; i++) { const float a = -1.5707963f + (float)i * 0.6283185f, rr = (i % 2 == 0) ? s * 0.85f : s * 0.36f; p[i] = ImVec2(cx + std::cos(a) * rr, cy + std::sin(a) * rr); }
			r->drawPolyline(p, 10, col, true, th);
			break;
		}
		}
	}
}

// ------------------------------------------------------------------------------------------------ rede
int MissoesPanel::Seq() { return g_seq.load(); }

void MissoesPanel::Reset()
{
	std::lock_guard<std::mutex> l(g_mtx);
	g_netInfo = Info(); g_infoVer++; g_netLists.clear(); g_netClaims.clear(); g_seq.store(0);
}

bool MissoesPanel::OnOpen(RakNet::BitStream& bs)
{
	Info in; uint16_t seq = 0, lvl = 0, lmax = 0, week = 0; uint8_t hasS = 0, nc = 0; uint32_t into = 0, need = 0, secs = 0;
	if (!bs.Read(seq) || !bs.Read(hasS) || !RdStr(bs, in.sname) || !bs.Read(lvl) || !bs.Read(into) || !bs.Read(need) || !bs.Read(lmax) || !RdStr(bs, in.grand) || !bs.Read(secs) || !bs.Read(week) || !bs.Read(nc)) return false;
	if (seq == 0 || nc > 16) return false;
	for (int i = 0; i < nc; i++) { uint8_t v = 0; if (!bs.Read(v)) return false; if (i < kCats) in.ready[i] = v; }
	in.seq = seq; in.hasS = hasS != 0; in.lvl = lvl; in.lmax = lmax; in.into = into; in.need = need ? need : 1; in.secs = secs; in.week = week; in.recvT = NowMs();
	std::lock_guard<std::mutex> l(g_mtx);
	g_netInfo = in; g_infoVer++; g_seq.store(seq);
	return true;
}

bool MissoesPanel::OnList(RakNet::BitStream& bs)
{
	uint16_t seq = 0; uint8_t cat = 0, n = 0;
	if (!bs.Read(seq) || !bs.Read(cat) || !bs.Read(n) || cat >= kCats || n > 16) return false;
	NetList nl; nl.seq = seq; nl.cat = cat; nl.d.have = true; nl.d.recvT = NowMs();
	for (int i = 0; i < n; i++)
	{
		Row w; uint16_t id = 0; uint8_t st = 0, rep = 0, rd = 0, dest = 0, icon = 0, bonus = 0;
		if (!bs.Read(id) || !RdStr(bs, w.name) || !RdStr(bs, w.desc) || !bs.Read(w.target) || !bs.Read(w.prog) || !bs.Read(st) || !bs.Read(rep) || !bs.Read(rd) || !bs.Read(dest) || !bs.Read(w.secs) || !bs.Read(icon) || !bs.Read(bonus) || !RdRewards(bs, w.rw)) return false;
		w.id = id; w.status = st; w.rep = rep; w.repDone = rd; w.dest = dest; w.icon = icon; w.bonus = bonus != 0; if (w.target == 0) w.target = 1;
		nl.d.rows.push_back(std::move(w));
	}
	std::lock_guard<std::mutex> l(g_mtx);
	g_netLists.push_back(std::move(nl));
	return true;
}

bool MissoesPanel::OnClaim(RakNet::BitStream& bs)
{
	uint16_t seq = 0, id = 0; uint8_t res = 0;
	NetClaim nc;
	if (!bs.Read(seq) || !bs.Read(id) || !bs.Read(res) || !RdRewards(bs, nc.rw)) return false;
	nc.seq = seq; nc.id = id; nc.res = res;
	std::lock_guard<std::mutex> l(g_mtx);
	g_netClaims.push_back(std::move(nc));
	return true;
}

// ------------------------------------------------------------------------------------------------ desenho
void MissoesPanel::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, int& arg)
{
	answer = 0; arg = 0;
	const int64_t now = NowMs();
	std::vector<NetClaim> claims;
	{
		std::lock_guard<std::mutex> l(g_mtx);
		if (g_infoVer != g_seen)
		{
			const bool same = (g_netInfo.seq == g_i.seq && g_i.seq != 0);       // mesmo numero = atualizacao dos dados (apos coletar): nao volta ao inicio
			g_i = g_netInfo; g_seen = g_infoVer;
			if (!same)
			{
				g_finger = -1; g_down = Target(); g_moved = false; g_inList = false; g_t0 = now; g_cat = 0;
				for (int c = 0; c < kCats; c++) { g_ls[c] = ListState(); g_scroll[c] = 0.0f; }
				g_claimBusyId = 0;
			}
		}
		for (NetList& n : g_netLists) if (n.seq == g_i.seq && n.cat >= 0 && n.cat < kCats) { const int64_t rq = g_ls[n.cat].reqT; g_ls[n.cat] = std::move(n.d); g_ls[n.cat].reqT = rq; g_ls[n.cat].loading = false; }
		g_netLists.clear();
		claims.swap(g_netClaims);
	}
	if (g_i.seq == 0) return;

	// resultado do COLETAR: aviso + atualiza as listas (o banco decide; aqui so mostra)
	for (const NetClaim& c : claims)
	{
		if (c.seq != g_i.seq) continue;
		g_claimBusyId = 0;
		std::string msg; int kind = 2;
		switch (c.res)
		{
		case 0:
		{
			msg = "Recompensa coletada:";
			for (size_t k = 0; k < c.rw.size(); k++) msg += (k ? ", " : " ") + RwText(c.rw[k]);
			kind = 0;
			break;
		}
		case 1: msg = "Essa missão ainda não foi concluída."; kind = 1; break;
		case 2: msg = "Essa recompensa já foi coletada."; kind = 1; break;
		case 3: msg = "Missão bloqueada: falta nível de temporada."; kind = 1; break;
		case 4: msg = "Essa missão expirou."; kind = 1; break;
		case 6: msg = "Aguarde um instante para coletar de novo."; kind = 1; break;
		default: msg = "Não foi possível coletar agora. Tente de novo."; kind = 1; break;
		}
		ShopPanel::LocalToast(kind, msg.c_str());
		for (int cc = 0; cc < kCats; cc++) g_ls[cc].stale = true;
	}

	float f = std::clamp((float)(now - g_t0) / 220.0f, 0.0f, 1.0f);
	f = 1.0f - (1.0f - f) * (1.0f - f);
	auto A = [&](int rr, int gg, int bb, int aa) { return ImColor(rr, gg, bb, (int)((float)aa * f)); };

	const float u = std::min(scrW / 1280.0f, scrH / 720.0f);
	const float ox = (scrW - 1280.0f * u) * 0.5f, oy = (scrH - 720.0f * u) * 0.5f + (1.0f - f) * 10.0f * u;
	auto V = [&](float x, float y) { return ImVec2(ox + x * u, oy + y * u); };
	auto txtW = [&](const std::string& s, float fs) { return r->calculateTextSize(s, fs * u).x / u; };
	auto txtH = [&](const std::string& s, float fs) { return r->calculateTextSize(s, fs * u).y / u; };
	auto txt = [&](float x, float y, const ImColor& col, const std::string& s, float fs, bool bold = false)
	{
		r->drawText(V(x, y), col, s, false, fs * u);
		if (bold) r->drawText(V(x + 0.45f, y), col, s, false, fs * u);
	};
	auto fit = [&](const std::string& s, float fs, float maxW)
	{
		if (txtW(s, fs) <= maxW) return s;
		std::string t = s;
		while (t.size() > 1 && txtW(t + "...", fs) > maxW) { t.pop_back(); while (t.size() > 1 && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); }
		return t + "...";
	};
	auto addHit = [&](float x0, float y0, float x1, float y1, int kind, int idx)
	{
		const ImVec2 a = V(x0, y0), b = V(x1, y1);
		g_hits.push_back({a.x, a.y, b.x, b.y, {kind, idx}});
	};
	g_hits.clear();
	const float th1 = std::max(1.0f, 1.0f * u);

	// ---- fundo azul ----
	const float bgA = PanelAlpha::Get();
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), A(10, 24, 54, (int)(255.0f * bgA)), true);
	r->drawRectFilledMulticolor(ImVec2(0, 0), ImVec2(scrW, scrH), A(30, 90, 190, (int)(150.0f * bgA)), A(20, 60, 140, (int)(110.0f * bgA)), A(8, 20, 50, 0), A(14, 38, 90, (int)(70.0f * bgA)));

	// ---- abas do topo + fechar ----
	{
		const char* tabs[3] = { "MISSÕES", "CONQUISTAS", "TEMPORADA" };
		float x = 30.0f;
		for (int t = 0; t < 3; t++)
		{
			const bool active = (t == 0 && g_cat != 2) || (t == 2 && g_cat == 2);
			const bool soon = (t == 1);
			const float w = txtW(tabs[t], 15.0f) + 44.0f;
			const bool dn = g_down.kind == K_TAB && g_down.idx == t;
			r->drawRoundedRect(V(x, 24), V(x + w, 66), active ? A(70, 160, 255, 255) : A(30, 66, 130, dn ? 230 : 170), 10.0f * u, true);
			r->drawRoundedRect(V(x, 24), V(x + w, 66), active ? A(190, 226, 255, 230) : A(110, 160, 230, 100), 10.0f * u, false, th1);
			txt(x + 22.0f, 24.0f + (42.0f - txtH(tabs[t], 15.0f)) * 0.5f, active ? A(255, 255, 255, 255) : A(200, 222, 255, soon ? 140 : 235), tabs[t], 15.0f, true);
			if (soon) { r->drawRoundedRect(V(x + w - 56, 12), V(x + w - 4, 28), A(120, 140, 176, 210), 8.0f * u, true); txt(x + w - 50.0f, 12.0f + (16.0f - txtH("EM BREVE", 7.0f)) * 0.5f, A(255, 255, 255, 255), "EM BREVE", 7.0f, true); }
			addHit(x, 24, x + w, 66, K_TAB, t);
			x += w + 12.0f;
		}
		const float x1 = 1250.0f, x0 = x1 - 46.0f;
		const bool dn = g_down.kind == K_CLOSE;
		r->drawRoundedRect(V(x0, 24), V(x1, 66), A(190, 60, 70, dn ? 255 : 220), 10.0f * u, true);
		r->drawRoundedRect(V(x0, 24), V(x1, 66), A(255, 190, 196, 200), 10.0f * u, false, th1);
		r->drawLine(V(x0 + 14, 38), V(x1 - 14, 52), A(255, 255, 255, 255), std::max(2.0f, 2.2f * u));
		r->drawLine(V(x0 + 14, 52), V(x1 - 14, 38), A(255, 255, 255, 255), std::max(2.0f, 2.2f * u));
		addHit(x0 - 8, 16, x1 + 8, 74, K_CLOSE, 0);
	}

	// ---- menu lateral ----
	{
		const float SX0 = 30.0f, SX1 = 250.0f, IH = 60.0f, STEP = 72.0f, Y0 = 100.0f;
		for (int k = 0; k < kCats; k++)
		{
			const float y = Y0 + (float)k * STEP;
			const bool active = g_cat == k;
			const bool dn = g_down.kind == K_CAT && g_down.idx == k;
			const bool empty = g_ls[k].have && g_ls[k].rows.empty();
			if (active) { for (int g = 3; g >= 1; g--) r->drawRoundedRect(V(SX0 - g * 2, y - g * 2), V(SX1 + g * 2, y + IH + g * 2), A(70, 170, 255, 24), (10.0f + g * 2) * u, true); }
			r->drawRoundedRect(V(SX0, y), V(SX1, y + IH), active ? A(70, 160, 255, 255) : A(24, 56, 116, dn ? 235 : 185), 10.0f * u, true);
			r->drawRoundedRect(V(SX0, y), V(SX1, y + IH), active ? A(200, 232, 255, 230) : A(104, 150, 224, 110), 10.0f * u, false, th1);
			Glyph(r, k == 0 ? 7 : (k == 1 ? 2 : (k == 2 ? 6 : (k == 3 ? 1 : 3))), V(SX0 + 32.0f, y + IH * 0.5f).x, V(SX0 + 32.0f, y + IH * 0.5f).y, 11.0f * u, active ? A(255, 255, 255, 255) : A(190, 216, 255, 230));
			txt(SX0 + 62.0f, y + (IH - txtH(kCat[k].name, 14.0f)) * 0.5f, active ? A(255, 255, 255, 255) : A(214, 230, 255, empty ? 150 : 255), kCat[k].name, 14.0f, true);
			if (g_i.ready[k] > 0)      // bolinha: tem recompensa para coletar
			{
				const ImVec2 c = V(SX1 - 20.0f, y + IH * 0.5f);
				Circle(r, c.x, c.y, 12.0f * u, A(60, 200, 110, 255), true, 1.0f);
				const std::string n = std::to_string(std::min(g_i.ready[k], 99));
				txt(SX1 - 20.0f - txtW(n, 11.0f) * 0.5f, y + (IH - txtH(n, 11.0f)) * 0.5f, A(255, 255, 255, 255), n, 11.0f, true);
			}
			else if (empty)
			{
				const ImVec2 c = V(SX1 - 22.0f, y + IH * 0.5f);
				r->drawRoundedRect(ImVec2(c.x - 7 * u, c.y - 2 * u), ImVec2(c.x + 7 * u, c.y + 8 * u), A(190, 210, 240, 170), 2.0f * u, true);     // cadeado
				Circle(r, c.x, c.y - 3.0f * u, 5.0f * u, A(190, 210, 240, 170), false, std::max(1.5f, 1.6f * u));
			}
			addHit(SX0, y, SX1, y + IH, K_CAT, k);
		}
		r->drawLine(V(270, 100), V(270, 650), A(110, 160, 230, 90), th1);
	}

	const ListState& ls = g_ls[g_cat];
	const Row* bonus = nullptr;
	for (const Row& w : ls.rows) if (w.bonus) { bonus = &w; break; }

	// ---- cartao central ----
	{
		const float X0 = 290.0f, X1 = 580.0f;
		r->drawRoundedRect(V(X0, 100), V(X1, 650), A(18, 46, 104, 215), 16.0f * u, true);
		r->drawRoundedRect(V(X0, 100), V(X1, 650), A(110, 164, 238, 120), 16.0f * u, false, th1);
		std::string title = g_cat == 0 ? "HOJE" : (g_cat == 1 ? "SEMANA " + std::to_string(g_i.week) : (g_cat == 2 ? (g_i.hasS ? g_i.sname : std::string("TEMPORADA")) : std::string(kCat[g_cat].name)));
		txt(X0 + (X1 - X0 - txtW(title, 26.0f)) * 0.5f, 118.0f, A(255, 255, 255, 255), fit(title, 26.0f, X1 - X0 - 20.0f), 26.0f, true);
		int64_t secs = -1;
		if (g_cat == 2 && g_i.hasS) secs = (int64_t)g_i.secs - (now - g_i.recvT) / 1000;
		else { for (const Row& w : ls.rows) { secs = (int64_t)w.secs - (now - ls.recvT) / 1000; break; } }
		if (secs >= 0)
		{
			const std::string tm = "Termina em " + Timer(secs);
			const float w = txtW(tm, 11.5f) + 28.0f, cx = (X0 + X1) * 0.5f;
			r->drawRoundedRect(V(cx - w * 0.5f, 158), V(cx + w * 0.5f, 184), A(14, 34, 80, 235), 13.0f * u, true);
			r->drawRoundedRect(V(cx - w * 0.5f, 158), V(cx + w * 0.5f, 184), A(110, 164, 238, 140), 13.0f * u, false, th1);
			txt(cx - txtW(tm, 11.5f) * 0.5f, 158.0f + (26.0f - txtH(tm, 11.5f)) * 0.5f, A(200, 226, 255, 255), tm, 11.5f);
		}
		const char* d1 = g_cat == 0 ? "Complete as missões de hoje" : (g_cat == 1 ? "Complete as missões da semana" : (g_cat == 2 ? "Missões acumulativas da temporada" : (g_cat == 3 ? "Missões especiais por tempo limitado" : "Eventos da cidade")));
		txt((X0 + X1) * 0.5f - txtW(d1, 11.0f) * 0.5f, 200.0f, A(170, 200, 244, 230), d1, 11.0f);

		// caixa da recompensa do periodo (a missao "premio" da categoria)
		const float BX0 = X0 + 18.0f, BX1 = X1 - 18.0f, BY0 = 236.0f, BY1 = 450.0f;
		r->drawRoundedRect(V(BX0, BY0), V(BX1, BY1), A(12, 32, 78, 235), 12.0f * u, true);
		r->drawRoundedRect(V(BX0, BY0), V(BX1, BY1), A(100, 150, 226, 130), 12.0f * u, false, th1);
		if (bonus)
		{
			const std::string h = g_cat == 0 ? "RECOMPENSA DO DIA" : "RECOMPENSA DA SEMANA";
			txt((BX0 + BX1) * 0.5f - txtW(h, 12.0f) * 0.5f, BY0 + 12.0f, A(230, 240, 255, 255), h, 12.0f, true);
			float yy = BY0 + 44.0f;
			for (const Rw& w : bonus->rw)
			{
				int cr, cg, cb; RwColor(w.type, cr, cg, cb);
				const std::string s = "+ " + RwText(w);
				const float cw = txtW(s, 14.0f) + 36.0f, cx = (BX0 + BX1) * 0.5f;
				r->drawRoundedRect(V(cx - cw * 0.5f, yy), V(cx + cw * 0.5f, yy + 30), A(cr / 3, cg / 3, cb / 3, 235), 15.0f * u, true);
				r->drawRoundedRect(V(cx - cw * 0.5f, yy), V(cx + cw * 0.5f, yy + 30), A(cr, cg, cb, 220), 15.0f * u, false, th1);
				txt(cx - txtW(s, 14.0f) * 0.5f, yy + (30.0f - txtH(s, 14.0f)) * 0.5f, A(cr, cg, cb, 255), s, 14.0f, true);
				yy += 38.0f;
			}
			// progresso do premio (o botao COLETAR fica no pe do cartao)
			const float py = BY1 - 70.0f;
			const float frac = std::clamp((float)bonus->prog / (float)bonus->target, 0.0f, 1.0f);
			r->drawRoundedRect(V(BX0 + 16, py), V(BX1 - 16, py + 12), A(8, 20, 50, 255), 6.0f * u, true);
			if (frac > 0.0f) r->drawRoundedRect(V(BX0 + 16, py), V(BX0 + 16 + (BX1 - BX0 - 32) * frac, py + 12), A(70, 200, 255, 255), 6.0f * u, true);
			const std::string pt = std::to_string(bonus->prog) + "/" + std::to_string(bonus->target) + " missões";
			txt((BX0 + BX1) * 0.5f - txtW(pt, 10.0f) * 0.5f, py + 16.0f, A(190, 214, 250, 230), pt, 10.0f);
		}
		else
		{
			const char* e = ls.have ? "Sem prêmio de período aqui." : "Carregando...";
			txt((BX0 + BX1) * 0.5f - txtW(e, 12.0f) * 0.5f, (BY0 + BY1) * 0.5f - 8.0f, A(170, 200, 244, 200), e, 12.0f);
		}

		// barra de nivel da temporada (sempre visivel quando existe temporada ativa)
		if (g_i.hasS)
		{
			const std::string gp = "Grande prêmio atual: " + g_i.grand;
			txt(X0 + 18.0f, 466.0f, A(255, 214, 110, 255), fit(gp, 11.5f, X1 - X0 - 36.0f), 11.5f, true);
			const float by = 494.0f, bx0 = X0 + 18.0f, bx1 = X1 - 18.0f;
			const float frac = (g_i.lvl >= g_i.lmax) ? 1.0f : std::clamp((float)g_i.into / (float)g_i.need, 0.0f, 1.0f);
			r->drawRoundedRect(V(bx0, by), V(bx1, by + 22), A(8, 20, 50, 255), 11.0f * u, true);
			if (frac > 0.0f) r->drawRoundedRect(V(bx0, by), V(bx0 + (bx1 - bx0) * frac, by + 22), A(250, 190, 60, 255), 11.0f * u, true);
			r->drawRoundedRect(V(bx0, by), V(bx1, by + 22), A(160, 190, 240, 140), 11.0f * u, false, th1);
			const std::string xp = g_i.lvl >= g_i.lmax ? std::string("NÍVEL MÁXIMO") : (Dots(g_i.into) + "/" + Dots(g_i.need));
			txt((bx0 + bx1) * 0.5f - txtW(xp, 11.0f) * 0.5f, by + (22.0f - txtH(xp, 11.0f)) * 0.5f, A(255, 255, 255, 255), xp, 11.0f, true);
			const std::string lv = "Nível " + std::to_string(g_i.lvl);
			txt(X0 + 18.0f, by + 30.0f, A(214, 230, 255, 255), lv, 12.5f, true);
		}
		else txt(X0 + 18.0f, 480.0f, A(170, 200, 244, 200), "Sem temporada ativa no momento.", 11.0f);

		// COLETAR do premio do periodo (botao grande no pe do cartao)
		if (bonus)
		{
			const bool can = bonus->status == 2, done = bonus->status == 3;
			const bool dn = g_down.kind == K_BONUS && can;
			const float bx0 = X0 + 40.0f, bx1 = X1 - 40.0f, by0 = 580.0f, by1 = 626.0f;
			ImColor fill = can ? A(60, 200, 110, dn ? 255 : 235) : (done ? A(80, 96, 124, 220) : A(40, 70, 130, 200));
			r->drawRoundedRect(V(bx0, by0), V(bx1, by1), fill, 12.0f * u, true);
			r->drawRoundedRect(V(bx0, by0), V(bx1, by1), can ? A(190, 255, 214, 230) : A(130, 160, 214, 110), 12.0f * u, false, th1);
			const std::string bl = done ? "COLETADO" : "COLETAR";
			txt((bx0 + bx1) * 0.5f - txtW(bl, 17.0f) * 0.5f, by0 + (by1 - by0 - txtH(bl, 17.0f)) * 0.5f, A(255, 255, 255, can || done ? 255 : 150), bl, 17.0f, true);
			if (can) addHit(bx0, by0, bx1, by1, K_BONUS, bonus->id);
		}
	}

	// ---- lista de missoes (rolagem) ----
	{
		const float LX0 = 600.0f, LX1 = 1250.0f, LY0 = 100.0f, LY1 = 650.0f, RH = 98.0f, GAP = 10.0f;
		g_listR[0] = V(LX0, LY0).x; g_listR[1] = V(LX0, LY0).y; g_listR[2] = V(LX1, LY1).x; g_listR[3] = V(LX1, LY1).y;
		std::vector<const Row*> rows;
		for (const Row& w : ls.rows) if (!w.bonus) rows.push_back(&w);
		const float total = (float)rows.size() * (RH + GAP);
		const float view = LY1 - LY0, maxS = std::max(0.0f, total - view);
		g_scroll[g_cat] = std::clamp(g_scroll[g_cat], 0.0f, maxS);
		r->pushClipRect(V(LX0, LY0), V(LX1, LY1), true);
		if (rows.empty())
		{
			const char* e = !ls.have ? "Carregando missões..." : (g_cat == 4 ? "Nenhum evento no momento. Volte em breve!" : "Nenhuma missão disponível agora.");
			txt((LX0 + LX1) * 0.5f - txtW(e, 14.0f) * 0.5f, 340.0f, A(170, 200, 244, 220), e, 14.0f);
		}
		for (size_t k = 0; k < rows.size(); k++)
		{
			const Row& w = *rows[k];
			const float y = LY0 + (float)k * (RH + GAP) - g_scroll[g_cat];
			if (y + RH < LY0 || y > LY1) continue;
			const bool doneRow = w.status == 3;
			r->drawRoundedRect(V(LX0, y), V(LX1 - 12, y + RH), A(20, 52, 116, doneRow ? 170 : 225), 12.0f * u, true);
			r->drawRoundedRect(V(LX0, y), V(LX1 - 12, y + RH), w.status == 2 ? A(90, 230, 150, 220) : A(104, 156, 232, 110), 12.0f * u, false, w.status == 2 ? std::max(1.5f, 1.8f * u) : th1);
			// icone
			r->drawRoundedRect(V(LX0 + 14, y + 16), V(LX0 + 78, y + 80), A(12, 34, 84, 255), 10.0f * u, true);
			r->drawRoundedRect(V(LX0 + 14, y + 16), V(LX0 + 78, y + 80), A(120, 170, 240, 140), 10.0f * u, false, th1);
			const ImVec2 ic = V(LX0 + 46.0f, y + 48.0f);
			Glyph(r, w.icon, ic.x, ic.y, 17.0f * u, w.status == 5 ? A(150, 170, 200, 200) : A(120, 210, 255, 255));
			// textos
			const float TX = LX0 + 92.0f, RBW = 120.0f, TW = (LX1 - 12 - 14 - RBW) - TX;
			txt(TX, y + 10.0f, A(255, 255, 255, doneRow ? 170 : 255), fit(w.name, 14.5f, TW - 80.0f), 14.5f, true);
			const std::string kd = kCat[g_cat].kind;
			const float kw = txtW(kd, 8.0f) + 14.0f, kx = TX + std::min(txtW(w.name, 14.5f), TW - 80.0f) + 10.0f;
			r->drawRoundedRect(V(kx, y + 12), V(kx + kw, y + 28), A(40, 110, 220, 230), 8.0f * u, true);
			txt(kx + 7.0f, y + 12.0f + (16.0f - txtH(kd, 8.0f)) * 0.5f, A(255, 255, 255, 255), kd, 8.0f, true);
			txt(TX, y + 34.0f, A(176, 204, 246, 230), fit(w.desc, 10.5f, TW), 10.5f);
			// recompensas
			float rx = TX;
			for (const Rw& rw : w.rw)
			{
				if (rw.type == 3 && w.rw.size() > 3) continue;
				int cr, cg, cb; RwColor(rw.type, cr, cg, cb);
				const std::string s = RwText(rw);
				const float cw = txtW(s, 9.5f) + 16.0f;
				if (rx + cw > TX + TW) break;
				r->drawRoundedRect(V(rx, y + 56), V(rx + cw, y + 74), A(cr / 4, cg / 4, cb / 4, 235), 9.0f * u, true);
				r->drawRoundedRect(V(rx, y + 56), V(rx + cw, y + 74), A(cr, cg, cb, 200), 9.0f * u, false, th1);
				txt(rx + 8.0f, y + 56.0f + (18.0f - txtH(s, 9.5f)) * 0.5f, A(cr, cg, cb, 255), s, 9.5f, true);
				rx += cw + 6.0f;
			}
			// progresso
			const float pbx0 = TX, pbx1 = TX + std::min(TW, 300.0f), pby = y + 80.0f;
			const float frac = std::clamp((float)w.prog / (float)w.target, 0.0f, 1.0f);
			r->drawRoundedRect(V(pbx0, pby), V(pbx1, pby + 8), A(8, 20, 50, 255), 4.0f * u, true);
			if (frac > 0.0f) r->drawRoundedRect(V(pbx0, pby), V(pbx0 + (pbx1 - pbx0) * frac, pby + 8), w.status >= 2 ? A(90, 230, 150, 255) : A(70, 190, 255, 255), 4.0f * u, true);
			std::string pg = Dots(w.prog) + "/" + Dots(w.target);
			if (w.rep > 1) pg += "  (" + std::to_string(std::min(w.repDone + (w.status == 3 ? 0 : 1), w.rep)) + "/" + std::to_string(w.rep) + ")";
			txt(pbx1 + 10.0f, pby - 3.0f, A(214, 230, 255, 255), pg, 10.0f, true);
			// botao da direita
			const float bx0 = LX1 - 12 - 14 - RBW + 10.0f, bx1 = LX1 - 12 - 14, by0 = y + 26.0f, by1 = y + 72.0f;
			if (w.status == 2)
			{
				const bool dn = g_down.kind == K_CLAIM && g_down.idx == w.id;
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(60, 200, 110, dn ? 255 : 235), 10.0f * u, true);
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(190, 255, 214, 230), 10.0f * u, false, th1);
				txt((bx0 + bx1) * 0.5f - txtW("COLETAR", 11.5f) * 0.5f, by0 + (by1 - by0 - txtH("COLETAR", 11.5f)) * 0.5f, A(255, 255, 255, 255), "COLETAR", 11.5f, true);
				addHit(bx0, by0, bx1, by1, K_CLAIM, w.id);
			}
			else if (w.status == 3)
			{
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(80, 96, 124, 220), 10.0f * u, true);
				txt((bx0 + bx1) * 0.5f - txtW("COLETADO", 11.0f) * 0.5f, by0 + (by1 - by0 - txtH("COLETADO", 11.0f)) * 0.5f, A(200, 214, 236, 220), "COLETADO", 11.0f, true);
			}
			else if (w.status == 4 || w.status == 5)
			{
				const char* lab = w.status == 4 ? "EXPIRADA" : "BLOQUEADA";
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(70, 60, 80, 200), 10.0f * u, true);
				txt((bx0 + bx1) * 0.5f - txtW(lab, 10.0f) * 0.5f, by0 + (by1 - by0 - txtH(lab, 10.0f)) * 0.5f, A(210, 190, 214, 220), lab, 10.0f, true);
			}
			else if (w.dest > 0)      // IR: so aparece quando o servidor mandou um destino
			{
				const bool dn = g_down.kind == K_GO && g_down.idx == w.id;
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(50, 130, 250, dn ? 255 : 230), 10.0f * u, true);
				r->drawRoundedRect(V(bx0, by0), V(bx1, by1), A(190, 220, 255, 230), 10.0f * u, false, th1);
				txt((bx0 + bx1) * 0.5f - txtW("IR", 14.0f) * 0.5f, by0 + (by1 - by0 - txtH("IR", 14.0f)) * 0.5f, A(255, 255, 255, 255), "IR", 14.0f, true);
				addHit(bx0, by0, bx1, by1, K_GO, w.id);
			}
		}
		r->popClipRect();
		if (maxS > 0.0f)
		{
			const float tl = std::max(30.0f, view * view / total), ty = LY0 + (view - tl) * (g_scroll[g_cat] / maxS);
			r->drawRoundedRect(V(LX1 - 6.0f, LY0), V(LX1 - 2.0f, LY1), A(120, 160, 230, 40), 2.0f * u, true);
			r->drawRoundedRect(V(LX1 - 6.0f, ty), V(LX1 - 2.0f, ty + tl), A(170, 220, 255, 235), 2.0f * u, true);
		}
	}

	// ---- rodape ----
	txt(30.0f, 668.0f, A(130, 168, 226, 170), "Máximo RP  •  as recompensas são entregues pelo servidor", 8.5f);

	// ---- toques ----
	auto hitAt = [&](float x, float y) -> Target
	{
		for (size_t i = g_hits.size(); i-- > 0;)
			if (x >= g_hits[i].x0 && x <= g_hits[i].x1 && y >= g_hits[i].y0 && y <= g_hits[i].y1) return g_hits[i].t;
		return Target();
	};
	auto inList = [&](float x, float y) { return g_listR[2] > g_listR[0] && x >= g_listR[0] && x <= g_listR[2] && y >= g_listR[1] && y <= g_listR[3]; };
	for (const ShopTouch& e : events)
	{
		if (f < 1.0f) continue;
		if (e.type == 2 && g_finger < 0)
		{
			g_finger = e.num; g_sx = g_lx = e.x; g_sy = g_ly = e.y; g_moved = false;
			g_down = hitAt(e.x, e.y); g_inList = inList(e.x, e.y);
		}
		else if (e.num == g_finger && e.type == 3)
		{
			if (!g_moved && (std::fabs(e.y - g_sy) > 7.0f * u || std::fabs(e.x - g_sx) > 7.0f * u)) { g_moved = true; g_down = Target(); }
			if (g_moved && g_inList) g_scroll[g_cat] -= (e.y - g_ly) / u;
			g_lx = e.x; g_ly = e.y;
			if (!g_moved && g_down.kind != K_NONE) { const Target t = hitAt(e.x, e.y); if (t.kind != g_down.kind || t.idx != g_down.idx) g_down = Target(); }
		}
		else if (e.num == g_finger && e.type == 1)
		{
			const Target was = g_down; const bool mv = g_moved;
			g_finger = -1; g_down = Target(); g_moved = false; g_inList = false;
			if (e.cancel || mv || was.kind == K_NONE) continue;
			const Target now2 = hitAt(e.x, e.y);
			if (now2.kind != was.kind || now2.idx != was.idx) continue;
			switch (was.kind)
			{
			case K_CLOSE: answer = 1; return;
			case K_TAB:
				if (was.idx == 1) { ShopPanel::LocalToast(2, "Conquistas: em breve."); break; }
				g_cat = (was.idx == 2) ? 2 : (g_cat == 2 ? 0 : g_cat);
				break;
			case K_CAT: if (was.idx >= 0 && was.idx < kCats) g_cat = was.idx; break;
			case K_CLAIM: case K_BONUS:
				if (g_claimBusyId == 0 || now - g_claimBusyT > 2500) { g_claimBusyId = was.idx; g_claimBusyT = now; answer = 3; arg = was.idx; return; }
				break;
			case K_GO: answer = 4; arg = was.idx; return;
			default: break;
			}
		}
		else if (e.num == g_finger && e.cancel) { g_finger = -1; g_down = Target(); g_moved = false; g_inList = false; }
	}

	// ---- pedido da lista da aba aberta (um por quadro; o servidor confere de novo). Dados velhos (apos coletar) continuam na tela ate a nova lista chegar ----
	if (answer == 0)
	{
		ListState& s = g_ls[g_cat];
		const bool need = !s.have || s.stale;
		const bool retry = s.loading && now - s.reqT > 1500;
		if (need && (!s.loading || retry)) { s.loading = true; s.stale = false; s.reqT = now; answer = 2; arg = g_cat; return; }
	}
}
