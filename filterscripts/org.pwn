// org.pwn - filterscript open.mp: ORGANIZACOES do Maximo RP (comeca pela Policia Militar).
//
//  * Tudo fica no PostgreSQL (celular-service, migracao 012) e e cadastrado pelo painel web  http://127.0.0.1:8789/organizacoes
//    ou pelos comandos de admin abaixo. Este script le a lista sozinho (consulta de versao a cada 30 s).
//  * Cada organizacao tem: uniforme (skin 271 + /corp <corp>, ex.: PM = goe), carros (id do cliente + modelo base do servidor),
//    NPC de BATER CARTAO (bate o ponto e veste/tira o uniforme) e NPC de GARAGEM (pega/guarda a viatura) e membros.
//  * Os NPCs sao actors; o MARCADOR (anel no chao + botao) e desenhado pelo CLIENTE (RPC 245 op 7, tipo 5). Tudo e por TOQUE
//    (o Android nao tem tecla E): tocar no NPC ou no botao ao lado dele. O cliente envia RPC 246 op 10 (botao) ou op 1 (toque no actor).
//  * Carros: o servidor cria com o modelo BASE (400-611, ex.: 596) e o ORPC 164 (WorldVehicleAdd) troca para o id novo (612, 794...)
//    so no pacote enviado aos jogadores - mesma tecnica do patinete. O open.mp nao aceita ids fora de 400-611.
//  * Bater cartao: so MEMBRO da organizacao. Inicio de expediente = veste skin 271 + corp (gamemode: Org_Dress, grava e todos veem);
//    fim = tira o uniforme (Org_Undress) e guarda a viatura. So membro EM SERVICO dirige a viatura da organizacao.
//  * Comandos de admin (admin DO JOGO nivel 5 via Poses_IsAdmin, nunca RCON):
//      /orgcriar <tag> <nome>             /orguniforme <tag> <corp> [skin]      /orgativar <tag> <0|1>
//      /orgcarro <tag> <modelo> [base]    /orgcarrorem <tag> <modelo>
//      /orgnpc <tag> <cartao|garagem> [skin]   (fique onde o NPC deve ficar)    /orgnpcrem <tag> <cartao|garagem>
//      /orgspawn <tag>                    (ACRESCENTA um local onde as viaturas nascem, ate 12; com varios, sorteia um LIVRE. De pe, ou dentro de um veiculo parado)
//      /orgspawnlista <tag>   /orgspawnrem <tag> <n|todos>
//      /orgmembro <id> <tag|0> [cargo 1-7]   /orglider <id> <tag>  (da o cargo de Lider)   /orgcargo <tag> <1-7> <nome>  (renomeia cargo)
//      /orglista    /orgrecarregar
//  * CARGOS: 7 por organizacao, nomes editaveis (painel ou /orgcargo). Cargo 1 = Lider (so o admin da), 2..7 = demais, 7 = o mais baixo (o convidado entra nele).
//    O LIDER usa:  /convidar <id>  /promover <id>  /rebaixar <id>  /expulsar <id>  (so na propria organizacao; promover vai ate o cargo 2).
//    O convidado responde com /aceitarorg ou /recusarorg (o convite vale 60 s).  /org mostra a sua organizacao e o seu cargo.
//  * ORDEM em config.json "side_scripts": este script ANTES de "emprego" (o emprego descarta o toque no actor que nao e dele).
//  * Requer no gamemode: Poses_IsAdmin, Cel_AccountId, Org_Dress e Org_Undress (maximorp.pwn) e o celular-service no ar.
#define FILTERSCRIPT

#include <open.mp>
#include <Pawn.RakNet>
#include <sscanf2>
#include <izcmd>
#include <maximo_notify>

// ------------------------------------------------------------------ configuracao ---
#define ORG_SEGREDO        "LKEZnsEmfC4enwZlowsgEEOBHrcr"   // IGUAL ao http.secret do config.json do celular-service
#define ORG_URL            "127.0.0.1:8787/v1/"
#define ORG_RPC_OUT        (245)
#define ORG_RPC_IN         (246)
// Gerenciador de Grupo (/grupo): ops novas das RPCs 245/246 - veja o bloco "GERENCIADOR DE GRUPO" mais abaixo
#define GRP_RPC_OPEN       (16)
#define GRP_RPC_DONE       (17)
#define GRP_RPC_TOAST      (18)
new gGrpOpen[MAX_PLAYERS];     // 1 = painel /grupo aberto no cliente (os avisos vao para dentro dele)
#define GRP_OP_MEMBER      (40)
#define GRP_OP_DEPOSIT     (41)
#define GRP_OP_WITHDRAW    (42)
#define GRP_OP_FARM        (43)
#define GRP_OP_GPS         (44)
#define GRP_OP_CLOSE       (45)
#define GRP_MAX_VALUE      (100000000)                      // maior deposito/saque de uma vez
#define GRP_PRES_SECS      (60)                             // de quanto em quanto tempo a presenca/tempo jogado vai ao banco
#define ORG_ORPC_VEHADD    (164)
#define ORG_MARKER_SEE     (60.0)                           // so envia o marcador a quem esta a menos disso
#define ORG_TAP_RANGE      (14.0)                           // distancia maxima do NPC para o toque valer (o botao so aparece ate 12 m)
#define ORG_POLL_MS        (30000)
#define ORG_MAX            (16)                             // organizacoes
#define ORG_MAX_PTS        (32)                             // NPCs
#define ORG_MAX_CARS       (96)
#define ORG_MAX_SPAWNS     (96)                             // locais de nascimento das viaturas (somando todas as organizacoes)
#define ORG_SPAWN_CLEAR    (4.0)                            // um local so conta como livre se nao houver viatura da organizacao a menos disso                             // carros (somando todas)
#define ORG_NO_ACTOR       (-1)
#define ORG_DLG            (9731)                           // dialogo da garagem
#define ORG_MARK_COLOR     (0x2EC4B6)

enum { OW_VER = 1, OW_DEFS, OW_PTS, OW_CARS, OW_MEMBER, OW_ADM, OW_RANKS, OW_INV, OW_JOIN, OW_RACT, OW_MYORG, OW_SPAWNS, OW_DUTY };
#define ORG_MEMBER_TTL_MS  (20000)                          // quanto tempo vale a organizacao/cargo guardada de cada jogador antes de consultar o banco de novo
#define ORG_INVITE_SECS    (60)
enum { OK_CARTAO = 0, OK_GARAGEM = 1 };

enum E_ORG { oId, oTag[17], oName[41], oCorp[16], oSkin, oActive }
enum E_PT
{
    pId, pOrg, pKind, pSkin, pName[25],
    Float:pNX, Float:pNY, Float:pNZ, Float:pNA, Float:pMX, Float:pMY, Float:pMZ, Float:pSX, Float:pSY, Float:pSZ, Float:pSA,
    pInt, pVW, pActor
}
enum E_CAR { cOrg, cModel, cBase, cName[25] }
enum E_SP { spOrg, spId, Float:spX, Float:spY, Float:spZ, Float:spA }

forward Org_Cb(index, response_code, data[]);
forward Org_Poll();
forward Org_Tick();

new gO[ORG_MAX][E_ORG], gOn;               // lista em uso
new gP[ORG_MAX_PTS][E_PT], gPn;
new gC[ORG_MAX_CARS][E_CAR], gCn;
new gSO[ORG_MAX][E_ORG], gSOn;             // lista sendo carregada
new gSP[ORG_MAX_PTS][E_PT], gSPn;
new gSC[ORG_MAX_CARS][E_CAR], gSCn;
new gSp[ORG_MAX_SPAWNS][E_SP], gSpn, gSSp[ORG_MAX_SPAWNS][E_SP], gSSpn;      // locais onde as viaturas nascem (sorteia um livre)
new gR[ORG_MAX][8][25], gSR[ORG_MAX][8][25];          // nomes dos cargos 1..7 por organizacao (indice = posicao em gO/gSO)
new gVer = -1, gLoading, gLoadTotal, gMkVer = 1;

new gSerial[MAX_PLAYERS], gBusy[MAX_PLAYERS], gActPt[MAX_PLAYERS], gLastTap[MAX_PLAYERS];
new gAction[MAX_PLAYERS], gArg[MAX_PLAYERS], gAct2[MAX_PLAYERS];     // gAction: 0 = toque no NPC, 1 = /org (info) | gArg = alvo do comando de lider | gAct2 = 1 promover 2 rebaixar 3 expulsar
new gInvOrg[MAX_PLAYERS], gInvFrom[MAX_PLAYERS], gInvAt[MAX_PLAYERS];
new gMyOrg[MAX_PLAYERS], gMyRank[MAX_PLAYERS], gMemAt[MAX_PLAYERS], gMemBusy[MAX_PLAYERS];   // cache: organizacao/cargo do jogador (0 = nenhuma). So MEMBRO ve o marcador/botao dos NPCs da propria organizacao  // convite pendente (org do banco, quem convidou, quando)
new gDuty[MAX_PLAYERS];                    // id (banco) da organizacao em que o jogador esta em servico (0 = fora)
new gVeh[MAX_PLAYERS];                     // viatura pega pelo jogador (0 = nenhuma)
new gMenuPt[MAX_PLAYERS], gMenuCar[MAX_PLAYERS][24], gMenuN[MAX_PLAYERS];
new gMkSig[MAX_PLAYERS];
new gRest[MAX_PLAYERS];             // volta ao servico: 0 nao consultado | 1 consultando | 3 aguardando vestir o uniforme | 2 resolvido
new gRestOrg[MAX_PLAYERS], gRestTry[MAX_PLAYERS];
new bool:gChatOn = true;        // chat liberado para os jogadores (admin sempre ve); /chatjogadores on|off, salvo em scriptfiles/chat_jogadores.txt
new gAngAt[MAX_PLAYERS];            // /ang: quando o lider anunciou pela ultima vez (gettime)
new gChatSig[MAX_PLAYERS];          // ultimo aviso de "chat liberado" enviado ao cliente (-1 = nenhum)
new gVModel[MAX_VEHICLES];                 // id do CLIENTE do veiculo (0 = nao e viatura de organizacao)
new gVOrg[MAX_VEHICLES];                   // organizacao dona da viatura (id do banco)
new gVOwner[MAX_VEHICLES];                 // jogador que pegou + 1

// corps validas (ESPELHA gLookCorps do maximo_look.inc e as pastas texdb/skins/corp/ do cliente)
new const gCorps[][16] = {
    "base", "bombeiros", "bombeiros_2", "cerimonia", "gnr", "gnr_negociador", "gnr_transito", "goe", "inem",
    "maritima", "mec", "municipal", "policia", "psp", "psp_goe", "psp_negociador", "psp_transito", "training"
};

// ------------------------------------------------------------------ utilidades ---
stock Org_Admin(playerid) { return CallRemoteFunction("Poses_IsAdmin", "d", playerid) ? 1 : 0; }
stock Org_Acct(playerid)  { return CallRemoteFunction("Cel_AccountId", "d", playerid); }
stock Org_Ok(playerid) { return playerid >= 0 && playerid < MAX_PLAYERS && IsPlayerConnected(playerid); }
stock Org_Msg(playerid, const text[]) { SendClientMessage(playerid, 0xBBBBBBFF, text); return 1; }

