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
	struct State { bool active = false; int veh = 0; bool moto = false; bool locked = false; int trunk = 0; int plate = -1; };   // trunk: 0 sem porta-malas | 1 fechado | 2 aberto | plate: -1 servidor antigo/sem placa | 0 com placa | 1 placa removida
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
	uint8_t on = 0, moto = 0, lk = 0, tk = 0, pl = 255; uint16_t veh = 0;
	if (!bs.Read(on) || !bs.Read(veh) || !bs.Read(moto) || !bs.Read(lk)) return;
	if (!bs.Read(tk)) tk = 0;                                            // extensao opcional no fim (servidor antigo = sem porta-malas)
	if (!bs.Read(pl)) pl = 255;                                          // extensao opcional: 0 com placa | 1 placa removida (servidor antigo = sem o item)
	std::lock_guard<std::mutex> l(g_mtx);
	g_net.active = on != 0 && veh > 0; g_net.veh = g_net.active ? (int)veh : 0; g_net.moto = moto != 0; g_net.locked = lk != 0; g_net.trunk = (g_net.active && !moto) ? std::min<int>(tk, 2) : 0;
	g_net.plate = (g_net.active && pl <= 1) ? (int)pl : -1;   // a moto 1194 tambem tem placa (o servidor manda 255 nas outras motos)
}

void CarInteract::Reset()
{
	{ std::lock_guard<std::mutex> l(g_mtx); g_net = State(); }
	g_menu = false;
}

void CarInteract::CloseMenu() { g_menu = false; }

bool CarInteract::Draw(ImGuiRenderer* r, float k, bool hasTap, float tx, float ty, int& sendVeh, int& sendAct)
{
	sendVeh = 0; sendAct = 0;
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

	// itens do menu (carro): 1 Trancar/Destrancar | 2 Abrir/Fechar porta-malas (so com porta-malas) | 3 Ver porta-malas (so com ele aberto) | 4 Remover placa | 5 Colocar placa
	struct Row { int act; const char* txt; };
	Row rows[4]; int nRows = 0;
	rows[nRows++] = { 1, s.locked ? (s.moto ? "Destrancar" : "Destrancar veículo") : (s.moto ? "Trancar" : "Trancar veículo") };
	if (s.trunk > 0) rows[nRows++] = { 2, s.trunk == 2 ? "Fechar porta-malas" : "Abrir porta-malas" };
	if (s.trunk == 2) rows[nRows++] = { 3, "Ver porta-malas" };
	if (s.plate == 0) rows[nRows++] = { 4, "Remover placa" };          // a placa vai para o inventario
	else if (s.plate == 1) rows[nRows++] = { 5, "Colocar placa" };     // usa a placa deste carro que esta no inventario
	const float fs = 9.5f * k;
	const bool oneBtn = s.moto && nRows == 1;          // moto sem placa: um unico botao; moto com placa: lista como a do carro
	const float rowH = 26.0f * k, rowW = (oneBtn ? 104.0f : 160.0f) * k, gap = 2.0f * k;
	const ImVec2 ma(dot.x + 12.0f * k, dot.y - rowH * 0.5f), mb(ma.x + rowW, ma.y + rowH);   // 1a linha
	auto rowA = [&](int i) { return ImVec2(ma.x, ma.y + (float)i * (rowH + gap)); };
	auto rowB = [&](int i) { return ImVec2(mb.x, mb.y + (float)i * (rowH + gap)); };

	bool consumed = false;
	if (g_menu)
	{
		if (hasTap)
		{
			consumed = true;
			for (int i = 0; i < nRows; i++) if (In(rowA(i), rowB(i), tx, ty)) { sendVeh = s.veh; sendAct = rows[i].act; }
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
	if (oneBtn)       // moto: um unico botao
	{
		r->drawRoundedRect(ma, mb, ImColor(200, 36, 62, 235), 6.0f * k, true);
		const ImVec2 ts = r->calculateTextSize(rows[0].txt, fs);
		r->drawText(ImVec2((ma.x + mb.x - ts.x) * 0.5f, (ma.y + mb.y - ts.y) * 0.5f), ImColor(255, 255, 255, 255), rows[0].txt, false, fs);
	}
	else              // carro: lista (fundo cinza translucido, barra de rolagem vermelha ao lado, como a referencia)
	{
		const ImVec2 la(ma.x - gap, ma.y - gap), lb(mb.x + gap + 4.0f * k, rowB(nRows - 1).y + gap);
		r->drawRect(la, lb, ImColor(28, 32, 36, 120), true);
		for (int i = 0; i < nRows; i++)
		{
			const ImVec2 a = rowA(i), b = rowB(i);
			const bool trunkRow = rows[i].act == 2;
			r->drawRect(a, b, trunkRow && s.trunk == 2 ? ImColor(52, 110, 70, 185) : ImColor(74, 80, 86, 175), true);
			float tx0 = a.x + 10.0f * k;
			if (trunkRow)      // indicador: verde = porta-malas aberto, vermelho = fechado
			{
				const ImColor ind = s.trunk == 2 ? ImColor(60, 220, 100, 255) : ImColor(235, 55, 70, 255);
				Disc(r, ImVec2(a.x + 12.0f * k, (a.y + b.y) * 0.5f), 4.0f * k, ind, ImColor(255, 255, 255, 190), 1.0f * k);
				tx0 = a.x + 22.0f * k;
			}
			const ImVec2 ts = r->calculateTextSize(rows[i].txt, fs);
			r->drawText(ImVec2(tx0, (a.y + b.y - ts.y) * 0.5f), ImColor(235, 238, 242, 255), rows[i].txt, false, fs);
		}
		r->drawRect(ImVec2(mb.x + gap, ma.y), ImVec2(mb.x + gap + 2.0f * k, rowB(nRows - 1).y), ImColor(220, 40, 70, 235), true);
	}
	return consumed;
}
