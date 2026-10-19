#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../vendor/stb/stb_image.h"   // implementacao em game/sprite2d.cpp
#include "inventory.h"
#include "inventory_atlas.h"

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

// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

// =====================================================================================
//  Estado recebido do servidor (RPC 203) - protegido por mutex
// =====================================================================================
namespace
{
	struct Item  { int slot = 0, id = 0, qty = 0, icon = 8; uint32_t unitW = 0; std::string name; };
	struct Drop  { int id = 0, item = 0, qty = 0, icon = 8; std::string name; };
	struct TItem { int item = 0, qty = 0, icon = 8; std::string name; };
	struct Trade
	{
		int mode = 0;                 // 0 fechada, 1 aguardando, 2 convite recebido, 3 ativa
		int partnerId = -1;
		std::string partner;
		bool myConf = false, theirConf = false;
		std::vector<TItem> mine, theirs;
	};
	struct State
	{
		int slots = 40;
		uint32_t weight = 0, cap = 65000;
		std::vector<Item> items;
		std::vector<Drop> ground;
		Trade trade;
	};

	std::mutex g_dm;
	State      g_state;
	std::string g_toastMsg;
	int         g_toastType = 0;
	int64_t     g_toastUntil = 0;

	struct TouchEv { int type; int num; float x; float y; bool cancel; };
	std::atomic<bool> g_open{false};
	std::atomic<bool> g_flush{false};
	std::atomic<bool> g_sendClose{false};
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::mutex        g_evMutex;
	std::vector<TouchEv> g_events;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	bool ReadStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], n)) return false;
		return true;
	}

	void SetToast(int type, const std::string& msg)
	{
		g_toastMsg = msg; g_toastType = type; g_toastUntil = NowMs() + 3200;
	}
}

// =====================================================================================
//  Textura (atlas embutido 4x4)
// =====================================================================================
namespace
{
	RwRaster* s_atlas = nullptr;
	bool      s_atlasFailed = false;

	RwRaster* CreateAtlasRaster()
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(InvAtlas::kPng, (int)InvAtlas::kPngSize, &w, &h, &ch, 4);
		if (!px) { FLog("[INV] stbi falhou: %s", stbi_failure_reason()); return nullptr; }

		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage)
		{
			FLog("[INV] funcoes RenderWare indisponiveis");
			stbi_image_free(px);
			return nullptr;
		}
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image))
		{
			if (image) RwImageDestroy(image);
			stbi_image_free(px);
			return nullptr;
		}
		for (int y = 0; y < h; ++y)
			memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		stbi_image_free(px);

		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888;
			rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image))
		{
			if (RwRasterDestroy) RwRasterDestroy(raster);
			RwImageDestroy(image);
			return nullptr;
		}
		RwImageDestroy(image);
		FLog("[INV] atlas %dx%d criado", w, h);
		return raster;
	}

	RwRaster* GetAtlas()
	{
		if (s_atlas) return s_atlas;
		if (s_atlasFailed) return nullptr;
		s_atlas = CreateAtlasRaster();
		if (!s_atlas) s_atlasFailed = true;
		return s_atlas;
	}

	void DrawIcon(ImGuiRenderer* r, int icon, const ImVec2& a, const ImVec2& b, int alpha = 255)
	{
		RwRaster* tex = GetAtlas();
		if (!tex) return;
		if (icon < 0 || icon > 15) icon = 8;
		const float cell = (float)InvAtlas::kCell, W = (float)InvAtlas::kWidth, H = (float)InvAtlas::kHeight;
		const float cx = (float)(icon % InvAtlas::kCols) * cell, cy = (float)(icon / InvAtlas::kCols) * cell;
		r->drawImage(a, b, (ImTextureID)tex, ImColor(255, 255, 255, alpha),
					 ImVec2((cx + 0.5f) / W, (cy + 0.5f) / H), ImVec2((cx + cell - 0.5f) / W, (cy + cell - 0.5f) / H));
	}
}

// =====================================================================================
//  API estatica
// =====================================================================================
bool InventoryUI::IsOpen() { return g_open.load(); }

void InventoryUI::Open()
{
	if (g_open.load()) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	for (auto& c : g_claimed) c.store(false);
	g_flush.store(true);
	g_open.store(true);
}

void InventoryUI::Close(bool notifyServer)
{
	if (!g_open.exchange(false)) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	if (notifyServer) g_sendClose.store(true);   // enviado na proxima draw() (thread do jogo)
}

void InventoryUI::Reset()
{
	g_open.store(false);
	g_sendClose.store(false);
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	std::lock_guard<std::mutex> l(g_dm);
	g_state = State();
	g_toastUntil = 0;
}

bool InventoryUI::ConsumeFlushRequest() { return g_flush.exchange(false); }

bool InventoryUI::OnBackPressed()
{
	// Debounce: onKeyDown e onBackPressed podem ambos disparar no mesmo toque; o segundo nao
	// pode cair no MaximoMenu (abriria o menu logo apos fechar o inventario).
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 400) return true;
	if (!g_open.load()) return false;
	s_last = now;
	Close(true);
	return true;
}

