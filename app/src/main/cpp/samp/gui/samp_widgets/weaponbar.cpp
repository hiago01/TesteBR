#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/armhook/patch.h"
#include "../../util/CUtil.h"
#include "../../game/Models/ModelInfo.h"
#include "weaponwheel_logic.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;

// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

// CWeaponInfo::GetWeaponInfo(type, skill) -- definido em game/playerped.cpp
extern uintptr_t GetWeaponInfo(int iWeapon, int iSkill);

using namespace WeaponWheel;

// =====================================================================================
//  Dados (nomes curtos so aparecem se a textura do icone nao for encontrada)
// =====================================================================================

static const char* const kWeaponNames[47] = {
	"PUNHO", "SOCO-ING", "GOLF", "CASSETE", "FACA", "TACO", "PA", "SINUCA", "KATANA", "SERRA",
	"DILDO", "DILDO", "VIBRA", "VIBRA", "FLORES", "BENGALA", "GRANADA", "GAS", "MOLOTOV", "-",
	"-", "-", "COLT45", "SILENC.", "DESERT", "SHOTGUN", "SAWN-OFF", "SPAS12", "UZI", "MP5",
	"AK-47", "M4", "TEC-9", "RIFLE", "SNIPER", "RPG", "HS-ROCKET", "LANCA", "MINIGUN", "C4",
	"DETONAD.", "SPRAY", "EXTINTOR", "CAMERA", "VISAO", "TERMICO", "PARAQ."
};

// Nome do modelo (dff) de cada arma; so usado se o modelinfo nao estiver disponivel.
static const char* const kFallbackModel[47] = {
	nullptr, "brassknuckle", "golfclub", "nitestick", "knifecur", "bat", "shovel", "poolcue", "katana", "chnsaw",
	"gun_dildo1", "gun_dildo2", "gun_vibe1", "gun_vibe2", "flowera", "gun_cane", "grenade", "teargas", "molotov", nullptr,
	nullptr, nullptr, "colt45", "silenced", "desert_eagle", "chromegun", "sawnoff", "shotgspa", "micro_uzi", "mp5lng",
	"ak47", "m4", "tec9", "cuntgun", "sniper", "rocketla", "heatseek", "flame", "minigun", "satchel",
	"bomb", "spraycan", "fire_ex", "camera", "nvgoggles", "irgoggles", "gun_para"
};

// =====================================================================================
//  Tema / proporcoes (tudo relativo a altura da tela, para escalar em qualquer resolucao)
// =====================================================================================

namespace
{
	const ImColor kGreen (50, 235, 30, 255);
	const ImColor kWhite (255, 255, 255, 255);

	constexpr float kAnimSeconds = 0.12f;      // abrir/fechar
	constexpr int   kArcSteps    = 48;

	std::atomic<int> s_holdMs(WeaponBar::kDefaultHoldMs);

	inline int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	inline ImVec2 Polar(const ImVec2& c, float a, float rad)
	{
		return ImVec2(c.x + std::sin(a) * rad, c.y - std::cos(a) * rad);
	}

	inline ImColor Col(int r, int g, int b, int a, float k)
	{
		return ImColor(r, g, b, std::clamp((int)((float)a * k), 0, 255));
	}

	// Roda: centro e raios (referencia: print 1870x841 -> centro ~ (0.68w, 0.48h), raio ~ 0.26h)
	struct Geo { ImVec2 c; float rIn, rOut, rCancel; };

	Geo WheelGeo(const ImVec2& screen)
	{
		Geo g;
		g.c       = ImVec2(screen.x * 0.68f, screen.y * 0.48f);
		g.rOut    = screen.y * 0.262f;
		g.rIn     = g.rOut * 0.50f;
		g.rCancel = g.rOut * 1.42f;   // anel externo (leque de sub-celulas); alem disso = cancelar
		return g;
	}

	// Botao no canto inferior direito (referencia: capsula ~12% x 7.8% da tela, margem ~2.4% / 3.9%).
	void ButtonRect(const ImVec2& screen, ImVec2& a, ImVec2& b)
	{
		const float h = screen.y * 0.078f;
		const float w = std::max(screen.x * 0.125f, h * 1.9f);
		b = ImVec2(screen.x * 0.976f, screen.y * 0.961f);
		a = ImVec2(b.x - w, b.y - h);
	}
}

