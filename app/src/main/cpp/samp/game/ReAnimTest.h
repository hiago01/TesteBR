// ReAnimTest.h - /reanim: testa as animacoes do pacote "Re Anims" (JB) SEM substituir o ped.ifp oficial.
// O ped.ifp do Re Anims entra como bloco customizado "reanim" (arquivo SAMP/anims/reanim_ped.ifp); os 10 blocos extras
// (baseball chainsaw fight_e goggles grenade knife parachute playidles shop vending) entram como "re_<nome>"
// (arquivos SAMP/anims/reanim/<nome>.ifp). Tudo so no seu cliente, nada muda para os outros jogadores.
//   /reanim carregar | extras | lista [novas|filtro] [pagina] | tocar <anim|numero|bloco:anim> [hold]
//   /reanim proxima | anterior | oficial <anim> | parar
#pragma once
#include <string>
namespace ReAnimTest { std::string Command(const std::string& args); }
