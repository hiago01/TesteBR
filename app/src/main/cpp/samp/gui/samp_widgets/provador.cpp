#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/CustomSkin.h"
#include "../../game/SkinSlots.h"
#include "../../game/ProvadorModel.h"
#include "../../net/netgame.h"
#include "provador.h"

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

// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

// =====================================================================================
//  Estado global / fila de toques (thread do jogo -> thread de desenho)
// =====================================================================================
namespace
{
	struct TouchEv { int type; int num; float x; float y; bool cancel; };

	std::atomic<bool> g_open{false};
	std::atomic<bool> g_flush{false};
	std::atomic<bool> g_resetView{false};
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::mutex        g_evMutex;
	std::vector<TouchEv> g_events;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- estado da tela (so a thread de desenho usa) ----
	typedef CCustomSkin::UiSlot  SlotState;
	using ProvadorModel::kUnavailable; using ProvadorModel::kDefault; using ProvadorModel::kEquipped;
	using ProvadorModel::kDisabled; using ProvadorModel::kCovered;

	int                  g_cat = 0;                 // categoria (SkinSlots::kUiCategories)
	int                  g_sel = -1;                // slot selecionado (indice em SkinSlots::kSlots)
	float                g_scrollList = 0.0f;
	float                g_scrollVar  = 0.0f;
	std::vector<int>     g_catSlots;                // slots da categoria atual (indices em kSlots)
	CCustomSkin::UiState g_st;                      // ultimo estado REAL publicado pelo CCustomSkin
	bool                 g_haveSt = false;
	uint32_t             g_stVer  = 0;
	bool                 g_noticeInit = false;
	uint32_t             g_noticeSeq = 0;           // último aviso assíncrono já mostrado (confirmado/recusado pelo servidor, queda...)
	uint32_t             g_inflight   = 0;          // id do pedido aguardando resposta (0 = nenhum)
	int64_t              g_inflightAt = 0;
	std::string          g_okToast;                 // mostrada se o pedido em andamento der certo
	bool                 g_afterPrepare = false;    // o "refresh" em andamento veio do botao PREPARAR (/skin7 apply)
	std::string          g_toast;
	bool                 g_toastErr   = false;
	int64_t              g_toastUntil = 0;

	enum Kind { K_NONE = 0, K_CAT, K_ROW, K_VAR, K_PREV, K_NEXT, K_TOGGLE, K_APPLY, K_CANCEL, K_BACK, K_PREPARE };

	struct Hit { int kind = K_NONE; int idx = -1; };

	struct Drag
	{
		int   finger = -1;
		Hit   hit;
		int   region = 0;        // 0 nada, 1 lista de opcoes, 2 grade de variantes (rolagem)
		float sy = 0.0f, scroll0 = 0.0f;
		bool  scrolling = false;
	} g_drag;

	const ImColor cPanel (13, 16, 21, 228);
	const ImColor cPanel2(20, 24, 31, 232);
	const ImColor cLine  (44, 52, 64, 255);
	const ImColor cText  (226, 231, 237, 255);
	const ImColor cDim   (124, 133, 145, 255);
	const ImColor cBlue  (52, 140, 235, 255);
	const ImColor cBlueBg(52, 140, 235, 46);
	const ImColor cRed   (232, 92, 84, 255);
	const ImColor cOff   (30, 35, 43, 235);
	const ImColor cAmber (240, 176, 56, 255);       // prévia (ainda não confirmada)
	const ImColor cAmberBg(240, 176, 56, 46);

	void Toast(const std::string& msg, bool err)
	{
		g_toast = msg; g_toastErr = err; g_toastUntil = NowMs() + 3200;
	}

	const SkinSlots::Slot& SlotAt(int k) { return SkinSlots::kSlots[k]; }

	const SlotState* StateOf(int k)
	{
		return (g_haveSt && k >= 0 && k < (int)g_st.slots.size()) ? &g_st.slots[(size_t)k] : nullptr;
	}

