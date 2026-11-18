#include "../../main.h"
#include "../gui.h"
#include "../imguirenderer.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../game/World.h"
#include "../../net/netgame.h"
#include "../../net/playerpool.h"
#include "../../net/localplayer.h"
#include "../../vendor/raknet/BitStream.h"
#include "waypoint3d.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

extern UI*       pUI;
extern CNetGame* pNetGame;
extern CGame*    pGame;

namespace
{
	constexpr int kMax = 12;

	struct Pin { float x = 0, y = 0, z = 0; std::string name, label; };
	std::mutex       g_mtx;
	Pin              g_sede; bool g_sedeOn = false; int g_sedeSrc = 0;     // src: 1 = sede (servidor, op 21), 2 = destino do mapa aberto (cliente)      // marcacao temporaria do servidor (op 21): "SEDE (grupo)"
	std::vector<Pin> g_pins;            // escrito pela thread de rede, lido pela de desenho (copia por quadro)

	// nome em MAIUSCULAS (UTF-8): ASCII e letras latinas acentuadas (U+00E0..U+00FE, exceto o sinal de divisao) como no HUD de referencia
	std::string Upper(const std::string& s)
	{
		std::string o; o.reserve(s.size());
		for (size_t i = 0; i < s.size(); i++)
		{
			const unsigned char c = (unsigned char)s[i];
			if (c >= 'a' && c <= 'z') o.push_back((char)(c - 32));
			else if (c == 0xC3 && i + 1 < s.size())
			{
				unsigned char d = (unsigned char)s[i + 1];
				if (d >= 0xA0 && d <= 0xBE && d != 0xB7) d = (unsigned char)(d - 0x20);
				o.push_back((char)c); o.push_back((char)d); i++;
			}
			else o.push_back((char)c);
		}
		return o;
	}