stock Org_HexByte(v) return (v < 10) ? ('0' + v) : ('a' + v - 10);
stock Org_Hex(const src[], dst[], dstSize)
{
    new n = 0, len = strlen(src);
    for (new i = 0; i < len && n + 2 < dstSize; i++)
    {
        new b = src[i] & 0xFF;
        dst[n++] = Org_HexByte(b >> 4);
        dst[n++] = Org_HexByte(b & 15);
    }
    dst[n] = 0;
    return n;
}
stock Org_HexVal(c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
stock Org_Unhex(const h[], out[], size)
{
    new n = 0;
    for (new i = 0; h[i] && h[i + 1] && n < size - 1; i += 2) out[n++] = (Org_HexVal(h[i]) << 4) | Org_HexVal(h[i + 1]);
    out[n] = 0;
    return n;
}
stock Org_Line(const data[], &pos, out[], size)
{
    new n = 0;
    while (data[pos] && data[pos] != '\n' && data[pos] != '\r') { if (n < size - 1) out[n++] = data[pos]; pos++; }
    out[n] = 0;
    if (data[pos] == '\r') pos++;
    if (data[pos] == '\n') pos++;
    return n;
}
stock Org_Field(const line[], idx, out[], size)
{
    new pos = 0, n = 0;
    out[0] = 0;
    for (new cur = 0; cur < idx; cur++)
    {
        while (line[pos] && line[pos] != '|') pos++;
        if (!line[pos]) return 0;
        pos++;
    }
    while (line[pos] && line[pos] != '|') { if (n < size - 1) out[n++] = line[pos]; pos++; }
    out[n] = 0;
    return n;
}

// corpo do pedido "campo=hex&campo=hex"
new gBody[640];
stock Org_BodyReset() { gBody[0] = 0; }
stock Org_BStr(const name[], const val[])
{
    new h[200];
    Org_Hex(val, h, sizeof(h));
    if (gBody[0]) strcat(gBody, "&", sizeof(gBody));
    strcat(gBody, name, sizeof(gBody));
    strcat(gBody, "=", sizeof(gBody));
    strcat(gBody, h, sizeof(gBody));
}
stock Org_BInt(const name[], v) { new t[16]; format(t, sizeof(t), "%d", v); Org_BStr(name, t); }
stock Org_BFloat(const name[], Float:v) { new t[24]; format(t, sizeof(t), "%.2f", v); Org_BStr(name, t); }

// op local do celular-service. playerid < 0 = pedido global (sem jogador)
stock Org_Post(playerid, what, const op[], const body[])
{
    new url[128];
    format(url, sizeof(url), "%s%s?k=%s", ORG_URL, op, ORG_SEGREDO);
    new pid = (playerid < 0) ? 1023 : (playerid & 0x3FF);
    new ser = (playerid < 0) ? 0 : gSerial[playerid];
    new idx = pid | ((what & 0xF) << 10) | ((ser & 0x3FFF) << 14);
    HTTP(idx, HTTP_POST, url, body, "Org_Cb");
    return 1;
}
stock Org_PostOff(what, const op[], offset)
{
    Org_BodyReset();
    Org_BInt("offset", offset);
    return Org_Post(-1, what, op, gBody);
}

// ------------------------------------------------------------------ rede (RPC 245 / 246) ---
static Org_WriteStr(BitStream:bs, const str[], maxLen)
{
    new len = strlen(str);
    if (len > maxLen) len = maxLen;
    BS_WriteValue(bs, PR_UINT8, len);
    for (new i = 0; i < len; i++) BS_WriteValue(bs, PR_UINT8, str[i] & 0xFF);
}
// aviso no canto da tela (cartao de sucesso/erro, RPC 247) + copia no chat
stock Org_Toast(playerid, bool:bad, const text[])
{
    if (playerid >= 0 && playerid < MAX_PLAYERS && gGrpOpen[playerid])
    {
        // painel aberto (tela cheia, o cartao do HUD nao aparece): o aviso vai para dentro do painel (RPC 245 op 18)
        new BitStream:tb = BS_New();
        BS_WriteValue(tb, PR_UINT8, GRP_RPC_TOAST, PR_UINT8, bad ? 1 : 0);
        Org_WriteStr(tb, text, 120);
        PR_SendRPC(tb, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
        BS_Delete(tb);
        return 1;
    }
    if (bad) Notify(playerid, NOTIFY_ERR, "Ocorreu um erro!", text, 4500);
    else     Notify(playerid, NOTIFY_OK, "Sucesso", text, 4000);
   // Org_Msg(playerid, text);
    return 1;
}

stock Org_FindOrg(id) { for (new i = 0; i < gOn; i++) if (gO[i][oId] == id) return i; return -1; }
stock Org_FindTag(const tag[]) { for (new i = 0; i < gOn; i++) if (!strcmp(gO[i][oTag], tag)) return i; return -1; }
// nome do cargo (1..7) da organizacao (id do banco); sem cadastro devolve "Cargo N"
stock Org_RankName(orgid, rank, out[], size)
{
    new o = Org_FindOrg(orgid);
    if (o != -1 && rank >= 1 && rank <= 7 && gR[o][rank][0]) format(out, size, "%s", gR[o][rank]);
    else format(out, size, "Cargo %d", rank);
}

// op 7 = marcadores de organizacao (grupo C do cliente). So os NPCs a menos de ORG_MARKER_SEE m
stock Org_SendMarkers(playerid, mask)
{
    new BitStream:bs = BS_New(), n = 0;
    for (new i = 0; i < gPn && i < 16; i++) if (mask & (1 << i)) n++;
    if (n > 8) n = 8;
    BS_WriteValue(bs, PR_UINT8, 7, PR_UINT8, n);
    new w = 0;
    for (new i = 0; i < gPn && i < 16 && w < n; i++)
    {
        if (!(mask & (1 << i))) continue;
        new text[41], st = 0;
        if (gP[i][pKind] == OK_CARTAO) { if (gDuty[playerid] == gP[i][pOrg]) { text = "Encerrar expediente"; st = 1; } else text = "Bater cartão e pegar uniforme"; }
        else if (gVeh[playerid]) text = "Garagem (guardar/trocar)";
        else text = "Pegar veículo";
        BS_WriteValue(bs, PR_UINT8, 5, PR_FLOAT, gP[i][pMX], PR_FLOAT, gP[i][pMY], PR_FLOAT, gP[i][pMZ],
            PR_FLOAT, gP[i][pNX], PR_FLOAT, gP[i][pNY], PR_FLOAT, gP[i][pNZ], PR_FLOAT, 1.3, PR_UINT32, ORG_MARK_COLOR);
        Org_WriteStr(bs, text, 40);
        Org_WriteStr(bs, gP[i][pName], 24);
        BS_WriteValue(bs, PR_UINT8, st);
        w++;
    }
    PR_SendRPC(bs, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    printf("[ORG] marcadores enviados ao jogador %d: %d (organizacao %d, cargo %d, em servico %d)", playerid, n, gMyOrg[playerid], gMyRank[playerid], gDuty[playerid]);
}
// Chat do cliente: admin do jogo (nivel 5) sempre; jogadores so com gChatOn. RPC 245 op 8 + u8. Reenvia so quando muda (login, promocao, saida).
#define ORG_PROV_MAX   (64)
#define ORG_PROV_ARQ   "provador_liberado.txt"
new gProvNome[ORG_PROV_MAX][MAX_PLAYER_NAME], gProvN;     // jogadores liberados por um admin para o /provador (por nome, salvo em disco)
stock Org_ProvIdx(const nome[]) { for (new i = 0; i < gProvN; i++) if (!strcmp(gProvNome[i], nome, true)) return i; return -1; }
stock Org_ProvOk(playerid)
{
    if (Org_Admin(playerid)) return 1;
    new n[MAX_PLAYER_NAME];
    GetPlayerName(playerid, n, sizeof(n));
    return Org_ProvIdx(n) != -1;
}
stock Org_ProvSave()
{
    new File:f = fopen(ORG_PROV_ARQ, io_write), l[MAX_PLAYER_NAME + 4];
    if (!f) return 0;
    for (new i = 0; i < gProvN; i++) { format(l, sizeof(l), "%s\r\n", gProvNome[i]); fwrite(f, l); }
    fclose(f);
    return 1;
}
stock Org_ProvLoad()
{
    gProvN = 0;
    if (!fexist(ORG_PROV_ARQ)) return;
    new File:f = fopen(ORG_PROV_ARQ, io_read), l[MAX_PLAYER_NAME + 4];
    if (!f) return;
    while (fread(f, l) && gProvN < ORG_PROV_MAX)
    {
        new n[MAX_PLAYER_NAME];
        if (sscanf(l, "s[24]", n)) continue;
        format(gProvNome[gProvN++], MAX_PLAYER_NAME, "%s", n);
    }
    fclose(f);
}
stock Org_ChatTick(playerid)
{
    new a = (Org_Admin(playerid) || gChatOn) ? 1 : 0, pv = Org_ProvOk(playerid) ? 1 : 0, sig = a | (pv << 1);
    if (gChatSig[playerid] == sig) return;
    gChatSig[playerid] = sig;
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 8, PR_UINT8, a);
    PR_SendRPC(bs, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    bs = BS_New();                                  // op 12: o cliente so abre o /provador com este aviso = 1
    BS_WriteValue(bs, PR_UINT8, 12, PR_UINT8, pv);
    PR_SendRPC(bs, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
}
// 1x por segundo por jogador; so reenvia quando o conjunto visivel ou o estado mudou
stock Org_MarkersTick(playerid)
{
    new mask = 0, bool:near = false;
    if (GetPlayerState(playerid) != PLAYER_STATE_NONE && GetPlayerState(playerid) != PLAYER_STATE_WASTED)
    {
        for (new i = 0; i < gPn && i < 16; i++)
        {
            new o = Org_FindOrg(gP[i][pOrg]);
            if (o == -1 || !gO[o][oActive]) continue;
            if (GetPlayerInterior(playerid) != gP[i][pInt] || GetPlayerVirtualWorld(playerid) != gP[i][pVW]) continue;
            if (!IsPlayerInRangeOfPoint(playerid, ORG_MARKER_SEE, gP[i][pNX], gP[i][pNY], gP[i][pNZ])) continue;
            near = true;
            if (gMyOrg[playerid] == gP[i][pOrg]) mask |= (1 << i);          // o marcador so aparece para MEMBRO da organizacao do NPC
        }
    }
    // perto de algum NPC com a organizacao ainda desconhecida/velha: consulta o banco (no maximo 1 pedido por vez)
    if (near && !gMemBusy[playerid] && (gMemAt[playerid] == 0 || GetTickCount() - gMemAt[playerid] > ORG_MEMBER_TTL_MS))
    {
        new acct = Org_Acct(playerid);
        if (acct > 0)
        {
            gMemBusy[playerid] = 1;
            Org_BodyReset(); Org_BInt("acct", acct);
            Org_Post(playerid, OW_MYORG, "org_member", gBody);
        }
    }
    new sig = mask ^ (gDuty[playerid] << 16) ^ (gMkVer << 24) ^ (gVeh[playerid] ? 0x40000000 : 0);
    if (gMkSig[playerid] == sig) return 0;
    gMkSig[playerid] = sig;
    Org_SendMarkers(playerid, mask);
    return 1;
}

// ------------------------------------------------------------------ actors ---
stock Org_ActorsDestroy()
{
    for (new i = 0; i < gPn; i++) if (gP[i][pActor] != ORG_NO_ACTOR) { DestroyActor(gP[i][pActor]); gP[i][pActor] = ORG_NO_ACTOR; }
}
stock Org_ActorsBuild()
{
    for (new i = 0; i < gPn; i++)
    {
        gP[i][pActor] = ORG_NO_ACTOR;
        new o = Org_FindOrg(gP[i][pOrg]);
        if (o == -1 || !gO[o][oActive]) continue;
        new a = CreateActor(gP[i][pSkin], gP[i][pNX], gP[i][pNY], gP[i][pNZ], gP[i][pNA]);
        if (a != ORG_NO_ACTOR)
        {
            SetActorInvulnerable(a, true);
            SetActorVirtualWorld(a, gP[i][pVW]);
            gP[i][pActor] = a;
        }
    }
}

// ------------------------------------------------------------------ viaturas ---
stock Org_VehFree(vid)
{
    if (vid > 0 && vid < MAX_VEHICLES) { gVModel[vid] = 0; gVOrg[vid] = 0; gVOwner[vid] = 0; }
}
stock Org_VehStore(playerid)
{
    new v = gVeh[playerid];
    gVeh[playerid] = 0;
    if (v > 0) { Org_VehFree(v); DestroyVehicle(v); }
}
// grava no banco se o jogador esta em servico (cartao batido): ao reconectar ele volta em servico
stock Org_DutySave(playerid, on)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return 0;
    Org_BodyReset(); Org_BInt("acct", acct); Org_BInt("on", on);
    return Org_Post(playerid, OW_DUTY, "org_duty", gBody);
}
stock Org_DutyOff(playerid)
{
    Org_VehStore(playerid);
    gDuty[playerid] = 0;
    gMkSig[playerid] = -1;
    Org_DutySave(playerid, 0);
}
// depois do login/spawn: consulta o banco uma vez e, se ficou em servico, veste o uniforme de novo (tenta ate o visual carregar)
stock Org_RestoreTick(playerid)
{
    if (gRest[playerid] == 0)
    {
        new PLAYER_STATE:st = GetPlayerState(playerid);
        if (st != PLAYER_STATE_ONFOOT && st != PLAYER_STATE_DRIVER && st != PLAYER_STATE_PASSENGER) return;
        new acct = Org_Acct(playerid);
        if (acct <= 0 || gMemBusy[playerid]) return;
        gRest[playerid] = 1; gMemBusy[playerid] = 1;
        Org_BodyReset(); Org_BInt("acct", acct);
        Org_Post(playerid, OW_MYORG, "org_member", gBody);
        return;
    }
    if (gRest[playerid] != 3) return;
    if (gDuty[playerid]) { gRest[playerid] = 2; return; }          // ja bateu o cartao de novo sozinho
    new o = Org_FindOrg(gRestOrg[playerid]);
    if (o == -1 || !gO[o][oActive] || gMyOrg[playerid] != gRestOrg[playerid] || ++gRestTry[playerid] > 90) { gRest[playerid] = 2; return; }
    if (!CallRemoteFunction("Org_Dress", "ds", playerid, gO[o][oCorp])) return;    // visual ainda carregando: tenta no proximo segundo
    gDuty[playerid] = gRestOrg[playerid];
    gMkSig[playerid] = -1;
    gRest[playerid] = 2;
    Org_Toast(playerid, false, "Cartão já batido: você voltou em serviço.");
}

// ------------------------------------------------------------------ carga dos cadastros ---
stock Org_LoadStart()
{
    if (gLoading) return 0;
    gLoading = 1; gSOn = 0; gSPn = 0; gSCn = 0; gSSpn = 0; gLoadTotal = 0;
    for (new i = 0; i < ORG_MAX; i++) for (new k = 0; k < 8; k++) gSR[i][k][0] = 0;
    return Org_PostOff(OW_DEFS, "org_defs", 0);
}
stock Org_LoadFinish()
{
    gLoading = 0;
    Org_ActorsDestroy();
    gOn = gSOn; gPn = gSPn; gCn = gSCn; gSpn = gSSpn;
    for (new i = 0; i < gSpn; i++) for (new k = 0; k < _:E_SP; k++) gSp[i][E_SP:k] = gSSp[i][E_SP:k];
    for (new i = 0; i < gOn; i++) for (new k = 0; k < _:E_ORG; k++) gO[i][E_ORG:k] = gSO[i][E_ORG:k];
    for (new i = 0; i < gPn; i++) for (new k = 0; k < _:E_PT; k++) gP[i][E_PT:k] = gSP[i][E_PT:k];
    for (new i = 0; i < gCn; i++) for (new k = 0; k < _:E_CAR; k++) gC[i][E_CAR:k] = gSC[i][E_CAR:k];
    for (new i = 0; i < ORG_MAX; i++) for (new k = 0; k < 8; k++) format(gR[i][k], 25, "%s", gSR[i][k]);
    Org_ActorsBuild();
    gMkVer = (gMkVer + 1) & 0x3F;
    // quem estava em servico numa organizacao que sumiu/foi desativada volta ao normal
    for (new p = 0; p < MAX_PLAYERS; p++)
    {
        if (!Org_Ok(p) || !gDuty[p]) continue;
        new o = Org_FindOrg(gDuty[p]);
        if (o == -1 || !gO[o][oActive])
        {
            Org_DutyOff(p);
            CallRemoteFunction("Org_Undress", "d", p);
            Org_Toast(p, true, "Seu expediente foi encerrado: a organização foi desativada.");
        }
    }
    printf("[ORG] cadastros carregados: %d organizacao(oes), %d NPC(s), %d carro(s), %d local(is) de saida", gOn, gPn, gCn, gSpn);
    return 1;
}
stock Org_ParseOrg(const line[])
{
    if (gSOn >= ORG_MAX) return 0;
    new f[120], i = gSOn;
    Org_Field(line, 0, f, sizeof(f)); gSO[i][oId] = strval(f);
    Org_Field(line, 1, gSO[i][oTag], 17);
    Org_Field(line, 2, f, sizeof(f)); Org_Unhex(f, gSO[i][oName], 41);
    Org_Field(line, 3, gSO[i][oCorp], 16);
    Org_Field(line, 4, f, sizeof(f)); gSO[i][oSkin] = strval(f);
    Org_Field(line, 5, f, sizeof(f)); gSO[i][oActive] = strval(f);
    gSOn++;
    return 1;
}
stock Org_ParsePt(const line[])
{
    if (gSPn >= ORG_MAX_PTS) return 0;
    new f[120], i = gSPn;
    Org_Field(line, 0, f, sizeof(f)); gSP[i][pId] = strval(f);
    Org_Field(line, 1, f, sizeof(f)); gSP[i][pOrg] = strval(f);
    Org_Field(line, 2, f, sizeof(f)); gSP[i][pKind] = !strcmp(f, "garagem") ? OK_GARAGEM : OK_CARTAO;
    Org_Field(line, 3, f, sizeof(f)); gSP[i][pSkin] = strval(f);
    Org_Field(line, 4, f, sizeof(f)); Org_Unhex(f, gSP[i][pName], 25);
    Org_Field(line, 5, f, sizeof(f)); gSP[i][pNX] = floatstr(f);
    Org_Field(line, 6, f, sizeof(f)); gSP[i][pNY] = floatstr(f);
    Org_Field(line, 7, f, sizeof(f)); gSP[i][pNZ] = floatstr(f);
    Org_Field(line, 8, f, sizeof(f)); gSP[i][pNA] = floatstr(f);
    Org_Field(line, 9, f, sizeof(f)); gSP[i][pMX] = floatstr(f);
    Org_Field(line, 10, f, sizeof(f)); gSP[i][pMY] = floatstr(f);
    Org_Field(line, 11, f, sizeof(f)); gSP[i][pMZ] = floatstr(f);
    Org_Field(line, 12, f, sizeof(f)); gSP[i][pSX] = floatstr(f);
    Org_Field(line, 13, f, sizeof(f)); gSP[i][pSY] = floatstr(f);
    Org_Field(line, 14, f, sizeof(f)); gSP[i][pSZ] = floatstr(f);
    Org_Field(line, 15, f, sizeof(f)); gSP[i][pSA] = floatstr(f);
    Org_Field(line, 16, f, sizeof(f)); gSP[i][pInt] = strval(f);
    Org_Field(line, 17, f, sizeof(f)); gSP[i][pVW] = strval(f);
    gSP[i][pActor] = ORG_NO_ACTOR;
    gSPn++;
    return 1;
}
stock Org_ParseCar(const line[])
{
    if (gSCn >= ORG_MAX_CARS) return 0;
    new f[120], i = gSCn;
    Org_Field(line, 0, f, sizeof(f)); gSC[i][cOrg] = strval(f);
    Org_Field(line, 1, f, sizeof(f)); gSC[i][cModel] = strval(f);
    Org_Field(line, 2, f, sizeof(f)); gSC[i][cBase] = strval(f);
    Org_Field(line, 3, f, sizeof(f)); Org_Unhex(f, gSC[i][cName], 25);
    gSCn++;
    return 1;
}

stock Org_ParseRank(const line[])
{
    new f[120];
    Org_Field(line, 0, f, sizeof(f));
    new oi = -1, id = strval(f);
    for (new i = 0; i < gSOn; i++) if (gSO[i][oId] == id) { oi = i; break; }
    Org_Field(line, 1, f, sizeof(f));
    new rk = strval(f);
    if (oi == -1 || rk < 1 || rk > 7) return 0;
    Org_Field(line, 2, f, sizeof(f)); Org_Unhex(f, gSR[oi][rk], 25);
    return 1;
}

stock Org_ParseSpawn(const line[])
{
    if (gSSpn >= ORG_MAX_SPAWNS) return 0;
    new f[40], i = gSSpn;
    Org_Field(line, 0, f, sizeof(f)); gSSp[i][spOrg] = strval(f);
    Org_Field(line, 1, f, sizeof(f)); gSSp[i][spId] = strval(f);
    Org_Field(line, 2, f, sizeof(f)); gSSp[i][spX] = floatstr(f);
    Org_Field(line, 3, f, sizeof(f)); gSSp[i][spY] = floatstr(f);
    Org_Field(line, 4, f, sizeof(f)); gSSp[i][spZ] = floatstr(f);
    Org_Field(line, 5, f, sizeof(f)); gSSp[i][spA] = floatstr(f);
    gSSpn++;
    return 1;
}

public Org_Poll()
{
    if (gLoading) return 1;
    Org_BodyReset();
    return Org_Post(-1, OW_VER, "org_ver", gBody);
}

// ------------------------------------------------------------------ garagem ---
stock Org_NearPoint(playerid, pi, Float:range)
{
    if (pi < 0 || pi >= gPn) return 0;
    if (GetPlayerInterior(playerid) != gP[pi][pInt] || GetPlayerVirtualWorld(playerid) != gP[pi][pVW]) return 0;
    return IsPlayerInRangeOfPoint(playerid, range, gP[pi][pNX], gP[pi][pNY], gP[pi][pNZ]);
}
stock Org_GarageMenu(playerid, pi)
{
    new orgid = gP[pi][pOrg], list[640], n = 0;
    list[0] = 0;
    for (new i = 0; i < gCn && n < 23; i++)
    {
        if (gC[i][cOrg] != orgid) continue;
        new line[64];
        if (gC[i][cName][0]) format(line, sizeof(line), "%s (modelo %d)\n", gC[i][cName], gC[i][cModel]);
        else format(line, sizeof(line), "Viatura modelo %d\n", gC[i][cModel]);
        strcat(list, line, sizeof(list));
        gMenuCar[playerid][n++] = i;
    }
    if (!n) return Org_Toast(playerid, true, "Esta organização ainda não tem veículos cadastrados.");
    if (gVeh[playerid]) { strcat(list, "» Guardar veículo atual\n", sizeof(list)); gMenuCar[playerid][n++] = -1; }
    gMenuPt[playerid] = pi; gMenuN[playerid] = n;
    ShowPlayerDialog(playerid, ORG_DLG, DIALOG_STYLE_LIST, gO[Org_FindOrg(orgid)][oName], list, "Escolher", "Fechar");
    return 1;
}
// sorteia um local de saida LIVRE da organizacao. -2 = nenhum cadastrado, -1 = todos ocupados
stock Org_PickSpawn(orgid)
{
    new cand[ORG_MAX_SPAWNS], n = 0, total = 0;
    for (new i = 0; i < gSpn; i++)
    {
        if (gSp[i][spOrg] != orgid) continue;
        total++;
        new bool:busy = false;
        for (new v = 1; v < MAX_VEHICLES && !busy; v++)
        {
            if (!gVOrg[v]) continue;
            new Float:vx, Float:vy, Float:vz;
            GetVehiclePos(v, vx, vy, vz);
            if (VectorSize(vx - gSp[i][spX], vy - gSp[i][spY], vz - gSp[i][spZ]) < ORG_SPAWN_CLEAR) busy = true;
        }
        if (!busy) cand[n++] = i;
    }
    if (!total) return -2;
    if (!n) return -1;
    return cand[random(n)];
}
stock Org_SpawnCar(playerid, pi, ci)
{
    // o jogador devolve a viatura anterior ANTES do sorteio (o local dela volta a ficar livre)
    Org_VehStore(playerid);
    new sp = Org_PickSpawn(gP[pi][pOrg]);
    if (sp == -2) return Org_Toast(playerid, true, "A garagem ainda não tem local de saída (admin: /orgspawn).");
    if (sp == -1) return Org_Toast(playerid, true, "Todos os locais da garagem estão ocupados. Tente novamente em instantes.");
    new vid = CreateVehicle(gC[ci][cBase], gSp[sp][spX], gSp[sp][spY], gSp[sp][spZ], gSp[sp][spA], 0, 1, -1);
    if (vid == INVALID_VEHICLE_ID || vid <= 0 || vid >= MAX_VEHICLES) return Org_Toast(playerid, true, "Não foi possível criar o veículo agora.");
    SetVehicleVirtualWorld(vid, gP[pi][pVW]);
    LinkVehicleToInterior(vid, gP[pi][pInt]);
    gVModel[vid] = (gC[ci][cModel] != gC[ci][cBase]) ? gC[ci][cModel] : 0;
    gVOrg[vid] = gP[pi][pOrg];
    gVOwner[vid] = playerid + 1;
    gVeh[playerid] = vid;
    gMkSig[playerid] = -1;
    PutPlayerInVehicle(playerid, vid, 0);
    printf("[ORG] jogador %d pegou viatura %d (modelo cliente %d, base %d) no local %d", playerid, vid, gC[ci][cModel], gC[ci][cBase], gSp[sp][spId]);
    return Org_Toast(playerid, false, "Viatura liberada. Bom serviço!");
}
public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if (dialogid != ORG_DLG) return 0;
    new pi = gMenuPt[playerid];
    if (!response || listitem < 0 || listitem >= gMenuN[playerid]) return 1;
    // revalida tudo: o dialogo pode ter ficado aberto
    new o = (pi >= 0 && pi < gPn) ? Org_FindOrg(gP[pi][pOrg]) : -1;
    if (o == -1 || !gO[o][oActive] || gP[pi][pKind] != OK_GARAGEM || gDuty[playerid] != gP[pi][pOrg]) return 1;
    if (!Org_NearPoint(playerid, pi, ORG_TAP_RANGE + 10.0)) { Org_Toast(playerid, true, "Chegue mais perto do NPC da garagem."); return 1; }
    new ci = gMenuCar[playerid][listitem];
    if (ci == -1) { Org_VehStore(playerid); gMkSig[playerid] = -1; Org_Toast(playerid, false, "Veículo guardado."); return 1; }
    if (ci < 0 || ci >= gCn || gC[ci][cOrg] != gP[pi][pOrg]) return 1;
    Org_SpawnCar(playerid, pi, ci);
    return 1;
}

// ------------------------------------------------------------------ toque: bater cartao / garagem ---
stock Org_Tap(playerid, pi)
{
    if (!Org_Ok(playerid) || pi < 0 || pi >= gPn) return 0;
    new o = Org_FindOrg(gP[pi][pOrg]);
    if (o == -1 || !gO[o][oActive]) return 0;
    if (!Org_NearPoint(playerid, pi, ORG_TAP_RANGE)) { Org_Toast(playerid, true, "Chegue mais perto do NPC."); return 0; }
    new now = GetTickCount();
    if (now - gLastTap[playerid] < 800) return 0;
    gLastTap[playerid] = now;
    if (gBusy[playerid]) { Org_Toast(playerid, true, "Aguarde, processando o pedido anterior..."); return 0; }
    new acct = Org_Acct(playerid);
    if (acct <= 0) { Org_Toast(playerid, true, "Faça login para usar a organização."); return 0; }
    gBusy[playerid] = 1; gActPt[playerid] = pi; gAction[playerid] = 0;
    Org_BodyReset();
    Org_BInt("acct", acct);
    return Org_Post(playerid, OW_MEMBER, "org_member", gBody);
}
// resposta de org_member ("org|patente|nome(hex)" ou vazio): decide o que o NPC faz
stock Org_Act(playerid, const memberLine[])
{
    new pi = gActPt[playerid];
    if (pi < 0 || pi >= gPn) return 0;
    new o = Org_FindOrg(gP[pi][pOrg]);
    if (o == -1 || !gO[o][oActive]) return 0;
    new f[24];
    Org_Field(memberLine, 0, f, sizeof(f));
    if (!memberLine[0] || strval(f) != gP[pi][pOrg])
    {
        new msg[96];
        format(msg, sizeof(msg), "Você não faz parte da %s.", gO[o][oName]);
        return Org_Toast(playerid, true, msg);
    }
    if (!Org_NearPoint(playerid, pi, ORG_TAP_RANGE + 6.0)) return Org_Toast(playerid, true, "Chegue mais perto do NPC.");
    if (gP[pi][pKind] == OK_CARTAO)
    {
        if (gDuty[playerid] == gP[pi][pOrg])
        {
            Org_DutyOff(playerid);
            CallRemoteFunction("Org_Undress", "d", playerid);
            return Org_Toast(playerid, false, "Cartão batido: expediente encerrado. Uniforme guardado.");
        }
        if (!CallRemoteFunction("Org_Dress", "ds", playerid, gO[o][oCorp])) return Org_Toast(playerid, true, "Aguarde o visual carregar e tente de novo.");
        gDuty[playerid] = gP[pi][pOrg];
        gMkSig[playerid] = -1;
        Org_DutySave(playerid, 1);
        new msg[120], cargo[25], rk;
        Org_Field(memberLine, 1, f, sizeof(f)); rk = strval(f);
        Org_RankName(gP[pi][pOrg], rk, cargo, sizeof(cargo));
        format(msg, sizeof(msg), "Cartão batido: expediente iniciado (%s - %s).", gO[o][oName], cargo);
        return Org_Toast(playerid, false, msg);
    }
    if (gDuty[playerid] != gP[pi][pOrg]) return Org_Toast(playerid, true, "Bata o cartão para entrar em serviço antes de pegar um veículo.");
    return Org_GarageMenu(playerid, pi);
}

// ------------------------------------------------------------------ resposta do servico ---
public Org_Cb(index, response_code, data[])
{
    new pid = index & 0x3FF, what = (index >> 10) & 0xF, ser = (index >> 14) & 0x3FFF;
    new bool:global = (pid == 1023);
    if (!global)
    {
        if (pid >= MAX_PLAYERS || !IsPlayerConnected(pid) || ser != (gSerial[pid] & 0x3FFF)) return 0;     // jogador saiu / outro login
        if (what == OW_MEMBER) gBusy[pid] = 0;
        if (what == OW_MYORG) gMemBusy[pid] = 0;
    }
    new pos = 0, line[900];
    Org_Line(data, pos, line, sizeof(line));
    new bool:ok = (response_code == 200 && !strcmp(line, "ok"));
    if (!ok) printf("[ORG] pedido %d falhou: http %d resposta '%s'", what, response_code, line);
    if (!ok && what == OW_MYORG && (pid < MAX_PLAYERS) && gRest[pid] == 1) gRest[pid] = 0;     // consulta da volta ao servico falhou: tenta de novo
    switch (what)
    {
        case OW_VER:
        {
            if (!ok) return 1;
            Org_Line(data, pos, line, sizeof(line));
            new v = strval(line);
            if (v != gVer) { gVer = v; Org_LoadStart(); }
        }
        case OW_DEFS:
        {
            if (!ok) { gLoading = 0; return 1; }
            Org_Line(data, pos, line, sizeof(line));             // V|ver|total
            new f[16];
            Org_Field(line, 2, f, sizeof(f)); gLoadTotal = strval(f);
            new got = 0;
            while (data[pos]) { if (!Org_Line(data, pos, line, sizeof(line))) continue; Org_ParseOrg(line); got++; }
            if (got > 0 && gSOn < gLoadTotal && gSOn < ORG_MAX) Org_PostOff(OW_DEFS, "org_defs", gSOn);
            else Org_PostOff(OW_PTS, "org_points", 0);
        }
        case OW_PTS:
        {
            if (!ok) { gLoading = 0; return 1; }
            new got = 0;
            while (data[pos]) { if (!Org_Line(data, pos, line, sizeof(line))) continue; Org_ParsePt(line); got++; }
            if (got >= 8 && gSPn < ORG_MAX_PTS) Org_PostOff(OW_PTS, "org_points", gSPn);
            else Org_PostOff(OW_CARS, "org_cars", 0);
        }
        case OW_CARS:
        {
            if (!ok) { gLoading = 0; return 1; }
            new got = 0;
            while (data[pos]) { if (!Org_Line(data, pos, line, sizeof(line))) continue; Org_ParseCar(line); got++; }
            if (got >= 30 && gSCn < ORG_MAX_CARS) Org_PostOff(OW_CARS, "org_cars", gSCn);
            else Org_PostOff(OW_RANKS, "org_ranks", 0);
        }
        case OW_RANKS:
        {
            if (!ok) { gLoading = 0; return 1; }
            new got = 0, total = 0;
            while (data[pos]) { if (!Org_Line(data, pos, line, sizeof(line))) continue; Org_ParseRank(line); got++; }
            for (new i = 0; i < gSOn; i++) for (new k = 1; k <= 7; k++) if (gSR[i][k][0]) total++;
            if (got >= 30) Org_PostOff(OW_RANKS, "org_ranks", total);
            else Org_PostOff(OW_SPAWNS, "org_spawns", 0);
        }
        case OW_SPAWNS:
        {
            if (!ok) { gLoading = 0; return 1; }
            new got = 0;
            while (data[pos]) { if (!Org_Line(data, pos, line, sizeof(line))) continue; Org_ParseSpawn(line); got++; }
            if (got >= 30 && gSSpn < ORG_MAX_SPAWNS) Org_PostOff(OW_SPAWNS, "org_spawns", gSSpn);
            else Org_LoadFinish();
        }
        case OW_MYORG:
        {
            if (!ok) return 1;
            Org_Line(data, pos, line, sizeof(line));
            new f[24];
            Org_Field(line, 0, f, sizeof(f)); new og = line[0] ? strval(f) : 0;
            Org_Field(line, 1, f, sizeof(f)); new rk = line[0] ? strval(f) : 0;
            gMemAt[pid] = GetTickCount();
            if (gMyOrg[pid] != og || gMyRank[pid] != rk) { gMyOrg[pid] = og; gMyRank[pid] = rk; gMkSig[pid] = -1; }
            if (gRest[pid] == 1)
            {
                Org_Field(line, 3, f, sizeof(f));
                if (og > 0 && line[0] && strval(f) == 1) { gRest[pid] = 3; gRestOrg[pid] = og; gRestTry[pid] = 0; }
                else gRest[pid] = 2;
            }
        }
        case OW_DUTY: { }
        case OW_MEMBER:
        {
            if (!ok) return Org_Toast(pid, true, "O serviço de organizações não respondeu. Avise um administrador.");
            Org_Line(data, pos, line, sizeof(line));
            if (gAction[pid] == 1) Org_Info(pid, line); else Org_Act(pid, line);
        }
        case OW_INV:
        {
            new res[40], f[24];
            Org_Line(data, pos, res, sizeof(res));
            new tg = gArg[pid];
            Grp_Done(pid, 0, !strcmp(res, "ok", false, 2));
            if (!ok) return Org_Toast(pid, true, "O serviço de organizações não respondeu.");
            if (!Org_Ok(tg)) return Org_Toast(pid, true, "O jogador saiu do servidor.");
            if (!strcmp(res, "noleader") || !strcmp(res, "noperm")) return Org_Toast(pid, true, "Seu cargo não tem permissão para convidar.");
            if (!strcmp(res, "full")) return Org_Toast(pid, true, "O grupo está cheio: não há vagas.");
            if (!strcmp(res, "has")) return Org_Toast(pid, true, "Esse jogador já faz parte de uma organização.");
            Org_Field(res, 1, f, sizeof(f));
            new o = Org_FindOrg(strval(f));
            if (o == -1) return Org_Toast(pid, true, "Organização indisponível.");
            gInvOrg[tg] = gO[o][oId]; gInvFrom[tg] = pid; gInvAt[tg] = gettime();
            new msg[128], nm[MAX_PLAYER_NAME + 1];
            GetPlayerName(tg, nm, sizeof(nm));
            format(msg, sizeof(msg), "Convite enviado para %s.", nm);
            Org_Toast(pid, false, msg);
            GetPlayerName(pid, nm, sizeof(nm));
            format(msg, sizeof(msg), "%s convidou você para a %s. Digite /aceitarorg ou /recusarorg (60 s).", nm, gO[o][oName]);
            Org_Toast(tg, false, msg);
        }
        case OW_JOIN:
        {
            new res[24];
            Org_Line(data, pos, res, sizeof(res));
            if (!ok) return Org_Toast(pid, true, "O serviço de organizações não respondeu.");
            if (strcmp(res, "ok")) return Org_Toast(pid, true, "Não foi possível entrar (você já tem uma organização?).");
            new o = Org_FindOrg(gInvOrg[pid]), msg[128], cargo[25];
            Org_RankName(gInvOrg[pid], 7, cargo, sizeof(cargo));
            new on[41];
            if (o != -1) format(on, sizeof(on), "%s", gO[o][oName]); else on = "organização";
            format(msg, sizeof(msg), "Bem-vindo �  %s! Seu cargo: %s.", on, cargo);
            Org_Toast(pid, false, msg);
            if (Org_Ok(gInvFrom[pid])) Org_Msg(gInvFrom[pid], "Convite aceito.");
            gInvOrg[pid] = 0;
            gMemAt[pid] = 0;
        }
        case OW_RACT:
        {
            new res[40], f[24];
            Org_Line(data, pos, res, sizeof(res));
            Grp_Done(pid, gAct2[pid], !strcmp(res, "ok", false, 2));
            if (!ok) return Org_Toast(pid, true, "O serviço de organizações não respondeu.");
            if (!strcmp(res, "noleader") || !strcmp(res, "noperm")) return Org_Toast(pid, true, "Seu cargo não tem permissão para fazer isso.");
            if (!strcmp(res, "higher")) return Org_Toast(pid, true, "Você só pode mexer em quem tem cargo abaixo do seu.");
            if (!strcmp(res, "self")) return Org_Toast(pid, true, "Você não pode fazer isso consigo mesmo.");
            if (!strcmp(res, "notmember")) return Org_Toast(pid, true, "Esse jogador não é da sua organização.");
            if (!strcmp(res, "isleader")) return Org_Toast(pid, true, "Não é possível mexer em um Líder.");
            if (!strcmp(res, "limit")) return Org_Toast(pid, true, gAct2[pid] == 1 ? "Ele já está no cargo mais alto que você pode dar." : "Ele já está no cargo mais baixo.");
            new tg = gArg[pid];
            if (Org_Ok(tg)) gMemAt[tg] = 0;
            Org_Field(res, 0, f, sizeof(f));
            if (strcmp(f, "ok")) return Org_Toast(pid, true, "Pedido recusado.");
            Org_Field(res, 1, f, sizeof(f)); new nr = strval(f);
            Org_Field(res, 2, f, sizeof(f)); new orgid = strval(f);
            new msg[128], cargo[25], nm[MAX_PLAYER_NAME + 1];
            if (Org_Ok(tg)) GetPlayerName(tg, nm, sizeof(nm)); else nm = "O jogador";
            if (nr == 0)
            {
                format(msg, sizeof(msg), "%s foi expulso da organização.", nm);
                Org_Toast(pid, false, msg);
                if (Org_Ok(tg))
                {
                    if (gDuty[tg]) { Org_DutyOff(tg); CallRemoteFunction("Org_Undress", "d", tg); }
                    Org_Toast(tg, true, "Você foi expulso da organização.");
                }
            }
            else
            {
                Org_RankName(orgid, nr, cargo, sizeof(cargo));
                format(msg, sizeof(msg), "%s agora é %s.", nm, cargo);
                Org_Toast(pid, false, msg);
                if (Org_Ok(tg)) { format(msg, sizeof(msg), "Seu cargo agora é: %s.", cargo); Org_Toast(tg, false, msg); }
            }
        }
        case OW_ADM:
        {
            new res[24];
            Org_Line(data, pos, res, sizeof(res));
            if (!ok) return Org_Msg(pid, "Organizacoes: pedido recusado (parametros invalidos ou servico fora do ar).");
            if (!strcmp(res, "ok")) Org_Msg(pid, "Organizacoes: salvo. Em instantes aparece no jogo.");
            else if (!strcmp(res, "noorg")) Org_Msg(pid, "Organizacoes: tag nao encontrada (veja /orglista).");
            else if (!strcmp(res, "nopoint")) Org_Msg(pid, "Organizacoes: esse NPC nao existe (crie com /orgnpc).");
            else if (!strcmp(res, "full")) Org_Msg(pid, "Organizacoes: maximo de 12 locais de saida por organizacao.");
            else if (!strcmp(res, "nospawn")) Org_Msg(pid, "Organizacoes: esse local nao existe.");
            else if (!strcmp(res, "nocar")) Org_Msg(pid, "Organizacoes: esse carro nao esta cadastrado.");
            else if (!strcmp(res, "exists")) Org_Msg(pid, "Organizacoes: ja existe uma organizacao com essa tag.");
            else Org_Msg(pid, "Organizacoes: pedido concluido.");
            gVer = -1;
            SetTimer("Org_Poll", 1200, false);
        }
    }
    return 1;
}

// ------------------------------------------------------------------ RPC do cliente (246) ---
// op 10 = botao ao lado do NPC | op 1 = toque no actor (u16 actorid). Outras ops sao de outros scripts: devolve 1 com o ponteiro no inicio.
IRPC:246(playerid, BitStream:bs)
{
    new op;
    BS_ResetReadPointer(bs);
    if (playerid < 0 || playerid >= MAX_PLAYERS || !IsPlayerConnected(playerid) || !BS_ReadValue(bs, PR_UINT8, op)) return 1;
    if (op == 10)
    {
        // toque no botao: o NPC mais proximo (em ate ORG_TAP_RANGE m) que o jogador pode ver
        new best = -1, Float:bd = 1000.0;
        for (new i = 0; i < gPn; i++)
        {
            if (gMyOrg[playerid] != gP[i][pOrg]) continue;                  // o botao so existe para membro: aqui so os NPCs da propria organizacao
            if (GetPlayerInterior(playerid) != gP[i][pInt] || GetPlayerVirtualWorld(playerid) != gP[i][pVW]) continue;
            new Float:d = GetPlayerDistanceFromPoint(playerid, gP[i][pNX], gP[i][pNY], gP[i][pNZ]);
            if (d < bd && d <= ORG_TAP_RANGE) { bd = d; best = i; }
        }
        BS_ResetReadPointer(bs);
        if (best != -1) { Org_Tap(playerid, best); return 0; }
        return 1;
    }
    if (op == 1)
    {
        new aid;
        if (BS_ReadValue(bs, PR_UINT16, aid))
        {
            for (new i = 0; i < gPn; i++) if (gP[i][pActor] != ORG_NO_ACTOR && gP[i][pActor] == aid) { Org_Tap(playerid, i); return 0; }
        }
    }
    if (op >= GRP_OP_MEMBER && op <= GRP_OP_CLOSE) return Grp_OnRpc(playerid, op, bs);      // Gerenciador de Grupo (abaixo)
    BS_ResetReadPointer(bs);
    return 1;
}

// ------------------------------------------------------------------ troca do modelo base -> id novo (so o que vai ao cliente) ---
// WorldVehicleAdd: u16 VehicleID | int32 modelo ...  (bit 16). SetWriteOffset TRUNCA o pacote: guarda o tamanho e restaura.
ORPC:164(playerid, BitStream:bs)
{
    new vid;
    if (!BS_ReadValue(bs, PR_UINT16, vid)) return 1;
    if (vid > 0 && vid < MAX_VEHICLES && gVModel[vid])
    {
        new total;
        BS_GetWriteOffset(bs, total);
        BS_SetWriteOffset(bs, 16);
        BS_WriteValue(bs, PR_INT32, gVModel[vid]);
        BS_SetWriteOffset(bs, total);
    }
    BS_ResetReadPointer(bs);
    return 1;
}

// ------------------------------------------------------------------ viaturas: so membro em servico dirige ---
public OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate)
{
    if (newstate == PLAYER_STATE_DRIVER)
    {
        new v = GetPlayerVehicleID(playerid);
        if (v > 0 && v < MAX_VEHICLES && gVOrg[v] && gDuty[playerid] != gVOrg[v])
        {
            RemovePlayerFromVehicle(playerid);
            Org_Toast(playerid, true, "Veículo exclusivo de membros em serviço.");
        }
    }
    return 1;
}
public OnVehicleDeath(vehicleid, killerid)
{
    if (vehicleid > 0 && vehicleid < MAX_VEHICLES && gVOwner[vehicleid])
    {
        new p = gVOwner[vehicleid] - 1;
        if (p >= 0 && p < MAX_PLAYERS && gVeh[p] == vehicleid) gVeh[p] = 0;
        Org_VehFree(vehicleid);
        DestroyVehicle(vehicleid);
    }
    return 1;
}

// ------------------------------------------------------------------ comandos de admin ---
stock Org_Lower(s[]) { for (new i = 0; s[i]; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32; }
stock Org_CorpKnown(const s[])
{
    if (!s[0]) return 1;                                 // vazio = so a skin, sem corp
    for (new i = 0; i < sizeof(gCorps); i++) if (!strcmp(s, gCorps[i])) return 1;
    return 0;
}
stock Org_Kind(const s[], kind[], size)
{
    if (!strcmp(s, "cartao") || !strcmp(s, "uniforme")) { format(kind, size, "cartao"); return 1; }
    if (!strcmp(s, "garagem") || !strcmp(s, "veiculo")) { format(kind, size, "garagem"); return 1; }
    return 0;
}

CMD:orgcriar(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], name[41];
    if (sscanf(params, "s[17]s[41]", tag, name)) return Org_Msg(playerid, "Uso: /orgcriar <tag> <nome>   ex.: /orgcriar pm Policia Militar"), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BStr("name", name);
    return Org_Post(playerid, OW_ADM, "org_create", gBody), 1;
}
CMD:orguniforme(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], corp[20], skin = 271;
    if (sscanf(params, "s[17]s[20]D(271)", tag, corp, skin)) return Org_Msg(playerid, "Uso: /orguniforme <tag> <corp> [skin]   ex.: /orguniforme pm goe   (corp 'nenhum' = so a skin)"), 1;
    Org_Lower(tag); Org_Lower(corp);
    if (!strcmp(corp, "nenhum")) corp[0] = 0;
    if (!Org_CorpKnown(corp)) return Org_Msg(playerid, "Corp inexistente. Veja a lista com /corp lista"), 1;
    if (skin < 0 || skin > 311) return Org_Msg(playerid, "Skin 0-311."), 1;
    Org_BodyReset(); Org_BStr("tag", tag); Org_BStr("corp", corp); Org_BInt("skin", skin);
    return Org_Post(playerid, OW_ADM, "org_corp", gBody), 1;
}
CMD:orgativar(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], on;
    if (sscanf(params, "s[17]d", tag, on) || on < 0 || on > 1) return Org_Msg(playerid, "Uso: /orgativar <tag> <0|1>"), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BInt("active", on);
    return Org_Post(playerid, OW_ADM, "org_active", gBody), 1;
}
CMD:orgcarro(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], model, base = 596;
    if (sscanf(params, "s[17]dD(596)", tag, model, base)) return Org_Msg(playerid, "Uso: /orgcarro <tag> <modelo do cliente> [base 400-611, padrao 596]   ex.: /orgcarro pm 612"), 1;
    if (model < 400 || model > 2000 || base < 400 || base > 611) return Org_Msg(playerid, "Modelo 400-2000 e base 400-611."), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BInt("model", model); Org_BInt("base", base); Org_BInt("add", 1);
    return Org_Post(playerid, OW_ADM, "org_car", gBody), 1;
}
CMD:orgcarrorem(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], model;
    if (sscanf(params, "s[17]d", tag, model)) return Org_Msg(playerid, "Uso: /orgcarrorem <tag> <modelo do cliente>"), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BInt("model", model); Org_BInt("add", 0);
    return Org_Post(playerid, OW_ADM, "org_car", gBody), 1;
}
CMD:orgnpc(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], what[12], kind[12], skin = 280;
    if (sscanf(params, "s[17]s[12]D(280)", tag, what, skin) || !Org_Kind(what, kind, sizeof(kind)) || skin < 0 || skin > 311)
        return Org_Msg(playerid, "Uso: /orgnpc <tag> <cartao|garagem> [skin]   (fique onde o NPC deve ficar, olhando para onde o jogador vai ficar)"), 1;
    Org_Lower(tag);
    new Float:x, Float:y, Float:z, Float:a;
    GetPlayerPos(playerid, x, y, z); GetPlayerFacingAngle(playerid, a);
    new Float:mx = x + 1.6 * floatsin(-a, degrees), Float:my = y + 1.6 * floatcos(-a, degrees), Float:mz = z - 0.95;     // anel 1,6 m a frente, no chao
    new nome[25];
    if (!strcmp(kind, "cartao")) nome = "Ponto"; else nome = "Garagem";
    Org_BodyReset(); Org_BStr("tag", tag); Org_BStr("kind", kind); Org_BInt("skin", skin); Org_BStr("name", nome);
    Org_BFloat("nx", x); Org_BFloat("ny", y); Org_BFloat("nz", z); Org_BFloat("na", a);
    Org_BFloat("mx", mx); Org_BFloat("my", my); Org_BFloat("mz", mz);
    Org_BInt("int", GetPlayerInterior(playerid)); Org_BInt("vw", GetPlayerVirtualWorld(playerid));
    return Org_Post(playerid, OW_ADM, "org_point", gBody), 1;
}
CMD:orgnpcrem(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], what[12], kind[12];
    if (sscanf(params, "s[17]s[12]", tag, what) || !Org_Kind(what, kind, sizeof(kind))) return Org_Msg(playerid, "Uso: /orgnpcrem <tag> <cartao|garagem>"), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BStr("kind", kind);
    return Org_Post(playerid, OW_ADM, "org_pointdel", gBody), 1;
}
CMD:orgspawn(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17];
    if (sscanf(params, "s[17]", tag)) return Org_Msg(playerid, "Uso: /orgspawn <tag>   ACRESCENTA um local onde as viaturas nascem (ate 12; com varios, o jogo sorteia um livre). De pe no local, ou dentro de um veiculo parado na posicao/angulo desejados."), 1;
    Org_Lower(tag);
    new Float:x, Float:y, Float:z, Float:a, v = GetPlayerVehicleID(playerid);
    if (v) { GetVehiclePos(v, x, y, z); GetVehicleZAngle(v, a); }
    else { GetPlayerPos(playerid, x, y, z); GetPlayerFacingAngle(playerid, a); z -= 0.3; }
    Org_BodyReset(); Org_BStr("tag", tag); Org_BFloat("x", x); Org_BFloat("y", y); Org_BFloat("z", z); Org_BFloat("a", a);
    return Org_Post(playerid, OW_ADM, "org_spawn", gBody), 1;
}
CMD:orgspawnlista(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17];
    if (sscanf(params, "s[17]", tag)) return Org_Msg(playerid, "Uso: /orgspawnlista <tag>"), 1;
    Org_Lower(tag);
    new o = Org_FindTag(tag);
    if (o == -1) return Org_Msg(playerid, "Tag nao encontrada (veja /orglista)."), 1;
    new n = 0, msg[128];
    for (new i = 0; i < gSpn; i++)
    {
        if (gSp[i][spOrg] != gO[o][oId]) continue;
        n++;
        format(msg, sizeof(msg), "#%d: %.1f, %.1f, %.1f  angulo %.0f", n, gSp[i][spX], gSp[i][spY], gSp[i][spZ], gSp[i][spA]);
        Org_Msg(playerid, msg);
    }
    if (!n) return Org_Msg(playerid, "Nenhum local de saida. Use /orgspawn <tag> de pe onde as viaturas devem nascer."), 1;
    format(msg, sizeof(msg), "%d local(is). Remover: /orgspawnrem %s <n|todos>", n, tag);
    return Org_Msg(playerid, msg), 1;
}
CMD:orgspawnrem(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], what[12];
    if (sscanf(params, "s[17]s[12]", tag, what)) return Org_Msg(playerid, "Uso: /orgspawnrem <tag> <n|todos>   (n = numero da /orgspawnlista)"), 1;
    Org_Lower(tag); Org_Lower(what);
    new o = Org_FindTag(tag), sid = 0;
    if (o == -1) return Org_Msg(playerid, "Tag nao encontrada (veja /orglista)."), 1;
    if (strcmp(what, "todos"))
    {
        new want = strval(what), n = 0;
        if (want < 1) return Org_Msg(playerid, "Informe o numero do local (/orgspawnlista) ou 'todos'."), 1;
        for (new i = 0; i < gSpn; i++) if (gSp[i][spOrg] == gO[o][oId] && ++n == want) { sid = gSp[i][spId]; break; }
        if (!sid) return Org_Msg(playerid, "Esse numero nao existe (veja /orgspawnlista)."), 1;
    }
    Org_BodyReset(); Org_BStr("tag", tag); Org_BInt("id", sid);
    return Org_Post(playerid, OW_ADM, "org_spawndel", gBody), 1;
}
CMD:orgmembro(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new target, tag[17], rank = 7;
    if (sscanf(params, "us[17]D(7)", target, tag, rank)) return Org_Msg(playerid, "Uso: /orgmembro <id do jogador> <tag|0> [cargo 1-7, padrao 7]   (1 = Lider; 0 = tira da organizacao)"), 1;
    if (!Org_Ok(target)) return Org_Msg(playerid, "Jogador nao conectado."), 1;
    new acct = Org_Acct(target);
    if (acct <= 0) return Org_Msg(playerid, "Esse jogador nao esta logado."), 1;
    if (rank < 1 || rank > 7) return Org_Msg(playerid, "Cargo 1-7 (1 = Lider)."), 1;
    Org_Lower(tag);
    if (!strcmp(tag, "0")) tag[0] = 0;
    new nm[MAX_PLAYER_NAME + 1];
    GetPlayerName(target, nm, sizeof(nm));
    Org_BodyReset(); Org_BInt("acct", acct); Org_BStr("tag", tag); Org_BInt("rank", rank); Org_BStr("name", nm);
    gMemAt[target] = 0;
    if (!tag[0] && gDuty[target]) { Org_DutyOff(target); CallRemoteFunction("Org_Undress", "d", target); Org_Toast(target, true, "Você foi removido da organização."); }
    return Org_Post(playerid, OW_ADM, "org_setmember", gBody), 1;
}
CMD:orglider(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new target, tag[17];
    if (sscanf(params, "us[17]", target, tag)) return Org_Msg(playerid, "Uso: /orglider <id do jogador> <tag>   (da o cargo de Lider; /orgmembro <id> 0 tira)"), 1;
    if (!Org_Ok(target)) return Org_Msg(playerid, "Jogador nao conectado."), 1;
    new acct = Org_Acct(target);
    if (acct <= 0) return Org_Msg(playerid, "Esse jogador nao esta logado."), 1;
    Org_Lower(tag);
    new nm[MAX_PLAYER_NAME + 1];
    GetPlayerName(target, nm, sizeof(nm));
    Org_BodyReset(); Org_BInt("acct", acct); Org_BStr("tag", tag); Org_BInt("rank", 1); Org_BStr("name", nm);
    gMemAt[target] = 0;
    Org_Toast(target, false, "Você agora é Líder da organização. Use /convidar, /promover, /rebaixar e /expulsar.");
    return Org_Post(playerid, OW_ADM, "org_setmember", gBody), 1;
}
CMD:orgcargo(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], rank, name[25];
    if (sscanf(params, "s[17]ds[25]", tag, rank, name) || rank < 1 || rank > 7) return Org_Msg(playerid, "Uso: /orgcargo <tag> <1-7> <nome do cargo>   (1 = Lider)   ex.: /orgcargo pm 3 Capitao"), 1;
    Org_Lower(tag);
    Org_BodyReset(); Org_BStr("tag", tag); Org_BInt("rank", rank); Org_BStr("name", name);
    return Org_Post(playerid, OW_ADM, "org_rankname", gBody), 1;
}

