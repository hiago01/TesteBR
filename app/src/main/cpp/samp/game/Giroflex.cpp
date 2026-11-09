#include "Giroflex.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "Coronas.h"
#include "../net/netgame.h"
#include "../net/vehiclepool.h"
#include "../net/localplayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern CGame*    pGame;
extern CNetGame* pNetGame;
extern uintptr_t g_libGTASA;

namespace
{
	constexpr int   kMaxVeh    = 3;       // veiculos desenhados por frame (os mais proximos)
	constexpr int   kMaxLights = 40;      // coronas por frame (o jogo tem 64 no total)
	constexpr float kMaxDist   = 110.0f;  // metros

	struct Light { float cx, cy, cz; bool useCol; uint8_t r, g, b, a; };
	struct Step  { int dur; std::vector<char> bits; };
	struct Group
	{
		int   dir = 1;                      // 0 = frente, 1 = todos os lados, 2 = tras
		float ox = 0, oy = 0, oz = 0, spacing = 0, curve = 0;
		int   n = 0;
		uint8_t r = 255, g = 0, b = 0, a = 255;
		bool  always = false;
		int   startStep = 0;
		std::vector<Light> lights;
		std::vector<Step>  steps;
		int   total = 0;                    // duracao do ciclo (ms)
	};
	using VehCfg = std::vector<Group>;

	std::mutex                       g_mtx;
	std::unordered_map<int, VehCfg>  g_cfg;
	bool                             g_loaded = false;
	bool                             g_on[MAX_VEHICLES] = {};

	using Clock = std::chrono::steady_clock;

	void Load()
	{
		g_loaded = true;
		g_cfg.clear();
		if (!g_pszStorage || !g_pszStorage[0]) return;
		const std::string path = std::string(g_pszStorage) + "SAMP/giroflex.txt";
		FILE* f = fopen(path.c_str(), "r");
		if (!f) { FLog("[GIROFLEX] %s nao encontrado: desligado", path.c_str()); return; }

		char line[512];
		int model = -1, nModels = 0, nGroups = 0;
		Group cur; bool inG = false;
		while (fgets(line, sizeof(line), f))
		{
			if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
			char t = line[0];
			if (t == 'V') { model = atoi(line + 1); g_cfg[model]; nModels++; }
			else if (t == 'G' && model >= 0)
			{
				cur = Group(); inG = true;
				int always = 0; float nc = 0; int ps = 0; int r, g, b, a;
				if (sscanf(line + 1, "%d %f %f %f %f %f %d %d %d %d %d %d %f %d", &cur.dir, &cur.ox, &cur.oy, &cur.oz,
					&cur.spacing, &cur.curve, &cur.n, &r, &g, &b, &a, &always, &nc, &ps) < 14 || cur.n < 1 || cur.n > 32) { inG = false; continue; }
				cur.r = (uint8_t)r; cur.g = (uint8_t)g; cur.b = (uint8_t)b; cur.a = (uint8_t)a;
				cur.always = always != 0; cur.startStep = ps;
			}
			else if (t == 'P' && inG)
			{
				Light l{}; int d, uc, r, g, b, a;
				if (sscanf(line + 1, "%f %f %f %d %d %d %d %d %d", &l.cx, &l.cy, &l.cz, &d, &uc, &r, &g, &b, &a) == 9)
				{ l.useCol = uc != 0; l.r = (uint8_t)r; l.g = (uint8_t)g; l.b = (uint8_t)b; l.a = (uint8_t)a; cur.lights.push_back(l); }
			}
			else if (t == 'S' && inG)
			{
				Step s; char bits[64] = {};
				if (sscanf(line + 1, "%d %63s", &s.dur, bits) == 2 && s.dur > 0 && (int)strlen(bits) == cur.n)
				{ for (int i = 0; i < cur.n; i++) s.bits.push_back(bits[i] == '1'); cur.total += s.dur; cur.steps.push_back(s); }
			}
			else if (t == 'E' && inG)
			{
				inG = false;
				if ((int)cur.lights.size() == cur.n && !cur.steps.empty() && cur.total > 0) { g_cfg[model].push_back(cur); nGroups++; }
			}
		}
		fclose(f);
		FLog("[GIROFLEX] %d modelo(s), %d grupo(s) de luzes carregados de %s", nModels, nGroups, path.c_str());
	}

	float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
}

void Giroflex_Reset()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	memset(g_on, 0, sizeof(g_on));
}

void Giroflex_OnServerPacket(uint16_t vehicleId, bool on)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	if (vehicleId < MAX_VEHICLES) g_on[vehicleId] = on;
}

