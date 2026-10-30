// ============================================================================
//  maximo_look.inc - Visual por slots SINCRONIZADO para todos os jogadores (Maximo RP)
//
//  O servidor e a AUTORIDADE: guarda o visual de cada jogador (slots + base), valida, grava em disco
//  e envia para quem o ve. O cliente nunca manda o visual direto para os outros jogadores; ele PEDE
//  ao servidor pelo comando oculto /lookx (mesmo padrao do /invx do inventario).
//
//  Protocolo servidor -> cliente (ver LookSlots.h no cliente):
//    RPC 232  slots (SNAPSHOT completo, substitui os slots do jogador)
//       uint16 playerid | uint8 version(=1) | uint8 flags (bit0 = limpar tudo) | uint8 n |
//       n x [ uint8 slotId | uint8 estado | (se estado==2) uint16 variante ]
//       estado: 0 = oculto | 1 = textura original do DFF | 2 = variante do pack
//    RPC 231  base (corp, tom de pele, rosto, cabelo, roupas por pasta, intima) - formato do CorpTex:
//       uint16 playerid | uint8 len | char corp[len] | uint8 tom, rosto, estilo, cor,
//       bootSet, bootVar, pantsSet, pantsVar, shirtSet, shirtVar, intima
//    O cliente guarda 231 e 232 separados (a ordem de chegada nao importa).
//
//  Quando envia:
//    - quando o visual MUDA: para o dono e para todos que o tem em stream;
//    - OnPlayerStreamIn: para quem acabou de ve-lo (o cliente solta o visual no stream-out);
//    - spawn e troca de skin (7/271): para o dono e o stream.
//    Nunca periodico. Sem visual alterado (tudo padrao) o stream-in nao manda nada.
//
//  Comandos:
//    /lookx set <slotId> <estado 0-2|255> [variante]   (cliente; 255 = voltar ao padrao)
//    /lookx base <tom> <rosto> <estilo> <cor> <bota> <botaV> <calca> <calcaV> <camisa> <camisaV> <intima> <corp|->
//    /lookx clear | clearbase | sync
//    /setlook <id> <slot> <estado|padrao> [variante] | limpar | info     (admin RCON)
//
//  Persistencia: DOF2.1, scriptfiles/MaximoRP/Visual/<nome-codificado>.ini (mesma codificacao do inventario).
//  Tabela de slots: maximo_look_slots.inc (GERADA do SkinSlots.h do cliente por tools_gen_look_table.py).
//  Variantes validas por slot (opcional): scriptfiles/MaximoRP/Visual/variantes.ini ("slot=maior_variante").
//
//  Integracao no gamemode (ver comentarios no fim do arquivo):
//    #include <maximo_look.inc> depois de maximo_inv.inc;  Look_OnGameModeInit() no OnGameModeInit;
//    Look_OnPlayerConnect / Look_OnPlayerDisconnect / Look_OnPlayerSpawn / Look_OnPlayerStreamIn / Look_OnSkinChanged;
//    CMD:lookx e CMD:setlook.
//  IMPORTANTE: arquivo so com ASCII (os fontes do servidor sao Latin-1).
// ============================================================================
#if defined _maximo_look_included
    #endinput
#endif
#define _maximo_look_included

// limites do cache do DOF2.1: o maximo_inv.inc ja os define antes de incluir o DOF2 (nao redefinir aqui)
#include <DOF2.1>
#include <a_samp>
#include <Pawn.RakNet>
// ---- tabela de slots (antes em maximo_look_slots.inc; agora embutida: nao precisa mais daquele arquivo) ----
// GERADO por tools_gen_look_table.py a partir de SkinSlots.h (cliente). NAO EDITE A MAO.
// 61 slots, maior id 61, 10 grupos exclusivos. Id = identificador ESTAVEL da rede (RPC 232): nunca renumerar.
#define LOOK_SLOT_COUNT  (61)
#define LOOK_ID_MAX      (61)
#define LOOK_NAME_LEN    (16)