bool InventoryUI::HandleRawTouch(int type, int num, int x, int y)
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

// ---- RPC 203 ----
void InventoryUI::OnServerPacket(const unsigned char* data, int bytes)
{
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	uint8_t op = 0;
	if (!bs.Read(op)) return;

	if (op == 1)   // FULL
	{
		uint16_t slots = 0, n = 0; uint32_t weight = 0, cap = 0;
		if (!bs.Read(slots) || !bs.Read(weight) || !bs.Read(cap) || !bs.Read(n)) return;
		if (slots < 1 || slots > 400 || n > 400) return;
		std::vector<Item> items;
		for (int i = 0; i < n; i++)
		{
			Item it; uint16_t s = 0, id = 0, q = 0; uint8_t ic = 0, fl = 0; uint32_t w = 0;
			if (!bs.Read(s) || !bs.Read(id) || !bs.Read(q) || !bs.Read(ic) || !bs.Read(w) || !bs.Read(fl) || !ReadStr(bs, it.name)) return;
			if (s >= slots) continue;
			it.slot = s; it.id = id; it.qty = q; it.icon = ic; it.unitW = w;
			items.push_back(std::move(it));
		}
		std::lock_guard<std::mutex> l(g_dm);
		g_state.slots = slots; g_state.weight = weight; g_state.cap = cap ? cap : 1;
		g_state.items.swap(items);
	}
	else if (op == 2)   // OPEN / CLOSE
	{
		uint8_t o = 0;
		if (!bs.Read(o)) return;
		if (o) Open(); else Close(false);
	}
	else if (op == 3)   // GROUND
	{
		uint16_t n = 0;
		if (!bs.Read(n) || n > 200) return;
		std::vector<Drop> g;
		for (int i = 0; i < n; i++)
		{
			Drop d; uint16_t id = 0, it = 0, q = 0; uint8_t ic = 0;
			if (!bs.Read(id) || !bs.Read(it) || !bs.Read(q) || !bs.Read(ic) || !ReadStr(bs, d.name)) return;
			d.id = id; d.item = it; d.qty = q; d.icon = ic;
			g.push_back(std::move(d));
		}
		std::lock_guard<std::mutex> l(g_dm);
		g_state.ground.swap(g);
	}
	else if (op == 4)   // TRADE
	{
		Trade t; uint8_t mode = 0, mc = 0, tc = 0; uint16_t pid = 0;
		if (!bs.Read(mode) || !bs.Read(pid) || !ReadStr(bs, t.partner) || !bs.Read(mc) || !bs.Read(tc)) return;
		t.mode = mode; t.partnerId = pid; t.myConf = mc != 0; t.theirConf = tc != 0;
		for (int side = 0; side < 2; side++)
		{
			uint8_t n = 0;
			if (!bs.Read(n) || n > 16) return;
			for (int i = 0; i < n; i++)
			{
				TItem e; uint16_t it = 0, q = 0; uint8_t ic = 0;
				if (!bs.Read(it) || !bs.Read(q) || !bs.Read(ic) || !ReadStr(bs, e.name)) return;
				e.item = it; e.qty = q; e.icon = ic;
				(side == 0 ? t.mine : t.theirs).push_back(std::move(e));
			}
		}
		std::lock_guard<std::mutex> l(g_dm);
		g_state.trade = std::move(t);
	}
	else if (op == 5)   // TOAST
	{
		uint8_t type = 0; std::string msg;
		if (!bs.Read(type) || !ReadStr(bs, msg)) return;
		std::lock_guard<std::mutex> l(g_dm);
		SetToast(type, msg);
	}
}

InventoryUI::InventoryUI() : Widget() {}

// =====================================================================================
//  Desenho + gestos (thread de desenho)
// =====================================================================================
namespace
{
	const ImColor cDim    (0, 0, 0, 120);
	const ImColor cPanelBg(22, 22, 28, 215);
	const ImColor cSlotBg (12, 12, 16, 170);
	const ImColor cSlotLn (255, 255, 255, 22);
	const ImColor cText   (238, 238, 244, 255);
	const ImColor cMute   (150, 150, 165, 255);
	const ImColor cPurple (176, 132, 255, 255);
	const ImColor cPurpleF(120, 84, 210, 110);
	const ImColor cGreen  (90, 210, 120, 255);
	const ImColor cRed    (235, 90, 90, 255);
	const ImColor cBtn    (38, 38, 48, 235);
	const ImColor cBtnOff (28, 28, 34, 200);

	constexpr int INV_TRADE_VIS = 6;       // linhas de oferta exibidas (= INV_TRADE_SLOTS do servidor)
	enum { P_INV = 1, P_GROUND = 2 };
	enum { B_NONE = 0, B_USE, B_DROP, B_TRADE, B_QTY, B_PICK, B_OFFER, B_CLOSE,
		   B_ACCEPT, B_REFUSE, B_CONFIRM, B_CANCEL, B_REFRESH };