	bool Proj(float x, float y, float z, ImVec2& o)
	{
		CVector w(x, y, z), sc;
		((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&w, &sc, 0, 0, 0, 0);   // CSprite::CalcScreenCoors
		if (sc.z < 1.0f) return false;
		o = ImVec2(sc.x, sc.y);
		return true;
	}

	std::string Dist(float d)
	{
		char b[24];
		const int m = (int)(d + 0.5f);
		if (m < 1000) snprintf(b, sizeof b, "%dm", m);
		else
		{
			const int dk = (int)(d / 100.0f + 0.5f);          // decimos de km
			snprintf(b, sizeof b, "%d,%dkm", dk / 10, dk % 10);
		}
		return b;
	}

	// pino (gota): circulo + triangulo tangente com a ponta para baixo, furo escuro no meio. (cx, cy) = centro do circulo; r = raio
	void DrawPin(ImGuiRenderer* r, float cx, float cy, float rad, float al)
	{
		const ImColor white(255, 255, 255, (int)(245.0f * al)), hole(24, 24, 28, (int)(235.0f * al));
		constexpr int N = 28;
		ImVec2 c[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; c[i] = ImVec2(cx + std::cos(a) * rad, cy + std::sin(a) * rad); }
		r->drawConvexPolyFilled(c, N, white);
		r->drawTriangle(ImVec2(cx, cy + rad * 1.62f), ImVec2(cx - rad * 0.80f, cy + rad * 0.60f), ImVec2(cx + rad * 0.80f, cy + rad * 0.60f), white, true);
		ImVec2 h[20];
		for (int i = 0; i < 20; i++) { const float a = (float)i * 6.2831853f / 20.0f; h[i] = ImVec2(cx + std::cos(a) * rad * 0.40f, cy + std::sin(a) * rad * 0.40f); }
		r->drawConvexPolyFilled(h, 20, hole);
	}

	struct Item { float dist; ImVec2 p, ground; bool hasGround; const Pin* pin; };
}

void Waypoint3D::OnPacket(RakNet::BitStream& bs)
{
	uint8_t n = 0;
	if (!bs.Read(n) || n > kMax) return;
	std::vector<Pin> v;
	for (int i = 0; i < n; i++)
	{
		Pin p; uint8_t len = 0;
		if (!bs.Read(p.x) || !bs.Read(p.y) || !bs.Read(p.z) || !bs.Read(len)) return;      // pacote quebrado: ignora tudo (mantem a lista atual)
		if (!(std::fabs(p.x) < 100000.0f && std::fabs(p.y) < 100000.0f && std::fabs(p.z) < 100000.0f)) return;
		p.name.assign(len, '\0');
		if (len && !bs.Read(&p.name[0], (unsigned)len)) return;
		for (char& ch : p.name) if ((unsigned char)ch < 32) ch = ' ';
		p.label = p.name;
		p.name = Upper(p.name);
		if (p.name.empty()) { p.name = "LOCAL"; p.label = "Local"; }
		v.push_back(std::move(p));
	}
	std::lock_guard<std::mutex> l(g_mtx);
	g_pins = std::move(v);
}

void Waypoint3D::OnSede(RakNet::BitStream& bs)
{
	uint8_t on = 0, len = 0; Pin p;
	if (!bs.Read(on) || !bs.Read(p.x) || !bs.Read(p.y) || !bs.Read(p.z) || !bs.Read(len)) return;
	if (on && !(std::fabs(p.x) < 100000.0f && std::fabs(p.y) < 100000.0f && std::fabs(p.z) < 100000.0f)) return;
	p.name.assign(len, '\0');
	if (len && !bs.Read(&p.name[0], (unsigned)len)) return;
	for (char& ch : p.name) if ((unsigned char)ch < 32) ch = ' ';
	p.name = Upper(p.name);
	if (p.name.empty()) p.name = "SEDE";
	std::lock_guard<std::mutex> l(g_mtx);
	if (on) { g_sedeOn = true; g_sedeSrc = 1; g_sede = std::move(p); }
	else if (g_sedeSrc == 1) g_sedeOn = false;                           // o servidor so apaga o que ele mesmo criou (nao o destino do mapa)
}

void Waypoint3D::Reset()
{
	std::lock_guard<std::mutex> l(g_mtx);
	g_pins.clear();
	g_sedeOn = false; g_sedeSrc = 0;
}

int Waypoint3D::Count() { std::lock_guard<std::mutex> l(g_mtx); return (int)g_pins.size(); }
bool Waypoint3D::Get(int i, float& x, float& y, std::string& label)
{
	std::lock_guard<std::mutex> l(g_mtx);
	if (i < 0 || i >= (int)g_pins.size()) return false;
	x = g_pins[i].x; y = g_pins[i].y; label = g_pins[i].label;
	return true;
}
void Waypoint3D::SetDest(float x, float y, float z, const std::string& label)
{
	if (!(std::fabs(x) < 100000.0f && std::fabs(y) < 100000.0f && std::fabs(z) < 100000.0f)) return;
	Pin p; p.x = x; p.y = y; p.z = z;
	p.label = label.empty() ? std::string("Destino") : label;
	p.name = Upper(p.label);
	std::lock_guard<std::mutex> l(g_mtx);
	g_sede = std::move(p); g_sedeOn = true; g_sedeSrc = 2;
}
void Waypoint3D::ClearDest()
{
	std::lock_guard<std::mutex> l(g_mtx);
	if (g_sedeSrc == 2) g_sedeOn = false;
}

void Waypoint3D::Draw(ImGuiRenderer* r, float k)
{
	std::vector<Pin> pins;
	{ std::lock_guard<std::mutex> l(g_mtx); pins = g_pins; if (g_sedeOn) pins.push_back(g_sede); }
	if (pins.empty() || !pNetGame || !pGame) return;

	// distancia a partir do PERSONAGEM; sem ele (ainda nao nasceu) nao desenha
	float px = 0, py = 0, pz = 0;
	bool havePl = false;
	if (CPlayerPool* pool = pNetGame->GetPlayerPool())
	{
		CLocalPlayer* lp = pool->GetLocalPlayer();
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		if (pp && pp->m_pPed) { const CVector pv = pp->m_pPed->GetPosition(); px = pv.x; py = pv.y; pz = pv.z; havePl = true; }
	}
	if (!havePl) return;
	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);

	// z = -1000 (criada no mapa): chao do lugar quando a area esta carregada (a ate ~350 m e perto da altura do jogador); senao a altura do jogador. Guarda 1,5 s.
	struct AutoZ { float x, y, z; int64_t t; };
	static std::vector<AutoZ> azc;
	const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	for (Pin& p : pins)
	{
		if (p.z > -900.0f) continue;
		AutoZ* e = nullptr;
		for (AutoZ& a : azc) if (a.x == p.x && a.y == p.y) { e = &a; break; }
		if (!e) { if (azc.size() >= 32) azc.erase(azc.begin()); azc.push_back({p.x, p.y, pz, 0}); e = &azc.back(); }
		if (nowMs - e->t > 1500)
		{
			e->t = nowMs;
			const float dx2 = p.x - px, dy2 = p.y - py;
			float gz = pz;
			if (dx2 * dx2 + dy2 * dy2 < 350.0f * 350.0f)
			{
				const float g = CWorld::FindGroundZForCoord(p.x, p.y);
				if (std::fabs(g - pz) < 60.0f) gz = g;
			}
			e->z = gz;
		}
		p.z = e->z;
	}