// indexados pelo ID do slot (ids sem slot = nome vazio, grupo -1, ev -1)
static const gLookSlotName[LOOK_ID_MAX + 1][LOOK_NAME_LEN] = {
    "",
    "torso1",
    "torso2",
    "torso3",
    "torso4",
    "pants1",
    "pants2",
    "pants3",
    "pants4",
    "short1",
    "short2",
    "short3",
    "boot1",
    "boot2",
    "boot3",
    "boot4",
    "slipper1",
    "slipper2",
    "slipper3",
    "hat1",
    "mask1",
    "mask2",
    "glasse1",
    "glasse2",
    "ear1",
    "ear2",
    "ear3",
    "ear4",
    "necklace1",
    "clock1",
    "clock2",
    "backpack1",
    "backpack2",
    "backpack3",
    "hair1",
    "hair2",
    "hair3",
    "hair4",
    "underwear",
    "head1",
    "head2",
    "head3",
    "head4",
    "head5",
    "brow1",
    "brow2",
    "brow3",
    "brow4",
    "brow5",
    "beard1",
    "beard2",
    "beard3",
    "beard4",
    "beard5",
    "skin_torso",
    "skin_leg",
    "skin_feet",
    "tat_back",
    "tat_chest",
    "tat_arm",
    "tat_shoulder",
    "tat_legs"
};
// grupo exclusivo (so UM visivel por grupo; -1 = sem grupo)
static const gLookSlotGroup[LOOK_ID_MAX + 1] = { -1, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 1, 1, 1, -1, 7, 7, 4, 4, 3, 3, 3, 3, -1, 2, 2, 0, 0, 0, 5, 5, 5, 5, -1, 6, 6, 6, 6, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
// evidencia no cliente: 2 = confirmado em jogo, 1 = tem material no DFF, 0 = HIPOTESE de mapeamento
static const gLookSlotEv[LOOK_ID_MAX + 1] = { -1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0 };

// ---- fim da tabela de slots ----

#define LOOK_RPC_SLOTS        (232)
#define LOOK_RPC_BASE         (231)
#define LOOK_VERSION          (1)
#define LOOK_MAX_SLOTS        (80)          // limite do protocolo (hoje existem LOOK_SLOT_COUNT)
#define LOOK_MAXVAR_DEFAULT   (1023)
#define LOOK_UNSET_WIRE       (255)         // /lookx set <id> 255 = volta ao padrao
#define LOOK_OP_COOLDOWN      (200)         // ms entre pedidos do cliente (<= 5/s)
#define LOOK_RESYNC_MS        (350)         // pedido descartado pelo limite: reenvia o estado real ao dono
#define LOOK_DIR              "MaximoRP/Visual/"
#define LOOK_VARFILE          "MaximoRP/Visual/variantes.ini"

// 1 = rejeita slots HIPOTESE (ev 0: pants/short, head, brow, beard, tat_*) de jogadores comuns; admin pode sempre.
// Comece com 0 para poder TESTAR essas pecas no jogo; ponha 1 quando quiser travar ate confirmar o mapeamento.
#define LOOK_BLOCK_UNCONFIRMED (0)
static bool:gLookBlockUnconfirmed = (LOOK_BLOCK_UNCONFIRMED != 0);

// 1 = escreve no log do servidor cada pedido /lookx (aceito ou rejeitado). Serve de PROVA no teste com 2 aparelhos.
#define LOOK_DEBUG_LOG (1)
static bool:gLookDebug = (LOOK_DEBUG_LOG != 0);

#define LOOK_SKIN_NORMAL      (7)           // so estes modelos usam o visual por slots (igual ao CorpTex do cliente)
#define LOOK_SKIN_COP         (271)

// pacote de slots: 5 bytes de cabecalho + ate 4 bytes por slot: tem que caber folgado
#assert (LOOK_MAX_SLOTS * 4 + 5) < 500

// ------------------------------------------------------------ estado --------
// slot: 0 = padrao (nao tocado); senao (estado + 1) | (variante << 2)   -> estado = (v & 3) - 1, variante = v >> 2
static gLookVal[MAX_PLAYERS][LOOK_ID_MAX + 1];
static gLookMaxVar[LOOK_ID_MAX + 1];
static bool:gLookVarFile = false;

enum E_LOOKBASE
{
    lbTone, lbFace, lbHair, lbHairColor,
    lbBootSet, lbBootVar, lbPantsSet, lbPantsVar, lbShirtSet, lbShirtVar,
    lbUnder,
    lbCorp[16]
}
static gLookBase[MAX_PLAYERS][E_LOOKBASE];

static bool:gLookLoaded[MAX_PLAYERS];       // false = arquivo ilegivel: NAO sobrescreve nem aceita pedidos
static gLookLastOp[MAX_PLAYERS];
static bool:gLookResync[MAX_PLAYERS];
static gLookLastSync[MAX_PLAYERS];
static gLookLastRefresh[MAX_PLAYERS];       // /atualizarvisual: NAO e zerado por Look_Reset (o refresh chama Look_Load)

// limites da base (espelham CorpTexMap.h do cliente)
static const gLookFaceMax[7]  = { 0, 6, 8, 8, 7, 5, 12 };    // rostos validos por tom 1..6
static const gLookHairMax[5]  = { 0, 6, 6, 5, 5 };           // cores validas por estilo 1..4

forward Look_ResyncTimer(playerid);

// ------------------------------------------------------------ tabela --------
stock Look_FindSlot(const name[])
{
    if (!name[0]) return 0;
    for (new id = 1; id <= LOOK_ID_MAX; id++)
        if (gLookSlotName[id][0] && !strcmp(name, gLookSlotName[id], true)) return id;
    return 0;
}

static bool:Look_SlotExists(id)
{
    return id >= 1 && id <= LOOK_ID_MAX && gLookSlotName[id][0] != 0;
}

static Look_LoadVariants()
{
    new id;
    for (id = 0; id <= LOOK_ID_MAX; id++) gLookMaxVar[id] = LOOK_MAXVAR_DEFAULT;
    gLookVarFile = false;
    if (!fexist(LOOK_VARFILE))
    {
        print("[LOOK] variantes.ini ausente: aceitando variantes 0..1023 (o cliente descarta a que nao existir no pack)");
        return;
    }
    new File:f = fopen(LOOK_VARFILE, io_read), line[64], n = 0;
    if (!f) return;
    while (fread(f, line))
    {
        if (line[0] == ';' || line[0] == '#') continue;
        new name[LOOK_NAME_LEN], mx;
        if (sscanf(line, "p<=>s[16]i", name, mx)) continue;
        new sid = Look_FindSlot(name);
        if (!sid) continue;
        if (mx < 0) mx = 0;
        if (mx > LOOK_MAXVAR_DEFAULT) mx = LOOK_MAXVAR_DEFAULT;
        gLookMaxVar[sid] = mx;
        n++;
    }
    fclose(f);
    gLookVarFile = true;
    printf("[LOOK] variantes.ini: %d slot(s) com maximo de variante definido", n);
}

// ------------------------------------------------------------ validacao -----
// slot/estado/variante validos? (nao mexe em nada)
static bool:Look_SlotValid(id, est, var, bool:admin)
{
    if (!Look_SlotExists(id)) return false;
    if (est < 0 || est > 2) return false;
    if (est == 2)
    {
        if (var < 0 || var > LOOK_MAXVAR_DEFAULT) return false;
        if (gLookVarFile && var > gLookMaxVar[id]) return false;          // pasta sem variantes: max 0 => so a variante 0
    }
    if (gLookBlockUnconfirmed && !admin && gLookSlotEv[id] == 0) return false;
    return true;
}

static bool:Look_CorpValid(const s[])
{
    new len = strlen(s);
    if (len < 1 || len > 15) return false;
    for (new i = 0; i < len; i++)
    {
        new c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return strcmp(s, "aparencia") != 0;
}

// ------------------------------------------------------------ envio ---------
static bool:Look_IsHostSkin(skin) return skin == LOOK_SKIN_NORMAL || skin == LOOK_SKIN_COP;

static bool:Look_HasSlots(owner)
{
    for (new id = 1; id <= LOOK_ID_MAX; id++) if (gLookVal[owner][id]) return true;
    return false;
}

static bool:Look_BaseActive(owner)
{
    if (gLookBase[owner][lbTone] || gLookBase[owner][lbHair] || gLookBase[owner][lbBootSet] ||
        gLookBase[owner][lbPantsSet] || gLookBase[owner][lbShirtSet] || gLookBase[owner][lbUnder] || gLookBase[owner][lbCorp][0]) return true;
    return false;
}

// RPC 232 (SNAPSHOT dos slots)
static Look_SendSlots(dest, owner)
{
    new n = 0, id;
    for (id = 1; id <= LOOK_ID_MAX; id++) if (gLookVal[owner][id]) n++;

    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, LOOK_VERSION, PR_UINT8, (n == 0) ? 1 : 0, PR_UINT8, n);
    for (id = 1; id <= LOOK_ID_MAX; id++)
    {
        new v = gLookVal[owner][id];
        if (!v) continue;
        new est = (v & 3) - 1;
        BS_WriteValue(bs, PR_UINT8, id, PR_UINT8, est);
        if (est == 2) BS_WriteValue(bs, PR_UINT16, v >> 2);
    }
    PR_SendRPC(bs, dest, LOOK_RPC_SLOTS, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    if (gLookDebug) printf("[LOOK] RPC %d: dono p%d -> destino p%d (%d slot(s))", LOOK_RPC_SLOTS, owner, dest, n);
    return 1;
}

// RPC 231 (base: formato do CorpTex)
static Look_SendBase(dest, owner)
{
    new len = strlen(gLookBase[owner][lbCorp]);
    if (len > 15) len = 15;
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT16, owner, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, gLookBase[owner][lbCorp][i]);
    BS_WriteValue(bs,
        PR_UINT8, gLookBase[owner][lbTone],     PR_UINT8, gLookBase[owner][lbFace],
        PR_UINT8, gLookBase[owner][lbHair],     PR_UINT8, gLookBase[owner][lbHairColor],
        PR_UINT8, gLookBase[owner][lbBootSet],  PR_UINT8, gLookBase[owner][lbBootVar],
        PR_UINT8, gLookBase[owner][lbPantsSet], PR_UINT8, gLookBase[owner][lbPantsVar],
        PR_UINT8, gLookBase[owner][lbShirtSet], PR_UINT8, gLookBase[owner][lbShirtVar],
        PR_UINT8, gLookBase[owner][lbUnder]);
    PR_SendRPC(bs, dest, LOOK_RPC_BASE, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

// manda o visual de `owner` para `dest`. force = true: manda mesmo que esteja tudo padrao (precisa na MUDANCA, para
// apagar o que o outro lado tinha; no stream-in o cliente comeca vazio, entao padrao nao precisa mandar nada).
static Look_SendTo(dest, owner, bool:force)
{
    if (!IsPlayerConnected(dest) || !IsPlayerConnected(owner) || IsPlayerNPC(dest)) return 0;
    // o proprio dono recebe sempre (o 1o 232 liga o modo online no cliente); os outros so se ele usa o modelo certo
    if (dest != owner && !Look_IsHostSkin(GetPlayerSkin(owner))) return 0;
    if (force || Look_BaseActive(owner)) Look_SendBase(dest, owner);
    if (force || Look_HasSlots(owner))   Look_SendSlots(dest, owner);
    return 1;
}

// dono + todos que o tem em stream. IsPlayerStreamedIn(dono, i) = "dono esta em stream para i".
// (Nao usa PR_SendRPCToPlayerStream: depende de GetPlayerPoolSize, quebrada no open.mp - ver customanim.inc.)
stock Look_Broadcast(owner, bool:force = true)
{
    if (!IsPlayerConnected(owner)) return 0;
    Look_SendTo(owner, owner, force);
    for (new i = 0; i < MAX_PLAYERS; i++)
    {
        if (i == owner || !IsPlayerConnected(i) || IsPlayerNPC(i)) continue;
        if (IsPlayerStreamedIn(owner, i)) Look_SendTo(i, owner, force);
    }
    return 1;
}

public Look_ResyncTimer(playerid)
{
    gLookResync[playerid] = false;
    if (IsPlayerConnected(playerid)) Look_SendTo(playerid, playerid, true);
    return 1;
}

// ------------------------------------------------------------ persistencia --
static Look_StoreAbort() { DOF2_SetFile(""); }

static bool:Look_StoreEnsure(const file[])
{
    if (fexist(file)) return true;
    return DOF2_CreateFile(file) != 0;
}

static bool:Look_StoreFlush(const file[])
{
    if (DOF2_SaveFile() && fexist(file)) return true;
    Look_StoreAbort();
    return false;
}

// MESMA codificacao do Inv_PlayerFile (minusculo; fora de [a-z0-9_] vira %hh)
static Look_PlayerFile(playerid, dest[], size = sizeof(dest))
{
    new name[MAX_PLAYER_NAME + 1], enc[96], j = 0;
    new const hx[] = "0123456789abcdef";
    GetPlayerName(playerid, name, sizeof name);
    for (new i = 0; name[i] && j < sizeof(enc) - 4; i++)
    {
        new c = name[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') enc[j++] = c;
        else { enc[j++] = '%'; enc[j++] = hx[(c >> 4) & 15]; enc[j++] = hx[c & 15]; }
    }
    enc[j] = 0;
    format(dest, size, LOOK_DIR "%s.ini", enc);
}

static bool:Look_Save(playerid)
{
    new file[128], key[12], val[24], name[MAX_PLAYER_NAME + 1];
    Look_PlayerFile(playerid, file);
    if (!Look_StoreEnsure(file)) return false;
    GetPlayerName(playerid, name, sizeof name);
    if (!DOF2_SetString(file, "nome", name)) { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "rev", DOF2_GetInt(file, "rev") + 1)) { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "b_tom",    gLookBase[playerid][lbTone]))      { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "b_rosto",  gLookBase[playerid][lbFace]))      { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "b_estilo", gLookBase[playerid][lbHair]))      { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "b_cor",    gLookBase[playerid][lbHairColor])) { Look_StoreAbort(); return false; }
    format(val, sizeof val, "%d,%d", gLookBase[playerid][lbBootSet],  gLookBase[playerid][lbBootVar]);
    if (!DOF2_SetString(file, "b_bota", val))   { Look_StoreAbort(); return false; }
    format(val, sizeof val, "%d,%d", gLookBase[playerid][lbPantsSet], gLookBase[playerid][lbPantsVar]);
    if (!DOF2_SetString(file, "b_calca", val))  { Look_StoreAbort(); return false; }
    format(val, sizeof val, "%d,%d", gLookBase[playerid][lbShirtSet], gLookBase[playerid][lbShirtVar]);
    if (!DOF2_SetString(file, "b_camisa", val)) { Look_StoreAbort(); return false; }
    if (!DOF2_SetInt(file, "b_intima", gLookBase[playerid][lbUnder]))     { Look_StoreAbort(); return false; }
    new cp[16];
    if (gLookBase[playerid][lbCorp][0]) strcat(cp, gLookBase[playerid][lbCorp], sizeof cp); else strcat(cp, "-", sizeof cp);
    if (!DOF2_SetString(file, "b_corp", cp)) { Look_StoreAbort(); return false; }
    for (new id = 1; id <= LOOK_ID_MAX; id++)
    {
        if (!gLookSlotName[id][0]) continue;
        new v = gLookVal[playerid][id];
        format(key, sizeof key, "s%d", id);
        if (v) format(val, sizeof val, "%d,%d", (v & 3) - 1, v >> 2);
        else   format(val, sizeof val, "%d,0", LOOK_UNSET_WIRE);
        if (!DOF2_SetString(file, key, val)) { Look_StoreAbort(); return false; }
    }
    return Look_StoreFlush(file);
}