	struct Sel { int panel = 0; int idx = -1; } g_sel;      // idx: slot (inventario) ou posicao no vetor ground
	bool    g_allQty = true;                                // false = 1 unidade, true = tudo
	float   g_scroll[3] = {0, 0, 0};                        // [P_INV], [P_GROUND]
	int64_t g_busyUntil = 0;                                // evita executar duas vezes o mesmo gesto

	struct Touch
	{
		int   finger = -1;
		int   kind = 0;           // 1 grade inventario, 2 grade chao, 3 botao, 4 outro
		int   panel = 0;
		int   src = -1;           // slot / indice tocado
		int   btn = B_NONE;
		float sx = 0, sy = 0, cx = 0, cy = 0, scroll0 = 0;
		int64_t t0 = 0;
		bool  scrolling = false, dragging = false;
	} g_t;

	struct Btn { int id; ImVec2 a, b; const char* label; bool enabled; };

	bool In(float x, float y, const ImVec2& a, const ImVec2& b) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; }

	void Send(const char* fmt, int a = 0, int b = 0)
	{
		if (!pNetGame) return;
		char buf[64];
		snprintf(buf, sizeof(buf), fmt, a, b);
		pNetGame->SendChatCommand(buf);
		g_busyUntil = NowMs() + 180;
	}

	std::string Kg(uint32_t g)
	{
		char b[24]; snprintf(b, sizeof(b), "%.2f", g / 1000.0f);
		return b;
	}
}

