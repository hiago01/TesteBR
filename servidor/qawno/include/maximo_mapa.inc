// =====================================================================================
//  maximo_mapa.inc - mapa aberto do Maximo RP (servidor). So ASCII.
//  Cliente: gui/samp_widgets/worldmap.cpp + WorldMapCore.h (RPC 204).
//
//  Uso no gamemode:
//    #include <maximo_mapa.inc>            (depois de a_samp, Pawn.RakNet, izcmd)
//    Mapa_OnGameModeInit();                (no OnGameModeInit)
//    Mapa_OnPlayerDisconnect(playerid);    (no OnPlayerDisconnect)
//  Comandos ocultos enviados pelo cliente: /mapax open|close|sel <id>|pos <x> <y>|clear|arrived <id>
//  O jogador abre o mapa com /mapa (cliente), pela aba MAPA do menu, ou voce chama Mapa_Open(playerid).
//
//  Protocolo RPC 204 (servidor -> cliente):
//    op1 LOCAIS : uint8 1 | uint16 n | n x [uint16 id | uint8 icon | float x,y,z | uint8 len | nome]
//    op2 ABRIR  : uint8 2 | uint8 (1 abre, 0 fecha)
//    op3 GPS    : uint8 3 | uint8 on | float x,y,z | uint16 locId (65535 = ponto livre)
//    op4 ROTA   : uint8 4 | uint16 n | n x [float x, y]      (so desenha se n >= 2)
//  Icones: 0 pino 1 casa 2 hospital 3 banco 4 trabalho 5 aviao 6 perigo 7 roupa 8 estrela 9 posto
// =====================================================================================
#if defined _maximo_mapa_included
    #endinput
#endif
#define _maximo_mapa_included

#include <a_samp>
#include <Pawn.RakNet>

#define MAPA_RPC          204
#define MAPA_MAX_LOCAIS   100
#define MAPA_NOME_MAX     32
#define MAPA_SEM_LOCAL    65535

enum eMapaLoc { mlId, mlIcon, Float:mlX, Float:mlY, Float:mlZ, mlNome[MAPA_NOME_MAX] };
new gMapaLoc[MAPA_MAX_LOCAIS][eMapaLoc];
new gMapaQtd = 0;
new bool:gMapaAberto[MAX_PLAYERS];

// Gancho opcional (public Mapa_OnRoute(playerid, Float:dx, Float:dy)) no seu gamemode: veja Mapa_CalcRota.

stock Mapa_AddLocal(id, icon, Float:x, Float:y, Float:z, const nome[])
{
    if (gMapaQtd >= MAPA_MAX_LOCAIS) return 0;
    gMapaLoc[gMapaQtd][mlId] = id;
    gMapaLoc[gMapaQtd][mlIcon] = icon;
    gMapaLoc[gMapaQtd][mlX] = x; gMapaLoc[gMapaQtd][mlY] = y; gMapaLoc[gMapaQtd][mlZ] = z;
    format(gMapaLoc[gMapaQtd][mlNome], MAPA_NOME_MAX, "%s", nome);
    gMapaQtd++;
    return 1;
}

stock Mapa_Find(id)
{
    for (new i = 0; i < gMapaQtd; i++) if (gMapaLoc[i][mlId] == id) return i;
    return -1;
}

stock Mapa_Send_Locais(playerid)
{
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 1, PR_UINT16, gMapaQtd);
    for (new i = 0; i < gMapaQtd; i++)
    {
        new len = strlen(gMapaLoc[i][mlNome]);
        BS_WriteValue(bs, PR_UINT16, gMapaLoc[i][mlId], PR_UINT8, gMapaLoc[i][mlIcon],
            PR_FLOAT, gMapaLoc[i][mlX], PR_FLOAT, gMapaLoc[i][mlY], PR_FLOAT, gMapaLoc[i][mlZ], PR_UINT8, len);
        for (new c = 0; c < len; c++) BS_WriteValue(bs, PR_UINT8, gMapaLoc[i][mlNome][c]);
    }
    PR_SendRPC(bs, playerid, MAPA_RPC, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
}

stock Mapa_Open(playerid)
{
    Mapa_Send_Locais(playerid);
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 2, PR_UINT8, 1);
    PR_SendRPC(bs, playerid, MAPA_RPC, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
}

stock Mapa_Close(playerid)
{
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 2, PR_UINT8, 0);
    PR_SendRPC(bs, playerid, MAPA_RPC, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
}

// Define (ou remove, on=0) o GPS do jogador a partir do servidor.
stock Mapa_SetGps(playerid, on, Float:x, Float:y, Float:z, locId = MAPA_SEM_LOCAL)
{
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 3, PR_UINT8, on, PR_FLOAT, x, PR_FLOAT, y, PR_FLOAT, z, PR_UINT16, locId);
    PR_SendRPC(bs, playerid, MAPA_RPC, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
}

