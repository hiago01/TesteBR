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

	struct Pin { float x = 0, y = 0, z = 0; std::string name, label, cats, area; };
	std::mutex       g_mtx;
	Pin              g_sede; bool g_sedeOn = false; int g_sedeSrc = 0;     // src: 1 = sede (servidor, op 21), 2 = destino do mapa aberto (cliente)      // marcacao temporaria do servidor (op 21): "SEDE (grupo)"
	std::vector<Pin> g_hq;              // sedes dos grupos (op 22); so o mapa aberto le
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

	struct Item { float dist; ImVec2 p, ground; bool hasGround; const Pin* pin; int id = 0; bool off = false; ImVec2 dir = ImVec2(0, 1); };
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

void Waypoint3D::OnHq(RakNet::BitStream& bs)
{
	uint8_t n = 0;
	if (!bs.Read(n) || n > 120) return;
	std::vector<Pin> v;
	for (int i = 0; i < n; i++)
	{
		Pin p; uint8_t len = 0;
		if (!bs.Read(p.x) || !bs.Read(p.y) || !bs.Read(p.z) || !bs.Read(len)) return;      // pacote quebrado: mantem a lista atual
		if (!(std::fabs(p.x) < 100000.0f && std::fabs(p.y) < 100000.0f && std::fabs(p.z) < 100000.0f)) return;
		p.name.assign(len, '\0');
		if (len && !bs.Read(&p.name[0], (unsigned)len)) return;
		for (char& ch : p.name) if ((unsigned char)ch < 32) ch = ' ';
		p.label = p.name;
		if (p.label.empty()) p.label = "Grupo";
		for (std::string* t : { &p.cats, &p.area })      // categorias "A;B" e local de atuacao (texto simples, vem normalizado do servidor)
		{
			uint8_t l2 = 0;
			if (!bs.Read(l2)) return;
			t->assign(l2, '\0');
			if (l2 && !bs.Read(&(*t)[0], (unsigned)l2)) return;
			for (char& ch : *t) if ((unsigned char)ch < 32) ch = ' ';
		}
		if (p.area.empty()) p.area = "Global";
		v.push_back(std::move(p));
	}
	std::lock_guard<std::mutex> l(g_mtx);
	g_hq = std::move(v);
}
int Waypoint3D::HqCount() { std::lock_guard<std::mutex> l(g_mtx); return (int)g_hq.size(); }
bool Waypoint3D::HqGet(int i, float& x, float& y, float& z, std::string& name, std::string& cats, std::string& area)
{
	std::lock_guard<std::mutex> l(g_mtx);
	if (i < 0 || i >= (int)g_hq.size()) return false;
	x = g_hq[i].x; y = g_hq[i].y; z = g_hq[i].z; name = g_hq[i].label; cats = g_hq[i].cats; area = g_hq[i].area;
	return true;
}

