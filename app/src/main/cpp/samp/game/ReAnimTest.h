// ReAnimTest.h - /reanim: testa as animacoes do pacote "Re Anims" (JB) SEM substituir o ped.ifp oficial.
// O ped.ifp do Re Anims entra como bloco customizado "reanim" (arquivo SAMP/anims/reanim_ped.ifp); os 10 blocos extras
// (baseball chainsaw fight_e goggles grenade knife parachute playidles shop vending) entram como "re_<nome>"
// (arquivos SAMP/anims/reanim/<nome>.ifp). Tudo so no seu cliente, nada muda para os outros jogadores.
//   Armas do "Re-anims For IWP": SAMP/anims/reanim/iwp/<nome>.ifp -> bloco "iwp_<nome>"  (/reanim armas)
//   /reanim carregar | extras | armas | lista [novas|filtro] [pagina] | tocar <anim|numero|bloco:anim> [hold]
//   /reanim proxima | anterior | oficial <anim> | parar
#pragma once
#include <string>
namespace ReAnimTest { std::string Command(const std::string& args); }