// =====================================================================================
//  Icones originais (textura "<modelo>icon" no banco gta3; punho = "fist" no banco txd)
// =====================================================================================

namespace
{
	struct IconEntry
	{
		RwTexture* tex = nullptr;
		uint8_t    tries = 0;
		uint32_t   nextTry = 0;
	};

	IconEntry s_icons[47];
	uint32_t  s_frame = 0;

	RwTexture* LoadFromDbs(const char* texName, const char* const* dbs, int ndb)
	{
		for (int i = 0; i < ndb; i++)
		{
			RwTexture* t = CUtil::LoadTextureFromDB(dbs[i], texName);
			if (t && t->raster) return t;
		}
		return nullptr;
	}

	// Mesma resolucao do CWidgetPlayerInfo::DrawWeaponIcon do jogo:
	//   modelo = CWeaponInfo::GetWeaponInfo(tipo, 1)->m_nModelId1 ; textura = nome do modelo + "icon"
	bool ResolveIconName(unsigned type, char* out, size_t n)
	{
		if (type == 0)
		{
			snprintf(out, n, "fist");
			return true;
		}

		const uintptr_t info = GetWeaponInfo((int)type, 1);
		const int model = info ? *(const int*)(info + 0xC) : -1;   // CWeaponInfo::m_nModelId1

		if (model > 0 && model < (int)CModelInfo::NUM_MODEL_INFOS)
		{
			const CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[model];
			if (mi && mi->m_modelName[0])
			{
				snprintf(out, n, "%sicon", mi->m_modelName);
				return true;
			}
		}

		if (type < 47 && kFallbackModel[type])
		{
			snprintf(out, n, "%sicon", kFallbackModel[type]);
			return true;
		}
		return false;
	}

	RwTexture* GetWeaponIcon(unsigned type)
	{
		if (type >= 47) type = 0;

		IconEntry& e = s_icons[type];
		if (e.tex) return e.tex;
		if (e.tries >= 4 || s_frame < e.nextTry) return nullptr;

		e.tries++;
		e.nextTry = s_frame + 240;

		char name[48];
		if (!ResolveIconName(type, name, sizeof(name))) { e.tries = 4; return nullptr; }

		static const char* const kDbsGun[]  = { "gta3", "txd", "gta_int", "samp", "player", "mobile" };
		static const char* const kDbsFist[] = { "txd", "gta3", "gta_int", "samp", "player", "mobile" };
		const char* const* dbs = (type == 0) ? kDbsFist : kDbsGun;
		const int ndb = 6;

		RwTexture* t = LoadFromDbs(name, dbs, ndb);
		if (!t)
		{
			std::string lower = name;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			if (lower != name) t = LoadFromDbs(lower.c_str(), dbs, ndb);
		}

		e.tex = t;
		return t;
	}
}

// =====================================================================================
//  Regras de estado do jogo
// =====================================================================================

static CPedGTA* GetLocalPed()
{
	if (!pNetGame || !pNetGame->GetPlayerPool()) return nullptr;
	CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
	if (!lp) return nullptr;
	CPlayerPed* pp = lp->GetPlayerPed();
	return pp ? pp->m_pPed : nullptr;
}

// Slot 0 (punhos) sempre existe. Os demais so se tiver a arma (corpo a corpo conta sempre;
// arma de fogo so com municao). Mesma regra da barra antiga.
static bool SlotOwned(CPedGTA* ped, int slot)
{
	const CWeapon& w = ped->m_aWeapons[slot];
	if (slot == 0) return true;
	if (w.dwType == 0) return false;
	return w.dwType <= 15 || w.dwAmmo > 0;
}

static uint32_t OwnedMask(CPedGTA* ped)
{
	uint32_t m = 0;
	for (int s = 0; s < kNumSlots; s++)
		if (SlotOwned(ped, s)) m |= (1u << s);
	return m;
}

// Estados em que o seletor nao aparece e a roda fecha: morto, em veiculo, jogo pausado,
// teclado/dialogo/lista de jogadores/spawn/splash visiveis.
static bool IsBlocked(CPedGTA* ped)
{
	if (!ped) return true;
	if (ped->m_fHealth <= 0.0f) return true;
	if (ped->IsInVehicle()) return true;
	if (pGame && pGame->IsGamePaused()) return true;
	if (pUI)
	{
		if (pUI->keyboard()      && pUI->keyboard()->visible())      return true;
		if (pUI->dialog()        && pUI->dialog()->visible())        return true;
		if (pUI->playertablist() && pUI->playertablist()->visible()) return true;
		if (pUI->spawn()         && pUI->spawn()->visible())         return true;
		if (pUI->splashscreen()  && pUI->splashscreen()->visible())  return true;
	}
	return false;
}

