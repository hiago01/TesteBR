// ============================================================================
//  corp_tex.inc - Visual por jogador da FemininaCorp (lado do SERVIDOR): corporacao, rosto, tom de pele, cabelo
//  Requer: a_samp + Pawn.RakNet (katursis). NAO foi compilado aqui: se a sua versao do Pawn.RakNet tiver
//  assinaturas diferentes, ajuste so CorpTex_Send(). Mesmo padrao de maximo_idcard.inc.
//
//  Protocolo (RPC 231, servidor -> client):
//    uint16 playerId | uint8 len | char corp[len] | uint8 tom | uint8 rosto | uint8 estiloCabelo | uint8 corCabelo
//  Tudo zero/vazio = limpar. So recebem quem tem o jogador no stream (e ele mesmo); quem entra no stream
//  recebe o estado atual (CorpTex_OnStreamIn). O cliente valida tudo de novo.
//
//  Opcoes validas:
//    corp   = nome da pasta em SAMP/skins/textures/272/<corp>/  (a-z 0-9 _ -, ate 15)
//    tom    = 0 (nenhum) ou 1..6   (1 medio, 2 escuro, 3 claro, 4 moreno, 5 avermelhado, 6 palido)
//    rosto  = 1..CORPTEX_FACE_MAX[tom]   (6,8,8,7,6,12)
//    estilo = 0 (nenhum) ou 1..5 ; cor = 1..CORPTEX_HAIR_MAX[estilo]   (6,6,7,7,7)
//
//  Uso (a corp NAO apaga rosto/cabelo/pele: sao campos independentes):
//    #include <corp_tex>
//    CorpTex_SetCorp(playerid, "pmerj");        // so a corp
//    CorpTex_SetFace(playerid, 3, 5);           // tom 3, rosto 5 (o tom tambem pinta o corpo)
//    CorpTex_SetHair(playerid, 2, 4);           // estilo 2, cor 4
//    CorpTex_ClearCorp(playerid);               // tira so a corp
//    // salvar/carregar (DOF2): CorpTex_GetCorp/Tone/Face/HairStyle/HairColor e CorpTex_SetAll(...)
//    public OnPlayerStreamIn(playerid, forplayerid)  { CorpTex_OnStreamIn(playerid, forplayerid); return 1; }
//    public OnPlayerDisconnect(playerid, reason)     { CorpTex_Reset(playerid); return 1; }
//    public OnPlayerSpawn(playerid) { CorpTex_Send(playerid, playerid); return 1; }  // reenvia ao proprio apos spawn/troca de skin
//  Lembrete: so aparece em quem esta com a skin do DFF da FemininaCorp (hoje modelo 7).
// ============================================================================
#if defined _corp_tex_included
    #endinput
#endif
#define _corp_tex_included

#include <a_samp>
#include <Pawn.RakNet>

#define CORPTEX_RPC_ID (231)

static const CORPTEX_FACE_MAX[7] = {0, 6, 8, 8, 7, 6, 12};
static const CORPTEX_HAIR_MAX[6] = {0, 6, 6, 7, 7, 7};

static CorpTex_Name[MAX_PLAYERS][16];
static CorpTex_Tone[MAX_PLAYERS], CorpTex_Face[MAX_PLAYERS], CorpTex_HairStyle[MAX_PLAYERS], CorpTex_HairColor[MAX_PLAYERS];

static bool:CorpTex_ValidName(const corp[])
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

// owner = dono do visual; sendTo = quem recebe
stock CorpTex_Send(owner, sendTo)
{
    if (!IsPlayerConnected(sendTo) || owner < 0 || owner >= MAX_PLAYERS) return 0;

    new BitStream:bs = BS_New(), len = strlen(CorpTex_Name[owner]);
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, CorpTex_Name[owner][i]);
    BS_WriteValue(bs, PR_UINT8, CorpTex_Tone[owner], PR_UINT8, CorpTex_Face[owner],
                      PR_UINT8, CorpTex_HairStyle[owner], PR_UINT8, CorpTex_HairColor[owner]);
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

stock CorpTex_SetCorp(playerid, const corp[])
{
    if (!IsPlayerConnected(playerid) || !CorpTex_ValidName(corp)) return 0;
    format(CorpTex_Name[playerid], 16, "%s", corp);
    CorpTex_Broadcast(playerid);
    return 1;
}

