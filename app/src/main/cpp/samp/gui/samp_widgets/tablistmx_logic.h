#pragma once

// =====================================================================================
//  TabListMX -- logica PURA (sem ImGui/jogo): geometria das duas barras de abas, toque (tap) e comando /tablistmx.
//  Testada no host (tests/t_tablistmx.cpp). O desenho/toque reais ficam em tablistmx.cpp.
//
//  Estilo 1: barra-pilula escura com contorno branco, aba escolhida "levantada" numa caixa (print 1 do usuario).
//  Estilo 2: icones+rotulos com contorno preto grosso, separador entre cada par, sem pilula (print 2).
//  Medidas em "pixels do print de referencia" multiplicadas por k (k = altura da barra / altura de referencia).
// =====================================================================================

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace TabLogic
{
	constexpr int kTabs = 5;
	enum Tab { Dialogo = 0, Inventario, Perfil, Tarefas, Ajuda };

	constexpr int kDefaultStyle = 1;
	constexpr int kDefaultSel   = Perfil;     // como no print 1: "Perfil" levantado
	constexpr int kDefaultPosY  = 93;         // centro da barra em % da altura da tela (base, centralizada)
	constexpr int kMinPosY = 6, kMaxPosY = 96;

	struct Rect
	{
		float l = 0, t = 0, r = 0, b = 0;
		float w() const { return r - l; }
		float h() const { return b - t; }
		bool  Contains(float x, float y) const { return x >= l && x <= r && y >= t && y <= b; }
	};

	struct BarLayout
	{
		int   style = 0;
		float k = 1;                   // escala (px de tela por px de referencia)
		Rect  bar;                     // estilo 1: a pilula; estilo 2: area dos icones
		Rect  cell[kTabs];             // area de cada aba
		Rect  hit[kTabs];              // area de toque (cell + folga vertical)
		float sepX[4] = {0, 0, 0, 0};  // x de cada separador
		int   nSep = 0;                // estilo 1: 2 (apos Dialogo e antes de Ajuda); estilo 2: 4
		float sepT = 0, sepB = 0;      // estilo 1: linha de sepT a sepB; estilo 2: sepT = topo do sprite do separador
		Rect  box;                     // estilo 1: caixa levantada da aba escolhida
	};

	inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

	// Referencia estilo 1: barra 1545 x 158; caixa da aba escolhida 322 x 175 (sobe 11, desce 6, folga lateral 6.5)
	constexpr float kS1W = 1545.0f, kS1H = 158.0f;
	// Referencia estilo 2: 5 celulas de 217 x 159 (sprite reduzido a 50%)
	constexpr float kS2CellW = 217.0f, kS2H = 159.0f;

	inline float BarHeightFor(float sh) { return Clamp(sh * 0.085f, 56.0f, 120.0f); }

	// sel: aba escolhida (so o estilo 1 usa para a caixa). posY: % da altura da tela.
	inline BarLayout Compute(int style, float sw, float sh, int posY, int sel)
	{
		BarLayout L;
		if ((style != 1 && style != 2) || !(sw > 100.0f) || !(sh > 100.0f)) return L;
		L.style = style;
		const float refW = (style == 1) ? kS1W : kS2CellW * kTabs;
		const float refH = (style == 1) ? kS1H : kS2H;
		float H = BarHeightFor(sh);
		float k = H / refH;
		if (refW * k > sw * 0.92f) k = sw * 0.92f / refW;       // telas estreitas: reduz
		L.k = k;
		const float W = refW * k; H = refH * k;
		const float cy  = sh * (float)std::min(std::max(posY, kMinPosY), kMaxPosY) / 100.0f;
		const float pad = 14.0f * k;
		L.bar.l = (sw - W) * 0.5f; L.bar.r = L.bar.l + W;
		L.bar.t = Clamp(cy - H * 0.5f, pad, sh - H - pad); L.bar.b = L.bar.t + H;
		const float cw = W / (float)kTabs;
		for (int i = 0; i < kTabs; i++)
		{
			L.cell[i] = { L.bar.l + cw * i, L.bar.t, L.bar.l + cw * (i + 1), L.bar.b };
			const float ex = H * 0.14f;
			L.hit[i] = { L.cell[i].l, L.cell[i].t - ex, L.cell[i].r, L.cell[i].b + ex };
		}
		if (style == 1)
		{
			L.nSep = 2;
			L.sepX[0] = L.bar.l + cw;            // Dialogo | Inventario
			L.sepX[1] = L.bar.l + cw * 4.0f;     // Tarefas | Ajuda
			L.sepT = L.bar.t + 35.0f * k; L.sepB = L.bar.t + 125.0f * k;
			const int s = std::min(std::max(sel, 0), kTabs - 1);
			L.box = { std::max(L.cell[s].l - 6.5f * k, L.bar.l), L.bar.t - 11.0f * k,
			          std::min(L.cell[s].r + 6.5f * k, L.bar.r), L.bar.b + 6.0f * k };
		}
		else
		{
			L.nSep = 4;
			for (int i = 0; i < 4; i++) L.sepX[i] = L.bar.l + cw * (float)(i + 1);
			L.sepT = L.bar.t + 24.5f * k;
		}
		return L;
	}

	// -1 = fora de todas as abas
	inline int HitTab(const BarLayout& L, float x, float y)
	{
		if (L.style == 0) return -1;
		for (int i = 0; i < kTabs; i++) if (L.hit[i].Contains(x, y)) return i;
		return -1;
	}

	// Toque valido: soltou na MESMA aba em que apertou, e o gesto nao foi cancelado.
	inline bool IsTap(int startTab, int endTab, bool cancelled) { return startTab >= 0 && startTab == endTab && !cancelled; }

	// Comando de chat configuravel por aba (settings.ini): so aceita "/palavra ..." curto e sem controle.
	inline std::string SanitizeCommand(const std::string& s)
	{
		std::string t = s;
		while (!t.empty() && (t.back() == ' ' || t.back() == '\r' || t.back() == '\n' || t.back() == '\t')) t.pop_back();
		size_t b = 0; while (b < t.size() && t[b] == ' ') b++;
		t = t.substr(b);
		if (t.size() < 2 || t.size() > 63 || t[0] != '/') return std::string();
		for (unsigned char c : t) if (c < 32 || c == 127) return std::string();
		return t;
	}

	struct State
	{
		int style = kDefaultStyle;       // 0 = oculta, 1, 2
		int lastStyle = kDefaultStyle;   // ultimo estilo ligado (para "on")
		int posY = kDefaultPosY;
		int sel = kDefaultSel;
	};

	inline const char* TabName(int i)
	{
		static const char* n[kTabs] = { "Diálogo", "Inventário", "Perfil", "Tarefas", "Ajuda" };
		return (i >= 0 && i < kTabs) ? n[i] : "?";
	}

	inline std::string StatusText(const State& s)
	{
		std::string r = "[TABLISTMX] estilo=";
		r += (s.style == 0) ? std::string("oculta") : std::to_string(s.style);
		r += " pos=" + std::to_string(s.posY) + "%";
		r += " aba=" + std::string(TabName(s.sel));
		return r;
	}

	// args = texto depois de "/tablistmx". Devolve a resposta do chat; muda 'st'. changed=true se algo deve ser salvo.
	inline std::string Command(const std::string& args, State& st, bool& changed)
	{
		changed = false;
		std::string a = args;
		while (!a.empty() && a.front() == ' ') a.erase(a.begin());
		while (!a.empty() && a.back()  == ' ') a.pop_back();
		for (char& c : a) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		const std::string usage = " | uso: /tablistmx 1 | 2 | 0 (oculta) | on | pos <6-96>";
		if (a.empty() || a == "status") return StatusText(st) + usage;
		if (a == "1" || a == "2")
		{
			st.style = a[0] - '0'; st.lastStyle = st.style; changed = true;
			return "[TABLISTMX] estilo " + a + " ativado";
		}
		if (a == "0" || a == "off" || a == "oculta" || a == "ocultar")
		{
			if (st.style != 0) st.lastStyle = st.style;
			st.style = 0; changed = true;
			return "[TABLISTMX] barra oculta (use /tablistmx 1 ou 2)";
		}
		if (a == "on" || a == "ligar") { st.style = st.lastStyle ? st.lastStyle : kDefaultStyle; changed = true; return "[TABLISTMX] estilo " + std::to_string(st.style) + " ativado"; }
		if (a.rfind("pos", 0) == 0)
		{
			std::string n = a.substr(3);
			while (!n.empty() && n.front() == ' ') n.erase(n.begin());
			char* e = nullptr;
			const long v = n.empty() ? -1 : std::strtol(n.c_str(), &e, 10);
			if (n.empty() || !e || *e != 0 || v < kMinPosY || v > kMaxPosY) return "[TABLISTMX] uso: /tablistmx pos <" + std::to_string(kMinPosY) + "-" + std::to_string(kMaxPosY) + "> (altura em % da tela)";
			st.posY = (int)v; changed = true;
			return "[TABLISTMX] posicao " + std::to_string(st.posY) + "%";
		}
		return "[TABLISTMX] argumento invalido" + usage;
	}
}
