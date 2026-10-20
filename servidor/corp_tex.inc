// ============================================================================
//  corp_tex.inc - Textura por corporacao (FemininaCorp) por jogador (lado do SERVIDOR)
//  Requer: a_samp + Pawn.RakNet (katursis). NAO foi compilado aqui: se a sua versao do Pawn.RakNet tiver
//  assinaturas diferentes, ajuste so CorpTex_Send(). Mesmo padrao de maximo_idcard.inc.
//
//  Protocolo (RPC 231, servidor -> client):  uint16 playerId | uint8 len | char corp[len]   (len 0 = limpar)
//  O nome da corp e o nome da pasta em  SAMP/skins/textures/272/<corp>/  (so a-z 0-9 _ -, ate 15).
//  So recebem quem tem o jogador no stream (e ele mesmo). Quem entra no stream recebe o estado atual.
//
//  Uso:
//    #include <corp_tex>
//    CorpTex_Set(playerid, "pmerj");      // todos os proximos passam a ver a corp
//    CorpTex_Clear(playerid);
//    public OnPlayerStreamIn(playerid, forplayerid)  { CorpTex_OnStreamIn(playerid, forplayerid); return 1; }
//    public OnPlayerDisconnect(playerid, reason)     { CorpTex_Clear(playerid); return 1; }
//    public OnPlayerSpawn(playerid) { CorpTex_Send(playerid, playerid); return 1; }  // reenvia ao proprio apos spawn/troca de skin
//  Lembrete: a corp so aparece em quem esta com a skin do DFF da FemininaCorp (hoje modelo 7).
// ============================================================================
#if defined _corp_tex_included
    #endinput
#endif
#define _corp_tex_included

#include <a_samp>
#include <Pawn.RakNet>

#define CORPTEX_RPC_ID (231)

static CorpTex_Name[MAX_PLAYERS][16];

static bool:CorpTex_Valid(const corp[])
{
    new len = strlen(corp);
    if (len < 1 || len > 15) return false;
    for (new i = 0; i < len; i++)
    {
        new c = corp[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

// owner = dono da corp; sendTo = quem recebe
stock CorpTex_Send(owner, sendTo)
{
    if (!IsPlayerConnected(sendTo) || owner < 0 || owner >= MAX_PLAYERS) return 0;

    new BitStream:bs = BS_New(), len = strlen(CorpTex_Name[owner]);
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, CorpTex_Name[owner][i]);
    BS_RPC(bs, sendTo, CORPTEX_RPC_ID, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

static CorpTex_Broadcast(owner)
{
    CorpTex_Send(owner, owner);
    for (new i = 0, m = GetMaxPlayers(); i < m; i++)
        if (i != owner && IsPlayerConnected(i) && IsPlayerStreamedIn(owner, i)) CorpTex_Send(owner, i);
}

stock CorpTex_Set(playerid, const corp[])
{
    if (!IsPlayerConnected(playerid) || !CorpTex_Valid(corp)) return 0;
    format(CorpTex_Name[playerid], 16, "%s", corp);
    CorpTex_Broadcast(playerid);
    return 1;
}

stock CorpTex_Clear(playerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS || !CorpTex_Name[playerid][0]) return 0;
    CorpTex_Name[playerid][0] = 0;
    CorpTex_Broadcast(playerid);      // len 0 = o cliente limpa
    return 1;
}

// chame no OnPlayerStreamIn(playerid, forplayerid): forplayerid passa a ver playerid
stock CorpTex_OnStreamIn(playerid, forplayerid)
{
    if (playerid >= 0 && playerid < MAX_PLAYERS && CorpTex_Name[playerid][0]) CorpTex_Send(playerid, forplayerid);
    return 1;
}