// Municao: armas de fogo (22..34) "total-pente"; lancadores, minigun, spray, camera: so o total;
// corpo a corpo: sem texto.
static bool BuildAmmoText(const CWeapon& w, char* out, size_t n)
{
	const unsigned t = w.dwType;
	if (t < 16 || t > 43 || t == 40) return false;

	unsigned total = w.dwAmmo;
	if (total > 9999) total = 9999;

	if (t >= 22 && t <= 34)
	{
		unsigned clip = w.dwAmmoInClip;
		if (clip > total) clip = total;
		snprintf(out, n, "%u-%u", total, clip);
	}
	else
	{
		snprintf(out, n, "%u", total);
	}
	return true;
}

// =====================================================================================
//  Primitivas de desenho (sem alocacao: so arrays na pilha)
// =====================================================================================

static void FillDiscN(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col, int n)
{
	ImVec2 p[kArcSteps];
	if (n > kArcSteps) n = kArcSteps;
	for (int i = 0; i < n; i++) p[i] = Polar(c, (float)i * (kTwoPi / (float)n), rad);
	r->drawConvexPolyFilled(p, n, col);
}

static void StrokeCircle(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col, float th)
{
	ImVec2 p[kArcSteps];
	for (int i = 0; i < kArcSteps; i++) p[i] = Polar(c, (float)i * (kTwoPi / (float)kArcSteps), rad);
	r->drawPolyline(p, kArcSteps, col, true, th);
}

static void StrokeArc(ImGuiRenderer* r, const ImVec2& c, float rad, float a0, float a1, const ImColor& col, float th)
{
	int n = (int)std::ceil((a1 - a0) / 0.10f);
	n = std::clamp(n, 2, kArcSteps - 1);
	ImVec2 p[kArcSteps];
	for (int i = 0; i <= n; i++) p[i] = Polar(c, a0 + (a1 - a0) * ((float)i / (float)n), rad);
	r->drawPolyline(p, n + 1, col, false, th);
}

// Setor anular preenchido [a0,a1] x [r0,r1], em fatias convexas (sentido horario).
static void FillAnnularSector(ImGuiRenderer* r, const ImVec2& c, float r0, float r1, float a0, float a1, const ImColor& col)
{
	int n = (int)std::ceil((a1 - a0) / 0.105f);
	if (n < 1) n = 1;
	for (int i = 0; i < n; i++)
	{
		const float aa = a0 + (a1 - a0) * ((float)i / (float)n);
		const float ab = a0 + (a1 - a0) * ((float)(i + 1) / (float)n);
		ImVec2 q[4] = { Polar(c, aa, r0), Polar(c, aa, r1), Polar(c, ab, r1), Polar(c, ab, r0) };
		r->drawConvexPolyFilled(q, 4, col);
	}
}

// Capsula (retangulo com as pontas semicirculares), convexa e em sentido horario.
static void FillCapsule(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const ImColor& col, bool outline, const ImColor& line, float th)
{
	const float h  = b.y - a.y;
	const float hr = h * 0.5f;
	const float my = a.y + hr;
	const ImVec2 cr(b.x - hr, my), cl(a.x + hr, my);

	ImVec2 p[26];
	for (int i = 0; i < 13; i++) p[i]      = Polar(cr, kPi * ((float)i / 12.0f), hr);
	for (int i = 0; i < 13; i++) p[13 + i] = Polar(cl, kPi + kPi * ((float)i / 12.0f), hr);

	r->drawConvexPolyFilled(p, 26, col);
	if (outline) r->drawPolyline(p, 26, line, true, th);
}

// Icone original (mantem proporcao) dentro da caixa centrada em [cx,cy].
static void DrawIcon(ImGuiRenderer* r, RwTexture* tex, float cx, float cy, float boxW, float boxH, int alpha)
{
	float iw = (float)tex->raster->width;
	float ih = (float)tex->raster->height;
	if (iw < 1.0f || ih < 1.0f) { iw = ih = 1.0f; }

	const float k = std::min(boxW / iw, boxH / ih);
	const float dw = iw * k, dh = ih * k;

	r->drawImage(ImVec2(cx - dw * 0.5f, cy - dh * 0.5f), ImVec2(cx + dw * 0.5f, cy + dh * 0.5f),
				 (ImTextureID)tex->raster, ImColor(255, 255, 255, alpha));
}