static Look_Reset(playerid)
{
    for (new id = 0; id <= LOOK_ID_MAX; id++) gLookVal[playerid][id] = 0;
    gLookBase[playerid][lbTone] = 0;   gLookBase[playerid][lbFace] = 0;
    gLookBase[playerid][lbHair] = 0;   gLookBase[playerid][lbHairColor] = 0;
    gLookBase[playerid][lbBootSet] = 0;  gLookBase[playerid][lbBootVar] = 0;
    gLookBase[playerid][lbPantsSet] = 0; gLookBase[playerid][lbPantsVar] = 0;
    gLookBase[playerid][lbShirtSet] = 0; gLookBase[playerid][lbShirtVar] = 0;
    gLookBase[playerid][lbUnder] = 0;
    gLookBase[playerid][lbCorp][0] = 0;
    gLookLoaded[playerid] = false;
    gLookResync[playerid] = false;
    gLookLastOp[playerid] = 0;
    gLookLastSync[playerid] = 0;
}

// aplica um slot validado: ligar uma peca VISIVEL de um grupo exclusivo volta ao padrao as outras visiveis do grupo
static Look_PutSlot(playerid, id, est, var)
{
    if (est == LOOK_UNSET_WIRE) { gLookVal[playerid][id] = 0; return; }
    if (est > 0 && gLookSlotGroup[id] >= 0)
    {
        for (new o = 1; o <= LOOK_ID_MAX; o++)
        {
            if (o == id || gLookSlotGroup[o] != gLookSlotGroup[id]) continue;
            if (gLookVal[playerid][o] && ((gLookVal[playerid][o] & 3) - 1) != 0) gLookVal[playerid][o] = 0;
        }
    }
    gLookVal[playerid][id] = (est + 1) | ((est == 2 ? var : 0) << 2);
}

