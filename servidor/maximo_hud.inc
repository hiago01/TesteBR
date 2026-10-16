// ============================================================================
//  maximo_hud.inc - HUD do Maximo RP (lado do SERVIDOR)
//  Requer: a_samp + Pawn.RakNet (katursis). NAO foi compilado aqui: se a sua
//  versao do Pawn.RakNet tiver assinaturas diferentes, ajuste so MaxHud_Send().
//
//  Protocolo (RPC 201, servidor -> client):
//    uint8 flags (bit0 = HUD visivel, bit1 = campo dinheiro valido) |
//    uint8 fome | uint8 sede | uint8 energia (0..100) |
//    int32 dinheiro | uint16 pessoaAtual | uint16 pessoaMax | int32 extra
//
//  Uso:
//    #include <maximo_hud>
//    MaxHud_Send(playerid, true, 80, 60, 45, GetPlayerMoney(playerid), 0, 10, 24600);
//  Vida e colete vem do proprio ped (nao precisam ser enviados).
// ============================================================================
#if defined _maximo_hud_included
    #endinput
#endif
#define _maximo_hud_included

#include <a_samp>
#include <Pawn.RakNet>

#define MAXHUD_RPC_ID (201)

stock MaxHud_Send(playerid, bool:visivel, fome, sede, energia, dinheiro, pessoaAtual = 0, pessoaMax = 0, extra = 0)
{
    if (!IsPlayerConnected(playerid)) return 0;
    if (fome < 0) fome = 0; if (fome > 100) fome = 100;
    if (sede < 0) sede = 0; if (sede > 100) sede = 100;
    if (energia < 0) energia = 0; if (energia > 100) energia = 100;

    new BitStream:bs = BS_New();
    BS_WriteValue(bs,
        PR_UINT8,  (visivel ? 1 : 0) | 2,
        PR_UINT8,  fome, PR_UINT8, sede, PR_UINT8, energia,
        PR_INT32,  dinheiro,
        PR_UINT16, pessoaAtual, PR_UINT16, pessoaMax,
        PR_INT32,  extra);
    BS_RPC(bs, playerid, MAXHUD_RPC_ID, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}
