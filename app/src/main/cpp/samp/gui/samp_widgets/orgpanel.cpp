#include "../../main.h"
#include "../gui.h"
#include "../sffonts.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../java/jniutil.h"
#include "../../game/RW/RenderWare.h"
#include "../../vendor/stb/stb_image.h"   // implementacao em game/sprite2d.cpp
#include "../orgpanel/OrgLogo.h"
#include "../../game/BrasaoTexDb.h"
#include "../maximoos/InputManager.h"
#include "../maximoos/GestureTracker.h"
#include "../orgpanel/OrgPanelUI.h"
#include "../phone/PhoneHttp.h"
#include "orgpanel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern UI*       pUI;
extern CNetGame* pNetGame;
extern CJavaWrapper* pJavaWrapper;
extern volatile bool g_bTouchCancel;      // game/multitouch.cpp

using namespace MaximoOS;

namespace
{
	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- Adaptador Canvas -> ImGuiRenderer (copia do adaptador do Maximo OS; la ele fica em namespace anonimo) ----
	class ImGuiCanvas : public Canvas
	{
	public:
		explicit ImGuiCanvas(ImGuiRenderer* r) : m_r(r) {}
		~ImGuiCanvas() { while (m_clips-- > 0) m_r->popClipRect(); }

		void fillRect(const Rect& r, Color c, float rad) override
		{
			if (c.a == 0 || r.empty()) return;
			if (rad > 0.5f) m_r->drawRoundedRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), std::min(rad, std::min(r.w, r.h) * 0.5f), true);
			else m_r->drawRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), true);
		}
		void strokeRect(const Rect& r, Color c, float rad, float th) override
		{
			if (c.a == 0 || r.empty()) return;
			if (rad > 0.5f) m_r->drawRoundedRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), std::min(rad, std::min(r.w, r.h) * 0.5f), false, th);
			else m_r->drawRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), false, th);
		}
		void fillGradientV(const Rect& r, Color top, Color bot) override
		{
			if (r.empty()) return;
			m_r->drawRectFilledMulticolor(V(r.x, r.y), V(r.right(), r.bottom()), C(top), C(top), C(bot), C(bot));
		}
		void fillCircle(Vec2 c, float rad, Color col) override
		{
			if (col.a == 0 || rad <= 0) return;
			ImVec2 p[40]; const int n = poly(p, c, rad);
			m_r->drawConvexPolyFilled(p, n, C(col));
		}
		void strokeCircle(Vec2 c, float rad, Color col, float th) override
		{
			if (col.a == 0 || rad <= 0) return;
			ImVec2 p[40]; const int n = poly(p, c, rad);
			m_r->drawPolyline(p, n, C(col), true, th);
		}
		void line(Vec2 a, Vec2 b, Color c, float th) override { if (c.a) m_r->drawLine(V(a.x, a.y), V(b.x, b.y), C(c), th); }
		void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) override { if (col.a) m_r->drawTriangle(V(a.x, a.y), V(b.x, b.y), V(c.x, c.y), C(col), true); }
		void setFont(int role) override { m_role = role; }
		void text(Vec2 p, Color c, const std::string& s, float size) override
		{
			if (!c.a || s.empty()) return;
			ImFont* sf = SfFonts::get(m_role); ImFont* old = sf ? m_r->swapFont(sf) : nullptr;
			m_r->drawText(V(p.x, p.y), C(c), s, false, size);
			if (old) m_r->swapFont(old);
		}
		Vec2 measure(const std::string& s, float size) override
		{
			ImFont* sf = SfFonts::get(m_role); ImFont* old = sf ? m_r->swapFont(sf) : nullptr;
			const ImVec2 v = m_r->calculateTextSize(s, size);
			if (old) m_r->swapFont(old);
			return { v.x, v.y };
		}
		void pushClip(const Rect& r) override { m_r->pushClipRect(V(r.x, r.y), V(r.right(), r.bottom()), true); ++m_clips; }
		void popClip() override { if (m_clips > 0) { m_r->popClipRect(); --m_clips; } }
		void setAlpha(float a) override { m_alpha = std::clamp(a, 0.0f, 1.0f); }
		void image(const Rect& r, uint64_t tex, Vec2 uv0, Vec2 uv1) override
		{
			if (!tex || r.empty()) return;
			m_r->drawImage(V(r.x, r.y), V(r.right(), r.bottom()), (ImTextureID)(uintptr_t)tex, ImColor(255, 255, 255, (int)std::lround(255.0f * m_alpha)), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
		}
	private:
		int m_role = 0;
		static ImVec2 V(float x, float y) { return ImVec2(x, y); }
		ImColor C(Color c) const { return ImColor(c.r, c.g, c.b, (int)std::lround(c.a * m_alpha)); }
		static int poly(ImVec2* p, Vec2 c, float rad)
		{
			const int n = rad < 6 ? 12 : rad < 20 ? 24 : 40;
			for (int i = 0; i < n; ++i) { const float a = 6.2831853f * i / n; p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad); }
			return n;
		}
		ImGuiRenderer* m_r; float m_alpha = 1.0f; int m_clips = 0;
	};

	// ---- logo do servidor (PNG embutido, mesmo caminho do atlas da HUD: stb_image -> RwImage -> RwRaster) ----
	RwRaster* s_logo = nullptr; bool s_logoFailed = false;
	RwRaster* CreateLogoRaster()
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(OrgLogo::kPng, (int)OrgLogo::kPngSize, &w, &h, &ch, 4);
		if (!px) { FLog("[GRUPO] logo: stbi falhou: %s", stbi_failure_reason()); return nullptr; }
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) { stbi_image_free(px); return nullptr; }
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image)) { if (image) RwImageDestroy(image); stbi_image_free(px); return nullptr; }
		for (int y = 0; y < h; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		stbi_image_free(px);
		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4)) { rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK); rf |= rwRASTERFORMAT8888; rd = 32; }
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;
		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image)) { if (RwRasterDestroy) RwRasterDestroy(raster); RwImageDestroy(image); return nullptr; }
		RwImageDestroy(image);
		FLog("[GRUPO] logo %dx%d criado (raster %dx%d)", w, h, rw, rh);
		return raster;
	}
	uint64_t LogoTex()
	{
		if (!s_logo && !s_logoFailed) { s_logo = CreateLogoRaster(); if (!s_logo) s_logoFailed = true; }
		return (uint64_t)(uintptr_t)s_logo;
	}

	// ---- estado (rede escreve em g_pend*; o desenho consome) ----
	OrgPanel::Panel& Pn() { static OrgPanel::Panel p; return p; }
	InputManager& In() { static InputManager i; return i; }
	GestureTracker& Gt() { static GestureTracker g; return g; }
	Phone::HttpClient& Http() { static Phone::HttpClient h(2); return h; }

	std::atomic<bool> g_open{ false };          // a tela esta aberta (captura de toque)
	std::atomic<int>  g_backReq{ 0 };
	std::atomic<bool> g_reset{ false };
	std::mutex g_mx;
	bool g_pendOpen = false; std::string g_pendToken; int g_pendPort = 0;
	struct Done { int what; bool ok; };
	std::vector<Done> g_pendDone;
	struct Msg { std::string text; bool bad; };
	std::vector<Msg> g_pendMsg;
	int g_port = 0;                              // porta publica do servico (do ultimo token)

	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		return !n || bs.Read(&out[0], (unsigned)n);
	}

	// RPC 246 (cliente -> servidor): ops 40..45 do org.pwn
	void SendRpc(int op, int a = 0, uint32_t b = 0, int c = 0)
	{
		if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED || !pNetGame->GetRakClient()) return;
		RakNet::BitStream bs;
		bs.Write((uint8_t)op);
		if (op == 40) { bs.Write((uint8_t)a); bs.Write((uint32_t)b); bs.Write((uint16_t)c); }   // acao | conta alvo | id do jogador (convite)
		else if (op == 41 || op == 42 || op == 43) bs.Write((uint32_t)a);                      // valor / quantidade
		pNetGame->GetRakClient()->RPC(&RPC_JobsRequest, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}

	void RunCmds(OrgPanel::Panel& p)
	{
		std::vector<OrgPanel::Cmd> v; v.swap(p.cmds());
		for (const OrgPanel::Cmd& c : v)
		{
			using K = OrgPanel::Cmd;
			switch (c.kind)
			{
			case K::Get: case K::Post:
			{
				Phone::HttpReq r;
				r.method = c.kind == K::Get ? "GET" : "POST"; r.host = pNetGame ? std::string(pNetGame->m_szHostOrIp) : std::string();
				r.port = g_port; r.path = c.path; r.body = c.body; r.contentType = "text/plain; charset=utf-8"; r.timeoutMs = 6000; r.maxResp = 128 * 1024; r.tag = (uint32_t)c.tag;
				if (r.host.empty() || r.port <= 0 || !Http().submit(r)) p.onHttp(c.tag, 0, std::string());      // sem servidor / fila cheia: erro de rede
				break;
			}
			case K::RpcMember: SendRpc(40, c.a, (uint32_t)c.b, c.c); break;
			case K::RpcDeposit: SendRpc(41, c.a); break;
			case K::RpcWithdraw: SendRpc(42, c.a); break;
			case K::RpcFarm: SendRpc(43, c.a); break;
			case K::RpcGps: SendRpc(44); break;
			case K::RpcClose: SendRpc(45); break;
			case K::Copy: if (pJavaWrapper) pJavaWrapper->SetClipboardString(c.body.c_str()); break;
			case K::Close: break;
			}
		}
	}
}