static Look_SanitizeBase(playerid)
{
    new t = gLookBase[playerid][lbTone];
    if (t < 0 || t > 6) t = 0;
    gLookBase[playerid][lbTone] = t;
    if (t) { if (gLookBase[playerid][lbFace] < 1) gLookBase[playerid][lbFace] = 1; if (gLookBase[playerid][lbFace] > gLookFaceMax[t]) gLookBase[playerid][lbFace] = gLookFaceMax[t]; }
    else gLookBase[playerid][lbFace] = 0;

    new h = gLookBase[playerid][lbHair];
    if (h < 0 || h > 4) h = 0;
    gLookBase[playerid][lbHair] = h;
    if (h) { if (gLookBase[playerid][lbHairColor] < 1) gLookBase[playerid][lbHairColor] = 1; if (gLookBase[playerid][lbHairColor] > gLookHairMax[h]) gLookBase[playerid][lbHairColor] = gLookHairMax[h]; }
    else gLookBase[playerid][lbHairColor] = 0;

    if (gLookBase[playerid][lbBootSet] < 0  || gLookBase[playerid][lbBootSet] > 9)  gLookBase[playerid][lbBootSet] = 0;
    if (gLookBase[playerid][lbPantsSet] < 0 || gLookBase[playerid][lbPantsSet] > 9) gLookBase[playerid][lbPantsSet] = 0;
    if (gLookBase[playerid][lbShirtSet] < 0 || gLookBase[playerid][lbShirtSet] > 9) gLookBase[playerid][lbShirtSet] = 0;
    if (!gLookBase[playerid][lbBootSet])  gLookBase[playerid][lbBootVar] = 0;
    else if (gLookBase[playerid][lbBootVar] < 1 || gLookBase[playerid][lbBootVar] > 255) gLookBase[playerid][lbBootVar] = 1;
    if (!gLookBase[playerid][lbPantsSet]) gLookBase[playerid][lbPantsVar] = 0;
    else if (gLookBase[playerid][lbPantsVar] < 1 || gLookBase[playerid][lbPantsVar] > 255) gLookBase[playerid][lbPantsVar] = 1;
    if (!gLookBase[playerid][lbShirtSet]) gLookBase[playerid][lbShirtVar] = 0;
    else if (gLookBase[playerid][lbShirtVar] < 1 || gLookBase[playerid][lbShirtVar] > 255) gLookBase[playerid][lbShirtVar] = 1;

    new u = gLookBase[playerid][lbUnder];
    if (u < 0 || (u > 6 && u != 9)) u = 0;
    gLookBase[playerid][lbUnder] = u;
    if (gLookBase[playerid][lbCorp][0] && !Look_CorpValid(gLookBase[playerid][lbCorp])) gLookBase[playerid][lbCorp][0] = 0;
}

