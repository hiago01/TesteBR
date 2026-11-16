#pragma once
// ---------------------------------------------------------------------------
// SkinMap - skins do gamemode (AddCharModel, IDs do servidor 20001..20026) -> IDs LOCAIS unicos do cliente.
//
// O jogo so tem modelos de 0 a 19999 (CModelInfo::ms_modelInfoPtrs), entao o ID 20001+ que o servidor manda
// nao existe como modelo. Cada skin foi instalada (SAMP/skinsnovas.ide + favela.img + banco texdb/favela) com um ID
// local livre 11754..11779, na MESMA ordem do artconfig.txt:  local = 11754 + (servidor - 20001).
// Se o modelo local nao estiver registrado (IDE/IMG faltando), cai na skin BASE do artconfig (o 1o numero do
// AddCharModel), para o jogador nunca ficar sem ped nem crashar. IDs que nao sao 20001..20026 passam sem mudar.
// Aplicado onde o servidor entrega a skin: SetSpawnInfo, RequestClass, WorldPlayerAdd, SetPlayerSkin e ShowActor.
// ---------------------------------------------------------------------------
#include <cstdint>
#include "util.h"

namespace SkinMap
{
	inline constexpr int kServerFirst = 20001;
	inline constexpr int kLocalFirst  = 11754;
	inline constexpr int kCount       = 26;

	// skin base de cada uma (artconfig.txt: AddCharModel(base, id, ...))
	inline constexpr uint16_t kBase[kCount] = { 299, 299, 287, 101, 162, 162, 299, 299, 299, 299, 299, 299, 299,
	                                            162, 299, 162, 299, 61, 0, 299, 62, 299, 299, 299, 299, 299 };

	inline bool IsCustomServerId(int id) { return id >= kServerFirst && id < kServerFirst + kCount; }
	inline int  LocalOf(int serverId)    { return kLocalFirst + (serverId - kServerFirst); }

	// id que o servidor mandou -> id de modelo que o cliente deve usar
	inline int Resolve(int id)
	{
		if (!IsCustomServerId(id)) return id;
		const int local = LocalOf(id);
		if (IsValidPedModel((uint)local)) return local;
		return kBase[id - kServerFirst];      // modelo local ausente: skin base (nunca invalido)
	}
}