// Icone da arma do slot (ou nome curto se nao houver textura).
static void DrawWeaponGlyph(ImGuiRenderer* r, CPedGTA* ped, int slot, float cx, float cy, float boxW, float boxH, float k)
{
	const unsigned t = ((unsigned)ped->m_aWeapons[slot].dwType < 47u) ? (unsigned)ped->m_aWeapons[slot].dwType : 0u;
	RwTexture* tex = GetWeaponIcon(t);
	if (tex)
	{
		DrawIcon(r, tex, cx, cy, boxW, boxH, (int)(255.0f * k));
	}
	else
	{
		const float fs = boxH * 0.34f;
		const std::string nm = kWeaponNames[t];            // curto: cabe no SSO, sem heap
		const ImVec2 ts = r->calculateTextSize(nm, fs);
		r->drawText(ImVec2(cx - ts.x * 0.5f, cy - fs * 0.5f), Col(255, 255, 255, 255, k), nm, true, fs);
	}
}

static void DrawTextCentered(ImGuiRenderer* r, const char* txt, float cx, float y, float fs, float maxW, const ImColor& col)
{
	const std::string s = txt;                              // curto: cabe no SSO, sem heap
	ImVec2 ts = r->calculateTextSize(s, fs);
	if (ts.x > maxW && ts.x > 0.0f)
	{
		fs *= maxW / ts.x;
		ts = r->calculateTextSize(s, fs);
	}
	r->drawText(ImVec2(cx - ts.x * 0.5f, y), col, s, true, fs);
}

// =====================================================================================
//  Botao (roda fechada)
// =====================================================================================

static void DrawButton(ImGuiRenderer* r, CPedGTA* ped, const ImVec2& screen, bool engaged)
{
	ImVec2 a, b;
	ButtonRect(screen, a, b);
	const float h = b.y - a.y;

	const int active = (ped->m_nActiveWeaponSlot < kNumSlots) ? ped->m_nActiveWeaponSlot : 0;
	const CWeapon& w = ped->m_aWeapons[active];

	FillCapsule(r, a, b, ImColor(0, 0, 0, engaged ? 185 : 130), true,
				engaged ? kGreen : ImColor(255, 255, 255, 70), std::max(1.5f, h * 0.025f));

	// icone a esquerda
	DrawWeaponGlyph(r, ped, active, a.x + h * 0.95f, a.y + h * 0.44f, h * 1.15f, h * 0.60f, 1.0f);

	// sublinhado verde sob o icone
	r->drawLine(ImVec2(a.x + h * 0.40f, b.y - h * 0.16f), ImVec2(a.x + h * 1.50f, b.y - h * 0.16f), kGreen, std::max(2.0f, h * 0.05f));

	// municao embaixo a direita
	char ammo[24];
	if (BuildAmmoText(w, ammo, sizeof(ammo)))
	{
		const float fs = h * 0.27f;
		const std::string s = ammo;
		const ImVec2 ts = r->calculateTextSize(s, fs);
		r->drawText(ImVec2(b.x - h * 0.55f - ts.x, b.y - h * 0.14f - ts.y), kWhite, s, true, fs);
	}
}

// =====================================================================================
//  Roda
// =====================================================================================

// Slot escolhido por um Pick (ou -1 = nada / cancelar).
static int SlotForPick(const Pick& pk, const Ring& ring, uint32_t owned, int active)
{
	if (pk.zone == Zone::Center) return 0;
	if (pk.zone == Zone::Cancel || pk.seg < 0 || pk.seg >= ring.count) return -1;

	const int cat = ring.cat[pk.seg];
	if (pk.zone == Zone::Sub)
	{
		int list[3];
		const int n = OwnedInCat(owned, cat, list);
		return (pk.sub >= 0 && pk.sub < n) ? list[pk.sub] : -1;
	}
	return DefaultSlotInCat(owned, cat, active);
}