// Visual inicial ("homem base"): pele do corpo + rosto/sobrancelha/cabelo/intima em variante 1.
// O DFF do modelo 7 nao tem as texturas originais (viram male01 = invisivel), entao o corpo SO aparece como variante PNG (estado 2).
// Slots nao listados continuam padrao (= invisiveis), nao precisa mandar 'off' para os outros.
static Look_ApplyDefault(playerid)
{
    static const defNames[][12] = { "skin_torso", "skin_leg", "skin_feet", "head1", "brow1", "hair1", "underwear" };
    for (new i = 0; i < sizeof defNames; i++)
    {
        new id = Look_FindSlot(defNames[i]);
        if (id) gLookVal[playerid][id] = 3 | (1 << 2);          // estado 2 (variante) + variante 1  => (2+1) | (1<<2)
    }
}

static bool:Look_Load(playerid)
{
    new file[128], key[12], val[24], name[MAX_PLAYER_NAME + 1], tmp[16];
    Look_Reset(playerid);
    Look_PlayerFile(playerid, file);
    if (!fexist(file))
    {
        // jogador novo: homem base E o arquivo ja nasce AGORA com o visual padrao (antes so nascia na 1a mudanca no /provador,
        // e quem nunca salvou ficava sem arquivo). Se nao der para gravar, vale so nesta sessao (aviso no log).
        Look_ApplyDefault(playerid);
        gLookLoaded[playerid] = true;
        if (!IsPlayerNPC(playerid) && !Look_Save(playerid)) printf("[LOOK] AVISO: nao foi possivel criar %s para o jogador novo %d (vale so nesta sessao)", file, playerid);
        else if (gLookDebug) printf("[LOOK] p%d: jogador novo -> %s criado com o visual padrao", playerid, file);
        return true;
    }
    // existe mas sem "nome" = ilegivel/corrompido: NAO libera (evita sobrescrever dados reais)
    if (!DOF2_GetStringEx(file, "nome", name, sizeof name))
    {
        printf("[LOOK] ERRO: %s ilegivel. Visual bloqueado para evitar sobrescrita.", file);
        return false;
    }
    gLookBase[playerid][lbTone]      = DOF2_GetInt(file, "b_tom");
    gLookBase[playerid][lbFace]      = DOF2_GetInt(file, "b_rosto");
    gLookBase[playerid][lbHair]      = DOF2_GetInt(file, "b_estilo");
    gLookBase[playerid][lbHairColor] = DOF2_GetInt(file, "b_cor");
    gLookBase[playerid][lbUnder]     = DOF2_GetInt(file, "b_intima");
    new a, b;
    if (DOF2_GetStringEx(file, "b_bota", val, sizeof val)   && !sscanf(val, "p<,>ii", a, b)) { gLookBase[playerid][lbBootSet]  = a; gLookBase[playerid][lbBootVar]  = b; }
    if (DOF2_GetStringEx(file, "b_calca", val, sizeof val)  && !sscanf(val, "p<,>ii", a, b)) { gLookBase[playerid][lbPantsSet] = a; gLookBase[playerid][lbPantsVar] = b; }
    if (DOF2_GetStringEx(file, "b_camisa", val, sizeof val) && !sscanf(val, "p<,>ii", a, b)) { gLookBase[playerid][lbShirtSet] = a; gLookBase[playerid][lbShirtVar] = b; }
    if (DOF2_GetStringEx(file, "b_corp", tmp, sizeof tmp) && strcmp(tmp, "-")) strcat(gLookBase[playerid][lbCorp], tmp, 16);
    Look_SanitizeBase(playerid);

    new bad = 0;
    for (new id = 1; id <= LOOK_ID_MAX; id++)
    {
        if (!gLookSlotName[id][0]) continue;
        format(key, sizeof key, "s%d", id);
        if (!DOF2_GetStringEx(file, key, val, sizeof val)) continue;
        new est, var;
        if (sscanf(val, "p<,>ii", est, var)) { bad++; continue; }
        if (est == LOOK_UNSET_WIRE) continue;
        if (!Look_SlotValid(id, est, var, true)) { bad++; continue; }
        gLookVal[playerid][id] = (est + 1) | ((est == 2 ? var : 0) << 2);
    }
    if (bad)
    {
        new bak[140];
        format(bak, sizeof bak, "%s.bad", file);
        if (!fexist(bak)) DOF2_CopyFile(file, bak);
        printf("[LOOK] %d entrada(s) invalida(s) em %s (copia em %s)", bad, file, bak);
    }
    gLookLoaded[playerid] = true;
    return true;
}

