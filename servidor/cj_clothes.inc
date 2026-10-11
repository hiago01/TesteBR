// ============================================================================
//  cj_clothes.inc  -  CJ real: roupas / tatuagens / cabelo (lado do SERVIDOR)
//  Requer: a_samp + Pawn.RakNet (katursis). NÃO foi compilado aqui: se a sua
//  versão do Pawn.RakNet tiver assinaturas diferentes, ajuste só CJ_SendTo().
//
//  Protocolo (RPC 200, servidor -> client), igual ao ScrSetPlayerClothes():
//    uint16 dono | uint8 flags (bit0 = substituir tudo) | uint8 qtd |
//    qtd x { uint8 componente | uint8 tamTextura | bytes | uint8 tamModelo | bytes }
//
//  Uso no gamemode:
//    #include <cj_clothes>
//    public OnPlayerConnect(playerid)            { CJ_ResetPlayer(playerid); return 1; }
//    public OnPlayerStreamIn(playerid, forplayerid) { CJ_SendTo(forplayerid, playerid); return 1; }
//    // depois de carregar do banco / comprar roupa:
//    CJ_SetItem(playerid, CJ_LEGS, "shortskhaki", "shorts");
//    CJ_SetItem(playerid, CJ_TATTOO_0, "4weed");          // tatuagem: sem modelo
//    CJ_Sync(playerid);
//
//  O par (textura, modelo) precisa ser válido no jogo. O client valida e registra
//  no samp_log.txt as linhas "[CJ] ... ignorado" para qualquer par inválido.
// ============================================================================

#if defined _cj_clothes_included
    #endinput
#endif
#define _cj_clothes_included

#include <a_samp>
#include <Pawn.RakNet>

#define CJ_RPC_ID        (200)
#define CJ_COMPONENTS    (18)
#define CJ_NAME_LEN      (32)      // 31 caracteres + terminador

// Componentes (partes de textura) - conferidos na libGTASA.so 2.10 arm64
#define CJ_TORSO         (0)
#define CJ_HAIR          (1)       // modelo "head"
#define CJ_LEGS          (2)
#define CJ_SHOES         (3)
#define CJ_TATTOO_0      (4)       // 4..12 = 9 áreas de tatuagem (só textura)
#define CJ_TATTOO_1      (5)
#define CJ_TATTOO_2      (6)
#define CJ_TATTOO_3      (7)
#define CJ_TATTOO_4      (8)
#define CJ_TATTOO_5      (9)
#define CJ_TATTOO_6      (10)
#define CJ_TATTOO_7      (11)
#define CJ_TATTOO_8      (12)
#define CJ_NECKLACE      (13)
#define CJ_WATCH         (14)
#define CJ_GLASSES       (15)
#define CJ_HAT           (16)
#define CJ_EXTRA         (17)

static gCJTexture[MAX_PLAYERS][CJ_COMPONENTS][CJ_NAME_LEN];
static gCJModel[MAX_PLAYERS][CJ_COMPONENTS][CJ_NAME_LEN];
static bool:gCJUsed[MAX_PLAYERS][CJ_COMPONENTS];

stock CJ_ResetPlayer(playerid)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS) return 0;
    for (new c = 0; c < CJ_COMPONENTS; c++)
    {
        gCJUsed[playerid][c] = false;
        gCJTexture[playerid][c][0] = '\0';
        gCJModel[playerid][c][0] = '\0';
    }
    return 1;
}

// Define uma peça (não envia nada; chame CJ_Sync depois).
stock CJ_SetItem(playerid, component, const texture[], const model[] = "")
{
    if (playerid < 0 || playerid >= MAX_PLAYERS) return 0;
    if (component < 0 || component >= CJ_COMPONENTS) return 0;
    gCJUsed[playerid][component] = true;
    format(gCJTexture[playerid][component], CJ_NAME_LEN, "%s", texture);
    format(gCJModel[playerid][component], CJ_NAME_LEN, "%s", model);
    return 1;
}

// Remove tatuagem/acessório (componentes 4..17). Roupa base (0..3) não pode ser removida.
stock CJ_RemoveItem(playerid, component)
{
    return CJ_SetItem(playerid, component, "", "");
}

static stock CJ_WriteName(BitStream:bs, const name[])
{
    new len = strlen(name);
    if (len > CJ_NAME_LEN - 1) len = CJ_NAME_LEN - 1;
    BS_WriteValue(bs, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, name[i]);
}

// Envia o estado COMPLETO de 'owner' para o client 'sendTo' (substitui tudo).
stock CJ_SendTo(sendTo, owner)
{
    if (!IsPlayerConnected(sendTo) || owner < 0 || owner >= MAX_PLAYERS) return 0;

    new count = 0;
    for (new c = 0; c < CJ_COMPONENTS; c++) if (gCJUsed[owner][c]) count++;

    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, 1, PR_UINT8, count);
    for (new c = 0; c < CJ_COMPONENTS; c++)
    {
        if (!gCJUsed[owner][c]) continue;
        BS_WriteValue(bs, PR_UINT8, c);
        CJ_WriteName(bs, gCJTexture[owner][c]);
        CJ_WriteName(bs, gCJModel[owner][c]);
    }
    BS_RPC(bs, sendTo, CJ_RPC_ID, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

// Envia para o próprio jogador e para quem o está vendo (streamed in).
stock CJ_Sync(playerid)
{
    if (!IsPlayerConnected(playerid)) return 0;
    CJ_SendTo(playerid, playerid);
    for (new i = 0; i < MAX_PLAYERS; i++)
    {
        if (i == playerid || !IsPlayerConnected(i)) continue;
        if (IsPlayerStreamedIn(playerid, i)) CJ_SendTo(i, playerid);
    }
    return 1;
}