// ---- comandos do LIDER (a regra e validada no banco: org.rank_act / org.invite_check) ----
stock Org_LeaderCmd(playerid, const params[], act, const usage[])
{
    new target;
    if (sscanf(params, "u", target)) return Org_Msg(playerid, usage), 1;
    if (!Org_Ok(target) || IsPlayerNPC(target)) return Org_Msg(playerid, "Jogador nao conectado."), 1;
    if (target == playerid) return Org_Toast(playerid, true, "Você não pode fazer isso consigo mesmo."), 1;
    new me = Org_Acct(playerid), tg = Org_Acct(target);
    if (me <= 0) return Org_Toast(playerid, true, "Faça login primeiro."), 1;
    if (tg <= 0) return Org_Toast(playerid, true, "Esse jogador não está logado."), 1;
    new now = GetTickCount();
    if (now - gLastTap[playerid] < 800) return 1;
    gLastTap[playerid] = now;
    gArg[playerid] = target; gAct2[playerid] = act;
    Org_BodyReset(); Org_BInt("leader", me); Org_BInt("target", tg);
    if (act == 0) return Org_Post(playerid, OW_INV, "org_invite", gBody), 1;
    Org_BStr("act", act == 1 ? "promote" : (act == 2 ? "demote" : "kick"));
    return Org_Post(playerid, OW_RACT, "org_rankact", gBody), 1;
}
CMD:convidar(playerid, params[])  { return Org_LeaderCmd(playerid, params, 0, "Uso: /convidar <id>   (so o Lider da organizacao)"); }
CMD:promover(playerid, params[])  { return Org_LeaderCmd(playerid, params, 1, "Uso: /promover <id>   (so o Lider; sobe um cargo, ate o cargo 2)"); }
CMD:rebaixar(playerid, params[])  { return Org_LeaderCmd(playerid, params, 2, "Uso: /rebaixar <id>   (so o Lider; desce um cargo)"); }
CMD:expulsar(playerid, params[])  { return Org_LeaderCmd(playerid, params, 3, "Uso: /expulsar <id>   (so o Lider; tira da organizacao)"); }
CMD:aceitarorg(playerid)
{
    if (gInvOrg[playerid] <= 0 || gettime() - gInvAt[playerid] > ORG_INVITE_SECS) { gInvOrg[playerid] = 0; return Org_Toast(playerid, true, "Você não tem convite pendente."), 1; }
    new acct = Org_Acct(playerid), o = Org_FindOrg(gInvOrg[playerid]);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro."), 1;
    if (o == -1 || !gO[o][oActive]) { gInvOrg[playerid] = 0; return Org_Toast(playerid, true, "Essa organização não está disponível."), 1; }
    new nm[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, nm, sizeof(nm));
    Org_BodyReset(); Org_BInt("acct", acct); Org_BStr("tag", gO[o][oTag]); Org_BStr("name", nm);
    return Org_Post(playerid, OW_JOIN, "org_join", gBody), 1;
}
CMD:recusarorg(playerid)
{
    if (gInvOrg[playerid] <= 0) return Org_Toast(playerid, true, "Você não tem convite pendente."), 1;
    gInvOrg[playerid] = 0;
    if (Org_Ok(gInvFrom[playerid])) Org_Msg(gInvFrom[playerid], "Convite recusado.");
    return Org_Toast(playerid, false, "Convite recusado."), 1;
}
CMD:org(playerid)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro."), 1;
    if (gBusy[playerid]) return 1;
    gBusy[playerid] = 1; gAction[playerid] = 1;
    Org_BodyReset(); Org_BInt("acct", acct);
    return Org_Post(playerid, OW_MEMBER, "org_member", gBody), 1;
}
stock Org_Info(playerid, const line[])
{
    new f[24];
    Org_Field(line, 0, f, sizeof(f));
    new o = Org_FindOrg(strval(f));
    if (!line[0] || o == -1) return Org_Toast(playerid, false, "Você não faz parte de nenhuma organização.");
    Org_Field(line, 1, f, sizeof(f));
    new cargo[25], msg[128];
    Org_RankName(gO[o][oId], strval(f), cargo, sizeof(cargo));
    format(msg, sizeof(msg), "%s — cargo: %s%s", gO[o][oName], cargo, gDuty[playerid] == gO[o][oId] ? " (em serviço)" : "");
    return Org_Toast(playerid, false, msg);
}

