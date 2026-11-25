#include "../../main.h"
#include "../imguirenderer.h"
#include "../../vendor/raknet/BitStream.h"
#include "perfilpanel.h"
#include "../PanelAlpha.h"          // transparencia compartilhada (Menu jogador + Gerenciador de grupo)
#include "../UiPrefs.h"             // minimapa ligado/desligado
#include "controlmode.h"            // botoes Inventario / Corrida-lock (ModernConfig)
#include "avatarpicker.h"           // foto + moldura + fundo da linha (pagina Personalizar)
#include "rankpodium.h"              // podio dos 3 primeiros do ranking
#include "../../game/BrasaoTexDb.h"      // brasao da organizacao (Inicio)
#include "../../game/RW/RenderWare.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Menu Jogador (ver perfilpanel.h). Medidas em "unidades de referencia": a tela de referencia tem 1280 x 720; tudo e multiplicado por u
// (escala da tela) e centralizado, como a loja / nota fiscal / garagem.

namespace
{
	struct RankRow { uint32_t pos = 0; std::string name; uint32_t val = 0; bool online = false; };
	struct PodInfo { bool ok = false; uint32_t acc = 0; uint8_t border = 0, style = 0; };
	struct RankState { bool have = false, loading = false; std::vector<RankRow> rows; uint32_t mePos = 0, meVal = 0; int64_t t = 0, reqT = 0; PodInfo pod[3]; };
	struct RuleLine { int style = 0; std::string text; };
	struct WLine { int style = 0; float y = 0.0f, h = 0.0f; std::string text; bool first = true; };
	struct RuleState { bool have = false, loading = false; int64_t reqT = 0; std::vector<RuleLine> lines; float layU = 0.0f, layW = 0.0f, total = 0.0f; std::vector<WLine> wl; };
	struct Info
	{
		int seq = 0; std::string name, phone, last, since, org, job;
		uint32_t rg = 0, pp = 0, vips = 0; int32_t money = 0; bool group = false; uint32_t keys = 0; uint32_t bank = 0;
		std::vector<int> cats; std::vector<std::string> tabs;
	};

	// ---- dados que chegam da rede (thread de rede) ----
	std::mutex g_mtx;
	Info       g_netInfo;
	std::string g_hudOrgTag;                   // tag da org para o brasao da HUD principal (nao some ao abrir o menu; limpa em Reset); g_mtx
	std::string g_orgTag;                     // tag da organizacao (op 58); guardada por g_mtx
	unsigned   g_infoVer = 0;
	struct NetRank { int seq; int cat; RankState d; };
	struct NetRules { int seq; int tab; RuleState d; };
	std::vector<NetRank>  g_netRanks;
	std::vector<NetRules> g_netRules;

	// ---- estado da thread de desenho ----
	Info       g_i;
	unsigned   g_seen = ~0u;
	int64_t    g_t0 = 0;
	int        g_page = 0;                         // 0 Inicio | 1 Ranking | 2 Regras | 3 Configuracoes
	int        g_rankCat = 0;                      // indice em g_i.cats
	int        g_ruleTab = 0;
	std::map<int, RankState> g_rank;               // por id de categoria
	std::map<int, RuleState> g_rules;              // por aba
	float      g_rankScroll = 0.0f, g_ruleScroll = 0.0f, g_tabsScroll = 0.0f;

	// ---- toque ----
	enum { K_NONE = 0, K_CLOSE, K_NAV, K_RTAB, K_UTAB, K_BAN_G, K_BAN_V, K_TSLD, K_TMIN, K_TPLUS, K_TOG, K_AVP };
	struct Target { int kind = K_NONE; int idx = 0; };
	struct HitR { float x0, y0, x1, y1; Target t; };
	std::vector<HitR> g_hits;
	float g_rankR[4] = {0, 0, 0, 0}, g_ruleR[4] = {0, 0, 0, 0}, g_tabsR[4] = {0, 0, 0, 0}, g_sldR[4] = {0, 0, 0, 0};   // g_sldR = trilho do controle de transparencia
	int    g_finger = -1;
	Target g_down;
	float  g_sx = 0, g_sy = 0, g_lx = 0, g_ly = 0;
	float  g_avp[3] = {0, 0, 0};                   // pagina Personalizar: origem x,y e largura (px) do seletor (avatarpicker.h), para o HitTest do toque
	bool   g_moved = false;
	int    g_area = 0;                             // 1 = tabela do ranking | 2 = texto das regras | 3 = fileira de abas das regras

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
		for (char& c : out) if ((unsigned char)c < 32) c = ' ';      // so texto imprimivel (bytes >= 128 = UTF-8, passam)
		return true;
	}

	std::string Dots(uint64_t v)
	{
		const std::string s = std::to_string(v);
		std::string o;
		for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return o;
	}
	std::string MoneyBR(int64_t v)       // "R$ 1.234,00"
	{
		const bool neg = v < 0; const uint64_t a = (uint64_t)(neg ? -v : v);
		return std::string(neg ? "-R$ " : "R$ ") + Dots(a) + ",00";
	}

	// categorias de ranking conhecidas (o servidor manda so o id; nomes e formato ficam aqui, em UTF-8)
	struct CatDef { const char* name; const char* metric; const char* sub; int r0, g0, b0, r1, g1, b1; int kind; };   // kind 0 = "PP n" | 1 = "n VIPs" | 2 = so numero
	const CatDef kCats[] = {
		{ "Titãs do investimento", "MB Pontos acumulados", "Quem mais acumulou MB Pontos", 64, 22, 96, 214, 52, 122, 0 },
		{ "Clube da Coroa", "VIPs adquiridos", "Quem mais adquiriu VIPs", 86, 58, 12, 226, 168, 44, 1 },
		{ "Magnatas", "Patrimônio total", "Top-50 jogadores: dinheiro + banco + empresas", 120, 80, 8, 244, 190, 40, 3 },
	};
	const CatDef kCatUnknown = { "Ranking", "Valor", "Classificação dos jogadores", 30, 70, 110, 70, 150, 200, 2 };
	const CatDef& CatOf(int id) { return (id >= 0 && id < (int)(sizeof(kCats) / sizeof(kCats[0]))) ? kCats[id] : kCatUnknown; }
	std::string CatValue(const CatDef& c, uint32_t v)
	{
		if (c.kind == 0) return "MB " + Dots(v);
		if (c.kind == 1) return Dots(v) + (v == 1 ? " VIP" : " VIPs");
		if (c.kind == 3) return "R$ " + Dots(v);
		return Dots(v);
	}

	struct NavDef { const char* name; int icon; int page; bool soon; bool tagNew; };
	const NavDef kNav[] = {
		{ "Início", 0, 0, false, false }, { "Loja premium", 1, -1, true, false }, { "Ranking", 2, 1, false, false }, { "Regras", 3, 2, false, false }, { "Personalizar", 5, 4, false, true },
		{ "Comandos e funções", 4, -1, true, false }, { "Configurações", 5, 3, false, true }, { "Dúvidas frequentes", 6, -1, true, false },
	};

	// ---- icones pequenos da barra lateral e dos cartoes (desenhados por codigo) ----
	void NavIcon(ImGuiRenderer* r, int kind, float cx, float cy, float s, const ImColor& col)
	{
		auto P = [&](float x, float y) { return ImVec2(cx + x * s, cy + y * s); };
		const float th = std::max(1.0f, 1.3f * s);
		switch (kind)
		{
		case 0:   // casa
		{
			ImVec2 roof[3] = { P(-7, -1), P(0, -7.5f), P(7, -1) };
			r->drawConvexPolyFilled(roof, 3, col);
			r->drawRoundedRect(P(-5, -1), P(5, 7), col, 0.6f * s, true);
			r->drawRoundedRect(P(-1.4f, 2.4f), P(1.4f, 7), ImColor(20, 30, 50, (int)(col.Value.w * 255.0f)), 0.4f * s, true);
			break;
		}
		case 1:   // lojinha
			r->drawRoundedRect(P(-6, -7), P(6, -2), col, 1.0f * s, true);
			r->drawRoundedRect(P(-5, -1), P(5, 7), col, 0.6f * s, true);
			r->drawRoundedRect(P(-1.6f, 2), P(1.6f, 7), ImColor(20, 30, 50, (int)(col.Value.w * 255.0f)), 0.4f * s, true);
			break;
		case 2:   // podio
			r->drawRoundedRect(P(-7, -1), P(-2.4f, 7), col, 0.6f * s, true);
			r->drawRoundedRect(P(-2.2f, -6), P(2.2f, 7), col, 0.6f * s, true);
			r->drawRoundedRect(P(2.4f, 2), P(7, 7), col, 0.6f * s, true);
			break;
		case 3:   // documento / escudo de regras
			r->drawRoundedRect(P(-6, -7), P(6, 7), col, 1.4f * s, true);
			for (int k = 0; k < 3; k++) r->drawLine(P(-3, -3.2f + k * 3.2f), P(3, -3.2f + k * 3.2f), ImColor(20, 30, 50, (int)(col.Value.w * 255.0f)), th);
			break;
		case 4:   // teclado
			r->drawRoundedRect(P(-8, -5), P(8, 5), col, 1.4f * s, false, th);
			for (int k = 0; k < 4; k++) r->drawLine(P(-5 + k * 3.4f, -1.6f), P(-4.2f + k * 3.4f, -1.6f), col, th);
			r->drawLine(P(-4, 2), P(4, 2), col, th);
			break;
		case 5:   // ajustes
			r->drawLine(P(-7, -3), P(7, -3), col, th); r->drawLine(P(-7, 3), P(7, 3), col, th);
			r->drawRoundedRect(P(-3.6f, -5), P(-0.4f, -1), col, 1.6f * s, true);
			r->drawRoundedRect(P(1.4f, 1), P(4.6f, 5), col, 1.6f * s, true);
			break;
		default:  // boia
			r->drawRoundedRect(P(-6.5f, -6.5f), P(6.5f, 6.5f), col, 7.0f * s, false, th * 1.6f);
			r->drawRoundedRect(P(-2.6f, -2.6f), P(2.6f, 2.6f), col, 3.0f * s, false, th);
			break;
		}
	}
}