// ------------------------------------------------------------ ganchos -------
stock Look_OnGameModeInit()
{
    Look_LoadVariants();
    for (new i = 0; i < MAX_PLAYERS; i++) Look_Reset(i);
	#if LOOK_BLOCK_UNCONFIRMED
	    printf("[LOOK] visual por slots: %d slots, RPC %d (slots) + %d (base), slots hipotese BLOQUEADOS", LOOK_SLOT_COUNT, LOOK_RPC_SLOTS, LOOK_RPC_BASE);
	#else
	    printf("[LOOK] visual por slots: %d slots, RPC %d (slots) + %d (base)", LOOK_SLOT_COUNT, LOOK_RPC_SLOTS, LOOK_RPC_BASE);
	#endif
}

stock Look_OnPlayerConnect(playerid)
{
    gLookLastRefresh[playerid] = 0;
    Look_Load(playerid);             // zera o estado do id (nunca herda o jogador anterior) e le o arquivo do NOME
}

stock Look_OnPlayerDisconnect(playerid)
{
    Look_Reset(playerid);            // zera tambem na saida (o idcard do gamemode nao faz isso: aqui nao deixa o id vazar)
}

stock Look_OnPlayerSpawn(playerid)
{
    Look_Broadcast(playerid, true);  // dono (liga o modo online no cliente) + quem o ve
}

// OnPlayerStreamIn(playerid, forplayerid): playerid apareceu para forplayerid
stock Look_OnPlayerStreamIn(playerid, forplayerid)
{
    Look_SendTo(forplayerid, playerid, false);
}

// chamar depois de SetPlayerSkin (o open.mp nao tem callback de troca de skin)
stock Look_OnSkinChanged(playerid)
{
    if (Look_IsHostSkin(GetPlayerSkin(playerid))) Look_Broadcast(playerid, true);
}

// ------------------------------------------------------------ comandos ------
static bool:Look_Throttle(playerid)
{
    new t = GetTickCount();
    if (t >= gLookLastOp[playerid] && t - gLookLastOp[playerid] < LOOK_OP_COOLDOWN) return true;
    gLookLastOp[playerid] = t;
    return false;
}

static Look_ScheduleResync(playerid)
{
    if (gLookResync[playerid]) return;
    gLookResync[playerid] = true;
    SetTimerEx("Look_ResyncTimer", LOOK_RESYNC_MS, false, "i", playerid);
}

// grava e retransmite. Se nao gravar, o visual continua valendo nesta sessao (so avisa no log).
static Look_Commit(playerid)
{
    if (!Look_Save(playerid)) printf("[LOOK] AVISO: nao foi possivel gravar o visual de %d (vale so nesta sessao)", playerid);
    Look_Broadcast(playerid, true);
}

// /atualizarvisual: recarrega o visual do ARQUIVO do jogador (scriptfiles/MaximoRP/Visual/<nome>.ini) e reenvia
//   1) o dele para ele e para todos que o veem (stream)   2) o de todos que ele ve, para ele.
// Serve para corrigir quem esta vendo roupa errada/repetida. Sem arquivo: cria com o visual atual. Arquivo ilegivel: NAO mexe
// no que esta em memoria. Limite: 1 uso a cada 3 s por jogador.
#define LOOK_REFRESH_COOLDOWN (3000)
stock Look_RefreshCommand(playerid)
{
    if (!IsPlayerConnected(playerid) || IsPlayerNPC(playerid)) return 1;
    new t = GetTickCount();
    if (gLookLastRefresh[playerid] && t >= gLookLastRefresh[playerid] && t - gLookLastRefresh[playerid] < LOOK_REFRESH_COOLDOWN)
        return SendClientMessage(playerid, 0xFFAA00FF, "Aguarde alguns segundos para atualizar o visual de novo.");
    gLookLastRefresh[playerid] = t;

    new file[128], nm[MAX_PLAYER_NAME + 1];
    Look_PlayerFile(playerid, file);
    if (!fexist(file))
    {
        if (!gLookLoaded[playerid]) return SendClientMessage(playerid, 0xFF0000FF, "Seu visual esta bloqueado (arquivo ilegivel). Avise um admin.");
        if (!Look_Save(playerid)) printf("[LOOK] AVISO: nao foi possivel criar %s no /atualizarvisual de %d", file, playerid);
    }
    else
    {
        if (!DOF2_GetStringEx(file, "nome", nm, sizeof nm))
        {
            printf("[LOOK] ERRO: %s ilegivel no /atualizarvisual de %d (mantido o visual da memoria)", file, playerid);
            return SendClientMessage(playerid, 0xFF0000FF, "Arquivo de visual ilegivel. Avise um admin.");
        }
        Look_Load(playerid);                    // zera e recarrega do arquivo (so chega aqui com o arquivo legivel)
    }
    Look_Broadcast(playerid, true);             // dono + quem o ve
    new n = 0;
    for (new i = 0; i < MAX_PLAYERS; i++)       // o que o dono ve: reenvia de todos (corrige roupa errada/repetida)
    {
        if (i == playerid || !IsPlayerConnected(i) || IsPlayerNPC(i)) continue;
        if (IsPlayerStreamedIn(i, playerid)) { Look_SendTo(playerid, i, true); n++; }
    }
    if (gLookDebug) printf("[LOOK] p%d: /atualizarvisual OK (arquivo recarregado; reenviado a quem o ve e %d jogador(es) para ele)", playerid, n);
    return SendClientMessage(playerid, 0x00CC66FF, "Visual atualizado para voce e para os jogadores proximos.");
}

