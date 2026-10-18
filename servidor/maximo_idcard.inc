// ============================================================================
//  maximo_idcard.inc - Cartao de identidade 3D do Maximo RP (lado do SERVIDOR)
//  Requer: a_samp + Pawn.RakNet (katursis). NAO foi compilado aqui: se a sua versao do
//  Pawn.RakNet tiver assinaturas diferentes, ajuste so MaxCard_WriteStr()/MaxCard_SendTo().
//  Segue o mesmo padrao de cj_clothes.inc (string = uint8 tamanho + bytes).
//
//  Protocolo (RPC 202, servidor -> client):
//    uint16 playerId | uint8 flags (bit0 = visivel) | uint8 categoria (0..5) |
//    uint8 cargoLen | cargo[] | uint8 funcLen | func[]
//  Nome e ID mostrados no cartao o cliente pega do pool de jogadores (nao trafegam aqui).
//
//  Categorias: 0 Civil | 1 Policial | 2 Delegado | 3 Medico | 4 Administrador | 5 Militar do Exercito
//
//  Uso:
//    #include <maximo_idcard>
//    MaxCard_Set(playerid, MAXCARD_POLICIAL, "Policial", "MX-0427");   // todos passam a ver o cartao
//    MaxCard_Remove(playerid);
//    public OnPlayerConnect(playerid)    { MaxCard_SyncTo(playerid); return 1; }  // novo jogador recebe os existentes
//    public OnPlayerDisconnect(playerid, reason) { MaxCard_Remove(playerid); return 1; }
// ============================================================================
#if defined _maximo_idcard_included
    #endinput
#endif
#define _maximo_idcard_included

#include <a_samp>
#include <Pawn.RakNet>

#define MAXCARD_RPC_ID (202)

enum
{
    MAXCARD_CIVIL = 0, MAXCARD_POLICIAL, MAXCARD_DELEGADO,
    MAXCARD_MEDICO, MAXCARD_ADMINISTRADOR, MAXCARD_MILITAR
}

static MaxCard_Cat[MAX_PLAYERS] = {-1, ...};
static MaxCard_Cargo[MAX_PLAYERS][32];
static MaxCard_Func[MAX_PLAYERS][24];

static MaxCard_WriteStr(BitStream:bs, const str[])
{
    new len = strlen(str);
    BS_WriteValue(bs, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, str[i]);
}

// owner = dono do cartao; sendTo = quem recebe
static MaxCard_SendTo(sendTo, owner, bool:visivel, cat, const cargo[], const func[])
{
    if (!IsPlayerConnected(sendTo)) return 0;

    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, (visivel ? 1 : 0), PR_UINT8, cat);
    MaxCard_WriteStr(bs, cargo);
    MaxCard_WriteStr(bs, func);
    BS_RPC(bs, sendTo, MAXCARD_RPC_ID, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

// cargo: ate 31 caracteres | func (n. funcional): ate 23 caracteres
stock MaxCard_Set(playerid, categoria, const cargo[], const func[] = "")
{
    if (!IsPlayerConnected(playerid) || categoria < 0 || categoria > MAXCARD_MILITAR) return 0;

    MaxCard_Cat[playerid] = categoria;
    format(MaxCard_Cargo[playerid], 32, "%s", cargo);
    format(MaxCard_Func[playerid],  24, "%s", func);

    for (new i = 0, m = GetMaxPlayers(); i < m; i++)
        if (IsPlayerConnected(i)) MaxCard_SendTo(i, playerid, true, categoria, MaxCard_Cargo[playerid], MaxCard_Func[playerid]);
    return 1;
}

stock MaxCard_Remove(playerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS || MaxCard_Cat[playerid] == -1) return 0;
    MaxCard_Cat[playerid] = -1;

    for (new i = 0, m = GetMaxPlayers(); i < m; i++)
        if (IsPlayerConnected(i)) MaxCard_SendTo(i, playerid, false, 0, "", "");
    return 1;
}

// Chame quando um jogador entra: ele recebe os cartoes ja existentes.
stock MaxCard_SyncTo(forplayerid)
{
    for (new i = 0, m = GetMaxPlayers(); i < m; i++)
        if (IsPlayerConnected(i) && MaxCard_Cat[i] != -1)
            MaxCard_SendTo(forplayerid, i, true, MaxCard_Cat[i], MaxCard_Cargo[i], MaxCard_Func[i]);
    return 1;
}
