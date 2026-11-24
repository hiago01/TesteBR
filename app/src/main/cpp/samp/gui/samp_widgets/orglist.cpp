#include "orglist.h"
#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/raknet/BitStream.h"
#include "../../game/BrasaoTexDb.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

extern CNetGame* pNetGame;

namespace
{
	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}
	struct Entry { std::string tag, name, area, leaders; int members = 0; };
	std::mutex g_m;
	std::vector<Entry> g_rows;
	bool g_active = false, g_loading = false;
	int  g_page = 0, g_pages = 1, g_total = 0;
	int64_t g_reqAt = 0;
	// areas tocaveis (preenchidas no desenho)
	struct Btn { float x0, y0, x1, y1; int id; };   // 1 fechar | 2 anterior | 3 proxima
	std::vector<Btn> g_btn;
	int g_down = 0;
	std::atomic<int> g_fire{0};

	bool RdStr(RakNet::BitStream& bs, std::string& out)
	{
		uint8_t n = 0;
		if (!bs.Read(n)) return false;
		out.assign(n, '\0');
		if (n && !bs.Read(&out[0], (unsigned)n)) return false;
		return true;
	}
	void Send(int op, int page)
	{
		if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED || !pNetGame->GetRakClient()) return;
		RakNet::BitStream bs;
		bs.Write((uint8_t)op);
		if (op == 56) bs.Write((uint8_t)std::max(0, std::min(page, 200)));
		pNetGame->GetRakClient()->RPC(&RPC_JobsRequest, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}
	// corta o texto com "..." para caber em 'w' pixels
	std::string Fit(ImGuiRenderer* r, const std::string& t, float fs, float w)
	{
		if (r->calculateTextSize(t, fs).x <= w) return t;
		std::string s = t;
		while (!s.empty())
		{
			// remove 1 caractere UTF-8 inteiro do fim
			size_t i = s.size() - 1; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
			s.erase(i);
			if (r->calculateTextSize(s + "...", fs).x <= w) return s + "...";
		}
		return "...";
	}
}

namespace OrgList
{
	bool OnPacket(RakNet::BitStream& bs)
	{
		uint8_t page = 0, pages = 1, n = 0; uint16_t total = 0;
		if (!bs.Read(page) || !bs.Read(pages) || !bs.Read(total) || !bs.Read(n) || n > 12) return false;
		std::vector<Entry> rows;
		for (int i = 0; i < n; ++i)
		{
			Entry e; uint16_t mb = 0;
			if (!RdStr(bs, e.tag) || !RdStr(bs, e.name) || !RdStr(bs, e.area) || !RdStr(bs, e.leaders) || !bs.Read(mb)) return false;
			e.members = mb; rows.push_back(e);
		}
		std::lock_guard<std::mutex> l(g_m);
		g_rows.swap(rows); g_page = page; g_pages = std::max<int>(1, pages); g_total = total; g_active = true; g_loading = false;
		return true;
	}

	void Reset() { std::lock_guard<std::mutex> l(g_m); g_active = false; g_loading = false; g_rows.clear(); g_btn.clear(); g_down = 0; }
	bool Active() { std::lock_guard<std::mutex> l(g_m); return g_active; }

	void Close(bool notifyServer)
	{
		{ std::lock_guard<std::mutex> l(g_m); if (!g_active) return; g_active = false; g_rows.clear(); g_btn.clear(); }
		if (notifyServer) Send(57, 0);
	}