void InventoryUI::draw(ImGuiRenderer* r)
{
	if (g_sendClose.exchange(false) && pNetGame) pNetGame->SendChatCommand("/invx close");

	if (!g_open.load())
	{
		g_t = Touch(); g_sel = Sel(); g_scroll[1] = g_scroll[2] = 0.0f;
		return;
	}

	// ---- snapshot do estado (evita segurar o mutex durante o desenho) ----
	State S; std::string toast; int toastType = 0; int64_t toastUntil = 0;
	{
		std::lock_guard<std::mutex> l(g_dm);
		S = g_state; toast = g_toastMsg; toastType = g_toastType; toastUntil = g_toastUntil;
	}

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float k = scr.y / 575.0f;

	// ---- geometria (tudo proporcional a altura da tela; 5 colunas, 4 linhas visiveis) ----
	const int   cols = 5, visRows = 4;
	const float slot = 58.0f * k, gap = 4.0f * k, pad = 10.0f * k, pitch = slot + gap;
	const float gridW = cols * slot + (cols - 1) * gap;
	const float gridH = visRows * slot + (visRows - 1) * gap;
	const float headH = 36.0f * k, infoH = 24.0f * k, barH = 36.0f * k;
	const float panW = gridW + 2.0f * pad + 8.0f * k;       // + trilho de rolagem
	const float panH = headH + gridH + 8.0f * k + infoH + 6.0f * k + barH + pad;
	const float betw = 22.0f * k;
	const float totW = panW * 2.0f + betw;
	const float xl = std::max(6.0f * k, (scr.x - totW) * 0.5f), xr = xl + panW + betw;
	const float y0 = std::max(4.0f * k, (scr.y - panH) * 0.5f);
	const ImVec2 gL(xl + pad, y0 + headH), gR(xr + pad, y0 + headH);       // origem das grades
	const float barY = gL.y + gridH + 8.0f * k + infoH + 6.0f * k;
	const float fs = 11.0f * k;

	const int rowsL = (S.slots + cols - 1) / cols;
	const int groundCells = std::max(cols * visRows, ((int)S.ground.size() + cols - 1) / cols * cols);
	const int rowsR = groundCells / cols;
	const float contentL = rowsL * pitch - gap, contentR = rowsR * pitch - gap;
	const float maxL = std::max(0.0f, contentL - gridH), maxR = std::max(0.0f, contentR - gridH);
	g_scroll[P_INV]    = std::clamp(g_scroll[P_INV], 0.0f, maxL);
	g_scroll[P_GROUND] = std::clamp(g_scroll[P_GROUND], 0.0f, maxR);

	const Trade& T = S.trade;
	const bool tradeMode = T.mode != 0;
	const bool tradeActive = T.mode == 3;

	// mapa slot -> item
	std::vector<const Item*> bySlot(S.slots, nullptr);
	for (const Item& it : S.items) if (it.slot >= 0 && it.slot < S.slots) bySlot[it.slot] = &it;

	// selecao invalida (item sumiu apos sync do servidor) -> limpa
	if (g_sel.panel == P_INV && (g_sel.idx < 0 || g_sel.idx >= S.slots || !bySlot[g_sel.idx])) g_sel = Sel();
	if (g_sel.panel == P_GROUND && (g_sel.idx < 0 || g_sel.idx >= (int)S.ground.size())) g_sel = Sel();

	// ---- botoes ----
	std::vector<Btn> btns;
	{
		const float bw = (panW - 2.0f * pad - 3.0f * 4.0f * k) / 4.0f, bh = barH;
		auto mk = [&](int i, int id, const char* lab, bool en) {
			const float bx = xl + pad + i * (bw + 4.0f * k);
			btns.push_back({id, ImVec2(bx, barY), ImVec2(bx + bw, barY + bh), lab, en});
		};
		const bool invSel = g_sel.panel == P_INV;
		const bool grdSel = g_sel.panel == P_GROUND;
		if (grdSel) mk(0, B_PICK, "PEGAR", !tradeActive);
		else        mk(0, B_USE, "USAR", invSel && !tradeActive);
		if (tradeActive) mk(1, B_OFFER, "OFERTAR", invSel && !T.myConf);
		else             mk(1, B_DROP, "DROPAR", invSel);
		mk(2, B_TRADE, "NEGOCIAR", !tradeMode);
		mk(3, B_QTY, g_allQty ? "QTD: TUDO" : "QTD: 1", true);
		btns.push_back({B_CLOSE, ImVec2(xl + panW - pad - 78.0f * k, y0 + 6.0f * k), ImVec2(xl + panW - pad, y0 + 6.0f * k + 24.0f * k), "FECHAR", true});

		// botoes do painel direito (negociacao)
		const float rbw = (panW - 2.0f * pad - 4.0f * k) / 2.0f;
		auto mr = [&](int i, int id, const char* lab, bool en) {
			const float bx = xr + pad + i * (rbw + 4.0f * k);
			btns.push_back({id, ImVec2(bx, barY), ImVec2(bx + rbw, barY + bh), lab, en});
		};
		if (T.mode == 2)      { mr(0, B_ACCEPT, "ACEITAR", true); mr(1, B_REFUSE, "RECUSAR", true); }
		else if (T.mode == 1) { mr(0, B_CANCEL, "CANCELAR", true); }
		else if (T.mode == 3) { mr(0, B_CONFIRM, T.myConf ? "EDITAR" : "CONFIRMAR", true); mr(1, B_CANCEL, "CANCELAR", true); }
		else                  { mr(0, B_REFRESH, "ATUALIZAR", true); }
	}
	auto btnAt = [&](float x, float y) -> const Btn* {
		for (const Btn& b : btns) if (In(x, y, b.a, b.b)) return &b;
		return nullptr;
	};
	const ImVec2 rightA(xr, y0), rightB(xr + panW, y0 + panH), leftA(xl, y0), leftB(xl + panW, y0 + panH);
	const ImVec2 vpLA(gL.x, gL.y), vpLB(gL.x + gridW, gL.y + gridH), vpRA(gR.x, gR.y), vpRB(gR.x + gridW, gR.y + gridH);

	auto slotAt = [&](int panel, float x, float y) -> int {
		const ImVec2 o = panel == P_INV ? gL : gR;
		const ImVec2 b(o.x + gridW, o.y + gridH);
		if (!In(x, y, o, b)) return -1;
		const int c = (int)std::floor((x - o.x) / pitch), rw = (int)std::floor((y - o.y + g_scroll[panel]) / pitch);
		if (c < 0 || c >= cols || rw < 0) return -1;
		const int i = rw * cols + c;
		return i < (panel == P_INV ? S.slots : groundCells) ? i : -1;
	};
	// linha de oferta propria (painel de negociacao)
	const float colW = (panW - 2.0f * pad - 6.0f * k) / 2.0f, rowH = 34.0f * k, listY = gR.y + 22.0f * k;
	auto offerAt = [&](float x, float y) -> int {
		if (!tradeActive) return -1;
		if (x < gR.x || x > gR.x + colW) return -1;
		const int i = (int)std::floor((y - listY) / rowH);
		return (i >= 0 && i < (int)T.mine.size()) ? i : -1;
	};

	// ---- executores (cada gesto dispara UM comando; o servidor valida) ----
	auto qtyOfSlot = [&](int s) { return (bySlot[s] ? (g_allQty ? bySlot[s]->qty : 1) : 0); };
	auto doUse   = [&](int s) { if (bySlot[s]) Send("/invx use %d", s); };
	auto doDrop  = [&](int s) { if (bySlot[s]) Send("/invx drop %d %d", s, qtyOfSlot(s)); };
	auto doOffer = [&](int s) { if (bySlot[s]) Send("/invx tadd %d %d", s, qtyOfSlot(s)); };
	auto doPick  = [&](int gi) {
		if (gi >= 0 && gi < (int)S.ground.size()) Send("/invx pickup %d %d", S.ground[gi].id, g_allQty ? S.ground[gi].qty : 1);
	};
	auto runButton = [&](int id) {
		switch (id)
		{
			case B_USE:   if (g_sel.panel == P_INV) doUse(g_sel.idx); break;
			case B_DROP:  if (g_sel.panel == P_INV) doDrop(g_sel.idx); break;
			case B_OFFER: if (g_sel.panel == P_INV) doOffer(g_sel.idx); break;
			case B_PICK:  if (g_sel.panel == P_GROUND) doPick(g_sel.idx); break;
			case B_TRADE: Send("/invx trade -1"); break;
			case B_QTY:   g_allQty = !g_allQty; break;            // so local, nao envia nada
			case B_CLOSE: InventoryUI::Close(true); break;
			case B_ACCEPT:  Send("/invx taccept"); break;
			case B_REFUSE:  Send("/invx trefuse"); break;
			case B_CONFIRM: Send("/invx tconfirm"); break;
			case B_CANCEL:  Send("/invx tcancel"); break;
			case B_REFRESH: Send("/invx sync"); break;
		}
		if (id != B_QTY && id != B_CLOSE) g_sel = Sel();
	};

	// ---- toques acumulados ----
	std::vector<TouchEv> evs;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		evs.swap(g_events);
	}
	const int64_t now = NowMs();

	// long-press: promove o toque a "arrastar item"
	if (g_t.finger >= 0 && (g_t.kind == 1 || g_t.kind == 2) && !g_t.dragging && !g_t.scrolling && now - g_t.t0 >= 250)
	{
		const bool has = g_t.panel == P_INV ? (g_t.src >= 0 && g_t.src < S.slots && bySlot[g_t.src])
											: (g_t.src >= 0 && g_t.src < (int)S.ground.size());
		if (has) { g_t.dragging = true; g_sel.panel = g_t.panel; g_sel.idx = g_t.src; }
	}

	for (const TouchEv& e : evs)
	{
		if (e.type == 2 && g_t.finger < 0)                                   // push
		{
			g_t = Touch();
			g_t.finger = e.num; g_t.sx = g_t.cx = e.x; g_t.sy = g_t.cy = e.y; g_t.t0 = now;
			if (const Btn* b = btnAt(e.x, e.y)) { g_t.kind = 3; g_t.btn = b->id; }
			else if (In(e.x, e.y, vpLA, vpLB)) { g_t.kind = 1; g_t.panel = P_INV;    g_t.src = slotAt(P_INV, e.x, e.y);    g_t.scroll0 = g_scroll[P_INV]; }
			else if (In(e.x, e.y, vpRA, vpRB)) { g_t.kind = 2; g_t.panel = P_GROUND; g_t.src = tradeMode ? -1 : slotAt(P_GROUND, e.x, e.y); g_t.scroll0 = g_scroll[P_GROUND]; }
			else g_t.kind = 4;
		}
		else if (e.num == g_t.finger && e.type == 3)                         // move
		{
			g_t.cx = e.x; g_t.cy = e.y;
			if ((g_t.kind == 1 || g_t.kind == 2) && !g_t.dragging)
			{
				if (std::fabs(e.y - g_t.sy) > 9.0f * k || std::fabs(e.x - g_t.sx) > 9.0f * k)
				{
					// moveu antes do long-press: e rolagem (so vertical)
					if (std::fabs(e.y - g_t.sy) >= std::fabs(e.x - g_t.sx)) g_t.scrolling = true;
				}
				if (g_t.scrolling)
					g_scroll[g_t.panel] = std::clamp(g_t.scroll0 - (e.y - g_t.sy), 0.0f, g_t.panel == P_INV ? maxL : maxR);
			}
		}
		else if (e.num == g_t.finger && e.type == 1)                         // pop / cancel
		{
			const Touch t = g_t;
			g_t = Touch();
			if (e.cancel || now < g_busyUntil) continue;                      // cancelado / anti-duplo

			if (t.kind == 3)                                                  // botao: solta sobre o mesmo botao
			{
				const Btn* b = btnAt(e.x, e.y);
				if (b && b->id == t.btn && b->enabled) runButton(b->id);
			}
			else if ((t.kind == 1 || t.kind == 2) && t.dragging)             // soltou item arrastado
			{
				const bool fromInv = t.panel == P_INV;
				const Btn* b = btnAt(e.x, e.y);
				const int dstInv = slotAt(P_INV, e.x, e.y);
				if (fromInv && t.src >= 0 && t.src < S.slots && bySlot[t.src])
				{
					if (b && b->enabled && b->id == B_USE)        doUse(t.src);          // arrastar ate "Usar"
					else if (b && b->enabled && (b->id == B_DROP || b->id == B_OFFER)) { if (b->id == B_DROP) doDrop(t.src); else doOffer(t.src); }
					else if (dstInv >= 0 && dstInv != t.src && !tradeActive) Send("/invx move %d %d", t.src, dstInv);
					else if (!b && In(e.x, e.y, rightA, rightB))  { if (tradeActive) { if (!T.myConf) doOffer(t.src); } else doDrop(t.src); }
				}
				else if (!fromInv && t.src >= 0 && t.src < (int)S.ground.size())
				{
					if (In(e.x, e.y, leftA, leftB) && !tradeActive) doPick(t.src);       // chao -> inventario
				}
				g_sel = Sel();
			}
			else if (t.kind == 2 && tradeMode && !t.scrolling)               // painel de negociacao
			{
				const int oi = offerAt(e.x, e.y);                             // toque na minha oferta = remove
				if (oi >= 0 && !T.myConf) Send("/invx tdel %d", oi);
			}
			else if ((t.kind == 1 || t.kind == 2) && !t.scrolling)           // toque simples: seleciona
			{
				bool ok = false;
				if (t.panel == P_INV) ok = t.src >= 0 && t.src < S.slots && bySlot[t.src];
				else                  ok = t.src >= 0 && t.src < (int)S.ground.size();
				if (ok)
				{
					if (g_sel.panel == t.panel && g_sel.idx == t.src) g_sel = Sel();   // toca de novo = desmarca
					else { g_sel.panel = t.panel; g_sel.idx = t.src; }
				}
				else if (!tradeActive || t.kind == 1) g_sel = Sel();
			}
		}
	}

	// =================================================================================
	//  Desenho
	// =================================================================================
	r->drawRect(ImVec2(0, 0), scr, cDim, true);
	for (int p = 0; p < 2; p++)
	{
		const float x = p == 0 ? xl : xr;
		r->drawRect(ImVec2(x, y0), ImVec2(x + panW, y0 + panH), cPanelBg, true);
		r->drawRect(ImVec2(x, y0), ImVec2(x + panW, y0 + 2.0f * k), cPurple, true);
	}

	// ---- cabecalho esquerdo ----
	r->drawText(ImVec2(xl + pad, y0 + 9.0f * k), cText, "Seu Inventário", false, 15.0f * k);
	{
		char buf[48]; snprintf(buf, sizeof(buf), "%s / %s", Kg(S.weight).c_str(), Kg(S.cap).c_str());
		const ImVec2 ts = r->calculateTextSize(buf, fs);
		r->drawText(ImVec2(xl + panW - pad - 86.0f * k - ts.x, y0 + 11.0f * k), S.weight > S.cap ? cRed : cMute, buf, false, fs);
	}
	r->drawRect(ImVec2(xl + pad, y0 + headH - 4.0f * k), ImVec2(xl + pad + 110.0f * k, y0 + headH - 3.0f * k), cText, true);

	// ---- grade do inventario (rolagem real, recorte) ----
	r->pushClipRect(vpLA, vpLB, true);
	for (int i = 0; i < S.slots; i++)
	{
		const int c = i % cols, rw = i / cols;
		const ImVec2 a(gL.x + c * pitch, gL.y + rw * pitch - g_scroll[P_INV]);
		const ImVec2 b(a.x + slot, a.y + slot);
		if (b.y < vpLA.y || a.y > vpLB.y) continue;
		const bool sel = g_sel.panel == P_INV && g_sel.idx == i;
		const bool ghost = g_t.dragging && g_t.panel == P_INV && g_t.src == i;
		r->drawRect(a, b, sel ? cPurpleF : cSlotBg, true);
		r->drawRect(a, b, sel ? cPurple : cSlotLn, false, sel ? 2.0f : 1.0f);
		if (const Item* it = bySlot[i])
		{
			const float m = 6.0f * k;
			DrawIcon(r, it->icon, ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, b.y - m), ghost ? 70 : 255);
			if (it->qty > 1)
			{
				char q[12]; snprintf(q, sizeof(q), "x%d", it->qty);
				const ImVec2 ts = r->calculateTextSize(q, 10.0f * k);
				r->drawText(ImVec2(b.x - ts.x - 4.0f * k, a.y + 3.0f * k), cText, q, true, 10.0f * k);
			}
		}
	}
	r->popClipRect();
	if (maxL > 0.0f)   // barra de rolagem
	{
		const float tx = gL.x + gridW + 3.0f * k, th = std::max(18.0f * k, gridH * gridH / contentL);
		const float ty = gL.y + (gridH - th) * (g_scroll[P_INV] / maxL);
		r->drawRect(ImVec2(tx, gL.y), ImVec2(tx + 3.0f * k, gL.y + gridH), ImColor(255, 255, 255, 25), true);
		r->drawRect(ImVec2(tx, ty), ImVec2(tx + 3.0f * k, ty + th), cText, true);
	}

	// ---- info do item selecionado ----
	{
		const float iy = gL.y + gridH + 8.0f * k;
		r->drawRect(ImVec2(xl + pad, iy), ImVec2(xl + panW - pad, iy + infoH), ImColor(255, 255, 255, 10), true);
		std::string info = "Toque para selecionar. Segure e arraste para usar, dropar ou mover.";
		if (g_sel.panel == P_INV && g_sel.idx >= 0 && g_sel.idx < S.slots && bySlot[g_sel.idx])
		{
			const Item* it = bySlot[g_sel.idx];
			char b[96]; snprintf(b, sizeof(b), "%s  x%d  -  %s kg cada", it->name.c_str(), it->qty, Kg(it->unitW).c_str());
			info = b;
		}
		else if (g_sel.panel == P_GROUND && g_sel.idx >= 0 && g_sel.idx < (int)S.ground.size())
		{
			char b[96]; snprintf(b, sizeof(b), "No chão: %s  x%d", S.ground[g_sel.idx].name.c_str(), S.ground[g_sel.idx].qty);
			info = b;
		}
		r->pushClipRect(ImVec2(xl + pad, iy), ImVec2(xl + panW - pad, iy + infoH), true);
		r->drawText(ImVec2(xl + pad + 6.0f * k, iy + (infoH - 10.0f * k) * 0.5f), cMute, info, false, 10.0f * k);
		r->popClipRect();
	}

	// ---- painel direito ----
	if (!tradeMode)
	{
		r->drawText(ImVec2(xr + pad, y0 + 9.0f * k), cText, "Chão", false, 15.0f * k);
		r->drawRect(ImVec2(xr + pad, y0 + headH - 4.0f * k), ImVec2(xr + pad + 44.0f * k, y0 + headH - 3.0f * k), cText, true);
		r->pushClipRect(vpRA, vpRB, true);
		for (int i = 0; i < groundCells; i++)
		{
			const int c = i % cols, rw = i / cols;
			const ImVec2 a(gR.x + c * pitch, gR.y + rw * pitch - g_scroll[P_GROUND]);
			const ImVec2 b(a.x + slot, a.y + slot);
			if (b.y < vpRA.y || a.y > vpRB.y) continue;
			const bool sel = g_sel.panel == P_GROUND && g_sel.idx == i;
			r->drawRect(a, b, sel ? cPurpleF : cSlotBg, true);
			r->drawRect(a, b, sel ? cPurple : cSlotLn, false, sel ? 2.0f : 1.0f);
			if (i < (int)S.ground.size())
			{
				const Drop& d = S.ground[i];
				const float m = 6.0f * k;
				DrawIcon(r, d.icon, ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, b.y - m));
				if (d.qty > 1)
				{
					char q[12]; snprintf(q, sizeof(q), "x%d", d.qty);
					const ImVec2 ts = r->calculateTextSize(q, 10.0f * k);
					r->drawText(ImVec2(b.x - ts.x - 4.0f * k, a.y + 3.0f * k), cText, q, true, 10.0f * k);
				}
			}
		}
		r->popClipRect();
		if (maxR > 0.0f)
		{
			const float tx = gR.x + gridW + 3.0f * k, th = std::max(18.0f * k, gridH * gridH / contentR);
			const float ty = gR.y + (gridH - th) * (g_scroll[P_GROUND] / maxR);
			r->drawRect(ImVec2(tx, gR.y), ImVec2(tx + 3.0f * k, gR.y + gridH), ImColor(255, 255, 255, 25), true);
			r->drawRect(ImVec2(tx, ty), ImVec2(tx + 3.0f * k, ty + th), cText, true);
		}
		const float iy = gR.y + gridH + 8.0f * k;
		r->drawText(ImVec2(xr + pad + 6.0f * k, iy + (infoH - 10.0f * k) * 0.5f), cMute,
					S.ground.empty() ? "Nada por perto. Arraste um item para cá para dropar." : "Itens a até 3 m. Toque e use PEGAR, ou arraste para o inventário.", false, 10.0f * k);
	}
	else
	{
		char title[64];
		snprintf(title, sizeof(title), "Negociação%s%s", T.partner.empty() ? "" : " - ", T.partner.c_str());
		r->pushClipRect(ImVec2(xr + pad, y0), ImVec2(xr + panW - pad, y0 + headH), true);
		r->drawText(ImVec2(xr + pad, y0 + 9.0f * k), cText, title, false, 15.0f * k);
		r->popClipRect();
		r->drawRect(ImVec2(xr + pad, y0 + headH - 4.0f * k), ImVec2(xr + pad + 60.0f * k, y0 + headH - 3.0f * k), cText, true);

		if (T.mode == 2)
		{
			char b[96]; snprintf(b, sizeof(b), "%s QUER NEGOCIAR COM VOCÊ!", T.partner.c_str());
			const ImVec2 ts = r->calculateTextSize(b, 13.0f * k);
			r->drawText(ImVec2(xr + (panW - ts.x) * 0.5f, gR.y + gridH * 0.4f), cText, b, false, 13.0f * k);
			const ImVec2 t2 = r->calculateTextSize("Use ACEITAR ou RECUSAR.", fs);
			r->drawText(ImVec2(xr + (panW - t2.x) * 0.5f, gR.y + gridH * 0.4f + 24.0f * k), cMute, "Use ACEITAR ou RECUSAR.", false, fs);
		}
		else if (T.mode == 1)
		{
			const char* m = "Aguardando resposta...";
			const ImVec2 ts = r->calculateTextSize(m, 13.0f * k);
			r->drawText(ImVec2(xr + (panW - ts.x) * 0.5f, gR.y + gridH * 0.4f), cText, m, false, 13.0f * k);
		}
		else
		{
			for (int side = 0; side < 2; side++)
			{
				const std::vector<TItem>& L = side == 0 ? T.mine : T.theirs;
				const float cx0 = gR.x + side * (colW + 6.0f * k);
				const bool conf = side == 0 ? T.myConf : T.theirConf;
				r->drawText(ImVec2(cx0, gR.y + 2.0f * k), cText, side == 0 ? "Você" : T.partner, false, fs);
				if (conf)
				{
					const ImVec2 ts = r->calculateTextSize("CONFIRMADO", 9.0f * k);
					r->drawText(ImVec2(cx0 + colW - ts.x, gR.y + 4.0f * k), cGreen, "CONFIRMADO", false, 9.0f * k);
				}
				for (int i = 0; i < INV_TRADE_VIS; i++)
				{
					const ImVec2 a(cx0, listY + i * rowH), b(cx0 + colW, listY + i * rowH + rowH - 3.0f * k);
					r->drawRect(a, b, cSlotBg, true);
					r->drawRect(a, b, conf ? cGreen : cSlotLn, false, 1.0f);
					if (i < (int)L.size())
					{
						DrawIcon(r, L[i].icon, ImVec2(a.x + 2.0f * k, a.y + 2.0f * k), ImVec2(a.x + (rowH - 5.0f * k), b.y - 2.0f * k));
						r->pushClipRect(ImVec2(a.x + rowH, a.y), ImVec2(b.x - 2.0f * k, b.y), true);
						r->drawText(ImVec2(a.x + rowH, a.y + 3.0f * k), cText, L[i].name, false, 9.5f * k);
						char q[16]; snprintf(q, sizeof(q), "x%d", L[i].qty);
						r->drawText(ImVec2(a.x + rowH, a.y + 15.0f * k), cMute, q, false, 9.5f * k);
						r->popClipRect();
					}
				}
			}
			const char* hint = T.myConf ? "Oferta confirmada. Use EDITAR para alterar." : "Arraste/OFERTE itens. Toque na sua oferta para remover.";
			r->pushClipRect(ImVec2(xr + pad, gR.y + gridH + 8.0f * k), ImVec2(xr + panW - pad, gR.y + gridH + 8.0f * k + infoH), true);
			r->drawText(ImVec2(xr + pad + 6.0f * k, gR.y + gridH + 8.0f * k + (infoH - 10.0f * k) * 0.5f), cMute, hint, false, 10.0f * k);
			r->popClipRect();
		}
	}

	// ---- botoes ----
	for (const Btn& b : btns)
	{
		const bool pressed = g_t.finger >= 0 && g_t.kind == 3 && g_t.btn == b.id;
		const bool hot = g_t.dragging && b.enabled && (b.id == B_USE || b.id == B_DROP || b.id == B_OFFER) && In(g_t.cx, g_t.cy, b.a, b.b);
		r->drawRect(b.a, b.b, hot || pressed ? cPurpleF : (b.enabled ? cBtn : cBtnOff), true);
		r->drawRect(b.a, b.b, hot ? cPurple : cSlotLn, false, 1.0f);
		const float bfs = 10.5f * k;
		const ImVec2 ts = r->calculateTextSize(b.label, bfs);
		r->drawText(ImVec2((b.a.x + b.b.x - ts.x) * 0.5f, (b.a.y + b.b.y - ts.y) * 0.5f), b.enabled ? cText : cMute, b.label, false, bfs);
	}

	// ---- item sendo arrastado ----
	if (g_t.dragging)
	{
		int icon = 8;
		if (g_t.panel == P_INV && g_t.src >= 0 && g_t.src < S.slots && bySlot[g_t.src]) icon = bySlot[g_t.src]->icon;
		else if (g_t.panel == P_GROUND && g_t.src >= 0 && g_t.src < (int)S.ground.size()) icon = S.ground[g_t.src].icon;
		const float h = slot * 0.5f;
		DrawIcon(r, icon, ImVec2(g_t.cx - h, g_t.cy - h), ImVec2(g_t.cx + h, g_t.cy + h), 230);
	}

	// ---- aviso (toast) ----
	if (now < toastUntil && !toast.empty())
	{
		const float tfs = 12.0f * k;
		const ImVec2 ts = r->calculateTextSize(toast, tfs);
		const float w = ts.x + 28.0f * k, h = ts.y + 14.0f * k, x = (scr.x - w) * 0.5f, y = 12.0f * k;
		const ImColor accent = toastType == 1 ? cRed : (toastType == 2 ? cGreen : cPurple);
		r->drawRect(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(18, 18, 24, 235), true);
		r->drawRect(ImVec2(x, y + h - 2.0f * k), ImVec2(x + w, y + h), accent, true);
		r->drawText(ImVec2(x + 14.0f * k, y + 7.0f * k), cText, toast, false, tfs);
	}
}