void Waypoint3D::Reset()
{
	std::lock_guard<std::mutex> l(g_mtx);
	g_pins.clear(); g_hq.clear();
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

	// camera: para marcacoes fora da tela / atras de voce (sem alcance maximo: a seta aparece a qualquer distancia)
	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const RwMatrix cam = TheCamera.GetMatrix().ToRwMatrix();
	const ImVec2 cen(scr.x * 0.5f, scr.y * 0.5f);

	std::vector<Item> items;
	int pinIdx = 0;
	for (const Pin& p : pins)
	{
		const int id = ++pinIdx;                                           // ID da marcacao: (1), (2)... na ordem da lista (a sede / destino do mapa vem depois)
		const float dx = p.x - px, dy = p.y - py, dz = p.z - pz;
		const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (d < 4.0f) continue;                                           // chegou: some
		Item it; it.dist = d; it.pin = &p; it.id = id;
		const bool front = Proj(p.x, p.y, p.z + 1.9f, it.p);              // 1,9 m acima do ponto: o rotulo "flutua" sobre o local
		const float mg = 26.0f * k;
		if (front && it.p.x >= mg && it.p.x <= scr.x - mg && it.p.y >= mg && it.p.y <= scr.y - mg)
		{
			it.hasGround = Proj(p.x, p.y, p.z, it.ground);
			items.push_back(it);
			continue;
		}
		// fora da tela ou atras: seta na borda apontando para onde virar
		it.off = true;
		{
			// SEMPRE no espaco da camera (direita / cima / frente): antes usava a projecao quando estava na frente e o espaco da camera quando
			// estava atras; as duas dao angulos diferentes, entao a seta "pulava" para o lado errado ao girar a camera.
			const float wx = p.x - cam.pos.x, wy = p.y - cam.pos.y, wz = (p.z + 1.9f) - cam.pos.z;
			const float cxr = wx * cam.right.x + wy * cam.right.y + wz * cam.right.z;     // para a direita da camera
			const float czu = wx * cam.at.x + wy * cam.at.y + wz * cam.at.z;              // para cima da camera
			const float cfw = wx * cam.up.x + wy * cam.up.y + wz * cam.up.z;              // para a frente da camera
			float dxs = cxr, dys = -czu;                                                   // tela: x para a direita, y para baixo
			if (cfw < 0.0f)                                                                // atras: indica para que lado GIRAR (nao sobe/desce)
			{
				const float hz = std::sqrt(cxr * cxr + cfw * cfw);
				dxs = hz > 1e-3f ? cxr / hz : 0.0f;
				dys = hz > 1e-3f ? -czu / hz : 0.0f;
				static float s_side = 1.0f;                                                // quase exatamente atras: mantem o ultimo lado (sem tremer)
				if (std::fabs(dxs) > 0.05f) s_side = dxs > 0.0f ? 1.0f : -1.0f;
				else dxs = 0.05f * s_side;
				dxs *= 4.0f;                                                               // pesa mais o lado do que a altura
			}
			it.dir = ImVec2(dxs, dys);
		}
		const float ln = std::sqrt(it.dir.x * it.dir.x + it.dir.y * it.dir.y);
		if (ln < 1e-3f) it.dir = ImVec2(0.0f, 1.0f); else it.dir = ImVec2(it.dir.x / ln, it.dir.y / ln);
		{   // suaviza o angulo por marcacao (tira o tremor quando a camera gira)
			struct Sm { float x, y; ImVec2 d; };
			static std::vector<Sm> sm;
			Sm* e = nullptr;
			for (Sm& q : sm) if (q.x == p.x && q.y == p.y) { e = &q; break; }
			if (!e) { if (sm.size() >= 24) sm.erase(sm.begin()); sm.push_back({p.x, p.y, it.dir}); e = &sm.back(); }
			ImVec2 nd(e->d.x + (it.dir.x - e->d.x) * 0.45f, e->d.y + (it.dir.y - e->d.y) * 0.45f);
			const float nl = std::sqrt(nd.x * nd.x + nd.y * nd.y);
			if (nl > 1e-3f) { nd.x /= nl; nd.y /= nl; e->d = nd; it.dir = nd; }
			else e->d = it.dir;
		}
		it.hasGround = false;
		items.push_back(it);
	}
	std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });    // longe primeiro: o perto fica por cima

	struct Box { ImVec2 a, b; };
	std::vector<Box> placed;
	for (const Item& it : items)
	{
		if (it.off) continue;
		const float al = std::min(1.0f, (it.dist - 4.0f) / 6.0f);          // 4 m: invisivel; 10 m ou mais: opaco
		if (al <= 0.02f) continue;
		const std::string dist = Dist(it.dist);
		const std::string label = "(" + std::to_string(it.id) + ") " + it.pin->name;
		const float sc = it.dist <= 60.0f ? 1.0f : std::max(0.78f, 1.0f - (it.dist - 60.0f) / 1500.0f);       // de 60 m a ~400 m encolhe ate 78%
		const float fsN = 17.0f * k * sc, fsD = 13.5f * k * sc;
		const float pad = 10.0f * k * sc, ir = 10.0f * k * sc;       // ir = raio do pino
		const ImVec2 tn = r->calculateTextSize(label, fsN), td = r->calculateTextSize(dist, fsD);
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
		r->drawText(ImVec2(tx, ty), ImColor(250, 250, 252, (int)(255.0f * al)), label, true, fsN);
		r->drawText(ImVec2(tx, ty + tn.y), ImColor(168, 170, 178, (int)(255.0f * al)), dist, false, fsD);
	}

	// ---- setas na borda da tela: marcacao fora da tela / atras, a QUALQUER distancia. A seta gira pela borda inteira seguindo a direcao ----
	std::vector<ImVec2> used;
	const float R = 19.0f * k, m = R + 30.0f * k;
	for (const Item& it : items)
	{
		if (!it.off) continue;
		const float adx = std::fabs(it.dir.x) < 1e-4f ? 1e-4f : std::fabs(it.dir.x), ady = std::fabs(it.dir.y) < 1e-4f ? 1e-4f : std::fabs(it.dir.y);
		const float hx = scr.x * 0.5f - m, hy = scr.y * 0.5f - m;
		const float t = std::min(hx / adx, hy / ady);
		ImVec2 P(cen.x + it.dir.x * t, cen.y + it.dir.y * t);
		const ImVec2 perp(-it.dir.y, it.dir.x);
		for (int tries = 0; tries < 6; tries++)                           // dois no mesmo lugar: o seguinte desliza pela borda
		{
			bool hit = false;
			for (const ImVec2& u : used) if ((u.x - P.x) * (u.x - P.x) + (u.y - P.y) * (u.y - P.y) < (R * 2.2f) * (R * 2.2f)) { hit = true; break; }
			if (!hit) break;
			P.x = std::min(std::max(P.x + perp.x * R * 2.4f, m), scr.x - m);
			P.y = std::min(std::max(P.y + perp.y * R * 2.4f, m), scr.y - m);
		}
		used.push_back(P);

		// ---- disco do ID: fundo azul em degrade + moldura dupla + brilho pulsante (sem o fundo preto) ----
		const float tt = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 2000) / 2000.0f;
		const float pul = 0.5f - 0.5f * std::cos(tt * 6.2831853f);                  // 0..1..0 a cada 2 s
		auto circle = [&](float rad, const ImColor& col, bool fillIt, float lw)
		{
			constexpr int NC = 32;
			ImVec2 q[NC];
			for (int a = 0; a < NC; a++) { const float an = (float)a * 6.2831853f / (float)NC; q[a] = ImVec2(P.x + std::cos(an) * rad, P.y + std::sin(an) * rad); }
			if (fillIt) r->drawConvexPolyFilled(q, NC, col); else r->drawPolyline(q, NC, col, true, lw);
		};
		circle(R + (5.0f + 7.0f * pul) * k, ImColor(80, 170, 255, (int)(70.0f * (1.0f - pul))), false, 2.2f * k);     // onda que se expande
		circle(R + 3.0f * k, ImColor(70, 150, 255, 60 + (int)(50.0f * pul)), true, 0.0f);                              // brilho externo
		// seta: triangulo claro com contorno azul, apontando para a direcao
		const ImVec2 tip(P.x + it.dir.x * (R + 17.0f * k), P.y + it.dir.y * (R + 17.0f * k));
		const ImVec2 b1(P.x + it.dir.x * (R + 2.0f * k) + perp.x * 10.0f * k, P.y + it.dir.y * (R + 2.0f * k) + perp.y * 10.0f * k);
		const ImVec2 b2(P.x + it.dir.x * (R + 2.0f * k) - perp.x * 10.0f * k, P.y + it.dir.y * (R + 2.0f * k) - perp.y * 10.0f * k);
		r->drawTriangle(tip, b1, b2, ImColor(40, 120, 235, 255), true);
		r->drawTriangle(tip, b1, b2, ImColor(255, 255, 255, 245), false, 1.6f * k);
		circle(R, ImColor(18, 52, 120, 255), true, 0.0f);                                                              // degrade: 3 discos
		circle(R * 0.80f, ImColor(34, 92, 190, 255), true, 0.0f);
		circle(R * 0.52f, ImColor(66, 140, 235, 150), true, 0.0f);
		circle(R, ImColor(255, 255, 255, 250), false, 2.8f * k);                                                       // moldura externa branca
		circle(R - 4.2f * k, ImColor(120, 200, 255, 220), false, 1.3f * k);                                            // moldura interna ciano
		// brilho no topo (reflexo)
		r->drawLine(ImVec2(P.x - R * 0.42f, P.y - R * 0.62f), ImVec2(P.x + R * 0.18f, P.y - R * 0.78f), ImColor(255, 255, 255, 120), 2.0f * k);
		const std::string idt = "(" + std::to_string(it.id) + ")";
		const float fi = 14.0f * k;
		const ImVec2 ti = r->calculateTextSize(idt, fi);
		r->drawText(ImVec2(P.x - ti.x * 0.5f + 0.8f * k, P.y - ti.y * 0.5f + 0.8f * k), ImColor(0, 20, 60, 200), idt, false, fi);   // sombra
		r->drawText(ImVec2(P.x - ti.x * 0.5f, P.y - ti.y * 0.5f), ImColor(255, 255, 255, 255), idt, true, fi);
		// nome + distancia do lado de dentro da tela
		std::string nm = it.pin->name;
		if (nm.size() > 22) nm = nm.substr(0, 21) + "...";
		const std::string ds = Dist(it.dist);
		const float fn = 12.0f * k, fd = 11.0f * k;
		const ImVec2 tn2 = r->calculateTextSize(nm, fn), td2 = r->calculateTextSize(ds, fd);
		const float bw = std::max(tn2.x, td2.x) + 12.0f * k, bh = tn2.y + td2.y + 8.0f * k;
		float bx = P.x - bw * 0.5f, by = P.y > cen.y ? P.y - R - bh - 4.0f * k : P.y + R + 4.0f * k;
		bx = std::min(std::max(bx, 4.0f * k), scr.x - bw - 4.0f * k);
		r->drawRoundedRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), ImColor(14, 30, 70, 205), 7.0f * k, true);
		r->drawRoundedRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), ImColor(120, 200, 255, 150), 7.0f * k, false, 1.2f);
		r->drawText(ImVec2(bx + (bw - tn2.x) * 0.5f, by + 4.0f * k), ImColor(250, 250, 252, 255), nm, false, fn);
		r->drawText(ImVec2(bx + (bw - td2.x) * 0.5f, by + 4.0f * k + tn2.y), ImColor(168, 170, 178, 255), ds, false, fd);
	}
}