	void OnTouch(int type, int x, int y)
	{
		std::lock_guard<std::mutex> l(g_m);
		int hit = 0;
		for (const Btn& b : g_btn) if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1) { hit = b.id; break; }
		if (type == 2) g_down = hit;
		else if (type == 1) { if (g_down && g_down == hit) g_fire.store(hit); g_down = 0; }
		else if (type == 3 && hit != g_down) g_down = 0;
	}

	void Draw(ImGuiRenderer* r, const ImVec2& scr)
	{
		std::vector<Entry> rows; int page, pages, total; bool loading;
		{ std::lock_guard<std::mutex> l(g_m); if (!g_active) return; rows = g_rows; page = g_page; pages = g_pages; total = g_total; loading = g_loading; }
		const float k = scr.y / 540.0f, W = scr.x / k;
		auto P = [&](float x, float y) { return ImVec2(x * k, y * k); };
		const int64_t now = NowMs();

		r->drawRect(ImVec2(0, 0), scr, ImColor(8, 12, 22, 238), true);
		r->drawRectFilledMulticolor(ImVec2(0, 0), ImVec2(scr.x, 70 * k), ImColor(30, 70, 150, 70), ImColor(30, 70, 150, 70), ImColor(0, 0, 0, 0), ImColor(0, 0, 0, 0));
		r->drawText(P(28, 16), ImColor(240, 244, 252, 255), "ORGANIZAÇÕES DA CIDADE", false, 20.0f * k);
		r->drawText(P(28, 44), ImColor(150, 168, 205, 255), std::to_string(total) + (total == 1 ? " organização ativa" : " organizações ativas"), false, 10.5f * k);
		r->drawRect(P(28, 70), P(W - 28, 71.2f), ImColor(120, 150, 210, 70), true);

		std::vector<Btn> btns;
		auto button = [&](float x, float y, float w, float h, const char* lab, int id, bool enabled)
		{
			const bool dn = (g_down == id) && enabled;
			const ImVec2 a = P(x, y), b = P(x + w, y + h);
			r->drawRoundedRect(a, b, enabled ? (dn ? ImColor(30, 90, 210, 255) : ImColor(40, 112, 255, 255)) : ImColor(52, 60, 80, 220), 6.0f * k, true);
			const float fs = 12.0f * k; const ImVec2 ts = r->calculateTextSize(lab, fs);
			r->drawText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y * 0.5f), enabled ? ImColor(255, 255, 255, 255) : ImColor(130, 140, 165, 255), lab, false, fs);
			if (enabled) btns.push_back({ a.x, a.y, b.x, b.y, id });
		};
		button(W - 28 - 96, 20, 96, 32, "Fechar", 1, true);

		// grade 3 x 2
		const int cols = 3; const float gx = 14, gy = 14, x0 = 28, y0 = 84, x1 = W - 28, y1 = 540 - 64;
		const float cw = (x1 - x0 - gx * (cols - 1)) / cols, ch = (y1 - y0 - gy) / 2;
		if (rows.empty())
			r->drawText(P(x0 + 6, y0 + 20), ImColor(170, 184, 214, 255), loading ? "Carregando..." : "Nenhuma organização cadastrada.", false, 13.0f * k);
		for (size_t i = 0; i < rows.size() && i < 6; ++i)
		{
			const Entry& e = rows[i];
			const int cx = (int)(i % cols), cy = (int)(i / cols);
			const float x = x0 + cx * (cw + gx), y = y0 + cy * (ch + gy);
			r->drawRoundedRect(P(x, y), P(x + cw, y + ch), ImColor(22, 30, 48, 245), 10.0f * k, true);
			r->drawRoundedRect(P(x, y), P(x + cw, y + ch), ImColor(70, 100, 170, 120), 10.0f * k, false, std::max(1.0f, 1.2f * k));
			// brasao
			const float bs = 84;
			const ImVec2 ba = P(x + 14, y + 16), bb = P(x + 14 + bs, y + 16 + bs);
			RwTexture* t = BrasaoTexDb::Texture("org_" + e.tag);
			if (t && t->raster) r->drawImage(ba, bb, (ImTextureID)t->raster, ImColor(255, 255, 255, 255), ImVec2(0, 0), ImVec2(1, 1));
			else
			{
				r->drawRoundedRect(ba, bb, ImColor(40, 56, 92, 255), 12.0f * k, true);
				std::string ini = e.name.empty() ? std::string("?") : e.name.substr(0, 1);
				const ImVec2 ts = r->calculateTextSize(ini, 34.0f * k);
				r->drawText(ImVec2((ba.x + bb.x) * 0.5f - ts.x * 0.5f, (ba.y + bb.y) * 0.5f - ts.y * 0.5f), ImColor(190, 205, 240, 255), ini, false, 34.0f * k);
			}
			const float tx = x + 14 + bs + 14, tw = cw - (14 + bs + 14) - 12;
			r->drawText(P(tx, y + 16), ImColor(244, 247, 253, 255), Fit(r, e.name, 14.0f * k, tw * k), false, 14.0f * k);
			r->drawText(P(tx, y + 38), ImColor(140, 160, 205, 255), Fit(r, e.area.empty() ? std::string("Global") : e.area, 10.0f * k, tw * k), false, 10.0f * k);
			r->drawText(P(tx, y + 62), ImColor(110, 130, 175, 255), e.members == 1 ? "1 membro" : std::to_string(e.members) + " membros", false, 10.0f * k);
			// lideres
			r->drawRect(P(x + 14, y + 112), P(x + cw - 14, y + 113), ImColor(80, 100, 150, 80), true);
			r->drawText(P(x + 14, y + 122), ImColor(110, 130, 175, 255), e.leaders.find(',') != std::string::npos ? "LÍDERES" : "LÍDER", false, 9.0f * k);
			r->drawText(P(x + 14, y + 138), e.leaders.empty() ? ImColor(130, 140, 165, 255) : ImColor(235, 240, 250, 255),
				Fit(r, e.leaders.empty() ? std::string("Sem líder") : e.leaders, 12.0f * k, (cw - 28) * k), false, 12.0f * k);
		}

		// rodape: paginas
		const float fy = 540 - 52;
		if (page > 0) button(W * 0.5f - 170, fy, 96, 36, "Anterior", 2, true);
		if (page + 1 < pages) button(W * 0.5f + 74, fy, 96, 36, "Próxima", 3, true);
		{
			const std::string pg = "Página " + std::to_string(page + 1) + " / " + std::to_string(pages);
			const float fs = 12.0f * k; const ImVec2 ts = r->calculateTextSize(pg, fs);
			r->drawText(ImVec2(scr.x * 0.5f - ts.x * 0.5f, (fy + 18) * k - ts.y * 0.5f), ImColor(214, 224, 244, 255), pg, false, fs);
		}
		{ std::lock_guard<std::mutex> l(g_m); g_btn = btns; }

		const int f = g_fire.exchange(0);
		if (f == 1) { Close(true); return; }
		if (f == 2 && page > 0 && now - g_reqAt > 300) { g_reqAt = now; { std::lock_guard<std::mutex> l(g_m); g_loading = true; } Send(56, page - 1); }
		if (f == 3 && page + 1 < pages && now - g_reqAt > 300) { g_reqAt = now; { std::lock_guard<std::mutex> l(g_m); g_loading = true; } Send(56, page + 1); }
	}
}
