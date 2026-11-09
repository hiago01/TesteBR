#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../vendor/stb/stb_image.h"   // implementacao em game/sprite2d.cpp
#include "inventory.h"
#include "inventory_atlas.h"
#include "../../util/CUtil.h"
#include "controlmode.h"
#include "controlsselector.h"

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
	struct Item  { int slot = 0, id = 0, qty = 0, icon = 8; uint32_t unitW = 0; std::string name, desc; };
	struct Drop  { int id = 0, item = 0, qty = 0, icon = 8; std::string name, desc; };
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

	struct HotSlot { int id = 0, qty = 0, icon = 8, flags = 0; std::string name; };
	HotSlot    g_hot[5];

	std::mutex g_dm;
	State      g_state;
	std::string g_toastMsg;
	int         g_toastType = 0;
	int64_t     g_toastUntil = 0;

	struct TouchEv { int type; int num; float x; float y; bool cancel; };
	std::atomic<bool> g_fireLock{false};   // servidor: arma na mao sem municao -> botao de tiro travado
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

	// O servidor manda "Nome\nDescricao" (celular: IMEI e chip). Separa; sem '\n' a descricao fica vazia.
	void SplitDesc(std::string& name, std::string* desc)
	{
		const size_t p = name.find('\n');
		if (p == std::string::npos) return;
		if (desc) *desc = name.substr(p + 1);
		name.resize(p);
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

	// --- Icones ORIGINAIS do GTA para armas (textura "<modelo>icon", a mesma da roda de armas) ---
	// icone do atlas -> id da arma (ver Inv_RegisterWeapons em maximo_inv.inc). 0 = nao e arma.
	int WeaponFromIcon(int icon)
	{
		static const unsigned char kMap[59] = {
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,      // 0..15
			4,5,22,25,28,30,34,35,16,18,           // 16..25
			41,43,14,1,46,9,                       // 26..31
			2,3,6,7,8,10,11,12,13,15,17,           // 32..42
			23,24,26,27,29,31,32,33,36,37,38,39,40,42,44,45 // 43..58
		};
		return (icon >= 0 && icon < 59) ? kMap[icon] : 0;
	}

	const char* WeaponModel(int w)
	{
		static const char* const k[47] = {
			nullptr, "brassknuckle", "golfclub", "nitestick", "knifecur", "bat", "shovel", "poolcue", "katana", "chnsaw",
			"gun_dildo1", "gun_dildo2", "gun_vibe1", "gun_vibe2", "flowera", "gun_cane", "grenade", "teargas", "molotov", nullptr,
			nullptr, nullptr, "colt45", "silenced", "desert_eagle", "chromegun", "sawnoff", "shotgspa", "micro_uzi", "mp5lng",
			"ak47", "m4", "tec9", "cuntgun", "sniper", "rocketla", "heatseek", "flame", "minigun", "satchel",
			"bomb", "spraycan", "fire_ex", "camera", "nvgoggles", "irgoggles", "gun_para"
		};
		return (w > 0 && w < 47) ? k[w] : nullptr;
	}

	RwTexture* GetGtaWeaponIcon(int w)
	{
		struct E { RwTexture* tex = nullptr; int tries = 0; uint32_t next = 0; };
		static E s_e[47];
		static uint32_t s_f = 0;
		s_f++;
		if (w <= 0 || w >= 47) return nullptr;
		E& e = s_e[w];
		if (e.tex) return e.tex;
		const char* m = WeaponModel(w);
		if (!m || e.tries >= 4 || s_f < e.next) return nullptr;
		e.tries++; e.next = s_f + 240;
		char name[48];
		snprintf(name, sizeof(name), "%sicon", m);
		static const char* const kDbs[] = { "gta3", "txd", "gta_int", "samp", "player", "mobile" };
		for (const char* db : kDbs)
		{
			RwTexture* t = CUtil::LoadTextureFromDB(db, name);
			if (t && t->raster) { e.tex = t; break; }
		}
		return e.tex;
	}

	void DrawIcon(ImGuiRenderer* r, int icon, const ImVec2& a, const ImVec2& b, int alpha = 255)
	{
		if (const int w = WeaponFromIcon(icon))
		{
			if (RwTexture* gt = GetGtaWeaponIcon(w))
			{
				// sprites do GTA sao largos: mantem a proporcao centralizada na celula
				const float cw = b.x - a.x, ch = b.y - a.y;
				float iw = (float)(gt->raster->width > 0 ? gt->raster->width : 1), ih = (float)(gt->raster->height > 0 ? gt->raster->height : 1);
				const float k = std::min(cw / iw, ch / ih);
				const float dw = iw * k, dh = ih * k;
				const ImVec2 a2(a.x + (cw - dw) * 0.5f, a.y + (ch - dh) * 0.5f);
				r->drawImage(a2, ImVec2(a2.x + dw, a2.y + dh), (ImTextureID)gt->raster, ImColor(255, 255, 255, alpha), ImVec2(0, 0), ImVec2(1, 1));
				return;
			}
		}
		RwRaster* tex = GetAtlas();
		if (!tex) return;
		if (icon < 0 || icon >= InvAtlas::kIcons) icon = 8;
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
	for (HotSlot& h : g_hot) h = HotSlot();
	g_fireLock.store(false);
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
			SplitDesc(it.name, &it.desc);
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
			SplitDesc(d.name, &d.desc);
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
				SplitDesc(e.name, nullptr);
				e.item = it; e.qty = q; e.icon = ic;
				(side == 0 ? t.mine : t.theirs).push_back(std::move(e));
			}
		}
		std::lock_guard<std::mutex> l(g_dm);
		g_state.trade = std::move(t);
	}
	else if (op == 6)   // HOTBAR (5 slots rapidos)
	{
		uint8_t n = 0;
		if (!bs.Read(n) || n > 5) return;
		HotSlot tmp[5];
		for (int i = 0; i < n; i++)
		{
			uint16_t id = 0, q = 0; uint8_t ic = 0, fl = 0;
			if (!bs.Read(id) || !bs.Read(q) || !bs.Read(ic) || !bs.Read(fl) || !ReadStr(bs, tmp[i].name)) return;
			SplitDesc(tmp[i].name, nullptr);
			tmp[i].id = id; tmp[i].qty = q; tmp[i].icon = ic; tmp[i].flags = fl;
		}
		std::lock_guard<std::mutex> l(g_dm);
		for (int i = 0; i < 5; i++) g_hot[i] = tmp[i];
	}
	else if (op == 8)   // abre a tela de escolha de controles (= /controles); servidor envia ao fim da cutscene de conta nova
	{
		ControlMode::EnsureLoaded();
		ControlsSelector::Open();
	}
	else if (op == 7)   // TRAVA DO TIRO (sem municao)
	{
		uint8_t lk = 0;
		if (!bs.Read(lk)) return;
		g_fireLock.store(lk != 0);
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
	const ImColor cPink   (236, 40, 96, 255);
	const ImColor cPinkF  (236, 40, 96, 90);
	const ImColor cCellBg (255, 255, 255, 18);
	const ImColor cCellLn (255, 255, 255, 62);
	const ImColor cTint   (26, 14, 40, 150);

	constexpr int INV_TRADE_VIS = 6;       // linhas de oferta exibidas (= INV_TRADE_SLOTS do servidor)
	enum { P_INV = 1, P_GROUND = 2, P_HOT = 3 };
	enum { B_NONE = 0, B_USE, B_DROP, B_TRADE, B_QTY, B_PICK, B_OFFER, B_CLOSE,
		   B_ACCEPT, B_REFUSE, B_CONFIRM, B_CANCEL, B_REFRESH };

	struct Sel { int panel = 0; int idx = -1; } g_sel;      // idx: slot (inventario) ou posicao no vetor ground
	bool    g_allQty = true;                                // false = 1 unidade, true = tudo
	float   g_scroll[3] = {0, 0, 0};                        // [P_INV], [P_GROUND]
	int64_t g_busyUntil = 0;                                // evita executar duas vezes o mesmo gesto

	struct Touch
	{
		int   finger = -1;
		int   kind = 0;           // 1 grade inventario, 2 grade chao, 3 botao, 4 outro, 5 slot rapido
		int   panel = 0;
		int   src = -1;           // slot / indice tocado
		int   btn = B_NONE;
		float sx = 0, sy = 0, cx = 0, cy = 0, scroll0 = 0;
		int64_t t0 = 0;
		bool  scrolling = false, dragging = false;
	} g_t;

	struct Btn { int id; ImVec2 a, b; const char* label; bool enabled; };

	bool In(float x, float y, const ImVec2& a, const ImVec2& b) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; }

	void Send(const char* fmt, int a = 0, int b = 0, int c = 0)
	{
		if (!pNetGame) return;
		char buf[64];
		snprintf(buf, sizeof(buf), fmt, a, b, c);
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
		// avisos do servidor (equipou/guardou/erro de slot rapido) tambem aparecem com o inventario fechado
		std::string tm; int tt = 0; int64_t tu = 0;
		{
			std::lock_guard<std::mutex> l(g_dm);
			tm = g_toastMsg; tt = g_toastType; tu = g_toastUntil;
		}
		if (NowMs() < tu && !tm.empty())
		{
			const ImVec2 sc = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
			const float kk = sc.y / 575.0f, tfs = 12.0f * kk;
			const ImVec2 ts = r->calculateTextSize(tm, tfs);
			const float w = ts.x + 28.0f * kk, h = ts.y + 14.0f * kk, x = (sc.x - w) * 0.5f, y = sc.y * 0.62f;
			const ImColor accent = tt == 1 ? cRed : (tt == 2 ? cGreen : cPurple);
			r->drawRect(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(18, 18, 24, 235), true);
			r->drawRect(ImVec2(x, y + h - 2.0f * kk), ImVec2(x + w, y + h), accent, true);
			r->drawText(ImVec2(x + 14.0f * kk, y + 7.0f * kk), cText, tm, false, tfs);
		}
		return;
	}

	// ---- snapshot do estado (evita segurar o mutex durante o desenho) ----
	State S; std::string toast; int toastType = 0; int64_t toastUntil = 0; HotSlot hot[5];
	{
		std::lock_guard<std::mutex> l(g_dm);
		S = g_state; toast = g_toastMsg; toastType = g_toastType; toastUntil = g_toastUntil;
		for (int i = 0; i < 5; i++) hot[i] = g_hot[i];
	}

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float k = scr.y / 575.0f;

	// ---- geometria: layout de referencia 1280x720 centralizado (u = escala) ----
	const float u  = std::min(scr.y / 720.0f, scr.x / 1100.0f);
	const float ox = scr.x * 0.5f - 640.0f * u, oy = (scr.y - 720.0f * u) * 0.5f;
	auto PX = [&](float v) { return ox + v * u; };
	auto PY = [&](float v) { return oy + v * u; };
	const int   cols = 6, visRows = 8;
	const float slot = 58.0f * u, gap = 4.0f * u, pad = 0.0f, pitch = slot + gap;
	const float gridW = cols * slot + (cols - 1) * gap;
	const float gridH = visRows * slot + (visRows - 1) * gap;
	const float headH = 126.0f * u, infoH = 20.0f * u, barH = 34.0f * u;
	const float panW = gridW;
	const float panH = 720.0f * u;
	const float xl = PX(296.0f), xr = PX(705.0f), y0 = oy;
	const ImVec2 gL(xl, PY(126.0f)), gR(xr, PY(126.0f));       // origem das grades
	const float infoY = PY(626.0f), barY = PY(652.0f);
	const float fs = 11.0f * u;

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
	// item atribuido a um SLOT RAPIDO fica SO no slot rapido (sai da grade do inventario enquanto estiver la; limpar o slot devolve);
	// o servidor continua guardando o item no inventario (peso/salvamento), so a exibicao muda.
	auto inHot = [&](int id) { for (int h = 0; h < 5; h++) if (hot[h].id == id) return true; return false; };
	for (const Item& it : S.items) if (it.slot >= 0 && it.slot < S.slots && !inHot(it.id)) bySlot[it.slot] = &it;

	// selecao invalida (item sumiu apos sync do servidor) -> limpa
	if (g_sel.panel == P_INV && (g_sel.idx < 0 || g_sel.idx >= S.slots || !bySlot[g_sel.idx])) g_sel = Sel();
	if (g_sel.panel == P_GROUND && (g_sel.idx < 0 || g_sel.idx >= (int)S.ground.size())) g_sel = Sel();

	// ---- botoes ----
	std::vector<Btn> btns;
	{
		const float bw = (gridW - 2.0f * gap) / 3.0f, bh = barH;
		auto mk = [&](int i, int id, const char* lab, bool en) {
			const float bx = xl + i * (bw + gap);
			btns.push_back({id, ImVec2(bx, barY), ImVec2(bx + bw, barY + bh), lab, en});
		};
		const bool invSel = g_sel.panel == P_INV;
		const bool grdSel = g_sel.panel == P_GROUND;
		if (grdSel) mk(0, B_PICK, "PEGAR", !tradeActive);
		else        mk(0, B_USE, "USAR", invSel && !tradeActive);
		if (tradeActive) mk(1, B_OFFER, "OFERTAR", invSel && !T.myConf);
		else             mk(1, B_DROP, "DROPAR", invSel);
		mk(2, B_TRADE, "NEGOCIAR", !tradeMode);
		btns.push_back({B_QTY, ImVec2(PX(474.0f), PY(87.0f)), ImVec2(PX(584.0f), PY(116.0f)), "", true});
		btns.push_back({B_QTY, ImVec2(PX(883.0f), PY(87.0f)), ImVec2(PX(993.0f), PY(116.0f)), "", true});
		btns.push_back({B_CLOSE, ImVec2(PX(1100.0f), PY(26.0f)), ImVec2(PX(1220.0f), PY(58.0f)), "", true});

		// botoes do painel direito (negociacao)
		const float rbw = (gridW - gap) / 2.0f;
		auto mr = [&](int i, int id, const char* lab, bool en) {
			const float bx = xr + i * (rbw + gap);
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
	const ImVec2 rightA(PX(690.0f), y0), rightB(PX(1100.0f), y0 + panH), leftA(PX(180.0f), y0), leftB(PX(690.0f), y0 + panH);
	const ImVec2 vpLA(gL.x, gL.y), vpLB(gL.x + gridW, gL.y + gridH), vpRA(gR.x, gR.y), vpRB(gR.x + gridW, gR.y + gridH);

	// slots rapidos: coluna a esquerda da mochila (alvo de arrastar-e-soltar)
	const float hotX = PX(191.0f), hotY0 = PY(224.0f), hotP = 69.0f * u;
	auto hotAt = [&](float x, float y) -> int {
		for (int i = 0; i < 5; i++)
		{
			const float ay = hotY0 + i * hotP;
			if (In(x, y, ImVec2(hotX, ay), ImVec2(hotX + slot, ay + slot))) return i;
		}
		return -1;
	};
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
	if (g_t.finger >= 0 && (g_t.kind == 1 || g_t.kind == 2 || g_t.kind == 5) && !g_t.dragging && !g_t.scrolling && now - g_t.t0 >= 250)
	{
		if (g_t.kind == 5)
		{
			if (g_t.src >= 0 && g_t.src < 5 && hot[g_t.src].id && !(hot[g_t.src].flags & 5)) g_t.dragging = true;   // arma equipada (na mao ou no corpo): travada, nao arrasta
		}
		else
		{
			const bool has = g_t.panel == P_INV ? (g_t.src >= 0 && g_t.src < S.slots && bySlot[g_t.src])
												: (g_t.src >= 0 && g_t.src < (int)S.ground.size());
			if (has) { g_t.dragging = true; g_sel.panel = g_t.panel; g_sel.idx = g_t.src; }
		}
	}

	for (const TouchEv& e : evs)
	{
		if (e.type == 2 && g_t.finger < 0)                                   // push
		{
			g_t = Touch();
			g_t.finger = e.num; g_t.sx = g_t.cx = e.x; g_t.sy = g_t.cy = e.y; g_t.t0 = now;
			if (const Btn* b = btnAt(e.x, e.y)) { g_t.kind = 3; g_t.btn = b->id; }
			else if (hotAt(e.x, e.y) >= 0) { g_t.kind = 5; g_t.panel = P_HOT; g_t.src = hotAt(e.x, e.y); }
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
				const int dstHot = hotAt(e.x, e.y);
				if (fromInv && t.src >= 0 && t.src < S.slots && bySlot[t.src])
				{
					const int srcId = bySlot[t.src]->id;
						const bool isAmmo = srcId >= 1100 && srcId <= 1107;                                  // municoes (INV_ID_AMMO_BASE..+7)
						if (dstHot >= 0 && isAmmo && hot[dstHot].id && (hot[dstHot].flags & 8))
							Send("/invx load %d %d %d", t.src, dstHot, g_allQty ? 999 : 1);                  // municao em cima da arma = carrega nela
						else if (dstHot >= 0)                         Send("/invx bar %d %d", dstHot, t.src);   // atribui ao slot rapido
					else if (b && b->enabled && b->id == B_USE)        doUse(t.src);          // arrastar ate "Usar"
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
			else if (t.kind == 5)                                             // slot rapido
			{
				if (t.src < 0 || t.src >= 5 || !hot[t.src].id) {}
				else if (t.dragging)
				{
					const int dh = hotAt(e.x, e.y);
					if (dh < 0 && (hot[t.src].flags & 8) && In(e.x, e.y, vpLA, vpLB)) Send("/invx unload %d %d", t.src, g_allQty ? 9999 : 1);   // arma de fogo solta na mochila = retira a municao
						else if (dh < 0 || dh == t.src) { if (dh < 0) Send("/invx barclear %d", t.src); }   // soltou fora = limpa o slot
					else { const int sl = [&]{ for (const Item& it : S.items) if (it.id == hot[t.src].id) return it.slot; return -1; }();
					       if (sl >= 0) { Send("/invx bar %d %d", dh, sl); } else Send("/invx barclear %d", t.src); }
				}
				else if (hot[t.src].flags & 4) Send("/invx hot %d", t.src);        // arma na mao: so guarda no corpo (nao mexe no slot)
				else if (hot[t.src].flags & 1) Send("/invx hotstow %d", t.src);   // inventario aberto: toque numa arma equipada = devolve ao inventario
				else Send("/invx hot %d", t.src);                              // toque = equipar/usar
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
	r->drawRect(ImVec2(0, 0), scr, cTint, true);
	const float fu = 12.0f * u;      // texto padrao
	const float fq = 11.0f * u;      // quantidade nas celulas

	auto header = [&](float x, const std::string& title) -> float {
		const float fh = 14.0f * u;
		r->drawText(ImVec2(x, PY(95.0f)), cText, title, false, fh);
		return r->calculateTextSize(title, fh).x;
	};
	auto lineTo = [&](float x0, float x1) {
		r->drawLine(ImVec2(x0, PY(107.0f)), ImVec2(x1, PY(107.0f)), ImColor(255, 255, 255, 70), std::max(1.0f, 1.5f * u));
	};
	auto scrollbar = [&](float x, float scroll, float maxS, float content, bool alwaysFull) {
		if (maxS <= 0.0f && !alwaysFull) return;
		const float th = maxS > 0.0f ? std::max(18.0f * u, gridH * gridH / content) : gridH;
		const float ty = maxS > 0.0f ? gL.y + (gridH - th) * (scroll / maxS) : gL.y;
		r->drawRect(ImVec2(x, gL.y), ImVec2(x + 2.0f * u, gL.y + gridH), ImColor(255, 255, 255, 20), true);
		r->drawRect(ImVec2(x, ty), ImVec2(x + 2.0f * u, ty + th), cPink, true);
	};
	auto drawQty = [&](const ImVec2& a, int q) {
		char t[16]; snprintf(t, sizeof(t), "%dx", q);
		r->drawText(ImVec2(a.x + 5.0f * u, a.y + 3.0f * u), cText, t, true, fq);
	};

	// ---- cabecalho esquerdo: Mochila ----
	{
		const float tw = header(xl, "Mochila");
		lineTo(xl + tw + 10.0f * u, PX(462.0f));
		char w1[24], w2[32];
		snprintf(w1, sizeof(w1), "%s", Kg(S.weight).c_str());
		snprintf(w2, sizeof(w2), "/%skg", Kg(S.cap).c_str());
		const ImVec2 t1 = r->calculateTextSize(w1, fu), t2 = r->calculateTextSize(w2, fu);
		const float wx = PX(665.0f) - t1.x - t2.x, wy = PY(101.0f);
		r->drawText(ImVec2(wx, wy), S.weight > S.cap ? cRed : cText, w1, false, fu);
		r->drawText(ImVec2(wx + t1.x, wy), cPink, w2, false, fu);
	}

	// ---- grade da mochila (rolagem real, recorte) ----
	r->pushClipRect(vpLA, vpLB, true);
	for (int i = 0; i < S.slots; i++)
	{
		const int c = i % cols, rw = i / cols;
		const ImVec2 a(gL.x + c * pitch, gL.y + rw * pitch - g_scroll[P_INV]);
		const ImVec2 b(a.x + slot, a.y + slot);
		if (b.y < vpLA.y || a.y > vpLB.y) continue;
		const bool sel = g_sel.panel == P_INV && g_sel.idx == i;
		r->drawRect(a, b, sel ? cPinkF : cCellBg, true);
		r->drawRect(a, b, sel ? cPink : cCellLn, false, sel ? 2.0f : 1.0f);
		if (const Item* it = bySlot[i])
		{
			const bool ghost = g_t.dragging && g_t.panel == P_INV && g_t.src == i;
			const float m = 8.0f * u;
			DrawIcon(r, it->icon, ImVec2(a.x + m, a.y + m + 3.0f * u), ImVec2(b.x - m, b.y - m + 3.0f * u), ghost ? 70 : 255);
			drawQty(a, it->qty);
		}
	}
	r->popClipRect();
	scrollbar(PX(674.0f), g_scroll[P_INV], maxL, contentL, true);   // divisor rosa = barra de rolagem da mochila

	// ---- info do item selecionado ----
	{
		std::string info = "Arraste a munição sobre a arma no slot para carregar. Arraste a arma (guardada) para a mochila para retirar.";
		if (g_sel.panel == P_INV && g_sel.idx >= 0 && g_sel.idx < S.slots && bySlot[g_sel.idx])
		{
			const Item* it = bySlot[g_sel.idx];
			char b[192];
			if (!it->desc.empty()) snprintf(b, sizeof(b), "%s  -  %s kg  -  %s", it->name.c_str(), Kg(it->unitW).c_str(), it->desc.c_str());   // celular: IMEI e chip na descrição
			else snprintf(b, sizeof(b), "%s  x%d  -  %s kg cada", it->name.c_str(), it->qty, Kg(it->unitW).c_str());
			info = b;
		}
		else if (g_sel.panel == P_GROUND && g_sel.idx >= 0 && g_sel.idx < (int)S.ground.size())
		{
			const Drop& gd = S.ground[g_sel.idx];
			char b[192];
			if (!gd.desc.empty()) snprintf(b, sizeof(b), "No chão: %s  -  %s", gd.name.c_str(), gd.desc.c_str());
			else snprintf(b, sizeof(b), "No chão: %s  x%d", gd.name.c_str(), gd.qty);
			info = b;
		}
		r->pushClipRect(ImVec2(xl, infoY), ImVec2(xl + gridW, infoY + infoH), true);
		r->drawText(ImVec2(xl, infoY + (infoH - 10.0f * u) * 0.5f), cMute, info, false, 10.0f * u);
		r->popClipRect();
	}

	// ---- painel direito ----
	if (!tradeMode)
	{
		const float tw = header(xr, "Chão");
		lineTo(xr + tw + 10.0f * u, PX(873.0f));
		r->pushClipRect(vpRA, vpRB, true);
		for (int i = 0; i < groundCells; i++)
		{
			const int c = i % cols, rw = i / cols;
			const ImVec2 a(gR.x + c * pitch, gR.y + rw * pitch - g_scroll[P_GROUND]);
			const ImVec2 b(a.x + slot, a.y + slot);
			if (b.y < vpRA.y || a.y > vpRB.y) continue;
			const bool sel = g_sel.panel == P_GROUND && g_sel.idx == i;
			r->drawRect(a, b, sel ? cPinkF : cCellBg, true);
			r->drawRect(a, b, sel ? cPink : cCellLn, false, sel ? 2.0f : 1.0f);
			if (i < (int)S.ground.size())
			{
				const Drop& d = S.ground[i];
				const float m = 8.0f * u;
				DrawIcon(r, d.icon, ImVec2(a.x + m, a.y + m + 3.0f * u), ImVec2(b.x - m, b.y - m + 3.0f * u));
				drawQty(a, d.qty);
			}
		}
		r->popClipRect();
		scrollbar(gR.x + gridW + 9.0f * u, g_scroll[P_GROUND], maxR, contentR, false);
		r->pushClipRect(ImVec2(xr, infoY), ImVec2(xr + gridW, infoY + infoH), true);
		r->drawText(ImVec2(xr, infoY + (infoH - 10.0f * u) * 0.5f), cMute,
					S.ground.empty() ? "Nada por perto. Arraste um item para cá para dropar." : "Itens a até 3 m. Toque e use PEGAR, ou arraste para a mochila.", false, 10.0f * u);
		r->popClipRect();
	}
	else
	{
		char title[64];
		snprintf(title, sizeof(title), "Negociação%s%s", T.partner.empty() ? "" : " - ", T.partner.c_str());
		r->pushClipRect(ImVec2(xr, y0), ImVec2(PX(875.0f), y0 + headH), true);
		const float tw = header(xr, title);
		r->popClipRect();
		lineTo(xr + std::min(tw, PX(875.0f) - xr) + 10.0f * u, PX(873.0f));

		if (T.mode == 2)
		{
			char b[96]; snprintf(b, sizeof(b), "%s QUER NEGOCIAR COM VOCÊ!", T.partner.c_str());
			const ImVec2 ts = r->calculateTextSize(b, 14.0f * u);
			r->drawText(ImVec2(xr + (panW - ts.x) * 0.5f, gR.y + gridH * 0.4f), cText, b, false, 14.0f * u);
			const ImVec2 t2 = r->calculateTextSize("Use ACEITAR ou RECUSAR.", fu);
			r->drawText(ImVec2(xr + (panW - t2.x) * 0.5f, gR.y + gridH * 0.4f + 26.0f * u), cMute, "Use ACEITAR ou RECUSAR.", false, fu);
		}
		else if (T.mode == 1)
		{
			const char* m = "Aguardando resposta...";
			const ImVec2 ts = r->calculateTextSize(m, 14.0f * u);
			r->drawText(ImVec2(xr + (panW - ts.x) * 0.5f, gR.y + gridH * 0.4f), cText, m, false, 14.0f * u);
		}
		else
		{
			for (int side = 0; side < 2; side++)
			{
				const std::vector<TItem>& L = side == 0 ? T.mine : T.theirs;
				const float cx0 = gR.x + side * (colW + 6.0f * k);
				const bool conf = side == 0 ? T.myConf : T.theirConf;
				r->drawText(ImVec2(cx0, gR.y + 2.0f * u), cText, side == 0 ? "Você" : T.partner, false, fu);
				if (conf)
				{
					const ImVec2 ts = r->calculateTextSize("CONFIRMADO", 9.0f * u);
					r->drawText(ImVec2(cx0 + colW - ts.x, gR.y + 4.0f * u), cGreen, "CONFIRMADO", false, 9.0f * u);
				}
				for (int i = 0; i < INV_TRADE_VIS; i++)
				{
					const ImVec2 a(cx0, listY + i * rowH), b(cx0 + colW, listY + i * rowH + rowH - 3.0f * k);
					r->drawRect(a, b, cCellBg, true);
					r->drawRect(a, b, conf ? cGreen : cCellLn, false, 1.0f);
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
			r->pushClipRect(ImVec2(xr, infoY), ImVec2(xr + gridW, infoY + infoH), true);
			r->drawText(ImVec2(xr, infoY + (infoH - 10.0f * u) * 0.5f), cMute, hint, false, 10.0f * u);
			r->popClipRect();
		}
	}

	// ---- slots rapidos (coluna vertical, numeros rosa) ----
	{
		r->drawText(ImVec2(hotX, hotY0 - 16.0f * u), cMute, "Slots rápidos", false, 9.5f * u);
		const int over = g_t.dragging ? hotAt(g_t.cx, g_t.cy) : -1;
		for (int i = 0; i < 5; i++)
		{
			const ImVec2 a(hotX, hotY0 + i * hotP), b(a.x + slot, a.y + slot);
			const HotSlot& h = hot[i];
			const bool eq = (h.flags & 1) != 0, miss = h.id && h.qty <= 0 && !eq;
			const bool tgt = over == i;
			r->drawRect(a, b, tgt ? cPinkF : cCellBg, true);
			r->drawRect(a, b, tgt ? cPink : ((h.flags & 4) ? ImColor(255, 210, 70, 255) : cCellLn), false, (tgt || (h.flags & 4)) ? 2.0f : 1.0f);   // so amarelo, e apenas na mao
			if (h.id)
			{
				const float m = 8.0f * u;
				const bool ghost = g_t.dragging && g_t.panel == P_HOT && g_t.src == i;
				DrawIcon(r, h.icon, ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, b.y - m), ghost ? 70 : (miss ? 90 : 255));
				if (h.flags & 4)   // arma na mao: travada
					r->drawText(ImVec2(a.x + 4.0f * u, a.y + 3.0f * u), ImColor(255, 210, 70, 255), "NA MÃO", true, 8.5f * u);
				if (h.qty > 1 || (h.flags & 8) || (eq && h.qty <= 0))   // arma de fogo sempre mostra a municao carregada
				{
					char q[12]; snprintf(q, sizeof(q), "%dx", h.qty);
					const ImVec2 ts = r->calculateTextSize(q, fq);
					r->drawText(ImVec2(b.x - ts.x - 4.0f * u, b.y - ts.y - 3.0f * u), cText, q, true, fq);
				}
			}
			// selo numerado rosa na borda direita do slot
			const float bs = 18.0f * u, by = a.y + (slot - bs) * 0.5f;
			r->drawRect(ImVec2(b.x - bs * 0.5f, by), ImVec2(b.x + bs * 0.5f, by + bs), cPink, true);
			char nb[4]; snprintf(nb, sizeof(nb), "%d", i + 1);
			const ImVec2 ns = r->calculateTextSize(nb, 11.0f * u);
			r->drawText(ImVec2(b.x - ns.x * 0.5f, by + (bs - ns.y) * 0.5f), ImColor(255, 255, 255, 255), nb, false, 11.0f * u);
		}
	}

	// ---- botoes ----
	for (const Btn& b : btns)
	{
		const bool pressed = g_t.finger >= 0 && g_t.kind == 3 && g_t.btn == b.id;
		if (b.id == B_CLOSE)
		{
			const char* lab = "Para fechar";
			const ImVec2 ts = r->calculateTextSize(lab, 11.0f * u);
			const ImVec2 ea(PX(1176.0f), PY(32.0f)), eb(PX(1216.0f), PY(54.0f));
			r->drawText(ImVec2(ea.x - 6.0f * u - ts.x, PY(43.0f) - ts.y * 0.5f), cText, lab, false, 11.0f * u);
			r->drawRect(ea, eb, pressed ? cPinkF : cCellBg, true);
			r->drawRect(ea, eb, cCellLn, false, 1.0f);
			const ImVec2 es = r->calculateTextSize("ESC", 10.0f * u);
			r->drawText(ImVec2((ea.x + eb.x - es.x) * 0.5f, (ea.y + eb.y - es.y) * 0.5f), cText, "ESC", false, 10.0f * u);
			continue;
		}
		if (b.id == B_QTY)
		{
			r->drawRect(b.a, b.b, pressed ? cPinkF : cCellBg, true);
			r->drawRect(b.a, b.b, cCellLn, false, 1.0f);
			const char* lab = g_allQty ? "Quantidade: Tudo" : "Quantidade: 1";
			const ImVec2 ts = r->calculateTextSize(lab, 11.0f * u);
			r->drawText(ImVec2((b.a.x + b.b.x - ts.x) * 0.5f, (b.a.y + b.b.y - ts.y) * 0.5f), cMute, lab, false, 11.0f * u);
			continue;
		}
		const bool hot = g_t.dragging && b.enabled && (b.id == B_USE || b.id == B_DROP || b.id == B_OFFER) && In(g_t.cx, g_t.cy, b.a, b.b);
		r->drawRect(b.a, b.b, hot || pressed ? cPinkF : (b.enabled ? cCellBg : ImColor(255, 255, 255, 8)), true);
		r->drawRect(b.a, b.b, hot ? cPink : cCellLn, false, 1.0f);
		const float bfs = 11.5f * u;
		const ImVec2 ts = r->calculateTextSize(b.label, bfs);
		r->drawText(ImVec2((b.a.x + b.b.x - ts.x) * 0.5f, (b.a.y + b.b.y - ts.y) * 0.5f), b.enabled ? cText : cMute, b.label, false, bfs);
	}

	// ---- item sendo arrastado ----
	if (g_t.dragging)
	{
		int icon = 8;
		if (g_t.panel == P_INV && g_t.src >= 0 && g_t.src < S.slots && bySlot[g_t.src]) icon = bySlot[g_t.src]->icon;
		else if (g_t.panel == P_GROUND && g_t.src >= 0 && g_t.src < (int)S.ground.size()) icon = S.ground[g_t.src].icon;
		else if (g_t.panel == P_HOT && g_t.src >= 0 && g_t.src < 5) icon = hot[g_t.src].icon;
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

// ---- slots rapidos / botao Inventario (HUD moderno) ----
bool InventoryUI::HotbarGet(int slot, int& icon, int& qty, int* flags)
{
	if (slot < 0 || slot >= 5) return false;
	std::lock_guard<std::mutex> l(g_dm);
	const HotSlot& h = g_hot[slot];
	if (!h.id) return false;
	icon = h.icon; qty = h.qty; if (flags) *flags = h.flags;
	return true;
}

void InventoryUI::HotbarUse(int slot)
{
	int icon = 0, qty = 0;
	if (g_open.load() || !HotbarGet(slot, icon, qty)) return;     // slot vazio: nada a enviar
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 250) return;                 // anti-repique
	s_last = now;
	if (pNetGame) { char b[32]; snprintf(b, sizeof b, "/invx hot %d", slot); pNetGame->SendChatCommand(b); }
}

bool InventoryIsOpenNow() { return g_open.load(); }   // usado por gui.cpp / game/hooks.cpp / voice (esconde o HUD com o inventario aberto)
bool InventoryFireLocked() { return g_fireLock.load(); }   // usado por game/pad.cpp (CPad::GetWeapon)

void InventoryUI::RequestOpen()
{
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (g_open.load() || !pNetGame || now - s_last < 700) return;
	s_last = now;
	pNetGame->SendChatCommand("/inv");
}

void InventoryUI::DrawHotbarIcon(ImGuiRenderer* r, int icon, const ImVec2& a, const ImVec2& b, int alpha) { DrawIcon(r, icon, a, b, alpha); }
