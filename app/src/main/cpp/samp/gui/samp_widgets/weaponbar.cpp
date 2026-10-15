#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/armhook/patch.h"

extern CNetGame* pNetGame;

static const char* const kWeaponNames[47] = {
	"PUNHO", "SOCO-ING", "GOLF", "CASSETE", "FACA", "TACO", "PA", "SINUCA", "KATANA", "SERRA",
	"DILDO", "DILDO", "VIBRA", "VIBRA", "FLORES", "BENGALA", "GRANADA", "GAS", "MOLOTOV", "-",
	"-", "-", "COLT45", "SILENC.", "DESERT", "SHOTGUN", "SAWN-OFF", "SPAS12", "UZI", "MP5",
	"AK-47", "M4", "TEC-9", "RIFLE", "SNIPER", "RPG", "HS-ROCKET", "LANCA", "MINIGUN", "C4",
	"DETONAD.", "SPRAY", "EXTINTOR", "CAMERA", "VISAO", "TERMICO", "PARAQ."
};

WeaponBar::WeaponBar()
	: Widget()
{
	m_pushedSlot = -1;
	m_pendingSlot = -1;
}

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

void WeaponBar::draw(ImGuiRenderer* renderer)
{
	m_cells.clear();

	CPedGTA* ped = GetLocalPed();
	if (!ped) return;

	// aplica a troca pedida pelo toque (aqui estamos na thread de render/jogo)
	if (m_pendingSlot >= 0)
	{
		int slot = m_pendingSlot;
		m_pendingSlot = -1;
		if (slot < 13 && SlotOwned(ped, slot) && !ped->IsInVehicle())
		{
			// CPed::SetCurrentWeapon(eWeaponType): acha o slot, tira o modelo antigo da mao,
			// grava m_nActiveWeaponSlot e chama AddWeaponModel (arma aparece na mao).
			// O SAMP ja sincroniza a arma ativa no sync onfoot (byteCurrentWeapon).
			CHook::CallFunction<void>("_ZN4CPed16SetCurrentWeaponE11eWeaponType",
									  ped, (eWeaponType)ped->m_aWeapons[slot].dwType);
		}
	}

	if (ped->IsInVehicle()) return;       // dentro de veiculo nao mostra

	int slots[13], n = 0;
	for (int s = 0; s < 13; s++)
		if (SlotOwned(ped, s)) slots[n++] = s;
	if (n == 0) return;

	const float sq   = size().y;                  // lado do quadrado
	const float gap  = sq * 0.10f;
	const float rowW = n * sq + (n - 1) * gap;
	float x = absolutePosition().x + (size().x - rowW) / 2.0f;
	const float y = absolutePosition().y;

	const float fs = UISettings::fontSize() / 2.6f;
	const int active = ped->m_nActiveWeaponSlot;

	for (int i = 0; i < n; i++)
	{
		const int s = slots[i];
		const CWeapon& w = ped->m_aWeapons[s];
		const bool selected = (s == active);
		const bool pressed  = (s == m_pushedSlot);

		ImVec2 a(x, y), b(x + sq, y + sq);
		m_cells.push_back({ a, b, s });

		ImColor fill = selected ? ImColor(25, 120, 35, 215)
				: pressed ? ImColor(90, 90, 90, 200)
				: ImColor(0, 0, 0, 150);
		renderer->drawRect(a, b, fill, true);
		renderer->drawRect(a, b,
				selected ? ImColor(255, 215, 0) : ImColor(255, 255, 255, 120),
				false, selected ? 3.0f : 1.5f);

		// nome (centralizado, parte de cima)
		unsigned t = w.dwType < 47 ? w.dwType : 0;
		std::string name = kWeaponNames[t];
		ImVec2 ts = renderer->calculateTextSize(name, fs);
		renderer->drawText(ImVec2(a.x + (sq - ts.x) / 2, a.y + sq * 0.12f),
				ImColor(255, 255, 255), name, true, fs);

		// municao (so armas com municao de verdade)
		if (t >= 16 && t <= 43 && t != 40)
		{
			std::string ammo = std::to_string(w.dwAmmo);
			ImVec2 as = renderer->calculateTextSize(ammo, fs);
			renderer->drawText(ImVec2(a.x + (sq - as.x) / 2, b.y - as.y - sq * 0.10f),
					ImColor(255, 230, 120), ammo, true, fs);
		}

		x += sq + gap;
	}
}

void WeaponBar::touchEvent(const ImVec2& pos, TouchType type)
{
	auto cellAt = [&](const ImVec2& p) -> int {
		for (const Cell& c : m_cells)
			if (p.x >= c.min.x && p.x <= c.max.x && p.y >= c.min.y && p.y <= c.max.y)
				return c.slot;
		return -1;
	};

	if (type == TouchType::push)
	{
		m_pushedSlot = cellAt(pos);
	}
	else if (type == TouchType::pop)
	{
		// so conta como clique se soltou no mesmo quadrado em que apertou
		int up = cellAt(pos);
		if (m_pushedSlot >= 0 && up == m_pushedSlot)
			m_pendingSlot = up;
		m_pushedSlot = -1;
	}
}
