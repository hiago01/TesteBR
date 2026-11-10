#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/vehiclepool.h"
#include "../../net/playerpool.h"
#include "../../net/localplayer.h"
#include "../../vendor/raknet/BitStream.h"
#include "carinteract.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>

extern CNetGame* pNetGame;

namespace
{
	struct State { bool active = false; int veh = 0; bool moto = false; bool locked = false; };
	std::mutex g_mtx;
	State      g_net;
	bool       g_menu = false;       // so a thread de desenho mexe

	bool Proj(float x, float y, float z, ImVec2& o)
	{
		CVector w(x, y, z), sc;
		((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&w, &sc, 0, 0, 0, 0);   // CSprite::CalcScreenCoors
		if (sc.z < 1.0f) return false;
		o = ImVec2(sc.x, sc.y);
		return true;
	}

	void Disc(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& fill, const ImColor& line, float lw)
	{
		constexpr int N = 24;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad); }
		r->drawConvexPolyFilled(p, N, fill);
		r->drawPolyline(p, N, line, true, lw);
	}

	bool In(const ImVec2& a, const ImVec2& b, float x, float y) { return x >= a.x && x <= b.x && y >= a.y && y <= b.y; }
}

void CarInteract::OnPacket(RakNet::BitStream& bs)
{
	uint8_t on = 0, moto = 0, lk = 0; uint16_t veh = 0;
	if (!bs.Read(on) || !bs.Read(veh) || !bs.Read(moto) || !bs.Read(lk)) return;
	std::lock_guard<std::mutex> l(g_mtx);
	g_net.active = on != 0 && veh > 0; g_net.veh = g_net.active ? (int)veh : 0; g_net.moto = moto != 0; g_net.locked = lk != 0;
}

void CarInteract::Reset()
{
	{ std::lock_guard<std::mutex> l(g_mtx); g_net = State(); }
	g_menu = false;
}

void CarInteract::CloseMenu() { g_menu = false; }

bool CarInteract::Draw(ImGuiRenderer* r, float k, bool hasTap, float tx, float ty, int& sendVeh)
{
	sendVeh = 0;
	State s;
	{ std::lock_guard<std::mutex> l(g_mtx); s = g_net; }
	if (!s.active || !pNetGame || !pNetGame->GetVehiclePool()) { g_menu = false; return false; }
	CVehicle* v = pNetGame->GetVehiclePool()->GetAt((VEHICLEID)s.veh);
	if (!v || !v->m_pVehicle) { g_menu = false; return false; }

	// ponto de ancoragem: traseira do carro (perto do porta-malas), um pouco acima do chao. up = frente do veiculo no GTA.
	const RwMatrix m = v->m_pVehicle->GetMatrix().ToRwMatrix();
	const float back = s.moto ? 0.8f : 2.1f;
	ImVec2 dot, ctr, top;
	if (!Proj(m.pos.x - m.up.x * back, m.pos.y - m.up.y * back, m.pos.z - m.up.z * back + 0.55f, dot) ||
	    !Proj(m.pos.x, m.pos.y, m.pos.z, ctr) || !Proj(m.pos.x, m.pos.y, m.pos.z + 1.0f, top)) { g_menu = false; return false; }
	const float pxm = std::max(18.0f * k, std::fabs(ctr.y - top.y));          // pixels por metro (aprox.) neste ponto
	const float carR = (s.moto ? 1.0f : 1.9f) * pxm;

	const char* act = s.locked ? (s.moto ? "Destrancar" : "Destrancar veículo") : (s.moto ? "Trancar" : "Trancar veículo");
	const float fs = 9.5f * k;
	const float rowH = 26.0f * k, rowW = (s.moto ? 104.0f : 150.0f) * k, gap = 2.0f * k;
	const ImVec2 ma(dot.x + 12.0f * k, dot.y - rowH * 0.5f), mb(ma.x + rowW, ma.y + rowH);   // item do menu (1 so por enquanto)

	bool consumed = false;
	if (g_menu)
	{
		if (hasTap)
		{
			consumed = true;
			if (In(ma, mb, tx, ty)) sendVeh = s.veh;
			g_menu = false;                                 // escolheu ou tocou fora: fecha
		}
	}
	else if (hasTap)
	{
		const ImVec2 la(dot.x - 16.0f * k, dot.y - 16.0f * k), lb(dot.x + 112.0f * k, dot.y + 16.0f * k);   // ponto + texto
		const float dx = tx - ctr.x, dy = ty - ctr.y;
		if (In(la, lb, tx, ty) || dx * dx + dy * dy <= carR * carR) { g_menu = true; consumed = true; }
	}

	if (!g_menu)
	{
		const char* lbl = "Abrir interação";
		Disc(r, dot, 4.5f * k, ImColor(232, 45, 70, 255), ImColor(255, 255, 255, 230), 1.4f * k);
		r->drawText(ImVec2(dot.x + 9.0f * k, dot.y - fs * 0.55f), ImColor(255, 255, 255, 235), lbl, true, fs);
		return consumed;
	}

	Disc(r, dot, 4.5f * k, ImColor(232, 45, 70, 255), ImColor(255, 255, 255, 230), 1.4f * k);
	if (s.moto)       // moto: um unico botao
	{
		r->drawRoundedRect(ma, mb, ImColor(200, 36, 62, 235), 6.0f * k, true);
		const ImVec2 ts = r->calculateTextSize(act, fs);
		r->drawText(ImVec2((ma.x + mb.x - ts.x) * 0.5f, (ma.y + mb.y - ts.y) * 0.5f), ImColor(255, 255, 255, 255), act, false, fs);
	}
	else              // carro: lista (fundo cinza translucido, barra de rolagem vermelha ao lado, como a referencia)
	{
		const ImVec2 la(ma.x - gap, ma.y - gap), lb(mb.x + gap + 4.0f * k, mb.y + gap);
		r->drawRect(la, lb, ImColor(28, 32, 36, 120), true);
		r->drawRect(ma, mb, ImColor(74, 80, 86, 175), true);
		r->drawRect(ImVec2(mb.x + gap, ma.y), ImVec2(mb.x + gap + 2.0f * k, mb.y), ImColor(220, 40, 70, 235), true);
		const ImVec2 ts = r->calculateTextSize(act, fs);
		r->drawText(ImVec2(ma.x + 10.0f * k, (ma.y + mb.y - ts.y) * 0.5f), ImColor(235, 238, 242, 255), act, false, fs);
	}
	return consumed;
}