	// "hair2" -> "Cabelo 2". O nome real do slot aparece no rodape; slot desconhecido mostra o proprio nome.
	std::string SlotLabel(const char* name)
	{
		struct Lbl { const char* prefix; const char* label; };
		static const Lbl kLbl[] = {
			{ "torso", "Camisa" }, { "pants", "Calça" }, { "short", "Bermuda" }, { "underwear", "Roupa íntima" },
			{ "hair", "Cabelo" }, { "head", "Rosto" }, { "glasse", "Óculos" }, { "ear", "Brinco" },
			{ "clock", "Relógio" }, { "mask", "Máscara" }, { "necklace", "Colar" }, { "hat", "Chapéu" },
			{ "boot", "Bota" }, { "slipper", "Chinelo" }, { "backpack", "Mochila" },
			{ "brow", "Sobrancelha" }, { "beard", "Barba" },
			{ "skin_torso", "Pele: tronco" }, { "skin_leg", "Pele: pernas" }, { "skin_feet", "Pele: pés" },
			{ "tat_back", "Tatuagem: costas" }, { "tat_chest", "Tatuagem: peito" }, { "tat_arm", "Tatuagem: braços" },
			{ "tat_shoulder", "Tatuagem: ombros" }, { "tat_legs", "Tatuagem: pernas" },
		};
		const size_t n = std::strlen(name);
		for (const Lbl& l : kLbl)
		{
			const size_t pn = std::strlen(l.prefix);
			if (n < pn || std::strncmp(name, l.prefix, pn) != 0) continue;
			bool digits = true;
			for (size_t i = pn; i < n; ++i) if (name[i] < '0' || name[i] > '9') digits = false;
			if (!digits) continue;
			std::string s = l.label;
			if (n > pn) s += std::string(" ") + (name + pn);
			return s;
		}
		return name;
	}

	void RebuildCategory()
	{
		g_catSlots.clear();
		for (int k = 0; k < SkinSlots::kCount; ++k)
			if (SkinSlots::UiCategoryOf(SlotAt(k).name) == g_cat) g_catSlots.push_back(k);
	}

	// Selecao inicial da categoria: o slot que ja esta visivel (estado real) ou o primeiro que existe no DFF.
	// Nao envia pedido nenhum: trocar de categoria nao muda a aparencia.
	void SelectDefault()
	{
		g_sel = -1;
		int firstOk = -1;
		for (int k : g_catSlots)
		{
			const SlotState* u = StateOf(k);
			if (!u || !u->hasMaterial) continue;
			if (firstOk < 0) firstOk = k;
			if (u->state == kEquipped) { g_sel = k; break; }
		}
		if (g_sel < 0) g_sel = firstOk;
		g_scrollList = 0.0f;
		g_scrollVar  = 0.0f;
	}

	void Request(const std::string& op, const char* okToast)
	{
		g_inflight   = CCustomSkin::UiRequest(op);       // 0 = fila cheia: o pedido NÃO foi enfileirado
		g_inflightAt = NowMs();
		g_okToast    = okToast ? okToast : "";
		if (!g_inflight) Toast("Sistema de skin ocupado, tente de novo", true);
	}

	void PreviewSlot(int k, const std::string& what)
	{
		Request(std::string("prev ") + SlotAt(k).name + " " + what, nullptr);
	}

	int VariantPos(const SlotState& u)
	{
		for (size_t i = 0; i < u.variants.size(); ++i) if (u.variants[i] == u.variant) return (int)i;
		return -1;
	}

	std::string InfoText(const SlotState* u, int k)
	{
		if (!u || k < 0) return "Selecione um item na lista";
		std::string s = SlotLabel(SlotAt(k).name) + " (" + SlotAt(k).name + ")";
		if (!u->hasMaterial) return s + "  -  indisponível no modelo";
		switch (u->state)
		{
			case kEquipped: s += "  -  EQUIPADO, variante " + std::to_string(u->variant); break;
			case kDisabled: s += "  -  desligado"; break;
			case kCovered:  s += "  -  coberto por outra peça"; break;
			default:        s += u->variants.empty() ? "  -  sem variantes no pack" : "  -  padrão, nada equipado (" + std::to_string(u->variants.size()) + " variantes)"; break;
		}
		if (u->previewed) s += "  -  PRÉVIA";
		else if (u->srvKnown) s += std::string("  -  servidor: ") + ProvadorModel::SrvWord(ProvadorModel::Srv{true, (uint8_t)u->srvEst, (uint16_t)u->srvVar}) + (u->srvEst == 2 ? " " + std::to_string(u->srvVar) : std::string());
		return s;
	}
}