stock CorpTex_ClearCorp(playerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS || !CorpTex_Name[playerid][0]) return 0;
    CorpTex_Name[playerid][0] = 0;
    CorpTex_Broadcast(playerid);
    return 1;
}

// tom 0 remove rosto e pele; variante 0 mantem a atual (ou 1)
stock CorpTex_SetFace(playerid, tone, face = 0)
{
    if (!IsPlayerConnected(playerid) || tone < 0 || tone > 6) return 0;
    if (tone == 0) { CorpTex_Tone[playerid] = 0; CorpTex_Face[playerid] = 0; }
    else
    {
        if (face == 0) face = (CorpTex_Face[playerid] > 0) ? CorpTex_Face[playerid] : 1;
        if (face < 1 || face > CORPTEX_FACE_MAX[tone]) return 0;
        CorpTex_Tone[playerid] = tone; CorpTex_Face[playerid] = face;
    }
    CorpTex_Broadcast(playerid);
    return 1;
}

// estilo 0 remove o cabelo; cor 0 mantem a atual (ou 1)
stock CorpTex_SetHair(playerid, style, color = 0)
{
    if (!IsPlayerConnected(playerid) || style < 0 || style > 5) return 0;
    if (style == 0) { CorpTex_HairStyle[playerid] = 0; CorpTex_HairColor[playerid] = 0; }
    else
    {
        if (color == 0) color = (CorpTex_HairColor[playerid] > 0) ? CorpTex_HairColor[playerid] : 1;
        if (color < 1 || color > CORPTEX_HAIR_MAX[style]) return 0;
        CorpTex_HairStyle[playerid] = style; CorpTex_HairColor[playerid] = color;
    }
    CorpTex_Broadcast(playerid);
    return 1;
}

// carregar tudo de uma vez (ex.: do DOF2 no login). Valores invalidos viram "nenhum".
stock CorpTex_SetAll(playerid, const corp[], tone, face, hairStyle, hairColor)
{
    if (!IsPlayerConnected(playerid)) return 0;
    CorpTex_Name[playerid][0] = 0;
    if (corp[0] && CorpTex_ValidName(corp)) format(CorpTex_Name[playerid], 16, "%s", corp);
    CorpTex_Tone[playerid] = 0; CorpTex_Face[playerid] = 0; CorpTex_HairStyle[playerid] = 0; CorpTex_HairColor[playerid] = 0;
    if (tone >= 1 && tone <= 6) { CorpTex_Tone[playerid] = tone; CorpTex_Face[playerid] = (face >= 1 && face <= CORPTEX_FACE_MAX[tone]) ? face : 1; }
    if (hairStyle >= 1 && hairStyle <= 5) { CorpTex_HairStyle[playerid] = hairStyle; CorpTex_HairColor[playerid] = (hairColor >= 1 && hairColor <= CORPTEX_HAIR_MAX[hairStyle]) ? hairColor : 1; }
    CorpTex_Broadcast(playerid);
    return 1;
}

// getters (para salvar no DOF2)
stock CorpTex_GetCorp(playerid, dest[], size = sizeof(dest)) { format(dest, size, "%s", CorpTex_Name[playerid]); return 1; }
stock CorpTex_GetTone(playerid)      return CorpTex_Tone[playerid];
stock CorpTex_GetFace(playerid)      return CorpTex_Face[playerid];
stock CorpTex_GetHairStyle(playerid) return CorpTex_HairStyle[playerid];
stock CorpTex_GetHairColor(playerid) return CorpTex_HairColor[playerid];

// OnPlayerDisconnect: esquece o estado do slot e avisa os clientes (NAO apaga o que voce salvou no DOF2)
stock CorpTex_Reset(playerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS) return 0;
    CorpTex_Name[playerid][0] = 0; CorpTex_Tone[playerid] = 0; CorpTex_Face[playerid] = 0;
    CorpTex_HairStyle[playerid] = 0; CorpTex_HairColor[playerid] = 0;
    CorpTex_Broadcast(playerid);
    return 1;
}

// chame no OnPlayerStreamIn(playerid, forplayerid): forplayerid passa a ver playerid
stock CorpTex_OnStreamIn(playerid, forplayerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS) return 1;
    if (CorpTex_Name[playerid][0] || CorpTex_Tone[playerid] || CorpTex_HairStyle[playerid]) CorpTex_Send(playerid, forplayerid);
    return 1;
}