static void DrawWheel(ImGuiRenderer* r, CPedGTA* ped, const ImVec2& screen, const Ring& ring,
					  const int* subCount, const int* defSlot, uint32_t owned, const Geo& g0,
					  const Pick& pick, int active, float anim)
{
	const float A  = anim * anim * (3.0f - 2.0f * anim);        // smoothstep
	const float sc = 0.88f + 0.12f * A;                         // "cresce" ao abrir
	const float rIn = g0.rIn * sc, rOut = g0.rOut * sc, rCan = g0.rCancel * sc;
	const ImVec2 c = g0.c;
	const int n = ring.count;
	const float u = screen.y / 841.0f;                          // 1 px da imagem de referencia

	// fundo escuro translucido + contornos
	FillDiscN(r, c, rOut, Col(8, 8, 8, 150, A), kArcSteps);
	FillDiscN(r, c, rIn,  Col(0, 0, 0, 70, A),  kArcSteps);
	StrokeCircle(r, c, rOut, Col(255, 255, 255, 80, A), std::max(1.5f, 2.0f * u));
	StrokeCircle(r, c, rIn,  Col(255, 255, 255, 70, A), std::max(1.5f, 2.0f * u));

	const float segSpan = (n > 0) ? (kTwoPi / (float)n) : kTwoPi;

	// divisorias entre segmentos
	if (n > 1)
	{
		for (int i = 0; i < n; i++)
		{
			const float a = SegmentCenter(i, n) + segSpan * 0.5f;
			r->drawLine(Polar(c, a, rIn), Polar(c, a, rOut), Col(255, 255, 255, 60, A), std::max(1.5f, 2.0f * u));
		}
	}

	// segmento destacado + arco verde por fora (so a categoria selecionada fica verde)
	if ((pick.zone == Zone::Ring || pick.zone == Zone::Sub) && pick.seg >= 0 && pick.seg < n)
	{
		const float a0 = SegmentCenter(pick.seg, n) - segSpan * 0.5f;
		const float a1 = SegmentCenter(pick.seg, n) + segSpan * 0.5f;
		FillAnnularSector(r, c, rIn, rOut, a0, a1, Col(255, 255, 255, 34, A));
		StrokeArc(r, c, rOut + 5.0f * u, a0 + 0.04f, a1 - 0.04f, Col(50, 235, 30, 255, A), std::max(3.0f, 6.0f * u));
	}

	// icones + municao por segmento
	const float midR = (rIn + rOut) * 0.5f;
	const float box  = (rOut - rIn) * 0.78f;
	for (int i = 0; i < n; i++)
	{
		const int slot = defSlot[i];
		if (slot < 0) continue;

		const ImVec2 p = Polar(c, SegmentCenter(i, n), midR - (rOut - rIn) * 0.08f);
		const bool sel = ((pick.zone == Zone::Ring || pick.zone == Zone::Sub) && pick.seg == i);

		DrawWeaponGlyph(r, ped, slot, p.x, p.y, box, box * 0.62f, A);

		char ammo[24];
		if (BuildAmmoText(ped->m_aWeapons[slot], ammo, sizeof(ammo)))
			DrawTextCentered(r, ammo, p.x, p.y + box * 0.30f, box * 0.27f, box * 1.05f,
							 sel ? Col(50, 235, 30, 255, A) : Col(255, 255, 255, 255, A));

		// pontinhos: a categoria tem mais de uma arma (arraste pra fora para escolher)
		if (subCount[i] > 1)
		{
			const int m = subCount[i];
			for (int k = 0; k < m; k++)
			{
				const float da = ((float)k - (float)(m - 1) * 0.5f) * 0.075f;
				FillDiscN(r, Polar(c, SegmentCenter(i, n) + da, rOut - 7.0f * u), std::max(1.5f, 2.6f * u),
						  Col(255, 255, 255, 200, A), 10);
			}
		}
	}

	// centro = punhos
	{
		const bool sel = (pick.zone == Zone::Center);
		if (sel) StrokeCircle(r, c, rIn, Col(50, 235, 30, 255, A), std::max(3.0f, 5.0f * u));
		DrawWeaponGlyph(r, ped, 0, c.x, c.y, rIn * 1.15f, rIn * 1.0f, A);
	}

	// leque de sub-celulas da categoria sob o dedo (so se tiver mais de uma arma)
	if ((pick.zone == Zone::Ring || pick.zone == Zone::Sub) && pick.seg >= 0 && pick.seg < n && subCount[pick.seg] > 1)
	{
		const int m = subCount[pick.seg];
		int list[3];
		OwnedInCat(owned, ring.cat[pick.seg], list);

		const float rho  = (rOut + rCan) * 0.5f;
		const float cellR = (rCan - rOut) * 0.40f;

		for (int k = 0; k < m; k++)
		{
			const ImVec2 p = Polar(c, SubCenter(pick.seg, n, k, m), rho);
			const bool sel = (pick.zone == Zone::Sub && pick.sub == k);

			FillDiscN(r, p, cellR, Col(8, 8, 8, sel ? 215 : 170, A), 32);
			StrokeCircle(r, p, cellR, sel ? Col(50, 235, 30, 255, A) : Col(255, 255, 255, 110, A), std::max(2.0f, (sel ? 3.5f : 2.0f) * u));
			DrawWeaponGlyph(r, ped, list[k], p.x, p.y - cellR * 0.12f, cellR * 1.45f, cellR * 0.95f, A);

			char ammo[24];
			if (BuildAmmoText(ped->m_aWeapons[list[k]], ammo, sizeof(ammo)))
				DrawTextCentered(r, ammo, p.x, p.y + cellR * 0.38f, cellR * 0.38f, cellR * 1.6f, Col(255, 255, 255, 255, A));
		}
	}

	// dica curta embaixo da roda
	{
		const char* hint = "Solte para equipar";
		DrawTextCentered(r, hint, c.x, c.y + rCan + 8.0f * u, std::max(12.0f, 20.0f * u), screen.x * 0.4f, Col(255, 255, 255, 210, A));
	}

	(void)active;
}