OrgPanelWidget::OrgPanelWidget() {}

bool OrgPanelWidget::IsOpen() { return g_open.load(); }

void OrgPanelWidget::Reset()
{
	g_reset.store(true);                       // a thread de desenho fecha e limpa no proximo quadro
	g_open.store(false);
	In().setOpen(false);
	std::lock_guard<std::mutex> l(g_mx);
	g_pendOpen = false; g_pendDone.clear(); g_pendMsg.clear();
}

bool OrgPanelWidget::HandleRawTouch(int type, int num, int x, int y) { return In().handleRaw(type, num, x, y, g_bTouchCancel); }
bool OrgPanelWidget::ConsumeFlushRequest() { return In().consumeFlush(); }

bool OrgPanelWidget::OnBackPressed()
{
	if (!g_open.load()) return false;
	static int64_t s_last = 0;                // onKeyDown e onBackPressed podem disparar juntos (Android 13+)
	const int64_t now = NowMs();
	if (now - s_last >= 300) { s_last = now; g_backReq.fetch_add(1); }
	return true;
}

void OrgPanelWidget::OnServerPacket(const unsigned char* data, int bytes)
{
	if (!data || bytes <= 0 || bytes > 2048) return;
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	uint8_t op = 0;
	if (!bs.Read(op)) return;
	if (op == 16)
	{
		uint16_t port = 0; std::string tok;
		if (!bs.Read(port) || !RdStr(bs, tok) || tok.empty() || port == 0) return;
		for (char ch : tok) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || ch == '.')) return;     // token = digitos, hex e pontos
		{ std::lock_guard<std::mutex> l(g_mx); g_pendOpen = true; g_pendToken = tok; g_pendPort = port; }
		In().setOpen(true);                    // a captura comeca ja; o desenho abre no proximo quadro
		g_open.store(true);
	}
	else if (op == 17) { uint8_t what = 0, ok = 0; if (bs.Read(what) && bs.Read(ok)) { std::lock_guard<std::mutex> l(g_mx); if (g_pendDone.size() < 16) g_pendDone.push_back({ (int)what, ok != 0 }); } }
	else if (op == 18) { uint8_t bad = 1; std::string s; if (bs.Read(bad) && RdStr(bs, s) && !s.empty()) { std::lock_guard<std::mutex> l(g_mx); if (g_pendMsg.size() < 8) g_pendMsg.push_back({ s, bad != 0 }); } }
}