// /chatjogadores [on|off]: admin do jogo (nivel 5, nunca RCON) liga/desliga o chat so dos jogadores; admin continua vendo
// /av <texto>: aviso para TODOS no canto da tela. Admin do jogo (nivel 5, nunca RCON).
CMD:av(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    if (!params[0]) return Org_Toast(playerid, true, "Use /av <texto>"), 1;
    NotifyAll(NOTIFY_WARN, "Aviso para todos", params, 90000);
    return 1;
}

// /ang <texto>: anuncio de lider. Só o lider (cargo 1) da organizacao anuncia para todos; 1 por minuto.
#define ORG_ANG_SECS (60)
CMD:ang(playerid, params[])
{
    new o = Org_FindOrg(gMyOrg[playerid]);
    if (o == -1 || gMyRank[playerid] != 1) return Org_Toast(playerid, true, "Só o líder de uma organização pode anunciar."), 1;
    if (!params[0]) return Org_Toast(playerid, true, "Use /ang <texto>"), 1;
    new wait = ORG_ANG_SECS - (gettime() - gAngAt[playerid]);
    if (wait > 0)
    {
        new m[64];
        format(m, sizeof(m), "Aguarde %d s para anunciar de novo.", wait);
        return Org_Toast(playerid, true, m), 1;
    }
    gAngAt[playerid] = gettime();
    new title[32];
    format(title, sizeof(title), "%s", gO[o][oName]);
    NotifyAll(NOTIFY_WARN, title, params, 90000);
    return 1;
}

