#pragma once
// Regra (pura, testada no PC) do filtro inicial do hook ComputeDamageResponse.
//   true  = IGNORAR este dano (o hook devolve 'true' e o jogo nao processa)
//   false = deixar seguir (envia ao servidor / aplica o dano nativo)
//
// Historico: a regra antiga era "agressor != jogador local => ignora" (porque IsValidGamePed(pedLocal) e sempre true).
// Isso descartava TODO tiro de outro jogador no ped local: nada de OnPlayerTakeDamage, vida nao descia.
namespace DamageRules
{
	inline bool Ignore(bool damagerIsLocal, bool victimIsLocal, bool damagerIsNetPlayer)
	{
		if (damagerIsLocal) return false;                       // eu atirei: segue (SendGiveDamage etc.)
		return !(victimIsLocal && damagerIsNetPlayer);          // jogador remoto me acertou: segue; qualquer outro caso: ignora como antes
	}

	// regra ANTIGA, so para o teste mostrar exatamente o que mudou
	inline bool IgnoreOld(bool damagerIsLocal, bool /*victimIsLocal*/, bool /*damagerIsNetPlayer*/)
	{
		return !damagerIsLocal;
	}
}
