// PlayerCarry.h - /carregar: o carregado fica de cabeca para baixo sobre o ombro do carregador (visual, em cada cliente).
// Servidor: maximo_carregar.inc. Logica pura/testada: PlayerCarryCore.h. Formato do RPC 233 (servidor -> cliente):
//   uint8 versao(1) | uint8 op(1 iniciar, 2 confirmar, 3 encerrar, 4 reaplicar) | uint16 carregador | uint16 carregado | uint16 geracao
//   | float x | float y | float z | float angulo      (x,y,z,angulo = ponto seguro do carregado; so valem no op 3)
// Nada e enviado por frame: a posicao e calculada localmente (Process) a partir da matriz do carregador.
#pragma once
#include <cstdint>
typedef unsigned short PLAYERID;   // igual ao typedef de net/netgame.h
class RakClientInterface;

namespace PlayerCarry {

extern int RPC_Carry;                      // = 233 (verificado: livre no cliente e no servidor)

void Register(RakClientInterface* rak);    // RegisterScriptRPCs()
void Unregister(RakClientInterface* rak);  // UnregisterScriptRPCs()
void ClearAll();                           // nova conexao / desconexao: descarta vinculos (restaura os peds que ainda existem)
void Process();                            // fim de CPlayerPool::Process(): posiciona os carregados
void ProcessAfterPreRender();               // CWorld::ProcessPedsAfterPreRender(): reposiciona o carregado DEPOIS do PreRender (pose final), antes de desenhar
bool IsInvolved(PLAYERID id);                // jogador participa de um transporte (anims do servidor sao ignoradas)
bool IsActorInvolved(PLAYERID actorId);    // idem para um Actor carregado
void OnPlayerLeft(PLAYERID id);            // CPlayerPool::Delete(): jogador saiu do servidor

} // namespace PlayerCarry