CMD:chatjogadores(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    if (!strcmp(params, "on", true)) gChatOn = true;
    else if (!strcmp(params, "off", true)) gChatOn = false;
    else { gChatOn = !gChatOn; }
    new File:f = fopen("chat_jogadores.txt", io_write);
    if (f) { fwrite(f, gChatOn ? "1" : "0"); fclose(f); }
    for (new i = 0; i < MAX_PLAYERS; i++) gChatSig[i] = -1;      // reenvia o estado ao cliente no proximo segundo
    return Org_Toast(playerid, false, gChatOn ? "Chat dos jogadores LIGADO." : "Chat dos jogadores DESLIGADO (so admin ve)."), 1;
}

// /liberar provador [id]  |  /bloquear provador [id]  (admin): quem nao e admin so usa o /provador se for liberado
CMD:liberar(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new what[16], alvo;
    if (sscanf(params, "s[16]u", what, alvo) || strcmp(what, "provador", true)) return Org_Msg(playerid, "Use: /liberar provador [id]"), 1;
    if (alvo == INVALID_PLAYER_ID || !IsPlayerConnected(alvo) || IsPlayerNPC(alvo)) return Org_Toast(playerid, true, "Jogador não encontrado."), 1;
    new n[MAX_PLAYER_NAME];
    GetPlayerName(alvo, n, sizeof(n));
    if (Org_ProvIdx(n) != -1) return Org_Toast(playerid, true, "Esse jogador já está liberado."), 1;
    if (gProvN >= ORG_PROV_MAX) return Org_Toast(playerid, true, "Lista de liberados cheia."), 1;
    format(gProvNome[gProvN++], MAX_PLAYER_NAME, "%s", n);
    Org_ProvSave();
    gChatSig[alvo] = -1;
    Org_Toast(alvo, false, "Um administrador liberou o /provador para você.");
    return Org_Toast(playerid, false, "/provador liberado para o jogador."), 1;
}
CMD:bloquear(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new what[16], alvo;
    if (sscanf(params, "s[16]u", what, alvo) || strcmp(what, "provador", true)) return Org_Msg(playerid, "Use: /bloquear provador [id]"), 1;
    if (alvo == INVALID_PLAYER_ID || !IsPlayerConnected(alvo)) return Org_Toast(playerid, true, "Jogador não encontrado."), 1;
    new n[MAX_PLAYER_NAME];
    GetPlayerName(alvo, n, sizeof(n));
    new i = Org_ProvIdx(n);
    if (i == -1) return Org_Toast(playerid, true, "Esse jogador não estava liberado."), 1;
    for (new j = i; j < gProvN - 1; j++) format(gProvNome[j], MAX_PLAYER_NAME, "%s", gProvNome[j + 1]);
    gProvN--;
    Org_ProvSave();
    gChatSig[alvo] = -1;
    return Org_Toast(playerid, false, "/provador bloqueado para o jogador."), 1;
}