// /lookx <acao> ...   (enviado pelo cliente; o jogador tambem pode digitar)
stock Look_Command(playerid, const params[])
{
    // s[10] como ULTIMO especificador consome a linha inteira ("set 1 2 2"), estoura com 10+ chars e nunca bate com "set".
    // Por isso a cauda "S()[128]" absorve o resto e s[10] fica so com a 1a palavra.
    new sub[10], tail[128];
    if (sscanf(params, "s[10]S()[128]", sub, tail)) return 1;

    if (!strcmp(sub, "sync", true))
    {
        new t = GetTickCount();
        if (t >= gLookLastSync[playerid] && t - gLookLastSync[playerid] < 1000) return 1;
        gLookLastSync[playerid] = t;
        Look_SendTo(playerid, playerid, true);
        return 1;
    }
    if (!strcmp(sub, "refresh", true)) return Look_RefreshCommand(playerid);
    if (!gLookLoaded[playerid])
    {
        if (gLookDebug) printf("[LOOK] p%d: /lookx %s IGNORADO (visual bloqueado: arquivo ilegivel)", playerid, params);
        return 1;
    }
    if (Look_Throttle(playerid))
    {
        if (gLookDebug) printf("[LOOK] p%d: /lookx %s DESCARTADO (menos de %d ms do anterior); reenviando estado ao dono", playerid, params, LOOK_OP_COOLDOWN);
        Look_ScheduleResync(playerid);
        return 1;                                                                   // descartado: o dono recebe o estado real
    }

    if (!strcmp(sub, "set", true))
    {
        new id, est, var;
        if (sscanf(params, "{s[10]}iiI(0)", id, est, var))
        {
            if (gLookDebug) printf("[LOOK] p%d: set REJEITADO (parametros invalidos): '%s'", playerid, params);
            Look_ScheduleResync(playerid);
            return 1;
        }
        if (est == LOOK_UNSET_WIRE)
        {
            if (!Look_SlotExists(id))
            {
                if (gLookDebug) printf("[LOOK] p%d: set REJEITADO (slot %d nao existe)", playerid, id);
                Look_ScheduleResync(playerid);
                return 1;
            }
        }
        else if (!Look_SlotValid(id, est, var, false))
        {
            if (gLookDebug) printf("[LOOK] p%d: set REJEITADO (slot %d estado %d variante %d invalidos)", playerid, id, est, var);
            Look_ScheduleResync(playerid);
            return 1;
        }
        Look_PutSlot(playerid, id, est, var);
        if (gLookDebug) printf("[LOOK] p%d: set OK slot %d (%s) estado %d variante %d -> gravando e enviando (dono + stream)", playerid, id, gLookSlotName[id], est, var);
        Look_Commit(playerid);
        return 1;
    }
    if (!strcmp(sub, "reset", true))
    {
        if (gLookDebug) printf("[LOOK] p%d: reset OK (volta ao visual base)", playerid);
        for (new id = 1; id <= LOOK_ID_MAX; id++) gLookVal[playerid][id] = 0;
        Look_ApplyDefault(playerid);
        Look_Commit(playerid);
        return 1;
    }
    if (!strcmp(sub, "clear", true))
    {
        for (new id = 1; id <= LOOK_ID_MAX; id++) gLookVal[playerid][id] = 0;
        Look_Commit(playerid);
        return 1;
    }
    if (!strcmp(sub, "clearbase", true))
    {
        gLookBase[playerid][lbTone] = 0; gLookBase[playerid][lbFace] = 0; gLookBase[playerid][lbHair] = 0; gLookBase[playerid][lbHairColor] = 0;
        gLookBase[playerid][lbBootSet] = 0; gLookBase[playerid][lbBootVar] = 0; gLookBase[playerid][lbPantsSet] = 0; gLookBase[playerid][lbPantsVar] = 0;
        gLookBase[playerid][lbShirtSet] = 0; gLookBase[playerid][lbShirtVar] = 0; gLookBase[playerid][lbUnder] = 0; gLookBase[playerid][lbCorp][0] = 0;
        Look_Commit(playerid);
        return 1;
    }
    if (!strcmp(sub, "base", true))
    {
        new tone, face, hair, color, bs, bv, ps, pv, ss, sv, under, corp[20];
        if (sscanf(params, "{s[10]}iiiiiiiiiiis[20]", tone, face, hair, color, bs, bv, ps, pv, ss, sv, under, corp)) { Look_ScheduleResync(playerid); return 1; }
        if (strcmp(corp, "-") && !Look_CorpValid(corp)) { Look_ScheduleResync(playerid); return 1; }
        gLookBase[playerid][lbTone] = tone; gLookBase[playerid][lbFace] = face; gLookBase[playerid][lbHair] = hair; gLookBase[playerid][lbHairColor] = color;
        gLookBase[playerid][lbBootSet] = bs; gLookBase[playerid][lbBootVar] = bv; gLookBase[playerid][lbPantsSet] = ps; gLookBase[playerid][lbPantsVar] = pv;
        gLookBase[playerid][lbShirtSet] = ss; gLookBase[playerid][lbShirtVar] = sv; gLookBase[playerid][lbUnder] = under;
        gLookBase[playerid][lbCorp][0] = 0;
        if (strcmp(corp, "-")) strcat(gLookBase[playerid][lbCorp], corp, 16);
        Look_SanitizeBase(playerid);
        Look_Commit(playerid);
        return 1;
    }
    return 1;
}