// ------------------------------------------------------------------------------------------------ rede
bool PerfilPanel::OnOpen(RakNet::BitStream& bs)
{
	Info d;
	uint16_t seq = 0; uint8_t nc = 0, nt = 0;
	if (!bs.Read(seq) || !RdStr(bs, d.name) || !bs.Read(d.rg) || !RdStr(bs, d.phone) || !RdStr(bs, d.last) || !RdStr(bs, d.since) || !RdStr(bs, d.org) || !RdStr(bs, d.job)) return false;
	uint8_t grp = 0; uint16_t keys = 0;
	if (!bs.Read(d.money) || !bs.Read(d.pp) || !bs.Read(d.vips) || !bs.Read(grp) || !bs.Read(keys) || !bs.Read(nc) || nc > 8) return false;
	d.keys = keys;
	d.group = grp != 0;
	for (int i = 0; i < nc; i++) { uint8_t id = 0; if (!bs.Read(id)) return false; d.cats.push_back((int)id); }
	if (!bs.Read(nt) || nt > 8) return false;
	for (int i = 0; i < nt; i++) { std::string s; if (!RdStr(bs, s)) return false; d.tabs.push_back(s); }
	{ uint32_t bank = 0; if (bs.Read(bank)) d.bank = bank; }        // (fim do pacote, opcional) saldo do banco; servidor antigo nao manda
	if (seq == 0) return false;
	d.seq = seq;
	std::lock_guard<std::mutex> l(g_mtx);
	g_netInfo = std::move(d); g_infoVer++; g_orgTag.clear();
	g_netRanks.clear(); g_netRules.clear();
	return true;
}

void PerfilPanel::OnOrgTag(RakNet::BitStream& bs)
{
	std::string t; if (!RdStr(bs, t)) return;
	if (t.size() > 24) t.resize(24);
	std::lock_guard<std::mutex> l(g_mtx); g_orgTag = t; g_hudOrgTag = t;
}

std::string PerfilPanel::HudOrgTag() { std::lock_guard<std::mutex> l(g_mtx); return g_hudOrgTag; }

bool PerfilPanel::OnRank(RakNet::BitStream& bs)
{
	uint16_t seq = 0; uint8_t cat = 0, n = 0;
	if (!bs.Read(seq) || !bs.Read(cat) || !bs.Read(n) || n > 50) return false;
	RankState d;
	for (int i = 0; i < n; i++)
	{
		RankRow w; if (!bs.Read(w.pos) || !RdStr(bs, w.name) || !bs.Read(w.val)) return false;
		d.rows.push_back(std::move(w));
	}
	if (!bs.Read(d.mePos) || !bs.Read(d.meVal)) return false;
	for (auto& rw : d.rows) { uint8_t on = 0; if (!bs.Read(on)) break; rw.online = on != 0; }     // (fim do pacote, opcional) 1 = jogador online agora; servidor antigo nao manda
	for (int k = 0; k < 3 && k < n; k++)                                                          // (fim do pacote, opcional) PODIO: conta, moldura do avatar, fundo da linha dos 3 primeiros
	{
		uint32_t acc = 0; uint8_t bd = 0, st = 0;
		if (!bs.Read(acc) || !bs.Read(bd) || !bs.Read(st)) break;
		d.pod[k].ok = true; d.pod[k].acc = acc; d.pod[k].border = bd; d.pod[k].style = st;
	}
	d.have = true; d.t = NowMs();
	std::lock_guard<std::mutex> l(g_mtx);
	if (seq == 0 || seq != g_netInfo.seq) return true;                       // resposta de uma tela que ja foi trocada: ignora (nao e erro)
	g_netRanks.push_back({(int)seq, (int)cat, std::move(d)});
	return true;
}

bool PerfilPanel::OnRules(RakNet::BitStream& bs)
{
	uint16_t seq = 0; uint8_t tab = 0, n = 0;
	if (!bs.Read(seq) || !bs.Read(tab) || !bs.Read(n) || n > 160) return false;
	RuleState d;
	for (int i = 0; i < n; i++)
	{
		uint8_t st = 0; RuleLine w;
		if (!bs.Read(st) || !RdStr(bs, w.text)) return false;
		w.style = st > 5 ? 0 : (int)st; d.lines.push_back(std::move(w));
	}
	d.have = true;
	std::lock_guard<std::mutex> l(g_mtx);
	if (seq == 0 || seq != g_netInfo.seq) return true;
	g_netRules.push_back({(int)seq, (int)tab, std::move(d)});
	return true;
}

void PerfilPanel::Reset()
{
	std::lock_guard<std::mutex> l(g_mtx);
	g_netInfo = Info(); g_infoVer++; g_netRanks.clear(); g_netRules.clear();
}

int PerfilPanel::Seq()
{
	std::lock_guard<std::mutex> l(g_mtx);
	return g_netInfo.seq;
}