CMD:orgdiag(playerid)
{
    if (!Org_Admin(playerid)) return 0;
    new msg[160];
    format(msg, sizeof(msg), "[orgdiag] organizacoes=%d NPCs=%d carros=%d locais=%d | voce: org=%d cargo=%d servico=%d cache=%s", gOn, gPn, gCn, gSpn, gMyOrg[playerid], gMyRank[playerid], gDuty[playerid], gMemAt[playerid] ? "ok" : "AINDA SEM CONSULTA");
    Org_Msg(playerid, msg);
    for (new i = 0; i < gPn; i++)
    {
        new o = Org_FindOrg(gP[i][pOrg]), tg[17];
        if (o != -1) format(tg, sizeof(tg), "%s", gO[o][oTag]); else tg = "?";
        format(msg, sizeof(msg), "NPC %d (%s) org=%s ativa=%d ator=%d dist=%.0fm int=%d/%d (voce %d/%d) %s", i, gP[i][pKind] == OK_CARTAO ? "cartao" : "garagem", tg, o != -1 ? gO[o][oActive] : 0, gP[i][pActor],
            GetPlayerDistanceFromPoint(playerid, gP[i][pNX], gP[i][pNY], gP[i][pNZ]), gP[i][pInt], gP[i][pVW], GetPlayerInterior(playerid), GetPlayerVirtualWorld(playerid), gMyOrg[playerid] == gP[i][pOrg] ? "VOCE E MEMBRO: o botao deve aparecer" : "voce NAO e membro: sem botao");
        Org_Msg(playerid, msg);
    }
    return 1;
}
CMD:orglista(playerid)
{
    if (!Org_Admin(playerid)) return 0;
    new msg[144];
    format(msg, sizeof(msg), "Organizacoes carregadas: %d  (cadastro completo: painel web /organizacoes)", gOn);
    Org_Msg(playerid, msg);
    for (new i = 0; i < gOn; i++)
    {
        new cars = 0, npcs = 0;
        for (new k = 0; k < gCn; k++) if (gC[k][cOrg] == gO[i][oId]) cars++;
        for (new k = 0; k < gPn; k++) if (gP[k][pOrg] == gO[i][oId]) npcs++;
        new corp[16];
        if (gO[i][oCorp][0]) format(corp, sizeof(corp), "%s", gO[i][oCorp]); else corp = "-";
        format(msg, sizeof(msg), "[%s] %s | /corp %s | skin %d | %d carro(s) | %d NPC(s) | %s", gO[i][oTag], gO[i][oName], corp, gO[i][oSkin], cars, npcs, gO[i][oActive] ? "ATIVA" : "desativada");
        Org_Msg(playerid, msg);
    }
    return 1;
}
CMD:orgrecarregar(playerid)
{
    if (!Org_Admin(playerid)) return 0;
    gVer = -1;
    Org_Poll();
    return Org_Msg(playerid, "Recarregando cadastros de organizacoes..."), 1;
}