// Envia a linha do caminho (pontos x,y em ordem, do jogador ate o destino). Max 512 pontos.
stock Mapa_SendRota(playerid, const Float:px[], const Float:py[], n)
{
    if (n < 2 || n > 512) return 0;
    new BitStream:bs = BS_New();
    BS_WriteValue(bs, PR_UINT8, 4, PR_UINT16, n);
    for (new i = 0; i < n; i++) BS_WriteValue(bs, PR_FLOAT, px[i], PR_FLOAT, py[i]);
    PR_SendRPC(bs, playerid, MAPA_RPC, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
    BS_Delete(bs);
    return 1;
}

// Rota "pela rua": o SA-MP puro NAO tem os nos de rua. Para seguir ruas de verdade, use um plugin de
// pathfinding (ex.: Pathfinder/NodeFinder) e chame Mapa_SendRota com os pontos que ele devolver.
// Sem plugin, nao enviamos rota e o cliente desenha uma linha reta tracejada (honesto: nao e pela rua).
// Exemplo de gancho (descomente e adapte ao seu plugin):
//   public Mapa_OnRoute(playerid, Float:dx, Float:dy) { /* calcula com o plugin e chama Mapa_SendRota */ return 1; }
stock Mapa_CalcRota(playerid, Float:dx, Float:dy)
{
    #pragma unused playerid, dx, dy
    return 0;
}

stock Mapa_Chegou(playerid, locId)
{
    #pragma unused playerid, locId
    return 1;   // ponha aqui sua mensagem/recompensa
}

stock Mapa_OnGameModeInit()
{
    gMapaQtd = 0;
    // EXEMPLOS: troque pelos seus locais (id unico por local).
    Mapa_AddLocal(1, 9, 1004.0, -936.0, 42.0, "Posto de Gasolina");
    Mapa_AddLocal(2, 3, 1481.0, -1772.0, 18.0, "Banco Central");
    Mapa_AddLocal(3, 2, 1172.0, -1323.0, 15.0, "Hospital");
    Mapa_AddLocal(4, 4, 1481.0, -1750.0, 15.0, "Prefeitura");
    Mapa_AddLocal(5, 5, 1685.0, -2328.0, 13.0, "Aeroporto");
    return 1;
}

stock Mapa_OnPlayerDisconnect(playerid) { gMapaAberto[playerid] = false; }

// Le "acao n1 n2" sem depender de plugin sscanf.
stock sscanf_simple(const s[], acao[], alen, &a, &b)
{
    new i = 0, n = strlen(s);
    while (i < n && s[i] == ' ') i++;
    new k = 0;
    while (i < n && s[i] != ' ' && k < alen - 1) acao[k++] = s[i++];
    acao[k] = 0;
    if (k == 0) return 0;
    a = 0; b = 0;
    while (i < n && s[i] == ' ') i++;
    new sg = 1;
    if (i < n && s[i] == '-') { sg = -1; i++; }
    while (i < n && s[i] >= '0' && s[i] <= '9') a = a * 10 + (s[i++] - '0');
    a *= sg;
    while (i < n && s[i] == ' ') i++;
    sg = 1;
    if (i < n && s[i] == '-') { sg = -1; i++; }
    while (i < n && s[i] >= '0' && s[i] <= '9') b = b * 10 + (s[i++] - '0');
    b *= sg;
    return 1;
}

// Comando oculto do cliente. No seu OnPlayerCommandText chame: if (Mapa_Cmd(playerid, cmdtext)) return 1;
// (izcmd: CMD:mapax(playerid, params[]) { Mapa_Params(playerid, params); return 1; })
stock Mapa_Params(playerid, const params[])
{
    new acao[12], a, b;
    if (sscanf_simple(params, acao, sizeof acao, a, b) == 0) return 0;
    if (!strcmp(acao, "open", true))        { gMapaAberto[playerid] = true; Mapa_Send_Locais(playerid); }
    else if (!strcmp(acao, "close", true))  { gMapaAberto[playerid] = false; }
    else if (!strcmp(acao, "sel", true))
    {
        new i = Mapa_Find(a);
        if (i < 0) return 1;
        Mapa_CalcRota(playerid, gMapaLoc[i][mlX], gMapaLoc[i][mlY]);
        CallLocalFunction("Mapa_OnRoute", "iff", playerid, gMapaLoc[i][mlX], gMapaLoc[i][mlY]);
    }
    else if (!strcmp(acao, "pos", true))    { CallLocalFunction("Mapa_OnRoute", "iff", playerid, float(a), float(b)); }
    else if (!strcmp(acao, "arrived", true)){ Mapa_Chegou(playerid, a); }
    return 1;
}