// ------------------------------------------------------------------------------------------------ desenho
void PerfilPanel::Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, int& arg)
{
	answer = 0; arg = 0;
	const int64_t now = NowMs();
	{
		std::lock_guard<std::mutex> l(g_mtx);
		if (g_infoVer != g_seen)
		{
			g_i = g_netInfo; g_seen = g_infoVer; g_finger = -1; g_down = Target(); g_moved = false; g_area = 0;
			g_t0 = now; g_page = 0; g_rankCat = 0; g_ruleTab = 0; g_rankScroll = g_ruleScroll = g_tabsScroll = 0.0f;
			g_rank.clear(); g_rules.clear();
		}
		for (NetRank& n : g_netRanks) if (n.seq == g_i.seq) { RankState& s = g_rank[n.cat]; const int64_t rq = s.reqT; s = std::move(n.d); s.reqT = rq; s.loading = false; }
		for (NetRules& n : g_netRules) if (n.seq == g_i.seq) { RuleState& s = g_rules[n.tab]; s = std::move(n.d); s.loading = false; s.layU = 0.0f; }
		g_netRanks.clear(); g_netRules.clear();
	}
	if (g_i.seq == 0) return;

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

	// ---- fundo: tela inteira, azul-acinzentado com brilho no canto (como o video) ----
	const float bgA = PanelAlpha::Get();      // opacidade = Configuracoes (a mesma do Gerenciador de grupo)
	r->drawRect(ImVec2(0, 0), ImVec2(scrW, scrH), A(24, 36, 60, (int)(255.0f * bgA)), true);
	r->drawRectFilledMulticolor(ImVec2(0, 0), ImVec2(scrW, scrH), A(76, 104, 150, (int)(150.0f * bgA)), A(52, 72, 112, (int)(110.0f * bgA)), A(22, 32, 56, 0), A(36, 52, 86, (int)(70.0f * bgA)));

	// ---- titulo + Sair ----
	txt(30.0f, 26.0f, A(236, 244, 255, 255), "Menu jogador", 36.0f, true);
	{
		const float x1 = 1250.0f, x0 = x1 - 132.0f, y0 = 32.0f, y1 = 62.0f;
		const bool dn = g_down.kind == K_CLOSE;
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(70, 94, 130, dn ? 210 : 140), 15.0f * u, true);
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(170, 196, 232, 90), 15.0f * u, false, std::max(1.0f, 1.0f * u));
		r->drawRoundedRect(V(x0 + 9, y0 + 7), V(x0 + 34, y1 - 7), A(226, 240, 250, 235), 3.0f * u, true);
		txt(x0 + 21.5f - txtW("ESC", 7.0f) * 0.5f, y0 + 7.0f + (16.0f - txtH("ESC", 7.0f)) * 0.5f, A(38, 52, 78, 255), "ESC", 7.0f, true);
		txt(x0 + 42.0f, y0 + (30.0f - txtH("Sair do Menu", 10.5f)) * 0.5f, A(230, 240, 255, 255), "Sair do Menu", 10.5f);
		addHit(x0 - 8, y0 - 8, x1 + 8, y1 + 8, K_CLOSE, 0);
	}

	// ---- barra lateral ----
	{
		const float SX0 = 30.0f, SX1 = 244.0f, IH = 42.0f, STEP = 50.0f, Y0 = 112.0f;
		for (int k = 0; k < (int)(sizeof(kNav) / sizeof(kNav[0])); k++)
		{
			const NavDef& nv = kNav[k];
			const float y = Y0 + (float)k * STEP;
			const bool active = !nv.soon && nv.page == g_page;
			const bool dn = g_down.kind == K_NAV && g_down.idx == k;
			if (active) { for (int g = 3; g >= 1; g--) r->drawRoundedRect(V(SX0 - g * 2, y - g * 2), V(SX1 + g * 2, y + IH + g * 2), A(80, 230, 250, 22), (8.0f + g * 2) * u, true); }
			r->drawRoundedRect(V(SX0, y), V(SX1, y + IH), active ? A(98, 232, 250, 255) : A(52, 72, 106, dn ? 230 : (nv.soon ? 110 : 170)), 8.0f * u, true);
			r->drawRoundedRect(V(SX0, y), V(SX1, y + IH), active ? A(190, 250, 255, 220) : A(150, 176, 214, nv.soon ? 70 : 110), 8.0f * u, false, std::max(1.0f, 1.0f * u));
			const ImColor ic = active ? A(24, 50, 74, 255) : A(214, 228, 248, nv.soon ? 150 : 235);
			const ImVec2 c = V(SX0 + 22.0f, y + IH * 0.5f);
			NavIcon(r, nv.icon, c.x, c.y, 1.15f * u, ic);
			const float tagW = (nv.soon || nv.tagNew) ? txtW(nv.tagNew ? "NOVO" : "EM BREVE", 7.0f) + 14.0f + 14.0f : 8.0f;      // reserva o espaco da etiqueta: o nome nunca passa por cima dela
			txt(SX0 + 44.0f, y + (IH - txtH(nv.name, 12.5f)) * 0.5f, active ? A(24, 50, 74, 255) : A(232, 240, 255, nv.soon ? 170 : 255), fit(nv.name, 12.5f, (SX1 - SX0) - 44.0f - tagW), 12.5f, true);
			if (nv.soon || nv.tagNew)
			{
				const std::string sb = nv.tagNew ? "NOVO" : "EM BREVE";
				const float tw = txtW(sb, 7.0f) + 14.0f, bx1 = SX1 - 8.0f, bx0 = bx1 - tw;
				r->drawRoundedRect(V(bx0, y + 12), V(bx1, y + 30), nv.tagNew ? A(236, 84, 150, 235) : A(120, 140, 176, 150), 9.0f * u, true);
				txt(bx0 + 7.0f, y + 12.0f + (18.0f - txtH(sb, 7.0f)) * 0.5f, A(255, 255, 255, 255), sb, 7.0f, true);
			}
			addHit(SX0, y, SX1, y + IH, K_NAV, k);
		}
		r->drawLine(V(264, 112), V(264, 640), A(120, 150, 196, 90), std::max(1.0f, 1.0f * u));
	}

	// ---- rodape ----
	txt(30.0f, 650.0f, A(150, 176, 214, 200), "Máximo RP", 10.0f);
	txt(30.0f, 667.0f, A(130, 156, 196, 140), "© 2026 Máximo RP. Máximo RP e qualquer logotipo associado são marcas do Máximo RP.", 7.6f);

	const float CX0 = 292.0f;
	const int ncat = (int)g_i.cats.size(), ntab = (int)g_i.tabs.size();
	auto card = [&](float x0, float y0, float x1, float y1)
	{
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(92, 120, 166, 70), 14.0f * u, true);
		r->drawRoundedRect(V(x0, y0), V(x1, y1), A(170, 196, 232, 70), 14.0f * u, false, std::max(1.0f, 1.0f * u));
	};
	auto onlineDot = [&](float cx, float cy)
	{
		const ImVec2 c = V(cx, cy);
		r->drawRoundedRect(ImVec2(c.x - 7.0f * u, c.y - 7.0f * u), ImVec2(c.x + 7.0f * u, c.y + 7.0f * u), A(60, 230, 120, 50), 7.0f * u, true);       // brilho
		r->drawRoundedRect(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), A(70, 235, 125, 255), 4.5f * u, true);
		r->drawRoundedRect(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), A(210, 255, 225, 200), 4.5f * u, false, std::max(1.0f, 1.0f * u));
	};
	auto pill = [&](float x0, float y0, float x1, float y1, bool on, bool dn, const std::string& label, float fs)
	{
		if (on) { for (int g = 2; g >= 1; g--) r->drawRoundedRect(V(x0 - g, y0 - g), V(x1 + g, y1 + g), A(80, 230, 250, 24), ((y1 - y0) * 0.5f + g) * u, true); }
		r->drawRoundedRect(V(x0, y0), V(x1, y1), on ? A(98, 232, 250, 255) : A(56, 78, 114, dn ? 230 : 170), (y1 - y0) * 0.5f * u, true);
		{
			const float th = std::max(1.5f, 1.5f * u);                                           // contorno centrado na borda do preenchimento (colado), raio = meia altura (pilula perfeita)
			const float hr = (y1 - y0) * 0.5f * u - th * 0.5f;
			r->drawRoundedRect(ImVec2(V(x0, y0).x + th * 0.5f, V(x0, y0).y + th * 0.5f), ImVec2(V(x1, y1).x - th * 0.5f, V(x1, y1).y - th * 0.5f), on ? A(190, 250, 255, 220) : A(170, 198, 236, 120), std::max(1.0f, hr), false, th);
		}
		txt(x0 + ((x1 - x0) - txtW(label, fs)) * 0.5f, y0 + ((y1 - y0) - txtH(label, fs)) * 0.5f, on ? A(24, 50, 74, 255) : A(226, 238, 255, 255), label, fs, true);
	};

	g_rankR[0] = g_rankR[1] = g_rankR[2] = g_rankR[3] = 0; g_ruleR[0] = g_ruleR[1] = g_ruleR[2] = g_ruleR[3] = 0; g_tabsR[0] = g_tabsR[1] = g_tabsR[2] = g_tabsR[3] = 0; g_sldR[0] = g_sldR[1] = g_sldR[2] = g_sldR[3] = 0;
	int wantRank = -1, wantRule = -1;

	if (g_page == 0)
	{
		// ================= INICIO =================
		const float CW = 470.0f, X1 = CX0 + CW;
		// cartao do perfil
		card(CX0, 112.0f, X1, 196.0f);
		{
			const ImVec2 c = V(CX0 + 46.0f, 154.0f);
			if (!PerfilDrawAvatar(r, c.x, c.y, 26.0f * u, (int)(255.0f * f)))        // foto + moldura do Central da Cidade (a mesma da conta)
			{
				r->drawRoundedRect(ImVec2(c.x - 29 * u, c.y - 29 * u), ImVec2(c.x + 29 * u, c.y + 29 * u), A(244, 248, 255, 255), 29.0f * u, true);
				std::string ini = g_i.name.empty() ? "?" : g_i.name.substr(0, 1);
				const float iw = txtW(ini, 22.0f), ih = txtH(ini, 22.0f);
				txt(CX0 + 46.0f - iw * 0.5f, 154.0f - ih * 0.5f, A(40, 62, 96, 255), ini, 22.0f, true);
			}
			txt(CX0 + 92.0f, 124.0f, A(206, 222, 248, 255), "Nome completo", 12.5f, true);
			txt(CX0 + 92.0f, 147.0f, A(244, 248, 255, 255), fit(g_i.name, 14.5f, 220.0f), 14.5f, true);
			txt(CX0 + 340.0f - 58.0f, 124.0f, A(206, 222, 248, 255), "RG", 12.5f, true);
			txt(CX0 + 340.0f - 58.0f, 147.0f, A(244, 248, 255, 255), std::to_string(g_i.rg), 14.5f, true);
			// brasao da organizacao (canto direito do cartao); sem organizacao / sem banco de brasoes = nada
			{
				std::string tag; { std::lock_guard<std::mutex> l(g_mtx); tag = g_orgTag; }
				if (!tag.empty())
				{
					if (RwTexture* bt = BrasaoTexDb::Texture("org_" + tag))
						if (bt->raster)
						{
							const float bsz = 64.0f;
							r->drawImage(V(X1 - 12.0f - bsz, 154.0f - bsz * 0.5f), V(X1 - 12.0f, 154.0f + bsz * 0.5f), (ImTextureID)bt->raster, ImColor(255, 255, 255, (int)(255.0f * f)), ImVec2(0, 0), ImVec2(1, 1));
						}
				}
			}
		}
		// cartao de dados
		card(CX0, 208.0f, X1, 360.0f);
		{
			struct Cell { std::string label; std::string val; bool dim; };
			const Cell cells[6] = {
				{ "Número de telefone", g_i.phone, false }, { "Última sessão", g_i.last, false }, { "Conta criada desde", g_i.since, false },
				{ "Organização", g_i.org, false }, { g_i.group ? "Cargo" : "Profissão", g_i.job, false },     // no grupo: o CARGO (ex.: Recruta (7)); sem grupo: a profissão
				{ "Veículos", g_i.keys == 1 ? "1 veículo" : (Dots(g_i.keys) + " veículos"), false },       // = chaves de veículo no inventário
			};
			const float colW = (CW - 36.0f) / 3.0f;
			for (int k = 0; k < 6; k++)
			{
				const float x = CX0 + 20.0f + (float)(k % 3) * colW, y = 208.0f + (k < 3 ? 22.0f : 90.0f);
				txt(x, y - 2.0f, A(206, 222, 248, 255), cells[k].label, 11.8f, true);
				txt(x, y + 20.0f, cells[k].dim ? A(170, 188, 214, 150) : A(244, 248, 255, 255), fit(cells[k].val, 12.2f, colW - 8.0f), 12.2f, !cells[k].dim);
			}
		}
		// cartao de dinheiro
		card(CX0, 372.0f, X1, 470.0f);
		{
			struct Row { const char* label; std::string val; bool dim; int icon; };
			const Row rows[3] = { { "Dinheiro em mãos", MoneyBR(g_i.money), false, 0 }, { "Banco", MoneyBR(g_i.bank), false, 1 }, { "MB Pontos", "MB " + Dots(g_i.pp), false, 2 } };
			for (int k = 0; k < 3; k++)
			{
				const float y = 372.0f + 12.0f + (float)k * 27.0f;
				void* mtex = PerfilMoneyTexture(k);                           // icone de arte (banco texdb/perfil): maos / banco / moeda MB; sem a textura = desenho antigo
				{
					// moldura melhorada (mesmo tamanho da antiga, 24x21): fundo escuro com brilho no topo, borda ciano dupla e aro suave por fora
					r->drawRoundedRect(V(CX0 + 17, y - 1), V(CX0 + 43, y + 22), A(98, 232, 250, 34), 7.5f * u, true);
					r->drawRoundedRect(V(CX0 + 18, y), V(CX0 + 42, y + 21), A(18, 40, 70, 235), 6.0f * u, true);
					r->drawRoundedRect(V(CX0 + 19, y + 1), V(CX0 + 41, y + 8), A(120, 190, 240, 46), 5.0f * u, true);
					r->drawRoundedRect(V(CX0 + 18, y), V(CX0 + 42, y + 21), A(98, 232, 250, 235), 6.0f * u, false, std::max(1.0f, 1.3f * u));
					r->drawRoundedRect(V(CX0 + 19.5f, y + 1.5f), V(CX0 + 40.5f, y + 19.5f), A(190, 244, 255, 70), 5.0f * u, false, std::max(1.0f, 0.8f * u));
				}
				const ImVec2 c = V(CX0 + 30.0f, y + 10.5f);
				if (mtex) r->drawImage(ImVec2(c.x - 9.5f * u, c.y - 9.5f * u), ImVec2(c.x + 9.5f * u, c.y + 9.5f * u), (ImTextureID)mtex, ImColor(255, 255, 255, (int)(255 * f)), ImVec2(0, 0), ImVec2(1, 1));
				else if (k == 0) { r->drawRoundedRect(ImVec2(c.x - 7 * u, c.y - 4 * u), ImVec2(c.x + 7 * u, c.y + 4 * u), A(236, 250, 255, 255), 1.5f * u, false, std::max(1.0f, 1.2f * u)); r->drawRoundedRect(ImVec2(c.x - 2 * u, c.y - 2 * u), ImVec2(c.x + 2 * u, c.y + 2 * u), A(236, 250, 255, 255), 2.0f * u, true); }
				else if (k == 1) { ImVec2 roof[3] = { ImVec2(c.x - 7 * u, c.y - 1 * u), ImVec2(c.x, c.y - 6 * u), ImVec2(c.x + 7 * u, c.y - 1 * u) }; r->drawConvexPolyFilled(roof, 3, A(236, 250, 255, 255)); r->drawRoundedRect(ImVec2(c.x - 5.5f * u, c.y), ImVec2(c.x + 5.5f * u, c.y + 5 * u), A(236, 250, 255, 255), 0.5f * u, true); }
				else
				{
					void* coin = PerfilBannerTexture(2);                          // moeda do MB Pontos (banco texdb/perfil); sem banco = losango
					if (coin) r->drawImage(ImVec2(c.x - 9 * u, c.y - 9 * u), ImVec2(c.x + 9 * u, c.y + 9 * u), (ImTextureID)coin, ImColor(255, 255, 255, (int)(255 * f)), ImVec2(0, 0), ImVec2(1, 1));
					else { ImVec2 d[4] = { ImVec2(c.x, c.y - 6 * u), ImVec2(c.x + 6 * u, c.y), ImVec2(c.x, c.y + 6 * u), ImVec2(c.x - 6 * u, c.y) }; r->drawConvexPolyFilled(d, 4, A(236, 250, 255, 255)); }
				}
				txt(CX0 + 54.0f, y + (21.0f - txtH(rows[k].val, 12.2f)) * 0.5f, rows[k].dim ? A(170, 188, 214, 150) : A(244, 248, 255, 255), rows[k].val, 12.2f, !rows[k].dim);
				const float lw = txtW(rows[k].label, 11.2f);
				txt(X1 - 18.0f - lw, y + (21.0f - txtH(rows[k].label, 11.2f)) * 0.5f, A(206, 222, 248, 255), rows[k].label, 11.2f, true);
			}
		}
		// banner "Painel do Grupo" (toque = /grupo): so aparece quando o jogador esta em um grupo (organizacao)
		float vipY = 482.0f;
		if (g_i.group)
		{
			vipY = 482.0f + CW / 5.4f + 12.0f;
			const float by0 = 482.0f, bh = CW / 5.4f, by1 = by0 + bh;
			void* tex = PerfilBannerTexture(0);
			const bool dn = g_down.kind == K_BAN_G;
			if (tex) r->drawImage(V(CX0, by0), V(X1, by1), (ImTextureID)tex, ImColor(255, 255, 255, (int)(255 * f * (dn ? 0.82f : 1.0f))), ImVec2(0, 0), ImVec2(1, 1));
			else
			{
				r->drawRoundedRect(V(CX0, by0), V(X1, by1), A(40, 56, 110, 255), 12.0f * u, true);
				r->drawRectFilledMulticolor(V(CX0 + 3, by0 + 3), V(X1 - 3, by1 - 3), A(120, 52, 170, 230), A(46, 110, 220, 230), A(46, 130, 230, 230), A(150, 50, 140, 230));
			}
			const std::string lb = "Painel do Grupo";
			const float lw = txtW(lb, 10.5f) + 38.0f, lx0 = CX0 + (CW - lw) * 0.5f, lx1 = lx0 + lw, ly0 = by0 + bh * 0.5f - 12.0f, ly1 = ly0 + 24.0f;
			r->drawRoundedRect(V(lx0, ly0), V(lx1, ly1), A(224, 120, 214, 215), 12.0f * u, true);
			r->drawRoundedRect(V(lx0, ly0), V(lx1, ly1), A(255, 214, 250, 200), 12.0f * u, false, std::max(1.0f, 1.0f * u));
			txt(lx0 + 14.0f, ly0 + (24.0f - txtH(lb, 10.5f)) * 0.5f, A(255, 255, 255, 255), lb, 10.5f, true);
			// setinha de "abrir"
			const ImVec2 ar = V(lx1 - 14.0f, ly0 + 12.0f);
			r->drawLine(ImVec2(ar.x - 3 * u, ar.y + 3 * u), ImVec2(ar.x + 3 * u, ar.y - 3 * u), A(255, 255, 255, 255), std::max(1.0f, 1.3f * u));
			r->drawLine(ImVec2(ar.x - 1 * u, ar.y - 3 * u), ImVec2(ar.x + 3 * u, ar.y - 3 * u), A(255, 255, 255, 255), std::max(1.0f, 1.3f * u));
			r->drawLine(ImVec2(ar.x + 3 * u, ar.y - 3 * u), ImVec2(ar.x + 3 * u, ar.y + 1 * u), A(255, 255, 255, 255), std::max(1.0f, 1.3f * u));
			addHit(CX0, by0, X1, by1, K_BAN_G, 0);
		}
		// banner "Eleve sua experiencia" (acessar loja = em breve)
		{
			const float by0 = vipY, bh = CW / 6.9f, by1 = by0 + bh;
			void* tex = PerfilBannerTexture(1);
			const bool dn = g_down.kind == K_BAN_V;
			if (tex) r->drawImage(V(CX0, by0), V(X1, by1), (ImTextureID)tex, ImColor(255, 255, 255, (int)(255 * f * (dn ? 0.85f : 1.0f))), ImVec2(0, 0), ImVec2(1, 1));
			else
			{
				r->drawRoundedRect(V(CX0, by0), V(X1, by1), A(244, 248, 255, 255), 12.0f * u, true);
				txt(CX0 + 20.0f, by0 + 8.0f, A(20, 30, 70, 255), "eleve sua EXPERIÊNCIA", 14.0f, true);
			}
			const std::string lb = "acessar loja";
			const float lw = txtW(lb, 10.2f) + 30.0f, lx1 = X1 - 18.0f, lx0 = lx1 - lw, ly0 = by0 + bh * 0.5f - 12.0f, ly1 = ly0 + 24.0f;
			r->drawRoundedRect(V(lx0, ly0), V(lx1, ly1), A(84, 104, 140, dn ? 255 : 235), 12.0f * u, true);
			txt(lx0 + 15.0f, ly0 + (24.0f - txtH(lb, 10.2f)) * 0.5f, A(238, 246, 255, 255), lb, 10.2f, true);
			addHit(CX0, by0, X1, by1, K_BAN_V, 0);
		}
	}
	else if (g_page == 1)
	{
		// ================= RANKING =================
		const float CW = 668.0f, X1 = CX0 + CW;
		NavIcon(r, 2, V(CX0 + 14.0f, 134.0f).x, V(CX0 + 14.0f, 134.0f).y, 1.9f * u, A(226, 238, 255, 235));
		txt(CX0 + 40.0f, 120.0f, A(240, 246, 255, 255), "Ranking de jogadores", 19.0f, true);
		{ const std::string up = "Atualizado a cada 10 minutos"; txt(X1 - txtW(up, 11.5f), 126.0f, A(206, 222, 248, 235), up, 11.5f, true); }
		// abas das categorias
		{
			float x = CX0;
			for (int k = 0; k < ncat; k++)
			{
				const CatDef& cd = CatOf(g_i.cats[(size_t)k]);
				const float w = txtW(cd.name, 12.6f) + 46.0f;                       // abas maiores (texto 12.6, altura 42)
				pill(x, 154.0f, x + w, 196.0f, k == g_rankCat, g_down.kind == K_RTAB && g_down.idx == k, cd.name, 12.6f);
				addHit(x, 148.0f, x + w, 200.0f, K_RTAB, k);
				x += w + 12.0f;
			}
		}
		if (g_rankCat >= ncat) g_rankCat = 0;
		const int catId = ncat > 0 ? g_i.cats[(size_t)g_rankCat] : 0;
		const CatDef& cd = CatOf(catId);
		// faixa da categoria (por codigo)
		{
			const float by0 = 204.0f, by1 = by0 + CW / 8.0f;                      // moldura de arte (1024x128 = 8:1); sem a textura = faixa por codigo
			void* btex = PerfilRankBanner(catId);
			if (btex) r->drawImage(V(CX0, by0), V(X1, by1), (ImTextureID)btex, ImColor(255, 255, 255, (int)(255 * f)), ImVec2(0, 0), ImVec2(1, 1));
			else
			{
				r->drawRoundedRect(V(CX0, by0), V(X1, by1), A(cd.r0, cd.g0, cd.b0, 255), 12.0f * u, true);
				r->drawRectFilledMulticolor(V(CX0 + 3, by0 + 3), V(X1 - 3, by1 - 3), A(cd.r0, cd.g0, cd.b0, 255), A(cd.r1, cd.g1, cd.b1, 255), A(cd.r1, cd.g1, cd.b1, 255), A(cd.r0, cd.g0, cd.b0, 255));
				r->drawRoundedRect(V(CX0, by0), V(X1, by1), A(255, 255, 255, 70), 12.0f * u, false, std::max(1.0f, 1.0f * u));
			}
			// titulo + subtitulo: bloco centralizado na vertical, dentro da faixa escura da moldura (depois da figura da esquerda, antes da silhueta da direita)
			const float tfs = btex ? 19.0f : 22.0f, sfs = btex ? 11.5f : 11.5f, gap = btex ? 4.0f : 8.0f;
			const float tx0 = btex ? CX0 + 176.0f : CX0 + 24.0f, tw = btex ? 215.0f : CW - 48.0f;
			// subtitulo em ate 2 linhas (quebra por palavras) para caber no espaco da faixa escura mesmo com a fonte maior
			std::vector<std::string> sl;
			{
				std::string cur, word; const std::string src = std::string(cd.sub) + " ";
				for (char ch : src)
				{
					if (ch != ' ') { word.push_back(ch); continue; }
					if (word.empty()) continue;
					const std::string t = cur.empty() ? word : cur + " " + word;
					if (!cur.empty() && txtW(t, sfs) > tw && sl.size() < 1) { sl.push_back(cur); cur = word; } else cur = t;
					word.clear();
				}
				if (!cur.empty()) sl.push_back(cur);
			}
			const float lh = txtH("Ag", sfs) + 1.0f;
			const float blkH = txtH(cd.name, tfs) + gap + lh * (float)sl.size(), ty0 = by0 + ((by1 - by0) - blkH) * 0.5f;
			txt(tx0, ty0, A(255, 255, 255, 255), fit(cd.name, tfs, tw + 10.0f), tfs, true);
			for (size_t li = 0; li < sl.size(); li++)
				txt(tx0, ty0 + txtH(cd.name, tfs) + gap + lh * (float)li, A(255, 255, 255, 245), fit(sl[li], sfs, tw), sfs, true);
		}
		// tabela
		RankState* rs = nullptr; { auto it = g_rank.find(catId); if (it != g_rank.end()) rs = &it->second; }
		const bool haveData = rs && rs->have;
		const bool showPod = haveData && !rs->rows.empty();                           // podio dos 3 primeiros (foto + moldura + fundo de linha); a tabela segue do 4o em diante
		const float PODY = 204.0f + CW / 8.0f + 6.0f;
		const float TY0 = 204.0f + CW / 8.0f + 12.0f + (showPod ? RankPodium::kHeight : 0.0f), TY1 = 634.0f;
		if (showPod)
		{
			RankPodium::Entry pe[3];
			for (int k = 0; k < 3 && k < (int)rs->rows.size(); k++)
			{
				const RankRow& rw = rs->rows[(size_t)k];
				pe[k].ok = true; pe[k].acc = rs->pod[k].ok ? rs->pod[k].acc : 0; pe[k].border = rs->pod[k].border; pe[k].style = rs->pod[k].style;
				pe[k].name = rw.name; pe[k].value = CatValue(cd, rw.val);
			}
			const ImVec2 po = V(CX0, PODY);
			RankPodium::Draw(r, po.x, po.y, CW * u, u, (double)now / 1000.0, pe);
		}
		card(CX0, TY0, X1, TY1);
		txt(CX0 + 20.0f, TY0 + 8.0f, A(206, 222, 248, 255), "Posição", 11.5f, true);
		txt(CX0 + 100.0f, TY0 + 8.0f, A(206, 222, 248, 255), "Jogador", 11.5f, true);
		txt(X1 - 22.0f - txtW(cd.metric, 11.5f), TY0 + 8.0f, A(206, 222, 248, 255), cd.metric, 11.5f, true);
		if (!haveData)
		{
			const std::string m = "Carregando ranking...";
			txt(CX0 + (CW - txtW(m, 11.0f)) * 0.5f, TY0 + 140.0f, A(190, 208, 236, 200), m, 11.0f);
			wantRank = catId;
		}
		else
		{
			if (now - rs->t > 600000) wantRank = catId;                      // dado velho (>10 min): pede de novo sem apagar o que ja esta na tela
			const float RH = showPod ? 30.0f : 34.0f;
			const int k0 = showPod ? std::min(3, (int)rs->rows.size()) : 0;     // com podio: a tabela comeca no 4o lugar
			float y = TY0 + 34.0f;
			// minha linha (fixa no topo da tabela)
			{
				r->drawRoundedRect(V(CX0 + 10, y), V(X1 - 10, y + RH - 4), A(98, 200, 230, 70), 8.0f * u, true);
				r->drawRoundedRect(V(CX0 + 10, y), V(X1 - 10, y + RH - 4), A(150, 232, 250, 140), 8.0f * u, false, std::max(1.0f, 1.0f * u));
				const std::string pos = rs->mePos ? std::to_string(rs->mePos) : "-";
				const std::string nm = g_i.name + " #" + std::to_string(g_i.rg);
				const std::string val = rs->mePos ? CatValue(cd, rs->meVal) : (rs->meVal ? CatValue(cd, rs->meVal) : "-");
				const float ty = y + (RH - 4 - txtH(nm, 11.0f)) * 0.5f;
				txt(CX0 + 22.0f, ty, A(244, 250, 255, 255), pos, 11.0f, true);
				onlineDot(CX0 + 84.0f, y + (RH - 4) * 0.5f);                          // eu estou online
				txt(CX0 + 100.0f, ty, A(244, 250, 255, 255), fit(nm, 11.0f, CW - 100.0f - 170.0f), 11.0f, true);
				txt(X1 - 24.0f - txtW(val, 11.0f), ty, A(244, 250, 255, 255), val, 11.0f, true);
			}
			const float LY0 = y + RH + 2.0f, LY1 = TY1 - 8.0f;
			const int n = (int)rs->rows.size();
			const float content = (float)(n - k0) * RH, view = LY1 - LY0, maxS = std::max(0.0f, content - view);
			g_rankScroll = std::clamp(g_rankScroll, 0.0f, maxS);
			const ImVec2 c0 = V(CX0 + 6, LY0), c1 = V(X1 - 6, LY1);
			g_rankR[0] = c0.x; g_rankR[1] = c0.y; g_rankR[2] = c1.x; g_rankR[3] = c1.y;
			if (n == 0)
			{
				const std::string m = "Ninguém no ranking ainda.";
				txt(CX0 + (CW - txtW(m, 11.0f)) * 0.5f, LY0 + 70.0f, A(190, 208, 236, 200), m, 11.0f);
			}
			r->pushClipRect(c0, c1, false);
			for (int k = k0; k < n; k++)
			{
				const RankRow& rw = rs->rows[(size_t)k];
				const float ry = LY0 + (float)(k - k0) * RH - g_rankScroll;
				if (ry + RH < LY0 || ry > LY1) continue;
				r->drawRoundedRect(V(CX0 + 10, ry), V(X1 - 10, ry + RH - 4), A(110, 138, 184, (k % 2) ? 44 : 78), 8.0f * u, true);
				const float ty = ry + (RH - 4 - txtH(rw.name, 11.0f)) * 0.5f;
				const ImColor pc = rw.pos == 1 ? A(255, 214, 90, 255) : (rw.pos == 2 ? A(214, 224, 238, 255) : (rw.pos == 3 ? A(226, 160, 100, 255) : A(226, 236, 252, 255)));
				txt(CX0 + 22.0f, ty, pc, std::to_string(rw.pos), 11.0f, true);
				if (rw.online) onlineDot(CX0 + 84.0f, ry + (RH - 4) * 0.5f);              // bolinha verde = jogador online agora
				txt(CX0 + 100.0f, ty, A(236, 244, 255, 255), fit(rw.name, 11.0f, CW - 100.0f - 170.0f), 11.0f);
				const std::string val = CatValue(cd, rw.val);
				txt(X1 - 24.0f - txtW(val, 11.0f), ty, A(226, 238, 255, 255), val, 11.0f, true);
			}
			r->popClipRect();
			if (maxS > 0.0f)
			{
				const float tl = std::max(26.0f, view * view / content), ty = LY0 + (view - tl) * (g_rankScroll / maxS);
				r->drawRoundedRect(V(X1 - 9.0f, LY0), V(X1 - 6.0f, LY1), A(120, 150, 196, 40), 1.5f * u, true);
				r->drawRoundedRect(V(X1 - 9.0f, ty), V(X1 - 6.0f, ty + tl), A(178, 232, 254, 235), 1.5f * u, true);
			}
		}
	}
	else if (g_page == 4)
	{
		// ================= PERSONALIZAR (foto + moldura do avatar + fundo da linha; seletor padronizado) =================
		const float CW = 668.0f, X1 = CX0 + CW;
		NavIcon(r, 5, V(CX0 + 14.0f, 134.0f).x, V(CX0 + 14.0f, 134.0f).y, 1.9f * u, A(226, 238, 255, 235));
		txt(CX0 + 40.0f, 120.0f, A(240, 246, 255, 255), "Personalizar", 19.0f, true);
		txt(CX0 + 40.0f, 146.0f, A(176, 198, 230, 220), fit("Foto, moldura do avatar e fundo da sua linha. Cadeado = bloqueado: só a administração libera.", 9.4f, CW - 40.0f), 9.4f);
		const ImVec2 po = V(CX0 + 22.0f, 190.0f);
		g_avp[0] = po.x; g_avp[1] = po.y; g_avp[2] = (CW - 44.0f) * u;
		const float ph = AvatarPicker::Height(g_avp[2], u) / u;
		card(CX0, 172.0f, X1, 190.0f + ph + 14.0f);
		const int prs = (g_down.kind == K_AVP) ? AvatarPicker::HitTest(g_avp[0], g_avp[1], g_avp[2], u, g_sx, g_sy) : 0;
		AvatarPicker::Draw(r, g_avp[0], g_avp[1], g_avp[2], u, (double)now / 1000.0, prs);
		addHit(CX0 + 22.0f, 190.0f, X1 - 22.0f, 190.0f + ph, K_AVP, 0);
	}
	else if (g_page == 3)
	{
		// ================= CONFIGURACOES =================
		const float CW = 668.0f, X1 = CX0 + CW;
		NavIcon(r, 5, V(CX0 + 14.0f, 134.0f).x, V(CX0 + 14.0f, 134.0f).y, 1.9f * u, A(226, 238, 255, 235));
		txt(CX0 + 40.0f, 120.0f, A(240, 246, 255, 255), "Configurações", 19.0f, true);
		card(CX0, 170.0f, X1, 330.0f);
		const float pa = PanelAlpha::Get();
		const int pct = (int)std::lround(pa * 100.0f);
		txt(CX0 + 22.0f, 190.0f, A(240, 246, 255, 255), "Transparência das telas", 13.0f, true);
		txt(CX0 + 22.0f, 212.0f, A(176, 198, 230, 220), "Vale para o Menu jogador e para o Gerenciador de grupo (as duas telas usam o mesmo valor).", 9.2f);
		const std::string pv = std::to_string(pct) + "% opaco";
		txt(X1 - 22.0f - txtW(pv, 13.0f), 190.0f, A(98, 232, 250, 255), pv, 13.0f, true);
		// trilho + controle (arrastar) e botoes - / +
		const float TX0 = CX0 + 84.0f, TX1 = X1 - 84.0f, TY = 270.0f;
		const float t = (pa - PanelAlpha::kMin) / (PanelAlpha::kMax - PanelAlpha::kMin);
		const float kx = TX0 + (TX1 - TX0) * std::clamp(t, 0.0f, 1.0f);
		r->drawRoundedRect(V(TX0, TY - 4.0f), V(TX1, TY + 4.0f), A(120, 150, 196, 90), 4.0f * u, true);
		r->drawRoundedRect(V(TX0, TY - 4.0f), V(kx, TY + 4.0f), A(98, 232, 250, 235), 4.0f * u, true);
		const bool sdn = g_down.kind == K_TSLD;
		for (int g = 2; g >= 1; g--) r->drawRoundedRect(V(kx - 11.0f - g, TY - 11.0f - g), V(kx + 11.0f + g, TY + 11.0f + g), A(80, 230, 250, sdn ? 40 : 24), (11.0f + g) * u, true);
		r->drawRoundedRect(V(kx - 11.0f, TY - 11.0f), V(kx + 11.0f, TY + 11.0f), A(236, 250, 255, 255), 11.0f * u, true);
		r->drawRoundedRect(V(kx - 11.0f, TY - 11.0f), V(kx + 11.0f, TY + 11.0f), A(98, 232, 250, 255), 11.0f * u, false, std::max(1.0f, 1.5f * u));
		const ImVec2 s0 = V(TX0, TY), s1 = V(TX1, TY);
		g_sldR[0] = s0.x; g_sldR[1] = s0.y - 22.0f * u; g_sldR[2] = s1.x; g_sldR[3] = s1.y + 22.0f * u;
		addHit(TX0 - 14.0f, TY - 22.0f, TX1 + 14.0f, TY + 22.0f, K_TSLD, 0);
		pill(CX0 + 22.0f, TY - 14.0f, CX0 + 62.0f, TY + 14.0f, false, g_down.kind == K_TMIN, "-", 16.0f);
		pill(X1 - 62.0f, TY - 14.0f, X1 - 22.0f, TY + 14.0f, false, g_down.kind == K_TPLUS, "+", 16.0f);
		addHit(CX0 + 22.0f, TY - 14.0f, CX0 + 62.0f, TY + 14.0f, K_TMIN, 0);
		addHit(X1 - 62.0f, TY - 14.0f, X1 - 22.0f, TY + 14.0f, K_TPLUS, 0);
		txt(CX0 + 22.0f, 304.0f, A(150, 176, 214, 190), "30% = bem transparente   |   100% = fundo sólido   (salvo automaticamente)", 8.6f);

		// ---- interruptores: botoes escondidos por padrao + minimapa ----
		{
			const CtlLogic::ModernConfig mc = ControlMode::GetConfig();
			struct Tg { const char* name; const char* sub; bool on; };
			const Tg tg[5] = {
				{ "Botão Inventário", "Botão da mochila na tela (controles modernos). Padrão: desativado.", mc.showInv },
				{ "Botão Corrida (lock)", "Botão que liga/desliga correr reto sozinho (controles modernos). Padrão: desativado.", mc.showRunLock },
				{ "Minimapa", "Minimapa e nome do local na tela, a pé e de carro. Padrão: ativado.", UiPrefs::MapOn() },
				{ "Tarja do local", "Nome do local (cidade | zona) e bússola na tela. Padrão: ativada.", UiPrefs::LocOn() },
				{ "Tarja de ID e horário", "ID da conta, hora e data no topo direito. Padrão: ativada.", UiPrefs::IdOn() },
			};
			const float cy0 = 346.0f, cy1 = cy0 + 16.0f + 5 * 50.0f;
			card(CX0, cy0, X1, cy1);
			for (int k = 0; k < 5; k++)
			{
				const float ry = cy0 + 12.0f + (float)k * 50.0f;
				txt(CX0 + 22.0f, ry + 2.0f, A(240, 246, 255, 255), tg[k].name, 13.0f, true);
				txt(CX0 + 22.0f, ry + 23.0f, A(176, 198, 230, 200), fit(tg[k].sub, 8.8f, CW - 44.0f - 90.0f), 8.8f);
				const float sw0 = X1 - 22.0f - 52.0f, sw1 = X1 - 22.0f, sy0 = ry + 6.0f, sy1 = sy0 + 26.0f;
				const bool down = g_down.kind == K_TOG && g_down.idx == k;
				const ImColor trk = tg[k].on ? A(98, 232, 250, 255) : A(88, 108, 142, down ? 230 : 190);
				r->drawRoundedRect(V(sw0, sy0), V(sw1, sy1), trk, 13.0f * u, true);
				const float kx = tg[k].on ? sw1 - 13.0f : sw0 + 13.0f;
				r->drawRoundedRect(V(kx - 10.0f, sy0 + 3.0f), V(kx + 10.0f, sy1 - 3.0f), A(246, 252, 255, 255), 10.0f * u, true);
				const std::string st = tg[k].on ? "ON" : "OFF";
				txt(tg[k].on ? sw0 + 9.0f : sw1 - 9.0f - txtW(st, 8.4f), sy0 + (26.0f - txtH(st, 8.4f)) * 0.5f, tg[k].on ? A(24, 50, 74, 255) : A(214, 226, 246, 255), st, 8.4f, true);
				addHit(CX0 + 6.0f, ry - 4.0f, X1 - 6.0f, ry + 44.0f, K_TOG, k);
			}
		}
	}
	else
	{
		// ================= REGRAS =================
		const float CW = 668.0f, X1 = CX0 + CW;
		NavIcon(r, 3, V(CX0 + 14.0f, 134.0f).x, V(CX0 + 14.0f, 134.0f).y, 1.9f * u, A(226, 238, 255, 235));
		txt(CX0 + 40.0f, 120.0f, A(240, 246, 255, 255), "Regras do servidor", 19.0f, true);
		if (ntab == 0)
		{
			card(CX0, 160.0f, X1, 300.0f);
			const std::string m = "As regras ainda não foram cadastradas pela equipe.";
			txt(CX0 + (CW - txtW(m, 11.5f)) * 0.5f, 220.0f, A(190, 208, 236, 210), m, 11.5f);
		}
		else
		{
			// fileira de abas (rola para os lados quando passa da largura)
			{
				const float rowX0 = CX0, rowX1 = X1;
				float total = 0.0f; std::vector<float> w((size_t)ntab);
				for (int k = 0; k < ntab; k++) { w[(size_t)k] = txtW(g_i.tabs[(size_t)k], 12.0f) + 40.0f; total += w[(size_t)k] + 12.0f; }
				const float maxS = std::max(0.0f, total - 12.0f - (rowX1 - rowX0));
				g_tabsScroll = std::clamp(g_tabsScroll, 0.0f, maxS);
				const ImVec2 c0 = V(rowX0, 148.0f), c1 = V(rowX1, 200.0f);
				g_tabsR[0] = c0.x; g_tabsR[1] = c0.y; g_tabsR[2] = c1.x; g_tabsR[3] = c1.y;
				r->pushClipRect(c0, c1, false);
				float x = rowX0 - g_tabsScroll;
				for (int k = 0; k < ntab; k++)
				{
					pill(x, 154.0f, x + w[(size_t)k], 196.0f, k == g_ruleTab, g_down.kind == K_UTAB && g_down.idx == k, g_i.tabs[(size_t)k], 12.0f);
					const float hx0 = std::max(x, rowX0), hx1 = std::min(x + w[(size_t)k], rowX1);
					if (hx1 > hx0) addHit(hx0, 148.0f, hx1, 200.0f, K_UTAB, k);
					x += w[(size_t)k] + 12.0f;
				}
				r->popClipRect();
			}
			if (g_ruleTab >= ntab) g_ruleTab = 0;
			const float BY0 = 204.0f, BY1 = 634.0f;
			card(CX0, BY0, X1, BY1);
			RuleState& rs = g_rules[g_ruleTab];
			if (!rs.have)
			{
				const std::string m = "Carregando regras...";
				txt(CX0 + (CW - txtW(m, 11.0f)) * 0.5f, BY0 + 160.0f, A(190, 208, 236, 200), m, 11.0f);
				wantRule = g_ruleTab;
			}
			else
			{
				const float TXW = CW - 56.0f;
				if (rs.layU != u || rs.layW != TXW)           // refaz a quebra de linhas so quando muda a escala ou o texto
				{
					rs.wl.clear(); float y = 0.0f;
					for (const RuleLine& ln : rs.lines)
					{
						if (ln.style == 3) { y += 12.0f; continue; }
						const bool head = ln.style == 1, sub = ln.style == 5, item = ln.style == 2 || ln.style == 4;
						const float fs = head ? 15.0f : (sub ? 12.4f : 11.6f), lh = head ? 22.0f : (sub ? 19.0f : 18.0f), ind = item ? 16.0f : 0.0f, maxW = TXW - ind;
						if (head) y += 8.0f;
						std::string cur; bool first = true;
						auto flush = [&]() { WLine w; w.style = ln.style; w.y = y; w.h = lh; w.text = cur; w.first = first; rs.wl.push_back(std::move(w)); y += lh; first = false; cur.clear(); };
						size_t i = 0; const std::string& s = ln.text;
						while (i <= s.size())
						{
							size_t j = s.find(' ', i); if (j == std::string::npos) j = s.size();
							const std::string word = s.substr(i, j - i);
							if (!word.empty())
							{
								const std::string cand = cur.empty() ? word : cur + " " + word;
								if (!cur.empty() && txtW(cand, fs) > maxW) { flush(); cur = word; } else cur = cand;
							}
							i = j + 1;
						}
						if (!cur.empty() || first) flush();
						y += sub ? 3.0f : 2.0f;
					}
					rs.total = y; rs.layU = u; rs.layW = TXW;
				}
				const float VY0 = BY0 + 14.0f, VY1 = BY1 - 14.0f, view = VY1 - VY0, maxS = std::max(0.0f, rs.total - view);
				g_ruleScroll = std::clamp(g_ruleScroll, 0.0f, maxS);
				const ImVec2 c0 = V(CX0 + 6, VY0), c1 = V(X1 - 6, VY1);
				g_ruleR[0] = c0.x; g_ruleR[1] = c0.y; g_ruleR[2] = c1.x; g_ruleR[3] = c1.y;
				if (rs.lines.empty()) { const std::string m = "Esta aba ainda está vazia."; txt(CX0 + (CW - txtW(m, 11.0f)) * 0.5f, BY0 + 160.0f, A(190, 208, 236, 200), m, 11.0f); }
				r->pushClipRect(c0, c1, false);
				for (const WLine& w : rs.wl)
				{
					const float y = VY0 + w.y - g_ruleScroll;
					if (y + w.h < VY0 || y > VY1) continue;
					const bool head = w.style == 1, sub = w.style == 5, item = w.style == 2 || w.style == 4;
					const float x = CX0 + 22.0f + (item ? 16.0f : 0.0f);
					if (item && w.first) { const ImVec2 b = V(CX0 + 26.0f, y + w.h * 0.5f); r->drawRoundedRect(ImVec2(b.x - 2 * u, b.y - 2 * u), ImVec2(b.x + 2 * u, b.y + 2 * u), A(98, 232, 250, 255), 2.0f * u, true); }
					txt(x, y, head ? A(244, 250, 255, 255) : (sub ? A(126, 226, 244, 255) : A(218, 230, 248, 255)), w.text, head ? 15.0f : (sub ? 12.4f : 11.6f), head || sub);
				}
				r->popClipRect();
				if (maxS > 0.0f)
				{
					const float tl = std::max(26.0f, view * view / rs.total), ty = VY0 + (view - tl) * (g_ruleScroll / maxS);
					r->drawRoundedRect(V(X1 - 12.0f, VY0), V(X1 - 9.0f, VY1), A(120, 150, 196, 40), 1.5f * u, true);
					r->drawRoundedRect(V(X1 - 12.0f, ty), V(X1 - 9.0f, ty + tl), A(178, 232, 254, 235), 1.5f * u, true);
				}
			}
		}
	}

	// ---- toques ----
	auto hitAt = [&](float x, float y) -> Target
	{
		for (size_t i = g_hits.size(); i-- > 0;)
			if (x >= g_hits[i].x0 && x <= g_hits[i].x1 && y >= g_hits[i].y0 && y <= g_hits[i].y1) return g_hits[i].t;
		return Target();
	};
	auto setAlphaFromX = [&](float x)
	{
		if (g_sldR[2] <= g_sldR[0]) return;
		const float tt = std::clamp((x - g_sldR[0]) / (g_sldR[2] - g_sldR[0]), 0.0f, 1.0f);
		PanelAlpha::Set(PanelAlpha::kMin + tt * (PanelAlpha::kMax - PanelAlpha::kMin));
	};
	auto inR = [](const float rc[4], float x, float y) { return rc[2] > rc[0] && x >= rc[0] && x <= rc[2] && y >= rc[1] && y <= rc[3]; };
	for (const ShopTouch& e : events)
	{
		if (f < 1.0f) continue;                                                // durante a animacao de entrada ignora toques
		if (e.type == 2 && g_finger < 0)
		{
			g_finger = e.num; g_sx = g_lx = e.x; g_sy = g_ly = e.y; g_moved = false;
			g_down = hitAt(e.x, e.y);
			g_area = inR(g_rankR, e.x, e.y) ? 1 : (inR(g_ruleR, e.x, e.y) ? 2 : (inR(g_tabsR, e.x, e.y) ? 3 : 0));
			if (g_down.kind == K_TSLD) setAlphaFromX(e.x);                          // tocar no trilho ja ajusta
		}
		else if (e.num == g_finger && e.type == 3 && g_down.kind == K_TSLD)
		{
			setAlphaFromX(e.x); g_lx = e.x; g_ly = e.y;                             // arrastando: ajusta ao vivo
		}
		else if (e.num == g_finger && e.type == 3)
		{
			if (!g_moved && (std::fabs(e.y - g_sy) > 7.0f * u || std::fabs(e.x - g_sx) > 7.0f * u)) { g_moved = true; g_down = Target(); }
			if (g_moved && g_area == 1) g_rankScroll -= (e.y - g_ly) / u;
			if (g_moved && g_area == 2) g_ruleScroll -= (e.y - g_ly) / u;
			if (g_moved && g_area == 3) g_tabsScroll -= (e.x - g_lx) / u;
			g_lx = e.x; g_ly = e.y;
			if (!g_moved && g_down.kind != K_NONE) { const Target t = hitAt(e.x, e.y); if (t.kind != g_down.kind || t.idx != g_down.idx) g_down = Target(); }
		}
		else if (e.num == g_finger && e.type == 1)
		{
			const Target was = g_down; const bool mv = g_moved;
			g_finger = -1; g_down = Target(); g_moved = false; g_area = 0;
			if (was.kind == K_TSLD) { if (!e.cancel) setAlphaFromX(e.x); PanelAlpha::Save(); continue; }
			if (e.cancel || mv || was.kind == K_NONE) continue;
			const Target now2 = hitAt(e.x, e.y);
			if (now2.kind != was.kind || now2.idx != was.idx) continue;
			switch (was.kind)
			{
			case K_CLOSE: answer = 1; return;
			case K_BAN_G: answer = 4; return;
			case K_BAN_V: ShopPanel::LocalToast(2, "A loja VIP chega em breve."); break;
			case K_NAV:
			{
				if (was.idx < 0 || was.idx >= (int)(sizeof(kNav) / sizeof(kNav[0]))) break;
				const NavDef& nv = kNav[was.idx];
				if (nv.soon) { ShopPanel::LocalToast(2, "Em breve."); break; }
				if (g_page != nv.page) { g_page = nv.page; g_rankScroll = 0.0f; g_ruleScroll = 0.0f; }
				break;
			}
			case K_TOG:
			{
				if (was.idx == 2) { UiPrefs::SetMapOn(!UiPrefs::MapOn()); UiPrefs::Save(); break; }
				if (was.idx == 3) { UiPrefs::SetLocOn(!UiPrefs::LocOn()); UiPrefs::Save(); break; }
				if (was.idx == 4) { UiPrefs::SetIdOn(!UiPrefs::IdOn()); UiPrefs::Save(); break; }
				CtlLogic::ModernConfig c = ControlMode::GetConfig();
				if (was.idx == 0) c.showInv = !c.showInv; else c.showRunLock = !c.showRunLock;
				ControlMode::SetConfig(c, true);
				break;
			}
			case K_AVP:
			{
				const int c0 = AvatarPicker::HitTest(g_avp[0], g_avp[1], g_avp[2], u, g_sx, g_sy), c1 = AvatarPicker::HitTest(g_avp[0], g_avp[1], g_avp[2], u, e.x, e.y);
				if (c0 > 0 && c0 == c1) AvatarPicker::Activate(c0);
				break;
			}
			case K_TMIN:  PanelAlpha::Set(PanelAlpha::Get() - PanelAlpha::kStep); PanelAlpha::Save(); break;
			case K_TPLUS: PanelAlpha::Set(PanelAlpha::Get() + PanelAlpha::kStep); PanelAlpha::Save(); break;
			case K_RTAB: if (was.idx >= 0 && was.idx < ncat && g_rankCat != was.idx) { g_rankCat = was.idx; g_rankScroll = 0.0f; } break;
			case K_UTAB: if (was.idx >= 0 && was.idx < ntab && g_ruleTab != was.idx) { g_ruleTab = was.idx; g_ruleScroll = 0.0f; } break;
			default: break;
			}
		}
		else if (e.num == g_finger && e.cancel) { g_finger = -1; g_down = Target(); g_moved = false; g_area = 0; }
	}

	// ---- pedidos ao servidor (um por quadro; o servidor confere de novo): ranking da aba aberta / regras da aba aberta ----
	if (answer == 0 && wantRank >= 0)
	{
		RankState& s = g_rank[wantRank];
		const bool retry = s.loading && now - s.reqT > 1500;
		if (!s.loading || retry) { s.loading = true; s.reqT = now; answer = 2; arg = wantRank; return; }
	}
	if (answer == 0 && wantRule >= 0)
	{
		RuleState& s = g_rules[wantRule];
		const bool retry = s.loading && now - s.reqT > 1500;
		if (!s.loading || retry) { s.loading = true; s.reqT = now; answer = 3; arg = wantRule; return; }
	}
}