// ------------------------------------------------------------------ ciclo de vida ---
// =================================================================================================================
// GERENCIADOR DE GRUPO  (/grupo)  -  painel do cliente com abas Inicio / Membros / Farm / Cofre.
// Os DADOS vem do PostgreSQL (celular-service, porta publica, token assinado). Aqui ficam so as acoes que mexem no que e do
// JOGADOR (dinheiro, inventario) ou no jogo (convite, cargo, uniforme): o cliente pede por RPC 246 e este script valida tudo.
//   servidor -> cliente (RPC 245):  op 16 = abrir o painel (u16 porta | str token)   |   op 17 = acao concluida (u8 acao | u8 ok): recarrega a aba
//   cliente -> servidor (RPC 246):  op 40 = membro (u8 acao 0 convidar/1 promover/2 rebaixar/3 expulsar | u32 conta | u16 id do jogador)
//                                   op 41 = depositar (u32) | op 42 = sacar (u32) | op 43 = entregar farm (u32, 0 = tudo) | op 44 = GPS | op 45 = fechou
// =================================================================================================================
enum { GW_TOKEN = 1, GW_BANK, GW_FARMI, GW_FARM, GW_PRES, GW_GPS, GW_CFG };
forward OrgM_Cb(index, response_code, data[]);

new gGrpAt[MAX_PLAYERS], gGrpBusy[MAX_PLAYERS];
new gGrpAmt[MAX_PLAYERS], gGrpDep[MAX_PLAYERS];             // cofre: valor pedido / 1 = deposito
new gGrpItem[MAX_PLAYERS], gGrpQty[MAX_PLAYERS];            // farm: item e quantidade pedidos / ja tirada do inventario
new gPresOn[MAX_PLAYERS], gPresAt[MAX_PLAYERS], gPresAcct[MAX_PLAYERS];

stock Grp_Reset(playerid)
{
    gGrpOpen[playerid] = 0; gGrpAt[playerid] = 0; gGrpBusy[playerid] = 0; gGrpAmt[playerid] = 0; gGrpDep[playerid] = 0; gGrpItem[playerid] = 0; gGrpQty[playerid] = 0;
    gPresOn[playerid] = 0; gPresAt[playerid] = 0; gPresAcct[playerid] = 0;
    return 1;
}

// op local do celular-service; o indice tem 6 bits para "what" (Org_Post so tem 4). playerid < 0 = sem jogador (resposta ignorada)
stock Org_MPost(playerid, what, const op[], const body[])
{
    new url[128];
    format(url, sizeof(url), "%s%s?k=%s", ORG_URL, op, ORG_SEGREDO);
    new pid = (playerid < 0) ? 1023 : (playerid & 0x3FF);
    new ser = (playerid < 0) ? 0 : gSerial[playerid];
    HTTP(pid | ((what & 0x3F) << 10) | ((ser & 0x3FFF) << 16), HTTP_POST, url, body, "OrgM_Cb");
    return 1;
}

// R$ 1.234.567
stock Grp_Money(v, out[], size)
{
    new t[16];
    format(t, sizeof(t), "%d", v < 0 ? -v : v);
    new len = strlen(t), o[24], w = 0;
    if (v < 0) o[w++] = '-';
    for (new i = 0; i < len; i++)
    {
        if (i > 0 && (len - i) % 3 == 0) o[w++] = '.';
        o[w++] = t[i];
    }
    o[w] = 0;
    format(out, size, "R$%s", o);
    return 1;
}
stock Grp_Name(playerid, out[], size)
{
    GetPlayerName(playerid, out, size);
    for (new i = 0; out[i]; i++) if (out[i] == '_') out[i] = ' ';
    return 1;
}

stock Grp_SendOpen(playerid, port, const token[])
{
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, GRP_RPC_OPEN, PR_UINT16, port);
    Org_WriteStr(bs, token, 80);
    PR_SendRPC(bs, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    gGrpOpen[playerid] = 1;
    return 1;
}
// avisa o painel aberto que uma acao terminou (o banco ja gravou): ele recarrega a aba
stock Grp_Done(playerid, what, ok)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS || !gGrpOpen[playerid]) return 0;
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, GRP_RPC_DONE, PR_UINT8, what, PR_UINT8, ok ? 1 : 0);
    PR_SendRPC(bs, playerid, ORG_RPC_OUT, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

stock Grp_Open(playerid)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro.");
    new now = GetTickCount();
    if (now - gGrpAt[playerid] < 1500) return 1;
    gGrpAt[playerid] = now;
    Org_BodyReset(); Org_BInt("acct", acct);
    return Org_MPost(playerid, GW_TOKEN, "org_mgr_token", gBody);
}
CMD:grupo(playerid)         { Grp_Open(playerid); return 1; }
CMD:gerenciador(playerid)   { Grp_Open(playerid); return 1; }

// ---- membros: convidar / promover / rebaixar / expulsar (mesma regra e mesmas mensagens de /convidar, /promover...) ----
stock Grp_Member(playerid, act, tacct, tid)
{
    new me = Org_Acct(playerid);
    if (me <= 0) return Org_Toast(playerid, true, "Faça login primeiro.");
    if (act < 0 || act > 3) return 0;
    new now = GetTickCount();
    if (now - gLastTap[playerid] < 800) return 1;
    gLastTap[playerid] = now;
    new target = INVALID_PLAYER_ID;
    if (act == 0)
    {
        if (!Org_Ok(tid) || IsPlayerNPC(tid)) return Org_Toast(playerid, true, "Jogador não conectado.");
        if (tid == playerid) return Org_Toast(playerid, true, "Você não pode fazer isso consigo mesmo.");
        target = tid;
        tacct = Org_Acct(tid);
        if (tacct <= 0) return Org_Toast(playerid, true, "Esse jogador não está logado.");
    }
    else
    {
        if (tacct < 1) return 0;
        for (new p = 0; p < MAX_PLAYERS; p++) if (IsPlayerConnected(p) && !IsPlayerNPC(p) && Org_Acct(p) == tacct) { target = p; break; }
    }
    gArg[playerid] = target; gAct2[playerid] = act;
    Org_BodyReset(); Org_BInt("leader", me); Org_BInt("target", tacct);
    if (act == 0) return Org_Post(playerid, OW_INV, "org_invite", gBody);
    Org_BStr("act", act == 1 ? "promote" : (act == 2 ? "demote" : "kick"));
    return Org_Post(playerid, OW_RACT, "org_rankact", gBody);
}

// ---- cofre ----
stock Grp_Bank(playerid, bool:deposit, amount)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro.");
    if (amount < 1 || amount > GRP_MAX_VALUE) return Org_Toast(playerid, true, "Valor inválido.");
    if (gGrpBusy[playerid]) return Org_Toast(playerid, true, "Aguarde o pedido anterior terminar.");
    if (deposit)
    {
        if (GetPlayerMoney(playerid) < amount) return Org_Toast(playerid, true, "Você não tem esse dinheiro em mãos.");
        GivePlayerMoney(playerid, -amount);                          // tira ANTES; devolve se o banco recusar
    }
    gGrpBusy[playerid] = 1; gGrpAmt[playerid] = amount; gGrpDep[playerid] = deposit ? 1 : 0;
    new nm[MAX_PLAYER_NAME + 1];
    Grp_Name(playerid, nm, sizeof(nm));
    if (!deposit) printf("[GRUPO] saque pedido: conta %d valor %d", acct, amount);
    Org_BodyReset(); Org_BInt("acct", acct); Org_BStr("kind", deposit ? "deposit" : "withdraw"); Org_BInt("amount", amount); Org_BStr("name", nm);
    return Org_MPost(playerid, GW_BANK, "org_bank", gBody);
}

// ---- farm: 1) pergunta qual item/preco o grupo usa  2) tira do inventario  3) registra e paga ----
stock Grp_FarmStart(playerid, qty)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro.");
    if (qty < 0 || qty > 100000) return Org_Toast(playerid, true, "Quantidade inválida.");
    if (gGrpBusy[playerid]) return Org_Toast(playerid, true, "Aguarde o pedido anterior terminar.");
    gGrpBusy[playerid] = 1; gGrpQty[playerid] = qty;
    Org_BodyReset(); Org_BInt("acct", acct);
    return Org_MPost(playerid, GW_FARMI, "org_farm_info", gBody);
}
stock Grp_Gps(playerid)
{
    new acct = Org_Acct(playerid);
    if (acct <= 0) return Org_Toast(playerid, true, "Faça login primeiro.");
    new now = GetTickCount();
    if (now - gGrpAt[playerid] < 1000) return 1;
    gGrpAt[playerid] = now;
    Org_BodyReset(); Org_BInt("acct", acct);
    return Org_MPost(playerid, GW_GPS, "org_gps", gBody);
}

stock Grp_OnRpc(playerid, op, BitStream:bs)
{
    switch (op)
    {
        case GRP_OP_MEMBER:
        {
            new act, tacct, tid;
            if (!BS_ReadValue(bs, PR_UINT8, act)) return 0;
            if (!BS_ReadValue(bs, PR_UINT32, tacct)) return 0;
            if (!BS_ReadValue(bs, PR_UINT16, tid)) return 0;
            Grp_Member(playerid, act, tacct, tid);
        }
        case GRP_OP_DEPOSIT, GRP_OP_WITHDRAW:
        {
            new v;
            if (BS_ReadValue(bs, PR_UINT32, v)) Grp_Bank(playerid, op == GRP_OP_DEPOSIT, v);
        }
        case GRP_OP_FARM:
        {
            new q;
            if (BS_ReadValue(bs, PR_UINT32, q)) Grp_FarmStart(playerid, q);
        }
        case GRP_OP_GPS: Grp_Gps(playerid);
        case GRP_OP_CLOSE: gGrpOpen[playerid] = 0;
    }
    return 0;
}