// =====================================================================================
//  API estatica
// =====================================================================================
bool Provador::IsOpen() { return g_open.load(); }

void Provador::Open()
{
	if (g_open.load()) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	for (auto& c : g_claimed) c.store(false);
	CCustomSkin::UiSetActive(true);        // antes do pedido: o estado e republicado depois de cada comando
	g_resetView.store(true);               // a thread de desenho zera a tela e pede o estado real
	g_flush.store(true);                   // o hook solta os dedos que o jogo considera pressionados
	g_open.store(true);
}

void Provador::Close()
{
	if (!g_open.exchange(false)) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	// g_claimed e mantido: o dedo que ainda esta na tela continua sendo engolido ate o UP.
	CCustomSkin::UiRequest("cancel");      // desfaz a previa nao confirmada (nao faz nada se nao houver)
	CCustomSkin::UiSetActive(false);
}

void Provador::Toggle() { if (g_open.load()) Close(); else Open(); }

bool Provador::ConsumeFlushRequest() { return g_flush.exchange(false); }

bool Provador::OnBackPressed()
{
	// Debounce: onKeyDown e onBackPressed podem ambos disparar no mesmo toque; o segundo nao pode cair
	// no MaximoMenu (abriria o menu logo apos fechar esta tela).
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 400) return true;
	if (!g_open.load()) return false;
	s_last = now;
	Close();
	return true;
}

bool Provador::HandleRawTouch(int type, int num, int x, int y)
{
	const int n = num & 3;
	const bool open = g_open.load();
	if (!open && !g_claimed[n].load()) return false;     // fluxo normal do jogo

	if (open)
	{
		g_claimed[n].store(true);
		std::lock_guard<std::mutex> l(g_evMutex);
		if (g_events.size() < 256) g_events.push_back({type, n, (float)x, (float)y, g_bTouchCancel});
	}
	if (type == 1) g_claimed[n].store(false);             // pop/cancel: dedo solto
	return true;                                          // consumido: nada chega ao GTA/SA-MP
}

Provador::Provador() : Widget() {}

