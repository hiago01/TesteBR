#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/armhook/patch.h"
#include "../../util/CUtil.h"
#include "../../game/Models/ModelInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern CNetGame* pNetGame;

// CWeaponInfo::GetWeaponInfo(type, skill) -- definido em game/playerped.cpp
extern uintptr_t GetWeaponInfo(int iWeapon, int iSkill);

// =====================================================================================
//  Dados
// =====================================================================================

// Nome curto (so aparece se a textura do icone nao for encontrada).
static const char* const kWeaponNames[47] = {
	"PUNHO", "SOCO-ING", "GOLF", "CASSETE", "FACA", "TACO", "PA", "SINUCA", "KATANA", "SERRA",
	"DILDO", "DILDO", "VIBRA", "VIBRA", "FLORES", "BENGALA", "GRANADA", "GAS", "MOLOTOV", "-",
	"-", "-", "COLT45", "SILENC.", "DESERT", "SHOTGUN", "SAWN-OFF", "SPAS12", "UZI", "MP5",
	"AK-47", "M4", "TEC-9", "RIFLE", "SNIPER", "RPG", "HS-ROCKET", "LANCA", "MINIGUN", "C4",
	"DETONAD.", "SPRAY", "EXTINTOR", "CAMERA", "VISAO", "TERMICO", "PARAQ."
};

// Nome do modelo (dff) de cada arma. So e usado se o modelinfo nao estiver disponivel;
// o caminho normal le o nome direto do CModelInfo, igual ao jogo.
static const char* const kFallbackModel[47] = {
	nullptr, "brassknuckle", "golfclub", "nitestick", "knifecur", "bat", "shovel", "poolcue", "katana", "chnsaw",
	"gun_dildo1", "gun_dildo2", "gun_vibe1", "gun_vibe2", "flowera", "gun_cane", "grenade", "teargas", "molotov", nullptr,
	nullptr, nullptr, "colt45", "silenced", "desert_eagle", "chromegun", "sawnoff", "shotgspa", "micro_uzi", "mp5lng",
	"ak47", "m4", "tec9", "cuntgun", "sniper", "rocketla", "heatseek", "flame", "minigun", "satchel",
	"bomb", "spraycan", "fire_ex", "camera", "nvgoggles", "irgoggles", "gun_para"
};

// =====================================================================================
//  Tema / proporcoes (tudo relativo a altura 'ch' de uma celula normal)
// =====================================================================================

namespace
{
	// cores
	const ImColor kGreen      (50, 235, 30, 255);   // borda / triangulo / setas
	const ImColor kGreenLight (130, 255, 105, 255); // colchetes e detalhes
	const ImColor kWhite      (255, 255, 255, 255);
	const ImColor kAmmoColor  (255, 255, 255, 255);

	// celula
	constexpr float kCellAspect   = 1.30f;   // largura / altura da celula normal
	constexpr float kSelScaleW    = 1.17f;   // selecionada: largura  x
	constexpr float kSelScaleH    = 1.09f;   // selecionada: altura   x
	constexpr float kGap          = 0.12f;   // espaco entre celulas
	constexpr float kChamferTop   = 0.10f;   // chanfro dos cantos de cima (x altura da celula)
	constexpr float kChamferBot   = 0.12f;   // chanfro dos cantos de baixo
	constexpr float kSelChamferTop = 0.09f;
	constexpr float kSelChamferBot = 0.17f;

	// setas laterais
	constexpr float kArrowW       = 0.28f;
	constexpr float kArrowH       = 0.68f;
	constexpr float kArrowGap     = 0.20f;

	// triangulo indicador (em cima da selecionada)
	constexpr float kTriW         = 0.27f;
	constexpr float kTriH         = 0.14f;
	constexpr float kTriGap       = 0.06f;
	constexpr float kTopMargin    = 0.05f;   // folga do brilho no topo do widget
	constexpr float kBottomMargin = 0.08f;   // folga do brilho embaixo
}