// ---- presenca e tempo jogado: o banco soma; so quem e membro de um grupo tem linha ----
stock Grp_PresTick(playerid)
{
    new t = gettime();
    if ((t + playerid) % 10 != 0) return 0;                          // olha cada jogador a cada 10 s (Cel_AccountId e uma chamada entre scripts)
    new acct = Org_Acct(playerid);
    if (acct <= 0)
    {
        if (gPresOn[playerid]) Grp_Leave(playerid);
        return 0;
    }
    if (gPresOn[playerid] && gPresAcct[playerid] != acct) Grp_Leave(playerid);     // trocou de conta sem desconectar
    if (!gPresOn[playerid])
    {
        gPresOn[playerid] = 1; gPresAt[playerid] = t; gPresAcct[playerid] = acct;
        Org_BodyReset(); Org_BInt("acct", acct); Org_BInt("state", 1); Org_BInt("secs", 0);
        return Org_MPost(-1, GW_PRES, "org_presence", gBody);
    }
    if (t - gPresAt[playerid] >= GRP_PRES_SECS)
    {
        new secs = t - gPresAt[playerid];
        if (secs > 3600) secs = 3600;
        gPresAt[playerid] = t;
        Org_BodyReset(); Org_BInt("acct", acct); Org_BInt("state", 2); Org_BInt("secs", secs);
        return Org_MPost(-1, GW_PRES, "org_presence", gBody);
    }
    return 0;
}
stock Grp_Leave(playerid)
{
    if (!gPresOn[playerid]) return 0;
    new secs = gettime() - gPresAt[playerid];
    if (secs < 0) secs = 0;
    if (secs > 3600) secs = 3600;
    Org_BodyReset(); Org_BInt("acct", gPresAcct[playerid]); Org_BInt("state", 0); Org_BInt("secs", secs);
    Org_MPost(-1, GW_PRES, "org_presence", gBody);
    gPresOn[playerid] = 0; gGrpOpen[playerid] = 0;
    return 1;
}

public OrgM_Cb(index, response_code, data[])
{
    new pid = index & 0x3FF, what = (index >> 10) & 0x3F, ser = (index >> 16) & 0x3FFF;
    new pos = 0, line[160], res[120];
    Org_Line(data, pos, line, sizeof(line));
    new bool:ok = (response_code == 200 && !strcmp(line, "ok"));
    if (ok) Org_Line(data, pos, res, sizeof(res)); else res[0] = 0;
    if (!ok && what != GW_PRES) printf("[GRUPO] pedido %d falhou: http %d resposta '%s'", what, response_code, line);
    if (pid == 1023) return 1;                                       // presenca: ninguem espera resposta
    if (pid >= MAX_PLAYERS || !IsPlayerConnected(pid) || ser != (gSerial[pid] & 0x3FFF))
    {
        if (ok && (what == GW_BANK || what == GW_FARM)) printf("[GRUPO] ATENCAO: jogador %d saiu antes da resposta do pedido %d: '%s' (confira org.bank_log / org.farm_log)", pid, what, res);
        return 0;
    }
    new f[24], msg[160], money[28];
    Org_Field(res, 0, f, sizeof(f));
    switch (what)
    {
        case GW_TOKEN:
        {
            if (!ok) return Org_Toast(pid, true, "O gerenciador está indisponível. Avise um administrador.");
            if (!strcmp(f, "nomember")) return Org_Toast(pid, true, "Você não faz parte de nenhum grupo.");
            if (strcmp(f, "ok")) return Org_Toast(pid, true, "Não foi possível abrir o gerenciador.");
            new tok[72], pt[12];
            Org_Field(res, 1, tok, sizeof(tok)); Org_Field(res, 2, pt, sizeof(pt));
            Grp_SendOpen(pid, strval(pt), tok);
        }
        case GW_BANK:
        {
            gGrpBusy[pid] = 0;
            new amt = gGrpAmt[pid], bool:dep = (gGrpDep[pid] == 1);
            Grp_Money(amt, money, sizeof(money));
            if (!ok || strcmp(f, "ok"))
            {
                if (dep) GivePlayerMoney(pid, amt);                  // devolve o que foi tirado
                Grp_Done(pid, 10, 0);
                if (!ok) return Org_Toast(pid, true, "O serviço não respondeu. Seu dinheiro foi devolvido.");
                if (!strcmp(f, "noperm")) return Org_Toast(pid, true, "Seu cargo não tem permissão para sacar do cofre.");
                if (!strcmp(f, "nofunds")) return Org_Toast(pid, true, "O cofre não tem esse valor.");
                if (!strcmp(f, "nomember")) return Org_Toast(pid, true, "Você não faz parte de nenhum grupo.");
                return Org_Toast(pid, true, "Pedido recusado.");
            }
            if (!dep) GivePlayerMoney(pid, amt);
            if (dep) format(msg, sizeof(msg), "Você depositou %s no cofre do grupo.", money);
            else     format(msg, sizeof(msg), "Você sacou %s do cofre do grupo.", money);
            Grp_Done(pid, 10, 1);
            Org_Toast(pid, false, msg);
        }
        case GW_FARMI:
        {
            if (!ok || strcmp(f, "ok"))
            {
                gGrpBusy[pid] = 0;
                if (!strcmp(f, "nomember")) return Org_Toast(pid, true, "Você não faz parte de nenhum grupo.");
                return Org_Toast(pid, true, "O serviço não respondeu.");
            }
            new it[16], item;
            Org_Field(res, 1, it, sizeof(it)); item = strval(it);
            if (item <= 0) { gGrpBusy[pid] = 0; return Org_Toast(pid, true, "A coleta deste grupo ainda não foi configurada."); }
            new have = CallRemoteFunction("Grp_ItemCount", "dd", pid, item), q = gGrpQty[pid];
            if (q <= 0 || q > have) q = have;
            if (q < 1) { gGrpBusy[pid] = 0; return Org_Toast(pid, true, "Você não tem itens de coleta no inventário."); }
            if (!CallRemoteFunction("Grp_ItemTake", "ddd", pid, item, q)) { gGrpBusy[pid] = 0; return Org_Toast(pid, true, "Não foi possível tirar os itens do inventário."); }
            gGrpItem[pid] = item; gGrpQty[pid] = q;
            new acct = Org_Acct(pid), nm[MAX_PLAYER_NAME + 1];
            Grp_Name(pid, nm, sizeof(nm));
            printf("[GRUPO] coleta: conta %d entregou %d do item %d", acct, q, item);
            Org_BodyReset(); Org_BInt("acct", acct); Org_BInt("qty", q); Org_BStr("name", nm);
            Org_MPost(pid, GW_FARM, "org_farm", gBody);
        }
        case GW_FARM:
        {
            gGrpBusy[pid] = 0;
            if (!ok || strcmp(f, "ok"))
            {
                CallRemoteFunction("Grp_ItemGive", "ddd", pid, gGrpItem[pid], gGrpQty[pid]);          // nao registrou: devolve os itens
                Grp_Done(pid, 11, 0);
                return Org_Toast(pid, true, "Não foi possível registrar a entrega. Seus itens foram devolvidos.");
            }
            new fp[16], ft[16], fg[16], paid, today, goal;
            Org_Field(res, 2, fp, sizeof(fp)); Org_Field(res, 3, ft, sizeof(ft)); Org_Field(res, 4, fg, sizeof(fg));
            paid = strval(fp); today = strval(ft); goal = strval(fg);
            if (paid > 0) GivePlayerMoney(pid, paid);
            Grp_Money(paid, money, sizeof(money));
            format(msg, sizeof(msg), "Entrega registrada: %d item(ns). Você recebeu %s. Hoje: %d/%d.", gGrpQty[pid], money, today, goal);
            Grp_Done(pid, 11, 1);
            Org_Toast(pid, false, msg);
            if (goal > 0 && today >= goal && today - gGrpQty[pid] < goal) Org_Toast(pid, false, "Meta diária de coleta batida! Parabéns!");
        }
        case GW_GPS:
        {
            if (!ok) return Org_Toast(pid, true, "O serviço não respondeu.");
            if (!strcmp(f, "nomember")) return Org_Toast(pid, true, "Você não faz parte de nenhum grupo.");
            if (!strcmp(f, "nogps")) return Org_Toast(pid, true, "A sede deste grupo ainda não tem GPS definido.");
            new gx[16], gy[16], gz[16];
            Org_Field(res, 1, gx, sizeof(gx)); Org_Field(res, 2, gy, sizeof(gy)); Org_Field(res, 3, gz, sizeof(gz));
            DisablePlayerCheckpoint(pid);
            SetPlayerCheckpoint(pid, floatstr(gx), floatstr(gy), floatstr(gz), 6.0);
            Org_Toast(pid, false, "GPS marcado: siga o marcador até a sede do grupo.");
        }
        case GW_CFG:
        {
            if (!ok) return Org_Msg(pid, "Grupo: pedido recusado (valor inválido ou serviço fora do ar).");
            if (!strcmp(f, "noorg")) return Org_Msg(pid, "Grupo: tag não encontrada (veja /orglista).");
            Org_Msg(pid, "Grupo: configuração salva.");
        }
    }
    return 1;
}

// /orgcfg <tag> <discord|radio|max|farmitem|farmname|farmprice|farmgoal|bank|gps> [valor]    (admin do jogo)
CMD:orgcfg(playerid, params[])
{
    if (!Org_Admin(playerid)) return 0;
    new tag[17], key[16], val[80];
    if (sscanf(params, "s[17]s[16]S()[80]", tag, key, val))
    {
        Org_Msg(playerid, "Uso: /orgcfg <tag> <chave> <valor>");
        Org_Msg(playerid, "Chaves: discord, radio, max (vagas), farmitem (id do item), farmname (nome da coleta), farmprice (R$ por unidade), farmgoal (meta diária), bank (saldo), gps (usa a sua posição)");
        return 1;
    }
    if (!strcmp(key, "gps", true))
    {
        new Float:x, Float:y, Float:z;
        GetPlayerPos(playerid, x, y, z);
        format(val, sizeof(val), "%.2f;%.2f;%.2f", x, y, z);
    }
    Org_BodyReset(); Org_BStr("tag", tag); Org_BStr("key", key); Org_BStr("value", val);
    return Org_MPost(playerid, GW_CFG, "org_cfg", gBody), 1;
}

public Org_Tick()
{
    for (new p = 0; p < MAX_PLAYERS; p++)
    {
        if (!IsPlayerConnected(p) || IsPlayerNPC(p)) continue;
        Org_ChatTick(p);
        Org_RestoreTick(p);
        Org_MarkersTick(p);
        Grp_PresTick(p);
    }
    return 1;
}
public OnFilterScriptInit()
{
    for (new i = 0; i < MAX_PLAYERS; i++) { gSerial[i] = 1; gBusy[i] = 0; gDuty[i] = 0; gVeh[i] = 0; gLastTap[i] = 0; gMkSig[i] = -1; gChatSig[i] = -1; gAngAt[i] = 0; gRest[i] = 0; gMenuN[i] = 0; gInvOrg[i] = 0; gAction[i] = 0; gMyOrg[i] = 0; gMyRank[i] = 0; gMemAt[i] = 0; gMemBusy[i] = 0; Grp_Reset(i); }
    for (new i = 0; i < MAX_VEHICLES; i++) { gVModel[i] = 0; gVOrg[i] = 0; gVOwner[i] = 0; }
    if (fexist("chat_jogadores.txt")) { new File:f = fopen("chat_jogadores.txt", io_read), l[4]; if (f) { fread(f, l); fclose(f); gChatOn = (l[0] != '0'); } }
    Org_ProvLoad();
    SetTimer("Org_Tick", 1000, true);
    SetTimer("Org_Poll", 3000, false);
    SetTimer("Org_Poll", ORG_POLL_MS, true);
    print("[ORG] Organizacoes carregadas (cadastros no PostgreSQL)");
    return 1;
}
public OnFilterScriptExit()
{
    for (new p = 0; p < MAX_PLAYERS; p++) if (IsPlayerConnected(p) && gVeh[p]) Org_VehStore(p);
    Org_ActorsDestroy();
    return 1;
}
public OnPlayerConnect(playerid)
{
    gSerial[playerid]++;
    Grp_Reset(playerid);
    gBusy[playerid] = 0; gDuty[playerid] = 0; gVeh[playerid] = 0; gLastTap[playerid] = 0; gMkSig[playerid] = -1; gChatSig[playerid] = -1; gAngAt[playerid] = 0; gRest[playerid] = 0; gMenuN[playerid] = 0; gInvOrg[playerid] = 0; gAction[playerid] = 0; gMyOrg[playerid] = 0; gMyRank[playerid] = 0; gMemAt[playerid] = 0; gMemBusy[playerid] = 0;
    return 1;
}
public OnPlayerDisconnect(playerid, reason)
{
    Grp_Leave(playerid);
    gSerial[playerid]++;
    Org_VehStore(playerid);
    gDuty[playerid] = 0; gBusy[playerid] = 0; gInvOrg[playerid] = 0; gMyOrg[playerid] = 0; gMemBusy[playerid] = 0;
    return 1;
}