// =====================================================================================
//  WeaponBar
// =====================================================================================

void WeaponBar::SetHoldMs(int ms) { s_holdMs.store(std::clamp(ms, 80, 1500)); }
int  WeaponBar::HoldMs()          { return s_holdMs.load(); }

WeaponBar::WeaponBar()
	: Widget()
{
	m_state.store(kIdle);
	m_finger.store(-1);
	m_orphan.store(-1);
	m_downMs.store(0);
	m_downX.store(0.0f); m_downY.store(0.0f);
	m_curX.store(0.0f);  m_curY.store(0.0f);
	m_cancelRelease.store(false);
	m_available.store(false);
	m_pendingSlot.store(-1);
	m_pendingStep.store(0);
	m_anim = 0.0f;
}

void WeaponBar::draw(ImGuiRenderer* r)
{
	s_frame++;

	CPedGTA* ped = GetLocalPed();
	const bool usable = (ped != nullptr) && !IsBlocked(ped);
	m_available.store(usable);

	if (!usable)
	{
		// fecha a roda e solta o toque: o dedo que ainda estiver na tela vira "orfao"
		// (seus eventos seguintes sao engolidos ate o pop, para nao chegarem ao jogo pela metade)
		if (m_state.exchange(kIdle) != kIdle) m_orphan.store(m_finger.load());
		m_pendingSlot.store(-1);
		m_pendingStep.store(0);
		m_anim = 0.0f;
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 screen = io.DisplaySize;
	const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);

	const int active = (ped->m_nActiveWeaponSlot < kNumSlots) ? (int)ped->m_nActiveWeaponSlot : 0;
	const uint32_t owned = OwnedMask(ped);
	const Ring ring = BuildRing(owned);

	int subCount[kNumCats] = {};
	int defSlot[kNumCats]  = {};
	for (int i = 0; i < ring.count; i++)
	{
		int tmp[3];
		subCount[i] = OwnedInCat(owned, ring.cat[i], tmp);
		defSlot[i]  = DefaultSlotInCat(owned, ring.cat[i], active);
	}

	const Geo g = WheelGeo(screen);
	const Params prm = { g.rIn, g.rOut, g.rCancel };

	// toque longo: abre a roda
	if (m_state.load() == kPressed && Classify(NowMs() - m_downMs.load(), HoldMs()) == Gesture::Hold)
	{
		int expected = kPressed;
		m_state.compare_exchange_strong(expected, kOpen);
	}

	// opcao sob o dedo
	const int st = m_state.load();
	Pick pick = { Zone::Cancel, -1, -1 };
	if (st == kOpen || st == kReleasing)
		pick = PickAt(m_curX.load() - g.c.x, m_curY.load() - g.c.y, prm, ring.count, subCount);

	// soltura: equipa se for uma opcao valida; senao preserva a arma anterior
	if (st == kReleasing)
	{
		if (!m_cancelRelease.load())
		{
			const int slot = SlotForPick(pick, ring, owned, active);
			if (slot >= 0 && slot != active) m_pendingSlot.store(slot);
		}
		m_state.store(kIdle);
	}

	// aplica a troca (toque curto ou soltura da roda) pela funcao NATIVA do jogo
	{
		const int reqSlot = m_pendingSlot.exchange(-1);
		const int reqStep = m_pendingStep.exchange(0);

		int slot = -1;
		if (reqSlot >= 0)      slot = reqSlot;
		else if (reqStep != 0) slot = NextSlot(owned, active, reqStep);

		if (slot >= 0 && slot < kNumSlots && slot != active && SlotOwned(ped, slot))
		{
			// CPed::SetCurrentWeapon(eWeaponType): acha o slot, tira o modelo antigo da mao,
			// grava m_nActiveWeaponSlot e chama AddWeaponModel. O SAMP ja sincroniza a arma
			// ativa no sync onfoot (byteCurrentWeapon).
			CHook::CallFunction<void>("_ZN4CPed16SetCurrentWeaponE11eWeaponType",
									  ped, (eWeaponType)ped->m_aWeapons[slot].dwType);
		}
	}

	// animacao de abertura/fechamento
	const bool wantOpen = (m_state.load() == kOpen);
	m_anim = std::clamp(m_anim + (wantOpen ? dt : -dt) / kAnimSeconds, 0.0f, 1.0f);

	const int stNow = m_state.load();
	DrawButton(r, ped, screen, stNow == kPressed || stNow == kOpen);

	if (m_anim > 0.0f)
		DrawWheel(r, ped, screen, ring, subCount, defSlot, owned, g, pick, active, m_anim);
}

