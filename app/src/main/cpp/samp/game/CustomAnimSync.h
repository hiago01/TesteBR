// CustomAnimSync.h - sincronizacao servidor -> clientes proximos
#pragma once
#include <cstdint>
typedef unsigned short PLAYERID;   // mesmo typedef de net/netgame.h (redeclaracao identica e valida); evita puxar netgame.h antes de CVector existir

class RakClientInterface;

namespace CustomAnimSync {

// TODO: confirme que nao colide com nenhum RPC do seu servidor (no SAMPRPC.cpp do projeto os IDs usados vao ate 201).
extern int RPC_CustomAnim;                 // = 230

void Register(RakClientInterface* rak);    // chamar em RegisterScriptRPCs()
void Unregister(RakClientInterface* rak);  // chamar no Unregister correspondente

void OnPlayerStreamIn(PLAYERID id);        // depois de WorldPlayerAdd criar o ped
void OnPlayerStreamOut(PLAYERID id);       // CRemotePlayer::Remove() e ~CRemotePlayer()
void OnPlayerStateChange(PLAYERID id, uint8_t newState);   // sai de ONFOOT -> limpa
void ClearAll();                           // desconexao / reinicio do netgame

// Teste local (sem servidor): faz o mesmo que o RPC receberia. block vazio = limpar. Usado pelo comando /ifp remoto.
bool DebugSet(PLAYERID id, const char* block, const char* anim, uint16_t flags, float delta);

} // namespace CustomAnimSync