// /setlook <id> <slot> <estado|padrao> [variante]  |  /setlook <id> limpar  |  /setlook <id> info     (admin RCON)
stock Look_AdminCommand(playerid, const params[])
{
//    if (!IsPlayerAdmin(playerid)) return SendClientMessage(playerid, 0xFF0000FF, "Apenas admin (RCON).");
    new target, name[LOOK_NAME_LEN], arg[12], var;
    if (sscanf(params, "us[16]S()[12]I(0)", target, name, arg, var))
        return SendClientMessage(playerid, -1, "Use: /setlook [id] [slot|limpar|info] [0 oculto|1 original|2 variante|padrao] [variante]");
    if (!IsPlayerConnected(target)) return SendClientMessage(playerid, 0xFF0000FF, "Jogador offline.");
    if (!gLookLoaded[target]) return SendClientMessage(playerid, 0xFF0000FF, "Visual desse jogador esta bloqueado (arquivo ilegivel).");

    if (!strcmp(name, "limpar", true))
    {
        for (new id = 1; id <= LOOK_ID_MAX; id++) gLookVal[target][id] = 0;
        Look_Commit(target);
        return SendClientMessage(playerid, 0x00FF00FF, "Slots limpos.");
    }
    if (!strcmp(name, "info", true))
    {
        new msg[144], n = 0;
        for (new id = 1; id <= LOOK_ID_MAX; id++)
        {
            new v = gLookVal[target][id];
            if (!v) continue;
            new est = (v & 3) - 1;
            if (est == 2)      format(msg, sizeof msg, "%s = variante %d", gLookSlotName[id], v >> 2);
            else if (est == 1) format(msg, sizeof msg, "%s = original", gLookSlotName[id]);
            else               format(msg, sizeof msg, "%s = oculto", gLookSlotName[id]);
            SendClientMessage(playerid, -1, msg);
            n++;
        }
        format(msg, sizeof msg, "%d slot(s) definido(s); skin %d; base: tom %d rosto %d cabelo %d/%d", n, GetPlayerSkin(target),
            gLookBase[target][lbTone], gLookBase[target][lbFace], gLookBase[target][lbHair], gLookBase[target][lbHairColor]);
        return SendClientMessage(playerid, 0x00FF00FF, msg);
    }
    new id = Look_FindSlot(name);
    if (!id) return SendClientMessage(playerid, 0xFF0000FF, "Slot desconhecido (nomes: torso1, pants1, hair1, hat1, ...).");
    new est;
    if (!strcmp(arg, "padrao", true))                       est = LOOK_UNSET_WIRE;
    else if (!strcmp(arg, "off", true) || !strcmp(arg, "0")) est = 0;
    else if (!strcmp(arg, "on", true)  || !strcmp(arg, "1")) est = 1;
    else if (!strcmp(arg, "2"))                              est = 2;
    else return SendClientMessage(playerid, 0xFF0000FF, "Estado: 0|off oculto, 1|on original, 2 variante, padrao.");
    if (est != LOOK_UNSET_WIRE && !Look_SlotValid(id, est, var, true))
        return SendClientMessage(playerid, 0xFF0000FF, "Estado/variante invalidos para esse slot.");
    Look_PutSlot(target, id, est, var);
    Look_Commit(target);
    return SendClientMessage(playerid, 0x00FF00FF, "Slot aplicado e enviado.");
}

// ----------------------------------------------------------------------------
// TEMPORARIO (STUB): o maximorp.pwn atual tem CMD:look -> Look_TextCommand, que existia numa versao mais nova deste
// arquivo (nao enviada). Este stub so deixa o servidor COMPILAR; /look nao faz nada ate voltar a funcao real.
// Se voce restaurar o seu maximo_look.inc com Look_TextCommand, APAGUE este bloco (senao da "symbol already defined").
// ----------------------------------------------------------------------------
stock Look_TextCommand(playerid, const params[])
{
    #pragma unused params
    return SendClientMessage(playerid, 0xFF0000FF, "/look indisponivel nesta versao do servidor.");
}

// ============================================================================
//  INTEGRACAO NO maximorp.pwn  (mudancas minimas):
//
//    #include <maximo_look.inc>                     // logo depois de #include <maximo_inv.inc>
//
//    public OnGameModeInit()      { ...  Inv_Init();  Look_OnGameModeInit();  return 1; }
//    public OnPlayerConnect(...)  { ...  Inv_OnPlayerConnect(playerid);  Look_OnPlayerConnect(playerid);  return 1; }
//    public OnPlayerDisconnect()  { Inv_OnPlayerDisconnect(playerid);  Look_OnPlayerDisconnect(playerid);  return 1; }
//    public OnPlayerSpawn(...)    { ...  Look_OnPlayerSpawn(playerid);  return 1; }
//    public OnPlayerStreamIn(playerid, forplayerid) { Look_OnPlayerStreamIn(playerid, forplayerid); return 1; }
//    CMD:minhaskin: depois de SetPlayerSkin(playerid, idHP);  Look_OnSkinChanged(playerid);
//    CMD:lookx(playerid, params[])   return Look_Command(playerid, params);
//    CMD:setlook(playerid, params[]) return Look_AdminCommand(playerid, params);
// ============================================================================