// =====================================================================================
//  Icones originais (textura "<modelo>icon" no banco gta3; punho = "fist" no banco txd)
// =====================================================================================

namespace
{
	struct IconEntry
	{
		RwTexture* tex = nullptr;
		uint8_t    tries = 0;       // tentativas de carga ja feitas
		uint32_t   nextTry = 0;     // frame minimo da proxima tentativa
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

	// Mesma resolucao que o CWidgetPlayerInfo::DrawWeaponIcon do jogo:
	//   modelo = CWeaponInfo::GetWeaponInfo(tipo, 1)->m_nModelId1
	//   textura = CModelInfo[modelo]->nome + "icon"      (modelo < 1 => punho => "fist")
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

	// Carrega (uma vez) e guarda o icone. Se nao achar, tenta de novo algumas vezes
	// (o banco pode ainda nao estar pronto no primeiro frame) e depois desiste.
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

		// ordem igual a do jogo (gta3 primeiro); o punho vive no banco do HUD (txd)
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
//  Regras de exibicao
// =====================================================================================

static CPedGTA* GetLocalPed()
{
	if (!pNetGame || !pNetGame->GetPlayerPool()) return nullptr;
	CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
	if (!lp) return nullptr;
	CPlayerPed* pp = lp->GetPlayerPed();
	return pp ? pp->m_pPed : nullptr;
}

// Slot 0 sempre aparece (punho / soco-ingles). Os demais, so se tiver a arma
// (corpo a corpo conta sempre; arma de fogo so com municao).
static bool SlotOwned(CPedGTA* ped, int slot)
{
	const CWeapon& w = ped->m_aWeapons[slot];
	if (slot == 0) return true;
	if (w.dwType == 0) return false;
	return w.dwType <= 15 || w.dwAmmo > 0;
}

// Monta o texto de municao. Armas de fogo (22..34): "total-pente".
// Lancadores, minigun, spray, camera etc.: so o total. Corpo a corpo: sem texto.
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
//  Desenho (primitivas)
// =====================================================================================

// Octogono (retangulo com os 4 cantos chanfrados), em ordem horaria.
static void MakeChamfer(ImVec2* out, const ImVec2& a, const ImVec2& b, float ct, float cb)
{
	out[0] = ImVec2(a.x + ct, a.y);
	out[1] = ImVec2(b.x - ct, a.y);
	out[2] = ImVec2(b.x,      a.y + ct);
	out[3] = ImVec2(b.x,      b.y - cb);
	out[4] = ImVec2(b.x - cb, b.y);
	out[5] = ImVec2(a.x + cb, b.y);
	out[6] = ImVec2(a.x,      b.y - cb);
	out[7] = ImVec2(a.x,      a.y + ct);
}

// Triangulo sempre em ordem horaria (necessario p/ o anti-aliasing do ImGui).
static void FillTriangle(ImGuiRenderer* r, ImVec2 p0, ImVec2 p1, ImVec2 p2, const ImColor& col)
{
	const float cross = (p1.x - p0.x) * (p2.y - p1.y) - (p1.y - p0.y) * (p2.x - p1.x);
	if (cross < 0.0f) std::swap(p1, p2);
	r->drawTriangle(p0, p1, p2, col, true);
}

// Seta lateral: "boomerang" (ponta grossa que afina nas duas pernas).
static void DrawChevron(ImGuiRenderer* r, float x, float cy, float w, float h, bool left, bool pressed)
{
	const float hy = h * 0.5f;
	ImVec2 tip, top, bot, notch;
	if (left)
	{
		tip   = ImVec2(x,              cy);
		top   = ImVec2(x + w,          cy - hy);
		bot   = ImVec2(x + w,          cy + hy);
		notch = ImVec2(x + w * 0.46f,  cy);
	}
	else
	{
		tip   = ImVec2(x + w,          cy);
		top   = ImVec2(x,              cy - hy);
		bot   = ImVec2(x,              cy + hy);
		notch = ImVec2(x + w * 0.54f,  cy);
	}

	// brilho
	ImVec2 outline[4] = { tip, top, notch, bot };
	const float gs = std::max(1.0f, h * 0.035f);
	r->drawPolyline(outline, 4, ImColor(50, 235, 30, pressed ? 90 : 55), true, gs * 4.0f);
	r->drawPolyline(outline, 4, ImColor(50, 235, 30, pressed ? 140 : 95), true, gs * 2.0f);

	const ImColor col = pressed ? kGreenLight : kGreen;
	FillTriangle(r, tip, top, notch, col);
	FillTriangle(r, tip, notch, bot, col);
}

// Icone original (mantem proporcao) dentro da caixa [cx,cy] +- [boxW,boxH]/2.
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

// Conteudo da celula: icone (ou nome, se nao houver textura) + municao.
static void DrawCellContent(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const CWeapon& w, bool selected)
{
	const float cw = b.x - a.x;
	const float ch = b.y - a.y;
	const float cx = (a.x + b.x) * 0.5f;

	char ammo[24];
	const bool hasAmmo = BuildAmmoText(w, ammo, sizeof(ammo));

	// icone
	const unsigned t = ((unsigned)w.dwType < 47u) ? (unsigned)w.dwType : 0u;
	RwTexture* tex = GetWeaponIcon(t);

	const float boxW = cw * 0.78f;
	const float boxH = ch * (hasAmmo ? 0.50f : 0.66f);
	const float icy  = hasAmmo ? a.y + ch * 0.38f : (a.y + b.y) * 0.5f;

	if (tex)
	{
		DrawIcon(r, tex, cx, icy, boxW, boxH, selected ? 255 : 235);
	}
	else
	{
		// sem textura: mostra o nome curto da arma
		const float fs = ch * 0.19f;
		const std::string name = kWeaponNames[t];
		ImVec2 ts = r->calculateTextSize(name, fs);
		if (ts.x > cw * 0.9f) ts.x = cw * 0.9f;
		r->drawText(ImVec2(cx - ts.x * 0.5f, icy - fs * 0.5f), kWhite, name, true, fs);
	}

	// municao
	if (hasAmmo)
	{
		float fs = ch * 0.25f;
		std::string txt = ammo;
		ImVec2 ts = r->calculateTextSize(txt, fs);
		if (ts.x > cw * 0.90f)
		{
			fs *= (cw * 0.90f) / ts.x;
			ts = r->calculateTextSize(txt, fs);
		}
		r->drawText(ImVec2(cx - ts.x * 0.5f, b.y - ts.y - ch * 0.05f), kAmmoColor, txt, true, fs);
	}
}

// Celula normal: fundo escuro translucido + borda branca fina, cantos chanfrados.
static void DrawNormalCell(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const CWeapon& w, bool pressed)
{
	const float h = b.y - a.y;

	ImVec2 poly[8];
	MakeChamfer(poly, a, b, h * kChamferTop, h * kChamferBot);

	r->drawConvexPolyFilled(poly, 8, ImColor(0, 0, 0, 105));
	if (pressed) r->drawConvexPolyFilled(poly, 8, ImColor(255, 255, 255, 50));

	r->drawPolyline(poly, 8, ImColor(255, 255, 255, 235), true, std::max(1.4f, h * 0.018f));

	DrawCellContent(r, a, b, w, false);
}

// Celula selecionada: maior, borda verde com brilho, colchetes nos cantos de baixo.
static void DrawSelectedCell(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, const CWeapon& w, bool pressed)
{
	const float cw = b.x - a.x;
	const float h  = b.y - a.y;
	const float ct = h * kSelChamferTop;
	const float cb = h * kSelChamferBot;

	ImVec2 poly[8];
	MakeChamfer(poly, a, b, ct, cb);

	// fundo (escuro com um toque de verde)
	r->drawConvexPolyFilled(poly, 8, ImColor(4, 20, 4, 150));
	if (pressed) r->drawConvexPolyFilled(poly, 8, ImColor(255, 255, 255, 40));

	// brilho (varias passadas, da mais larga p/ a mais estreita)
	const float t  = std::max(2.0f, h * 0.034f);
	const float gs = std::max(1.0f, h * 0.020f);
	r->drawPolyline(poly, 8, ImColor(50, 235, 30, 10), true, t + gs * 8.0f);
	r->drawPolyline(poly, 8, ImColor(50, 235, 30, 24), true, t + gs * 6.0f);
	r->drawPolyline(poly, 8, ImColor(50, 235, 30, 44), true, t + gs * 4.0f);
	r->drawPolyline(poly, 8, ImColor(50, 235, 30, 70), true, t + gs * 2.0f);

	// borda
	r->drawPolyline(poly, 8, kGreen, true, t);

	// colchetes (trecho mais grosso e claro) nos dois cantos de baixo
	const float arm = cw * 0.17f;
	const float up  = h * 0.10f;
	const float bt  = t * 1.55f;
	{
		ImVec2 bl[4] = {
			ImVec2(a.x,              b.y - cb - up),
			ImVec2(a.x,              b.y - cb),
			ImVec2(a.x + cb,         b.y),
			ImVec2(a.x + cb + arm,   b.y)
		};
		ImVec2 br[4] = {
			ImVec2(b.x,              b.y - cb - up),
			ImVec2(b.x,              b.y - cb),
			ImVec2(b.x - cb,         b.y),
			ImVec2(b.x - cb - arm,   b.y)
		};
		r->drawPolyline(bl, 4, kGreenLight, false, bt);
		r->drawPolyline(br, 4, kGreenLight, false, bt);

		// fio interno paralelo ao chanfro (efeito de linha dupla)
		const float d = h * 0.06f;
		ImVec2 il[3] = {
			ImVec2(a.x + d,              b.y - cb - d * 0.35f),
			ImVec2(a.x + cb + d * 0.35f, b.y - d),
			ImVec2(a.x + cb + arm * 0.6f, b.y - d)
		};
		ImVec2 ir[3] = {
			ImVec2(b.x - d,              b.y - cb - d * 0.35f),
			ImVec2(b.x - cb - d * 0.35f, b.y - d),
			ImVec2(b.x - cb - arm * 0.6f, b.y - d)
		};
		const float it = std::max(1.2f, t * 0.45f);
		r->drawPolyline(il, 3, ImColor(150, 255, 130, 210), false, it);
		r->drawPolyline(ir, 3, ImColor(150, 255, 130, 210), false, it);
	}

	DrawCellContent(r, a, b, w, true);

	// triangulo apontando pra arma selecionada
	{
		const float ch = h / kSelScaleH;           // altura da celula normal
		const float tw = ch * kTriW;
		const float th = ch * kTriH;
		const float cx = (a.x + b.x) * 0.5f;
		const float ty = a.y - ch * kTriGap - th;

		ImVec2 p0(cx - tw * 0.5f, ty), p1(cx + tw * 0.5f, ty), p2(cx, ty + th);
		ImVec2 tri[3] = { p0, p1, p2 };
		r->drawPolyline(tri, 3, ImColor(50, 235, 30, 55), true, gs * 4.0f);
		r->drawPolyline(tri, 3, ImColor(50, 235, 30, 100), true, gs * 2.0f);
		FillTriangle(r, p0, p1, p2, kGreen);
	}
}

// =====================================================================================
//  WeaponBar
// =====================================================================================

ImVec2 WeaponBar::SizeFor(const ImVec2& screen)
{
	const float ch = screen.y * 0.12f;                 // altura de uma celula normal
	return ImVec2(screen.x * 0.62f, ch * kHeightRatio);
}

ImVec2 WeaponBar::PosFor(const ImVec2& screen)
{
	const ImVec2 sz = SizeFor(screen);
	return ImVec2((screen.x - sz.x) * 0.5f, screen.y - sz.y - screen.y * 0.025f);
}

WeaponBar::WeaponBar()
	: Widget()
{
	m_cellCount.store(0);
	m_pushedId.store(kHitNone);
	m_pendingSlot.store(-1);
	m_pendingStep.store(0);
}

void WeaponBar::draw(ImGuiRenderer* renderer)
{
	s_frame++;
	m_cellCount.store(0);          // sem celulas = nada clicavel

	CPedGTA* ped = GetLocalPed();
	if (!ped) return;

	// armas que o jogador tem, em ordem de slot
	int slots[13], n = 0;
	for (int s = 0; s < 13; s++)
		if (SlotOwned(ped, s)) slots[n++] = s;

	auto indexOfSlot = [&](int slot) -> int {
		for (int i = 0; i < n; i++) if (slots[i] == slot) return i;
		return -1;
	};

	// ---- aplica a troca pedida pelo toque (aqui estamos na thread de render/jogo) ----
	{
		const int reqSlot = m_pendingSlot.exchange(-1);
		const int reqStep = m_pendingStep.exchange(0);

		if ((reqSlot >= 0 || reqStep != 0) && n > 0 && !ped->IsInVehicle())
		{
			int slot = -1;
			if (reqSlot >= 0)
			{
				slot = reqSlot;
			}
			else
			{
				const int cur = indexOfSlot(ped->m_nActiveWeaponSlot);
				const int ni  = (cur < 0) ? (reqStep > 0 ? 0 : n - 1) : (cur + reqStep + n) % n;
				slot = slots[ni];
			}

			if (slot >= 0 && slot < 13 && SlotOwned(ped, slot))
			{
				// CPed::SetCurrentWeapon(eWeaponType): acha o slot, tira o modelo antigo da mao,
				// grava m_nActiveWeaponSlot e chama AddWeaponModel (arma aparece na mao).
				// O SAMP ja sincroniza a arma ativa no sync onfoot (byteCurrentWeapon).
				CHook::CallFunction<void>("_ZN4CPed16SetCurrentWeaponE11eWeaponType",
										  ped, (eWeaponType)ped->m_aWeapons[slot].dwType);
			}
		}
	}

	if (ped->IsInVehicle()) return;       // dentro de veiculo nao mostra
	if (n == 0) return;

	// ---- janela de celulas visiveis ----
	const int active = ped->m_nActiveWeaponSlot;
	const int sel    = indexOfSlot(active);               // -1 se a arma ativa nao esta na lista
	const int vis    = std::min(n, kMaxVisible);
	int first = 0;
	if (n > kMaxVisible)
		first = std::clamp((sel >= 0 ? sel : 0) - vis / 2, 0, n - vis);

	const bool arrows = n > 1;

	// ---- medidas ----
	const float ch0 = size().y / kHeightRatio;            // altura da celula normal (sem reduzir)

	float perCh = (vis - 1) * (kCellAspect + kGap) + kCellAspect;       // em multiplos de ch
	if (sel >= first && sel < first + vis) perCh += kCellAspect * (kSelScaleW - 1.0f);
	if (arrows) perCh += 2.0f * (kArrowW + kArrowGap);

	float ch = ch0;
	if (perCh * ch > size().x) ch = size().x / perCh;     // tela estreita: encolhe tudo

	const float cw     = ch * kCellAspect;
	const float selW   = cw * kSelScaleW;
	const float selH   = ch * kSelScaleH;
	const float gap    = ch * kGap;
	const float arrowW = ch * kArrowW;
	const float arrowH = ch * kArrowH;
	const float arrowG = ch * kArrowGap;

	const float absX = absolutePosition().x;
	const float absY = absolutePosition().y;

	// linha de centro das celulas: ancorada na base do widget
	const float cy = absY + size().y - ch0 * kBottomMargin - ch0 * kSelScaleH * 0.5f;

	const float totalW = perCh * ch;
	const float startX = absX + (size().x - totalW) * 0.5f;

	const float hitHalfH = selH * 0.5f + ch * 0.10f;

	Cell tmp[kMaxCells];
	int  cnt = 0;

	const int pushed = m_pushedId.load();

	float x = startX + (arrows ? arrowW + arrowG : 0.0f);

	// seta esquerda
	const float leftArrowX = startX;
	if (arrows)
	{
		DrawChevron(renderer, leftArrowX, cy, arrowW, arrowH, true, pushed == kHitPrev);
		tmp[cnt++] = { ImVec2(leftArrowX - ch * 0.30f, cy - hitHalfH),
					   ImVec2(leftArrowX + arrowW + arrowG - gap * 0.5f, cy + hitHalfH), kHitPrev };
	}

	// celulas (as normais primeiro; a selecionada por cima, com o brilho)
	ImVec2 selA(0, 0), selB(0, 0);
	int    selSlotIdx = -1;

	for (int i = 0; i < vis; i++)
	{
		const int idx = first + i;
		const int s   = slots[idx];
		const bool isSel = (idx == sel);

		const float w = isSel ? selW : cw;
		const float h = isSel ? selH : ch;

		const ImVec2 a(x, cy - h * 0.5f);
		const ImVec2 b(x + w, cy + h * 0.5f);

		tmp[cnt++] = { ImVec2(a.x - gap * 0.5f, cy - hitHalfH), ImVec2(b.x + gap * 0.5f, cy + hitHalfH), s };

		if (isSel) { selA = a; selB = b; selSlotIdx = s; }
		else       DrawNormalCell(renderer, a, b, ped->m_aWeapons[s], pushed == s);

		x += w + gap;
	}

	if (selSlotIdx >= 0)
		DrawSelectedCell(renderer, selA, selB, ped->m_aWeapons[selSlotIdx], pushed == selSlotIdx);

	// seta direita
	if (arrows)
	{
		const float rightArrowX = (x - gap) + arrowG;
		DrawChevron(renderer, rightArrowX, cy, arrowW, arrowH, false, pushed == kHitNext);
		tmp[cnt++] = { ImVec2(rightArrowX - arrowG + gap * 0.5f, cy - hitHalfH),
					   ImVec2(rightArrowX + arrowW + ch * 0.30f, cy + hitHalfH), kHitNext };
	}

	// publica as areas de toque deste frame
	for (int i = 0; i < cnt; i++) m_cells[i] = tmp[i];
	m_cellCount.store(cnt);
}

void WeaponBar::touchEvent(const ImVec2& pos, TouchType type)
{
	auto hit = [&](const ImVec2& p) -> int {
		const int n = m_cellCount.load();
		for (int i = 0; i < n; i++)
		{
			const Cell& c = m_cells[i];
			if (p.x >= c.min.x && p.x <= c.max.x && p.y >= c.min.y && p.y <= c.max.y)
				return c.id;
		}
		return kHitNone;
	};

	if (type == TouchType::push)
	{
		m_pushedId.store(hit(pos));
	}
	else if (type == TouchType::pop)
	{
		// so conta como clique se soltou no mesmo lugar em que apertou
		const int up      = hit(pos);
		const int pushed  = m_pushedId.exchange(kHitNone);

		if (pushed != kHitNone && up == pushed)
		{
			if (up >= 0)               m_pendingSlot.store(up);
			else if (up == kHitPrev)   m_pendingStep.store(-1);
			else if (up == kHitNext)   m_pendingStep.store(+1);
		}
	}
}