// =====================================================================================
//  Desenho + tratamento dos toques
// =====================================================================================
void Provador::draw(ImGuiRenderer* r)
{
	if (!g_open.load())
	{
		g_drag = Drag();
		return;
	}

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float  k   = scr.y / 540.0f;

	// ---- abertura: zera a tela e pede o estado real ----
	if (g_resetView.exchange(false))
	{
		g_drag = Drag();
		g_cat = 0; g_sel = -1; g_haveSt = false; g_stVer = ~0u; g_noticeInit = false;
		g_toastUntil = 0; g_okToast.clear(); g_afterPrepare = false;
		RebuildCategory();
		Request("refresh", nullptr);
	}

	// ---- estado real publicado pelo CCustomSkin (so copia quando mudou) ----
	{
		const uint32_t v = CCustomSkin::UiVersion();
		if (v != g_stVer)
		{
			CCustomSkin::UiState tmp;
			if (CCustomSkin::UiGetState(tmp))
			{
				g_st = std::move(tmp);
				g_haveSt = true;
				g_stVer = v;
				if (g_sel < 0 || StateOf(g_sel) == nullptr) SelectDefault();
				if (!g_noticeInit) { g_noticeInit = true; g_noticeSeq = g_st.noticeSeq; }   // avisos antigos (de antes de abrir) não aparecem
				else if (g_st.noticeSeq != g_noticeSeq)                                      // servidor confirmou/recusou, conexão caiu...
				{
					g_noticeSeq = g_st.noticeSeq;
					if (!g_st.notice.empty()) Toast(g_st.notice, g_st.notice.find("salva") == std::string::npos);
				}
				if (g_inflight && g_st.reqId == g_inflight)
				{
					g_inflight = 0;
					if (!g_st.reqOk) Toast(g_st.msg.empty() ? "Não foi possível aplicar" : g_st.msg, true);
					else if (!g_okToast.empty()) Toast(g_okToast, false);
					g_okToast.clear();
					if (g_afterPrepare)                  // o apply pode ter falhado: confere o estado real, nao o pedido
					{
						g_afterPrepare = false;
						if (g_st.skinReady) Toast("Skin preparada", false);
						else Toast(CCustomSkin::Info(), true);
					}
				}
			}
		}
		if (g_inflight && NowMs() - g_inflightAt > 2500)   // pedido perdido: nao deixa a tela travada
		{
			g_inflight = 0;
			Toast("Sem resposta do sistema de skin", true);
		}
	}

	const SlotState* selU = StateOf(g_sel);
	const bool ready    = g_haveSt && g_st.skinReady;
	const bool pending  = g_haveSt && g_st.pending;
	const bool awaiting = g_haveSt && g_st.awaiting;
	const bool online   = g_haveSt && g_st.online;
	const bool dffOk    = !g_haveSt || g_st.dffOk;
	const bool busy     = g_inflight != 0 || awaiting;      // esperando o servidor = nada novo pode ser pedido

	// ---- layout (unidades de projeto: altura 540 -> k) ----
	const float m     = 8.0f * k;
	const float hdrY  = 4.0f * k, hdrH = 38.0f * k;
	const float top   = hdrY + hdrH + 6.0f * k;
	const float bot   = scr.y - m;
	const float catW  = std::clamp(scr.x * 0.14f, 105.0f * k, 150.0f * k);
	const float panW  = std::clamp(scr.x * 0.30f, 210.0f * k, 300.0f * k);
	const float catX  = m;
	const float panX  = scr.x - m - panW;
	const float cx0   = catX + catW + m, cx1 = panX - m;      // centro (ped do jogo aparece aqui)
	const float pad   = 8.0f * k;
	const float fs    = 12.0f * k, fsS = 10.5f * k;

	const float catH = 34.0f * k, catGap = 4.0f * k;

	const float titleH  = 20.0f * k;
	const float listTop = top + titleH;
	const float listBot = top + (bot - top) * 0.52f;
	const float varTitleY = listBot + 4.0f * k;
	const float varTop  = varTitleY + titleH;
	const float varBot  = bot;
	const float rowH    = 30.0f * k;

	const int   cols    = 5;
	const float cellGap = 4.0f * k;
	const float cellW   = (panW - 2.0f * pad - (cols - 1) * cellGap) / cols;
	const float cellH   = 30.0f * k;

	const float barH = 104.0f * k, barY = bot - barH;
	const float rowAY = barY, rowAH = 26.0f * k;
	const float rowBY = barY + 30.0f * k, rowBH = 34.0f * k;
	const float rowCY = barY + 68.0f * k, rowCH = 34.0f * k;
	const float bw = cx1 - cx0;
	const float gap = 6.0f * k;

	// lista de opcoes / grade de variantes
	const int   nRows = (int)g_catSlots.size();
	const float listContent = nRows * rowH;
	const float listView = listBot - listTop;
	const float listMax = std::max(0.0f, listContent - listView);
	g_scrollList = std::clamp(g_scrollList, 0.0f, listMax);

	const int   nVars = selU ? (int)selU->variants.size() : 0;
	const int   varRows = (nVars + cols - 1) / cols;
	const float varContent = varRows * (cellH + cellGap);
	const float varView = varBot - varTop;
	const float varMax = std::max(0.0f, varContent - varView);
	g_scrollVar = std::clamp(g_scrollVar, 0.0f, varMax);

	// botoes
	const float stepW = 30.0f * k, togW = 74.0f * k;
	const ImVec2 prevA(cx1 - stepW * 2 - togW - gap * 2, rowBY), prevB(prevA.x + stepW, rowBY + rowBH);
	const ImVec2 nextA(prevB.x + gap, rowBY),                    nextB(nextA.x + stepW, rowBY + rowBH);
	const ImVec2 togA (nextB.x + gap, rowBY),                    togB (cx1, rowBY + rowBH);
	const float  cbw = (bw - 2.0f * gap) / 3.0f;
	const ImVec2 apA(cx0, rowCY),                 apB(cx0 + cbw, rowCY + rowCH);
	const ImVec2 caA(apB.x + gap, rowCY),         caB(caA.x + cbw, rowCY + rowCH);
	const ImVec2 bkA(caB.x + gap, rowCY),         bkB(cx1, rowCY + rowCH);
	const ImVec2 prepA(cx0, rowBY),               prepB(cx1, rowBY + rowBH);

	auto inRect = [](float x, float y, float ax, float ay, float bx, float by) {
		return x >= ax && x <= bx && y >= ay && y <= by;
	};
	auto inBox = [&](float x, float y, const ImVec2& a, const ImVec2& b) { return inRect(x, y, a.x, a.y, b.x, b.y); };

	const bool canStep   = ready && selU && selU->hasMaterial && nVars > 0;
	const bool canToggle = ready && selU && selU->hasMaterial;

	auto hitTest = [&](float x, float y, int& region) -> Hit {
		Hit h; region = 0;
		if (inBox(x, y, bkA, bkB)) { h.kind = K_BACK; return h; }
		if (!ready)
		{
			if (inBox(x, y, prepA, prepB)) { h.kind = K_PREPARE; return h; }
		}
		else
		{
			if (inBox(x, y, prevA, prevB)) { h.kind = K_PREV; return h; }
			if (inBox(x, y, nextA, nextB)) { h.kind = K_NEXT; return h; }
			if (inBox(x, y, togA, togB))   { h.kind = K_TOGGLE; return h; }
		}
		if (inBox(x, y, apA, apB)) { h.kind = K_APPLY; return h; }
		if (inBox(x, y, caA, caB)) { h.kind = K_CANCEL; return h; }
		if (inRect(x, y, catX, top, catX + catW, top + SkinSlots::kUiCategoryCount * (catH + catGap)))
		{
			const int i = (int)((y - top) / (catH + catGap));
			if (i >= 0 && i < SkinSlots::kUiCategoryCount) { h.kind = K_CAT; h.idx = i; return h; }
		}
		if (inRect(x, y, panX, listTop, panX + panW, listBot))
		{
			region = 1;
			const int i = (int)std::floor((y - listTop + g_scrollList) / rowH);
			if (i >= 0 && i < nRows) { h.kind = K_ROW; h.idx = i; }
			return h;
		}
		if (inRect(x, y, panX, varTop, panX + panW, varBot))
		{
			region = 2;
			const float lx = x - (panX + pad);
			const float ly = y - varTop + g_scrollVar;
			if (lx >= 0.0f)
			{
				const int c = (int)(lx / (cellW + cellGap));
				const int rr = (int)(ly / (cellH + cellGap));
				const int i = rr * cols + c;
				const bool inCellX = c < cols && (lx - c * (cellW + cellGap)) <= cellW;
				const bool inCellY = (ly - rr * (cellH + cellGap)) <= cellH;
				if (ly >= 0.0f && inCellX && inCellY && i >= 0 && i < nVars) { h.kind = K_VAR; h.idx = i; }
			}
			return h;
		}
		return h;
	};

	auto sameHit = [](const Hit& a, const Hit& b) { return a.kind == b.kind && a.idx == b.idx; };

	// ---- acoes ----
	auto doAction = [&](const Hit& h) -> bool {      // retorna true se a tela foi fechada
		switch (h.kind)
		{
			case K_BACK: Provador::Close(); return true;
			case K_PREPARE:
				if (busy || !dffOk) break;
				g_afterPrepare = true;
				Request("prepare", nullptr);         // instala o molde (= /skin7 apply) e leva o estado do servidor para ele
				break;
			case K_CAT:
				if (h.idx != g_cat) { g_cat = h.idx; RebuildCategory(); SelectDefault(); }
				break;
			case K_ROW:
			{
				if (h.idx < 0 || h.idx >= nRows) break;
				const int k2 = g_catSlots[(size_t)h.idx];
				const SlotState* u = StateOf(k2);
				if (!u || !u->hasMaterial) { Toast("Este item não existe no modelo carregado", true); break; }
				g_sel = k2; g_scrollVar = 0.0f;
				if (!ready) { Toast("Skin não instalada: toque em PREPARAR", true); break; }
				if (busy || u->state == kEquipped) break;   // ja equipado: so seleciona (visivel != equipado)
				if (!u->variants.empty()) PreviewSlot(k2, std::to_string(u->variant >= 0 ? u->variant : u->variants.front()));
				else PreviewSlot(k2, "on");
				break;
			}
			case K_VAR:
			{
				if (!selU || h.idx < 0 || h.idx >= nVars || busy) break;
				if (!ready) { Toast("Skin não instalada: toque em PREPARAR", true); break; }
				const int v = selU->variants[(size_t)h.idx];
				if (selU->state == kEquipped && selU->variant == v) break;
				PreviewSlot(g_sel, std::to_string(v));
				break;
			}
			case K_PREV:
			case K_NEXT:
			{
				if (!canStep || busy) break;
				const int n = nVars, pos = VariantPos(*selU);
				const int dir = (h.kind == K_NEXT) ? 1 : -1;
				const int to = (pos < 0) ? (dir > 0 ? 0 : n - 1) : (pos + dir + n) % n;
				PreviewSlot(g_sel, std::to_string(selU->variants[(size_t)to]));
				break;
			}
			case K_TOGGLE:
				if (!canToggle || busy) break;
				PreviewSlot(g_sel, (selU->state == kDisabled || selU->state == kCovered) ? "on" : "off");
				break;
			case K_APPLY:
				if (!pending || busy) break;
				if (!online) { Toast("Sem conexão com o servidor: não é possível aplicar", true); break; }
				Request("commit", "Enviado ao servidor...");
				break;
			case K_CANCEL:
				if (!pending || busy) break;
				Request("cancel", "Prévia cancelada");
				break;
		}
		return false;
	};

	// ---- toques acumulados desde o ultimo frame ----
	std::vector<TouchEv> evs;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		evs.swap(g_events);
	}

	for (const TouchEv& e : evs)
	{
		if (e.type == 2 && g_drag.finger < 0)                    // push (primeiro dedo livre)
		{
			g_drag = Drag();
			g_drag.finger = e.num;
			int region = 0;
			g_drag.hit = hitTest(e.x, e.y, region);
			g_drag.region = region;
			g_drag.sy = e.y;
			g_drag.scroll0 = (region == 1) ? g_scrollList : g_scrollVar;
		}
		else if (e.num == g_drag.finger && e.type == 3)          // move
		{
			if (g_drag.region != 0)
			{
				if (std::fabs(e.y - g_drag.sy) > 10.0f * k) g_drag.scrolling = true;
				if (g_drag.scrolling)
				{
					if (g_drag.region == 1) g_scrollList = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, listMax);
					else                    g_scrollVar  = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, varMax);
				}
			}
		}
		else if (e.num == g_drag.finger && e.type == 1)          // pop / cancel
		{
			const bool ok = !e.cancel && !g_drag.scrolling && g_drag.hit.kind != K_NONE;
			Hit up; int reg2 = 0;
			if (ok) up = hitTest(e.x, e.y, reg2);
			const bool same = ok && sameHit(up, g_drag.hit);     // soltou no mesmo botao em que tocou
			const Hit act = g_drag.hit;
			g_drag = Drag();
			if (same && doAction(act)) return;                   // fechou: nao desenha mais neste frame
		}
		// toques de outros dedos: consumidos e ignorados
	}

	// ---- pintura ----
	// cabecalho
	r->drawRect(ImVec2(m, hdrY), ImVec2(scr.x - m, hdrY + hdrH), cPanel, true);
	r->drawRect(ImVec2(m, hdrY + hdrH - 2.0f * k), ImVec2(scr.x - m, hdrY + hdrH), cBlue, true);
	r->drawText(ImVec2(m + 10.0f * k, hdrY + 4.0f * k), cBlue, "/SKIN7", false, fsS);
	r->drawText(ImVec2(m + 10.0f * k, hdrY + 16.0f * k), cText, "PERSONALIZAÇÃO", false, 17.0f * k);
	{
		std::string who = "Jogador";
		if (pNetGame && pNetGame->GetPlayerPool())
		{
			const char* nm = pNetGame->GetPlayerPool()->GetLocalPlayerName();
			who = std::string(nm ? nm : "Jogador") + "  [" + std::to_string((int)pNetGame->GetPlayerPool()->GetLocalPlayerID()) + "]";
		}
		const ImVec2 ts = r->calculateTextSize(who, fs);
		r->drawText(ImVec2(scr.x - m - 10.0f * k - ts.x, hdrY + (hdrH - ts.y) * 0.5f), cText, who, false, fs);
	}

	auto button = [&](const ImVec2& a, const ImVec2& b, const char* text, bool enabled, bool accent, float size) {
		r->drawRect(a, b, enabled ? (accent ? cBlue : cPanel2) : cOff, true);
		if (enabled && !accent) r->drawRect(a, b, cLine, false);
		const ImVec2 ts = r->calculateTextSize(text, size);
		r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
					enabled ? cText : cDim, text, false, size);
	};

	// categorias
	for (int i = 0; i < SkinSlots::kUiCategoryCount; ++i)
	{
		const float ay = top + i * (catH + catGap);
		const bool act = (i == g_cat);
		r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), act ? cPanel2 : cPanel, true);
		if (act)
		{
			r->drawRect(ImVec2(catX, ay), ImVec2(catX + 3.0f * k, ay + catH), cBlue, true);
			r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), cBlueBg, true);
		}
		const char* t = SkinSlots::kUiCategories[i].title;
		const ImVec2 ts = r->calculateTextSize(t, fs);
		r->drawText(ImVec2(catX + 12.0f * k, ay + (catH - ts.y) * 0.5f), act ? cText : cDim, t, false, fs);
	}

	// painel de opcoes (lista rolavel)
	r->drawRect(ImVec2(panX, top), ImVec2(panX + panW, bot), cPanel, true);
	r->drawText(ImVec2(panX + pad, top + 4.0f * k), cBlue, SkinSlots::kUiCategories[g_cat].title, false, fsS);
	r->drawRect(ImVec2(panX, listTop - 2.0f * k), ImVec2(panX + panW, listTop - 1.0f * k), cLine, true);

	r->pushClipRect(ImVec2(panX, listTop), ImVec2(panX + panW, listBot), true);
	for (int i = 0; i < nRows; ++i)
	{
		const float ry = listTop + i * rowH - g_scrollList;
		if (ry + rowH < listTop || ry > listBot) continue;
		const int k2 = g_catSlots[(size_t)i];
		const SlotState* u = StateOf(k2);
		const bool avail = u && u->hasMaterial;
		const bool sel = (k2 == g_sel);

		if (sel)
		{
			r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), cBlueBg, true);
			r->drawRect(ImVec2(panX, ry), ImVec2(panX + 3.0f * k, ry + rowH), cBlue, true);
		}
		else if (i & 1) r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), ImColor(255, 255, 255, 8), true);

		const float ty = ry + (rowH - fs) * 0.5f;
		r->drawText(ImVec2(panX + pad + 4.0f * k, ty), avail ? cText : cDim, SlotLabel(SlotAt(k2).name), false, fs);

		std::string st;
		ImColor stc = cDim;
		if (!u)           st = "...";
		else if (!avail)  st = "indisponível";
		else if (u->state == kEquipped) { st = "EQUIPADO " + std::to_string(u->variant); stc = cBlue; }
		else if (u->state == kCovered)  st = "coberto";
		else if (u->state == kDisabled) st = "desligado";
		else if (u->partial)            st = "parcial";      // ex.: pele das pernas parcialmente coberta por uma bermuda
		else                            st = "padrão";       // visível com a textura original = nada equipado
		if (u && avail && u->previewed) { st += " *"; stc = cAmber; }
		const ImVec2 ts = r->calculateTextSize(st, fsS);
		r->drawText(ImVec2(panX + panW - pad - ts.x, ry + (rowH - ts.y) * 0.5f), stc, st, false, fsS);
	}
	r->popClipRect();

	// grade de variantes (rolavel)
	r->drawRect(ImVec2(panX, varTitleY - 2.0f * k), ImVec2(panX + panW, varTitleY - 1.0f * k), cLine, true);
	r->drawText(ImVec2(panX + pad, varTitleY + 3.0f * k), cBlue, "VARIANTES", false, fsS);
	r->pushClipRect(ImVec2(panX, varTop), ImVec2(panX + panW, varBot), true);
	if (!selU || !selU->hasMaterial)
		r->drawText(ImVec2(panX + pad, varTop + 6.0f * k), cDim, selU ? "Item indisponível no modelo" : "Escolha um item acima", false, fsS);
	else if (nVars == 0)
		r->drawText(ImVec2(panX + pad, varTop + 6.0f * k), cDim, "Sem variantes no pack (use LIGAR/DESLIGAR)", false, fsS);
	else
	{
		for (int i = 0; i < nVars; ++i)
		{
			const float cxp = panX + pad + (i % cols) * (cellW + cellGap);
			const float cyp = varTop + (i / cols) * (cellH + cellGap) - g_scrollVar;
			if (cyp + cellH < varTop || cyp > varBot) continue;
			const int v = selU->variants[(size_t)i];
			const bool cur = selU->state == kEquipped && selU->variant == v;
			const bool pv  = cur && selU->previewed;
			r->drawRect(ImVec2(cxp, cyp), ImVec2(cxp + cellW, cyp + cellH), pv ? cAmberBg : (cur ? cBlueBg : cPanel2), true);
			r->drawRect(ImVec2(cxp, cyp), ImVec2(cxp + cellW, cyp + cellH), pv ? cAmber : (cur ? cBlue : cLine), false);
			const std::string t = std::to_string(v);
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(cxp + (cellW - ts.x) * 0.5f, cyp + (cellH - ts.y) * 0.5f), cur ? cText : cDim, t, false, fs);
		}
	}
	r->popClipRect();

	// barra inferior (centro): estado, variante atual, navegacao e botoes
	r->drawRect(ImVec2(cx0, barY - 2.0f * k), ImVec2(cx1, bot), cPanel, true);

	{   // linha A: aviso (toast / previa pendente / skin nao instalada)
		std::string msg; ImColor mc = cDim;
		if (NowMs() < g_toastUntil) { msg = g_toast; mc = g_toastErr ? cRed : cBlue; }
		else if (!g_haveSt) msg = "Carregando...";
		else if (!dffOk) { msg = "skin.dff não encontrado em SAMP/skins"; mc = cRed; }
		else if (!ready) { msg = g_st.prepMsg.empty() ? "Preparando a skin..." : g_st.prepMsg; mc = g_st.prepMsg.empty() ? cDim : cRed; }
		else if (awaiting) { msg = "Salvando no servidor..."; mc = cAmber; }
		else if (!online) { msg = "Sem conexão com o servidor: só visualização"; mc = cRed; }
		else if (pending) { msg = "PRÉVIA (não salva): toque APLICAR para confirmar"; mc = cAmber; }
		r->pushClipRect(ImVec2(cx0, rowAY), ImVec2(cx1, rowAY + rowAH), true);
		r->drawText(ImVec2(cx0 + pad, rowAY + (rowAH - fsS) * 0.5f), mc, msg, false, fsS);
		r->popClipRect();
	}

	if (!ready)
	{
		button(prepA, prepB, busy ? "PREPARANDO..." : (dffOk ? "PREPARAR (tentar de novo)" : "skin.dff ausente"), !busy && dffOk, true, fs);
	}
	else
	{
		r->pushClipRect(ImVec2(cx0, rowBY), ImVec2(prevA.x - gap, rowBY + rowBH), true);
		r->drawText(ImVec2(cx0 + pad, rowBY + (rowBH - fs) * 0.5f), cText, InfoText(selU, g_sel), false, fs);
		r->popClipRect();
		button(prevA, prevB, "<", canStep && !busy, false, 16.0f * k);
		button(nextA, nextB, ">", canStep && !busy, false, 16.0f * k);
		button(togA, togB, (selU && (selU->state == kDisabled || selU->state == kCovered)) ? "LIGAR" : "DESLIGAR", canToggle && !busy, false, fsS);
	}

	button(apA, apB, awaiting ? "SALVANDO..." : "APLICAR", pending && online && !busy, true, fs);
	button(caA, caB, "CANCELAR", pending && !busy, false, fs);
	button(bkA, bkB, "VOLTAR", true, false, fs);
}