	std::vector<Item> items;
	for (const Pin& p : pins)
	{
		const float dx = p.x - px, dy = p.y - py, dz = p.z - pz;
		const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (d < 4.0f) continue;                                           // chegou: some
		Item it; it.dist = d; it.pin = &p;
		if (!Proj(p.x, p.y, p.z + 1.9f, it.p)) continue;                  // 1,9 m acima do ponto: o rotulo "flutua" sobre o local
		if (it.p.x < -scr.x * 0.3f || it.p.x > scr.x * 1.3f || it.p.y < -scr.y * 0.3f || it.p.y > scr.y * 1.3f) continue;
		it.hasGround = Proj(p.x, p.y, p.z, it.ground);
		items.push_back(it);
	}
	std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });    // longe primeiro: o perto fica por cima

	struct Box { ImVec2 a, b; };
	std::vector<Box> placed;
	for (const Item& it : items)
	{
		const float al = std::min(1.0f, (it.dist - 4.0f) / 6.0f);          // 4 m: invisivel; 10 m ou mais: opaco
		if (al <= 0.02f) continue;
		const std::string dist = Dist(it.dist);
		const float sc = it.dist <= 60.0f ? 1.0f : std::max(0.78f, 1.0f - (it.dist - 60.0f) / 1500.0f);       // de 60 m a ~400 m encolhe ate 78%
		const float fsN = 17.0f * k * sc, fsD = 13.5f * k * sc;
		const float pad = 10.0f * k * sc, ir = 10.0f * k * sc;       // ir = raio do pino
		const ImVec2 tn = r->calculateTextSize(it.pin->name, fsN), td = r->calculateTextSize(dist, fsD);
		const float tw = std::max(tn.x, td.x), th = tn.y + td.y;
		const float w = pad + ir * 2.0f + 8.0f * k * sc + tw + pad, h = std::max(th + pad * 1.4f, ir * 3.6f + pad);
		ImVec2 a(it.p.x - w * 0.5f, it.p.y - h * 0.5f), b(a.x + w, a.y + h);

		// dois rotulos no mesmo lugar da tela: o mais perto sobe (ate 4 vezes) em vez de cobrir o outro
		for (int tries = 0; tries < 4; tries++)
		{
			bool hit = false;
			for (const Box& o : placed) if (a.x < o.b.x && b.x > o.a.x && a.y < o.b.y && b.y > o.a.y) { hit = true; break; }
			if (!hit) break;
			a.y -= h + 3.0f * k; b.y -= h + 3.0f * k;
		}
		placed.push_back({a, b});

		// haste fina ate o chao (ponto exato do local) + ponto
		if (it.hasGround && it.ground.y > b.y)
		{
			r->drawLine(ImVec2(it.p.x, b.y), it.ground, ImColor(255, 255, 255, (int)(110.0f * al)), 1.3f * k);
			constexpr int N = 14;
			ImVec2 c[N];
			for (int i = 0; i < N; i++) { const float t = (float)i * 6.2831853f / (float)N; c[i] = ImVec2(it.ground.x + std::cos(t) * 3.4f * k, it.ground.y + std::sin(t) * 3.4f * k); }
			r->drawConvexPolyFilled(c, N, ImColor(255, 255, 255, (int)(235.0f * al)));
		}

		// painel escuro translucido + pino + nome (negrito claro) + distancia (cinza) embaixo
		r->drawRoundedRect(a, b, ImColor(16, 16, 20, (int)(185.0f * al)), 9.0f * k, true);
		r->drawRoundedRect(ImVec2(a.x, a.y), ImVec2(b.x, a.y + (b.y - a.y) * 0.5f), ImColor(255, 255, 255, (int)(14.0f * al)), 9.0f * k, true);
		r->drawRoundedRect(a, b, ImColor(255, 255, 255, (int)(38.0f * al)), 9.0f * k, false, 1.0f);
		DrawPin(r, a.x + pad + ir, (a.y + b.y) * 0.5f - ir * 0.30f, ir, al);
		const float tx = a.x + pad + ir * 2.0f + 8.0f * k * sc, ty = a.y + (h - th) * 0.5f;
		r->drawText(ImVec2(tx, ty), ImColor(250, 250, 252, (int)(255.0f * al)), it.pin->name, true, fsN);
		r->drawText(ImVec2(tx, ty + tn.y), ImColor(168, 170, 178, (int)(255.0f * al)), dist, false, fsD);
	}
}