bool WeaponBar::onRawTouch(int type, int num, int x, int y)
{
	const float fx = (float)x, fy = (float)y;

	// 1) dedo orfao (roda fechada a forca): engole ate o pop
	{
		const int orphan = m_orphan.load();
		if (orphan >= 0 && orphan == num)
		{
			if (type == 2) m_orphan.store(-1);              // novo push no mesmo id: perdeu-se o pop
			else
			{
				if (type == 1) m_orphan.store(-1);
				return true;
			}
		}
	}

	// 2) push: so comeca se nao ha toque em andamento e o dedo caiu em cima do botao
	if (type == 2)
	{
		if (m_state.load() != kIdle || !m_available.load()) return false;

		const ImVec2 screen = ImGui::GetIO().DisplaySize;
		ImVec2 a, b;
		ButtonRect(screen, a, b);
		const float pad = (b.y - a.y) * 0.22f;              // area de toque um pouco maior que o desenho

		if (fx < a.x - pad || fx > b.x + pad || fy < a.y - pad || fy > b.y + pad) return false;

		m_finger.store(num);
		m_downMs.store(NowMs());
		m_downX.store(fx); m_downY.store(fy);
		m_curX.store(fx);  m_curY.store(fy);
		m_cancelRelease.store(false);
		m_state.store(kPressed);
		return true;
	}

	// 3) move / pop: so do dedo dono
	const int st = m_state.load();
	if (st == kIdle || st == kReleasing || num != m_finger.load()) return false;

	m_curX.store(fx);
	m_curY.store(fy);

	if (type == 3) return true;

	if (type == 1)
	{
		const bool cancel = g_bTouchCancel;                 // ACTION_CANCEL do Android
		const int64_t held = NowMs() - m_downMs.load();

		int expected = kPressed;
		if (m_state.compare_exchange_strong(expected, kIdle))
		{
			// soltou antes de abrir a roda: toque curto = proxima arma (a menos que tenha sido
			// cancelado, ou o dedo tenha arrastado para longe do botao)
			const float dx = fx - m_downX.load(), dy = fy - m_downY.load();
			const float slop = ImGui::GetIO().DisplaySize.y * 0.05f;
			if (!cancel && Classify(held, HoldMs()) == Gesture::Tap && (dx * dx + dy * dy) <= slop * slop)
				m_pendingStep.store(+1);
		}
		else
		{
			// a roda ja estava aberta: o draw decide o slot com a posicao final
			m_cancelRelease.store(cancel);
			expected = kOpen;
			if (!m_state.compare_exchange_strong(expected, kReleasing)) m_state.store(kIdle);
		}
		return true;
	}

	return false;
}