void OrgPanelWidget::draw(ImGuiRenderer* r)
{
	OrgPanel::Panel& p = Pn();
	static int64_t s_prev = 0;
	const int64_t now = NowMs();
	const float dt = s_prev ? (float)std::min<int64_t>(now - s_prev, 100) : 16.0f;
	s_prev = now;

	if (g_reset.exchange(false)) { p.close(false); Gt().reset(); g_backReq.store(0); }
	{
		std::lock_guard<std::mutex> l(g_mx);
		if (g_pendOpen)
		{
			g_pendOpen = false; g_port = g_pendPort;
			p.open(g_pendToken, now); Gt().reset();
			Gt().slop = std::max(10.0f, (pUI ? pUI->displaySize().y : 720.0f) * 0.012f);
		}
		for (const Done& d : g_pendDone) p.onServerDone(d.what, d.ok);
		g_pendDone.clear();
		for (const Msg& m : g_pendMsg) p.onServerToast(m.text, m.bad);
		g_pendMsg.clear();
	}
	if (!p.isOpen())
	{
		g_backReq.store(0);
		if (g_open.load()) { g_open.store(false); In().setOpen(false); }
		return;
	}
	for (int n = g_backReq.exchange(0); n > 0 && p.isOpen(); --n) p.back();

	// respostas HTTP prontas
	{
		std::vector<Phone::HttpRes> res; Http().poll(res);
		for (Phone::HttpRes& h : res) p.onHttp((int)h.tag, h.status, h.body);
	}
	// toques -> gestos
	std::vector<Touch> touches;
	{
		std::vector<RawTouch> raw; In().drain(raw);
		for (const RawTouch& t : raw) Gt().feed(t.type, t.num, t.x, t.y, t.cancel, now, touches);
		Gt().tick(now, touches);
	}
	const ImVec2 sc = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	{   // brasao da organizacao (texdb/brasao, textura org_<tag>); sem banco/sem tag/sem textura = painel sem brasao
		RwTexture* bt = p.orgTag().empty() ? nullptr : BrasaoTexDb::Texture("org_" + p.orgTag());
		p.setBrasao(bt && bt->raster ? (uint64_t)(uintptr_t)bt->raster : 0);
	}
	p.setLogo(LogoTex());                       // so cria na primeira vez que o painel desenha (thread de desenho/GL)
	{
		ImGuiCanvas cv(r);
		p.frame(cv, sc.x, sc.y, touches, now, dt);
	}
	RunCmds(p);
	if (!p.isOpen() && g_open.load()) { g_open.store(false); In().setOpen(false); }       // o proprio painel se fechou
}
