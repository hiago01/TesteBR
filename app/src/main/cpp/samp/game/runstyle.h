// runstyle.h - estilo de andar/correr do jogador local, definido PELO SERVIDOR por SKIN.
//
//  O servidor (maximo_corrida.inc) le scriptfiles/corrida_skins.ini (linhas "skin=estilo", editado so por administrador nivel 5 com /corrida)
//  e manda ao jogador ao logar / nascer / trocar de skin: RPC 245, op 25, u8 estilo (0 = nativo do jogo, 1..15 = lista abaixo).
//  O jogador comum NAO escolhe: nao existe comando no cliente.
//
//  Nao troca o ped.ifp: so seleciona QUAL grupo de animacao ja existente o ped usa (CPed::m_nAnimGroup, offset 0x630).
//  Indices (mesma ordem do servidor): 1 civil 2 cj 3 musculoso 4 gangue 5 gangue2 6 corredor 7 gordo 8 idoso 9 arrastado
//  10 bebado 11 swat 12 mulher 13 sexy 14 apressada 15 corredora
#pragma once
#include <cstdint>
namespace RunStyle {
	void OnServer(int style);   // thread de rede: so guarda o valor
	void OnTable(const uint16_t* skins, const uint8_t* styles, int n);   // op 26: tabela skin->estilo (aplica nos peds de TODOS os jogadores)
	void Reset();               // nova conexao: volta ao nativo
	void Tick();                // thread do jogo, todo frame (leve)
}