void Giroflex_Render()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	if (!g_loaded) Load();
	if (g_cfg.empty() || !pNetGame) return;
	CVehiclePool* pool = pNetGame->GetVehiclePool();
	if (!pool) return;

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector cam = TheCamera.GetPosition();

	struct Cand { int id; float d2; const VehCfg* cfg; CVehicleGTA* v; };
	Cand cands[kMaxVeh]; int nc = 0;

	for (int id = 0; id < MAX_VEHICLES; id++)
	{
		CVehicle* pv = pool->GetAt((VEHICLEID)id);
		if (!pv || !pv->m_pVehicle) continue;
		CVehicleGTA* gv = pv->m_pVehicle;
		auto it = g_cfg.find((int)gv->m_nModelIndex);
		if (it == g_cfg.end()) continue;
		if (!gv->m_matrix) continue;
		bool any = g_on[id];
		if (!any) for (const Group& gr : it->second) if (gr.always) { any = true; break; }
		if (!any) continue;
		const CVector p = gv->GetPosition();
		const float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
		const float d2 = dx * dx + dy * dy + dz * dz;
		if (d2 > kMaxDist * kMaxDist) continue;

		Cand c{ id, d2, &it->second, gv };
		if (nc < kMaxVeh) cands[nc++] = c;
		else
		{
			int worst = 0;
			for (int i = 1; i < nc; i++) if (cands[i].d2 > cands[worst].d2) worst = i;
			if (d2 < cands[worst].d2) cands[worst] = c;
		}
	}
	if (nc == 0) return;

	const uint32_t nowMs = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
	int budget = kMaxLights;

	for (int ci = 0; ci < nc && budget > 0; ci++)
	{
		const Cand& c = cands[ci];
		CVehicleGTA* gv = c.v;
		const CVector pos = gv->GetPosition();
		const CVector rgt = gv->m_matrix->GetRight(), fwd = gv->m_matrix->GetForward(), upv = gv->m_matrix->GetUp();
		const bool sirenOn = g_on[c.id];

		for (size_t gi = 0; gi < c.cfg->size() && budget > 0; gi++)
		{
			const Group& gr = (*c.cfg)[gi];
			if (!gr.always && !sirenOn) continue;

			// passo atual do padrao
			uint32_t t = nowMs % (uint32_t)gr.total;
			size_t si = 0; uint32_t acc = 0;
			for (; si < gr.steps.size(); si++) { if (t < acc + (uint32_t)gr.steps[si].dur) break; acc += gr.steps[si].dur; }
			if (si >= gr.steps.size()) si = gr.steps.size() - 1;
			si = (si + (size_t)std::max(0, gr.startStep)) % gr.steps.size();
			const Step& st = gr.steps[si];

			for (int k = 0; k < gr.n && budget > 0; k++)
			{
				if (!st.bits[k]) continue;
				const Light& L = gr.lights[k];
				const float u = (float)k - (float)(gr.n - 1) * 0.5f;       // indice centrado
				CVector lp(gr.ox + u * gr.spacing + L.cx, gr.oy + L.cy + gr.curve * u * u, gr.oz + L.cz);

				// visibilidade por direcao: frente (0) / tras (2) somem quando a camera esta do outro lado
				float vis = 1.0f;
				if (gr.dir != 1)
				{
					const CVector w(pos.x + rgt.x * lp.x + fwd.x * lp.y + upv.x * lp.z,
					                pos.y + rgt.y * lp.x + fwd.y * lp.y + upv.y * lp.z,
					                pos.z + rgt.z * lp.x + fwd.z * lp.y + upv.z * lp.z);
					float tx = cam.x - w.x, ty = cam.y - w.y, tz = cam.z - w.z;
					const float len = sqrtf(tx * tx + ty * ty + tz * tz);
					if (len > 0.001f)
					{
						const float d = (fwd.x * tx + fwd.y * ty + fwd.z * tz) / len;
						vis = Clamp01(((gr.dir == 0 ? d : -d) + 0.15f) / 0.5f);
					}
				}
				if (vis <= 0.01f) continue;

				const uint8_t r = L.useCol ? L.r : gr.r, g = L.useCol ? L.g : gr.g, b = L.useCol ? L.b : gr.b;
				const uint8_t a0 = L.useCol ? L.a : gr.a;
				const uint8_t a = (uint8_t)std::min(255.0f, (float)a0 * 1.4f * vis);   // coronas do jogo ficam mais fracas que o LED do mod
				const float radius = (gr.n == 1) ? 0.45f : (gr.spacing < 0.3f ? 0.28f : 0.38f);
				const uint32_t cid = 0xB1000000u | ((uint32_t)c.id << 12) | ((uint32_t)gi << 8) | (uint32_t)k;

				CCoronas::RegisterCorona(cid, gv, r, g, b, a, &lp, radius, 120.0f,
					eCoronaType::CORONATYPE_SHINYSTAR, eCoronaFlareType::FLARETYPE_NONE,
					false, false, 0, 0.0f, false, 0, 0, 60.0f, false, false);
				budget--;
			}
		}
	}
}

std::string Giroflex_Command(const std::string& args)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	if (args == "recarregar") { Load(); return "[GIROFLEX] recarregado: " + std::to_string(g_cfg.size()) + " modelo(s)"; }
	if (args == "teste" || args == "teste off")
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return "[GIROFLEX] sem conexao";
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		CPlayerPed* ped = lp ? lp->GetPlayerPed() : nullptr;
		CVehiclePool* pool = pNetGame->GetVehiclePool();
		CVehicleGTA* gtaV = ped ? ped->GetGtaVehicle() : nullptr;
		if (!pool || !gtaV) return "[GIROFLEX] entre num veiculo";
		VEHICLEID vid = pool->FindIDFromGtaPtr(gtaV);
		if (vid >= MAX_VEHICLES) return "[GIROFLEX] veiculo nao encontrado";
		g_on[vid] = (args == "teste");
		return std::string("[GIROFLEX] teste local ") + (g_on[vid] ? "LIGADO" : "desligado") + " (veiculo " + std::to_string(vid) + ")";
	}
	if (!g_loaded) Load();
	std::string s = "[GIROFLEX] modelos com luzes: ";
	for (auto& kv : g_cfg) s += std::to_string(kv.first) + "(" + std::to_string(kv.second.size()) + "g) ";
	return s + "| /giroc teste | teste off | recarregar";
}
