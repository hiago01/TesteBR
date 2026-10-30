#define MIXED_SPELLINGS

#include <open.mp>
#include <sscanf2> 
#include <maximo_conta.inc> // Registro/Login com DOF2 (precisa vir ANTES do izcmd)
#include <izcmd.inc>
#include <maximo_idcard.inc> 
#include <maximo_inv.inc>   // Inventario Maximo RP (DOF2.1)
#include <maximo_look.inc>  // Visual por slots (/skin7 slot) sincronizado para todos os jogadores (RPC 232 + 231)
// Amount of kills required per level-up
#define KILLS_PER_LEVEL 3

// Definition of body part ID that corresponds to the head of a character
#define BODYPART_HEAD 9
// Define os dialogid dentro de uma enum
enum
{
    DIALOG_LOGIN,
    DIALOG_WELCOME,
    DIALOG_WEAPONS
}

// Alternativamente, por meio de macros:
#define DIALOG_LOGIN 1
#define DIALOG_WELCOME 2
#define DIALOG_WEAPONS 3
// These are the random locations the user can spawn at
enum e_STATUS
{
    e_LEVEL,
    e_KILLS_AT_LEVEL,
	e_Admin,
    bool:e_DEAD,
    bool:e_HOLDING_PRIMARY // Whether the player holds their primary weapon
};

new
    gPlayerStatus[MAX_PLAYERS][e_STATUS],
    Text:gRespawn;

forward Restart();

main()
{
    print("\n----------------------------------");
    print("  ");
    print(" ");
    print(" ");
    print(" ");
    print("----------------------------------\n");
}

public OnGameModeInit()
{
	CreateObject(1345, 193.83459, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 196.05460, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 198.27460, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 200.49460, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 202.60361, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 204.82359, -151.15840, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 207.17149, -151.16910, 1.30710,   0.00000, 0.00000, -360.00000);
	CreateObject(1345, 209.04160, -151.82440, 1.19610,   0.00000, 0.00000, -90.00000);
	CreateObject(1345, 209.04160, -154.15540, 1.19610,   0.00000, 0.00000, -90.00000);
	CreateObject(1345, 209.04179, -156.35539, 1.19610,   0.00000, 0.00000, -90.00000);
	CreateObject(1345, 209.08160, -158.59579, 1.19610,   0.00000, 0.00000, -90.00000);
	CreateObject(1345, 209.04160, -160.81540, 1.19610,   0.00000, 0.00000, -90.00000);
	CreateObject(1345, 209.04160, -163.14639, 1.19610,   0.00000, 0.00000, -90.00000);

	DisableNameTagLOS();
	ShowNameTags(false);
	EnableStuntBonusForAll(false);
    SetGameModeText("Maximo RP");
    AddPlayerClass(0, 2085.00000, 7607.00000, -1809.00000, 355.3697, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    ShowPlayerMarkers(PLAYER_MARKERS_MODE_OFF);
    gRespawn = TextDrawCreate(320.000000, 155.000000, "~y~Press '~r~~k~~VEHICLE_ENTER_EXIT~~y~' to spawn!");
    TextDrawAlignment(gRespawn, TEXT_DRAW_ALIGN_CENTER);
    TextDrawBackgroundColor(gRespawn, 255);
    TextDrawFont(gRespawn, TEXT_DRAW_FONT_2);
    TextDrawLetterSize(gRespawn, 0.549999, 1.500000);
    TextDrawColor(gRespawn, -65281);
    TextDrawSetOutline(gRespawn, 0);
    TextDrawSetProportional(gRespawn, true);
    TextDrawSetShadow(gRespawn, 3);
    //gGameInProgress = true;
    Inv_Init();
    Look_OnGameModeInit();
    Conta_OnGameModeInit();
    return 1;
}

public OnGameModeExit()
{
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    // Sem login nao nasce: mostra Registro/Login (o spawn acontece em Conta_FinishLogin).
    if (!Conta_IsLogged(playerid))
    {
        Conta_Prompt(playerid);
        return 1;
    }
    //SetSpawnInfo(playerid, 0, 7, 2085.00000, 7607.00000, -1809.00000, 269.14);
    //SpawnPlayer(playerid);
    return 1;
}

public OnPlayerRequestSpawn(playerid)
{
    return Conta_IsLogged(playerid) ? 1 : 0;
}

public OnPlayerText(playerid, text[])
{
    if (!Conta_IsLogged(playerid))
    {
        SendClientMessage(playerid, 0xFF5C6CFF, "Voce precisa fazer login antes de falar no chat.");
        return 0;
    }
    return 1;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if (Conta_OnDialogResponse(playerid, dialogid, response, listitem, inputtext)) return 1;
    return 0;
}

// Chamados pelo maximo_conta.inc: copiam os dados da conta de/para o gPlayerStatus
forward OnContaLogin(playerid);
public OnContaLogin(playerid)
{
    gPlayerStatus[playerid][e_Admin] = gConta[playerid][cAdmin];
    gPlayerStatus[playerid][e_LEVEL] = gConta[playerid][cLevel];
    gPlayerStatus[playerid][e_KILLS_AT_LEVEL] = gConta[playerid][cKills];
    return 1;
}

forward OnContaSave(playerid);
public OnContaSave(playerid)
{
    gConta[playerid][cAdmin] = gPlayerStatus[playerid][e_Admin];
    gConta[playerid][cLevel] = gPlayerStatus[playerid][e_LEVEL];
    gConta[playerid][cKills] = gPlayerStatus[playerid][e_KILLS_AT_LEVEL];
    return 1;
}

public OnPlayerConnect(playerid)
{
	SetPlayerCameraPos(playerid, 2055.0581, -1857.6940, 61.4906);
	SetPlayerCameraLookAt(playerid, 2055.5916, -1856.8501, 60.7806);
	//RemoveBuildingForPlayer(playerid, 3377, -207.6563, -246.7344, 1.5781, 0.25);/
    gPlayerStatus[playerid][e_LEVEL] = 0;
    gPlayerStatus[playerid][e_KILLS_AT_LEVEL] = 0;
    gPlayerStatus[playerid][e_Admin] = 0;
    gPlayerStatus[playerid][e_DEAD] = true;
    gPlayerStatus[playerid][e_HOLDING_PRIMARY] = true;
    TextDrawHideForPlayer(playerid, gRespawn);
    SetPlayerColor(playerid, 0xFF0000FF);
	SetPlayerSkin(playerid, 7);
    Inv_OnPlayerConnect(playerid);
    Look_OnPlayerConnect(playerid);
    Conta_OnPlayerConnect(playerid);   // registro/login (dialogo aparece no OnPlayerRequestClass)
    return 1;
}

#include <a_samp>

CMD:ir(playerid, params[])
{
    // 1. Verifica??o de Admin (Substitua 'PlayerInfo[playerid][pAdmin]' pela sua vari?vel de admin)
    //if(PlayerInfo[playerid][pAdmin] < 1) 
      //  return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Voc? n?o tem permiss?o para usar este comando.");

    new targetid;
    // 2. Verifica se o ID foi digitado corretamente usando sscanf
    if(sscanf(params, "u", targetid)) 
        return SendClientMessage(playerid, -1, "{A9A9A9}[Uso]:{FFFFFF} /ir [ID do Jogador / Parte do Nome]");

    // 3. Verifica se o jogador alvo est? online
    if(targetid == INVALID_PLAYER_ID) 
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Jogador n?o encontrado/online.");

    // 4. Cria as vari?veis para armazenar as coordenadas do alvo
    new Float:x, Float:y, Float:z, interior, virtualworld;
    
    // Pegando a posi??o, interior e mundo virtual do alvo
    GetPlayerPos(targetid, x, y, z);
    interior = GetPlayerInterior(targetid);
    virtualworld = GetPlayerVirtualWorld(targetid);

    // 5. Teleporta o admin para a posi??o do alvo (com um leve ajuste no Z para n?o nascer dentro dele)
    SetPlayerInterior(playerid, interior);
    SetPlayerVirtualWorld(playerid, virtualworld);
    
    if(IsPlayerInAnyVehicle(playerid)) // Se o admin estiver de carro, teleporta o carro junto
    {
        new vehicleid = GetPlayerVehicleID(playerid);
        SetVehiclePos(vehicleid, x + 2.0, y, z + 1.0);
        LinkVehicleToInterior(vehicleid, interior);
        SetVehicleVirtualWorld(vehicleid, virtualworld);
    }
    else // Se estiver a p?
    {
        SetPlayerPos(playerid, x + 1.0, y, z + 1.0);
    }

    // 6. Avisos no chat
    new string[128], adminname[MAX_PLAYER_NAME], targetname[MAX_PLAYER_NAME];
    GetPlayerName(playerid, adminname, sizeof(adminname));
    GetPlayerName(targetid, targetname, sizeof(targetname));

    format(string, sizeof(string), "{00FF00}[Admin]:{FFFFFF} Voc? se teleportou at? o jogador %s (ID: %d).", targetname, targetid);
    SendClientMessage(playerid, -1, string);
    
    format(string, sizeof(string), "{00FF00}[Admin]:{FFFFFF} O Administrador %s se teleportou at? voc?.", adminname);
    SendClientMessage(targetid, -1, string);

    return 1;
}

// Comando /trazer - Teleporta o jogador alvo at? o administrador
CMD:trazer(playerid, params[])
{
    // 1. Verifica??o de Admin (Substitua pela sua vari?vel de admin)
   // if(PlayerInfo[playerid][pAdmin] < 1) 
     //   return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Voc? n?o tem permiss?o para usar este comando.");

    new targetid;
    // 2. Verifica o par?metro
    if(sscanf(params, "u", targetid)) 
        return SendClientMessage(playerid, -1, "{A9A9A9}[Uso]:{FFFFFF} /trazer [ID do Jogador / Parte do Nome]");

    // 3. Verifica se est? online
    if(targetid == INVALID_PLAYER_ID) 
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Jogador n?o encontrado/online.");

    // 4. Pega a posi??o atual do admin
    new Float:x, Float:y, Float:z, interior, virtualworld;
    GetPlayerPos(playerid, x, y, z);
    interior = GetPlayerInterior(playerid);
    virtualworld = GetPlayerVirtualWorld(playerid);

    // 5. Teleporta o jogador alvo at? o admin
    SetPlayerInterior(targetid, interior);
    SetPlayerVirtualWorld(targetid, virtualworld);

    if(IsPlayerInAnyVehicle(targetid)) // Se o alvo estiver em um ve?culo, traz o ve?culo junto
    {
        new vehicleid = GetPlayerVehicleID(targetid);
        SetVehiclePos(vehicleid, x + 2.0, y, z + 1.0);
        LinkVehicleToInterior(vehicleid, interior);
        SetVehicleVirtualWorld(vehicleid, virtualworld);
    }
    else // Se estiver a p?
    {
        SetPlayerPos(targetid, x + 1.0, y, z + 1.0);
    }

    // 6. Avisos no chat
    new string[128], adminname[MAX_PLAYER_NAME], targetname[MAX_PLAYER_NAME];
    GetPlayerName(playerid, adminname, sizeof(adminname));
    GetPlayerName(targetid, targetname, sizeof(targetname));

    format(string, sizeof(string), "{00FF00}[Admin]:{FFFFFF} Voc? trouxe o jogador %s (ID: %d) at? voc?.", targetname, targetid);
    SendClientMessage(playerid, -1, string);
    
    format(string, sizeof(string), "{00FF00}[Admin]:{FFFFFF} Voc? foi trazido pelo Administrador %s.", adminname);
    SendClientMessage(targetid, -1, string);

    return 1;
}


CMD:gmx(playerid, params[])
{
    //SendClientMessageToAll(-1, "{FFFF00}[Servidor]: O servidor esta sendo reiniciado pelo administrador...");
	//SendRconCommand("gmx");
    return 1;
}
CMD:clearb(playerid, params[])
{
    ClearPlayerWorldBounds(playerid);
	return 1;
}
CMD:ircarro(playerid, params[])
{
    new vehicleid;
    
    // Verifica se o jogador digitou um ID de ve?culo v?lido
    if (sscanf(params, "i", vehicleid))
    {
        return SendClientMessage(playerid, -1, "{FF0000}Uso: {FFFFFF}/ircarro [ID do Ve?culo]");
    }
    
    // Verifica se o ve?culo realmente existe no servidor
    if (!IsValidVehicle(vehicleid))
    {
        return SendClientMessage(playerid, -1, "{FF0000}Erro: {FFFFFF}Este ve?culo n?o existe!");
    }
    
    new Float:x, Float:y, Float:z;
    new interior, virtualworld;
    
    // Pega a posi??o, interior e mundo virtual do ve?culo
    GetVehiclePos(vehicleid, x, y, z);
    interior = GetVehicleInterior(vehicleid);
    virtualworld = GetVehicleVirtualWorld(vehicleid);
    
    // Seta os dados do jogador para os mesmos do ve?culo
    SetPlayerInterior(playerid, interior);
    SetPlayerVirtualWorld(playerid, virtualworld);
    
    // Verifica se o jogador est? em um ve?culo, se estiver, teletransporta o ve?culo dele junto
    if (IsPlayerInAnyVehicle(playerid))
    {
        new currentveh = GetPlayerVehicleID(playerid);
        SetVehiclePos(currentveh, x + 2.0, y, z + 1.0); // Coloca levemente ao lado
      //  SetVehicleInterior(currentveh, interior);
        SetVehicleVirtualWorld(currentveh, virtualworld);
    }
    else 
    {
        // Se estiver a p?, teletransporta apenas o jogador (levemente acima do carro para n?o bugar)
        SetPlayerPos(playerid, x, y, z + 2.0);
    }
    
    new message[64];
    format(message, sizeof(message), "{00FF00}Sucesso: {FFFFFF}Voc? foi teletransportado para o ve?culo ID %d!", vehicleid);
    SendClientMessage(playerid, -1, message);
    
    return 1;
}

CMD:dararma(playerid, params[])
{
    new targetid, weaponid, ammo;

    // Processa os par?metros digitados pelo usu?rio
    if (sscanf(params, "ddd", targetid, weaponid, ammo))
    {
        return SendClientMessage(playerid, -1, "Uso correto: /dararma [ID do Jogador] [ID da Arma] [Muni/Balas]");
    }

    // Verifica se o jogador alvo est? online
    if (!IsPlayerConnected(targetid))
    {
        return SendClientMessage(playerid, -1, "Erro: O jogador digitado nao esta online.");
    }

    // Valida se o ID da arma ? v?lido (IDs do GTA SA v?o de 1 a 46)
    if (weaponid < 1 || weaponid > 46)
    {
        return SendClientMessage(playerid, -1, "Erro: ID de arma invalido (Use de 1 a 46).");
    }

    // Executa a fun??o nativa do open.mp / SA-MP
    GivePlayerWeapon(targetid, WEAPON:weaponid, ammo);

    // Mensagens de confirma??o
    new msg[128];
    format(msg, sizeof(msg), "Voce deu a arma ID %d (%d balas) para o jogador ID %d.", weaponid, ammo, targetid);
    SendClientMessage(playerid, -1, msg);

    format(msg, sizeof(msg), "O administrador lhe deu a arma ID %d com %d balas.", weaponid, ammo);
    SendClientMessage(targetid, -1, msg);

    return 1;
}


// Comando: /ao [tipo] [documento]
CMD:cartao(playerid, params[])
{
    new tipo[24], documento[24];
    if(sscanf(params, "s[24]s[24]", tipo, documento))
    {
        SendClientMessage(playerid, 0xFFFFFFFF, "Use: /cartao [tipo] [documento] (Ex: /cartao teste cnh)");
        return 1;
    }

    if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "0", true) == 0)
    {
	MaxCard_Set(playerid, MAXCARD_CIVIL,"Cidad\xE3;o", "RG, MX-####");
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "1", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_POLICIAL,"Policial", "RE, MX-####");	
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "2", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_DELEGADO,"Delegado", "RE, MX-####");	
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "3", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_MEDICO,"M\xE9;dico", "MX-####");	
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "4", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_ADMINISTRADOR,"ADM", "Posso ajudar, player?");	
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "5", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_MILITAR,"Soldado", "RA MILITAR , MX-####");	
    }
	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "6", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_CNH, "Habilita\xE7;\xE3;o AB", "");	
    }

	if(strcmp(tipo, "teste", true) == 0 && strcmp(documento, "7", true) == 0)
    {	
	MaxCard_Set(playerid, MAXCARD_FUNCIONAL, "Policia", "RE, MX-####");	
    }

    return 1;
}

CMD:limparchat(playerid, params[])
{
    for(new i = 0; i < 50; i++)
    {
        SendClientMessage(playerid, -1, " ");
    }
    return 1;
}

CMD:motor(playerid, params[])
{
    if(!IsPlayerInAnyVehicle(playerid)) 
    return SendClientMessage(playerid, -1, "{FF0000}Erro:{FFFFFF} Voce precisa estar dentro de um ve?culo.");

    // Verifica se o jogador ? o motorista (assento 0)
    if(GetPlayerVehicleSeat(playerid) != 0) 
    return SendClientMessage(playerid, -1, "{FF0000}Erro:{FFFFFF} Voce precisa ser o motorista para ligar o motor.");

    new vehicleid = GetPlayerVehicleID(playerid);
    new engine, lights, alarm, doors, bonnet, boot, objective;
    
    // Pega os parametros atuais do veiculo
    GetVehicleParamsEx(vehicleid, engine, lights, alarm, doors, bonnet, boot, objective);
    
    // Alterna o estado do motor
    if(engine == VEHICLE_PARAMS_ON)
    {
        SetVehicleParamsEx(vehicleid, VEHICLE_PARAMS_OFF, lights, alarm, doors, bonnet, boot, objective);
        SendClientMessage(playerid, -1, "{FFFF00}Info:{FFFFFF} Voce desligou o motor do veiculo.");
    }
    else
    {
        SetVehicleParamsEx(vehicleid, VEHICLE_PARAMS_ON, lights, alarm, doors, bonnet, boot, objective);
        SendClientMessage(playerid, -1, "{00FF00}Info:{FFFFFF} Voce ligou o motor do veiculo.");
    }
    
    return 1;
}
CMD:tempo(playerid, params[])
{
    new string[128];
    new idTempo;

    if (sscanf(params, "i", idTempo))
    {
        SendClientMessage(playerid, -1, "ID: Use /tempo [ID do Clima]");
        return 1;
    }

    if (idTempo < 0 || idTempo > 45)
    {
        SendClientMessage(playerid, 0xFF0000FF, "Erro: ID de clima inv?lido! Escolha entre 0 e 45.");
        return 1;
    }
	SetPlayerWeather(playerid, idTempo);
    format(string, sizeof(string), "Seu clima foi alterado para o ID: %d", idTempo);
    SendClientMessage(playerid, 0x00FF00FF, string);
	return 1;
} 

CMD:meuhp(playerid, params[])
{
    new string[128];
    new idHP;

    if (sscanf(params, "i", idHP))
    {
        SendClientMessage(playerid, -1, "ID: Use /meuhp [valor]");
        return 1;
    }
    
	format(string, sizeof(string), "Seu hp foi alterado para o ID: %d", idHP);
    SendClientMessage(playerid, 0x00FF00FF, string);
	SetPlayerHealth(playerid, idHP);
    return 1;
} 

CMD:setveiculo(playerid, params[])
{
    new string[128];
    new id;

    if (sscanf(params, "i", id))
    {
        SendClientMessage(playerid, -1, "ID: Use /setveiculo [ID]");
        return 1;
    }
    new Float:x, Float:y, Float:z;
	GetPlayerPos(playerid, x, y, z);

	format(string, sizeof(string), "O veiculo foi criado com o ID: %d", id);
    SendClientMessage(playerid, 0x00FF00FF, string);
	new vehicleid = CreateVehicle(411, x, y, z, 0.0000, 6, 0, 100);
    SetVehicleSpawnInfo(vehicleid, id, x, y, z, 0.0000, 1, 7);
	SetVehicleToRespawn(vehicleid);
	return 1;
} 

CMD:minhaskin(playerid, params[])
{
    new string[128];
    new idHP;

    if (sscanf(params, "i", idHP))
    {
        SendClientMessage(playerid, -1, "ID: Use /minhaskin [ID]");
        return 1;
    }
    
	format(string, sizeof(string), "Skin acabou e foi alterado para o ID: %d", idHP);
    SendClientMessage(playerid, 0x00FF00FF, string);
	SetPlayerSkin(playerid, idHP);
    Look_OnSkinChanged(playerid);
    return 1;
} 

CMD:spawncarro(playerid, params[])
{
    new string[128];
    new id;

    if (sscanf(params, "i", id))
    {
        SendClientMessage(playerid, -1, "ID: Use /spawncarro [ID]");
        return 1;
    }
    
	format(string, sizeof(string), "O veiculo com o ID(%d), foi respawnado.", id);
    SendClientMessage(playerid, 0x00FF00FF, string);
	SetVehicleToRespawn(id);
    return 1;
} 

 CMD:setarveiculoid(playerid, params[])
{
    new Float:x, Float:y, Float:z;
	GetPlayerPos(playerid, x, y, z);
	new id_alvo, quantidade;
    
    if(sscanf(params, "dd", id_alvo, quantidade)) 
    {
        return SendClientMessage(playerid, -1, "Uso correto: /setveiculoid (idcarro) (novo modelo)");
    }
    SendClientMessage(playerid, -1, "Veiculo alterado o modelo com sucesso!");
 	SetVehicleSpawnInfo(id_alvo, quantidade, x, y, z, 0, 124, 124, 0);
	SetVehicleToRespawn(id_alvo);
    return 1;
}

CMD:v(playerid, params[])
{
   /* new string[128];
    new id;

    if (sscanf(params, "i", id))
    {
        SendClientMessage(playerid, -1, "ID: Use /v [valor]");
        return 1;
    }
    
	new Float:x, Float:y, Float:z;
	GetPlayerPos(playerid, x, y, z);

	new vehicleid = CreateVehicle(461, x, y, z, 0.0000, 0, 0, 9999);
    SetVehicleSpawnInfo(vehicleid, id, x, y, z, 0.0000, 124, 124, -2, -2);
	SetVehicleToRespawn(vehicleid);
	format(string, sizeof(string), "O veiculo foi criado com o ID: %d(%d)", id, vehicleid);
    SendClientMessage(playerid, 0x00FF00FF, string);*/
	return 1;
} 

CMD:sethora(playerid, params[])
{
    /*new string[128];
    new idHora;

    if (sscanf(params, "i", idHora))
    {
        SendClientMessage(playerid, -1, "ID: Use /sethora [valor]");
        return 1;
    }
    
	format(string, sizeof(string), "O horario foi alterado para o ID: %d", idHora);
    SendClientMessage(playerid, 0x00FF00FF, string);
	SetWorldTime(idHora); */
	return 1;
} 

CMD:teleporte(playerid, params[])
{
    new string[128];
    new idTeleporte;

    if (sscanf(params, "i", idTeleporte))
    {
        SendClientMessage(playerid, -1, "ID: Use /teleporte [valor]");
        return 1;
    }
	if(idTeleporte == 1)
	{
		SetPlayerPos(playerid, 93.4063, -173.8281, 1.9688);
   	}    
	if(idTeleporte == 2)
	{
		SetPlayerPos(playerid, 1958.33, 1343.12, 15.36);
    }
	format(string, sizeof(string), "Teleportado para o ID: %d", idTeleporte);
    SendClientMessage(playerid, 0x00FF00FF, string);
	SetWorldTime(0); 
	return 1;
} 

public OnPlayerSpawn(playerid)
{
    // Set position
    SetPlayerPos(playerid, 2087.8,-1804.2,13.5);
    //SetPlayerFacingAngle(playerid, gRandPos[rand][3]);
   // SetPlayerWorldBounds(playerid, -1274.2817, -1358.5095, 2575.6509, 2472.3486);
    ///SetCameraBehindPlayer(playerid);

    // General stuff
    gPlayerStatus[playerid][e_DEAD] = false;
    gPlayerStatus[playerid][e_HOLDING_PRIMARY] = true;
    Conta_OnPlayerSpawn(playerid);  // 1o spawn apos o login: volta para a posicao/vida salvas
    Look_OnPlayerSpawn(playerid);   // visual por slots: manda ao dono e a quem o ve
    return 1;
}

public OnPlayerDeath(playerid, killerid, WEAPON:reason)
{
	if(killerid == INVALID_PLAYER_ID)
	{
		SetPlayerCameraPos(playerid, 80.8927, -67.0447, 62.9513);
		SetPlayerCameraLookAt(playerid, 81.8129, -67.4325, 62.4013);
		GameTextForPlayer(playerid, "~r~TESTE", 1650, 6);
	}
    /*SendDeathMessage(killerid, playerid, reason);
    TextDrawShowForPlayer(playerid, gRespawn);
    TogglePlayerSpectating(playerid, true);
    gPlayerStatus[playerid][e_DEAD] = true;
    if(killerid == INVALID_PLAYER_ID)
    {
		SetPlayerCameraPos(playerid, 80.8927, -67.0447, 62.9513);
		SetPlayerCameraLookAt(playerid, 81.8129, -67.4325, 62.4013);
    }
    else
    {
        //PlayerSpectatePlayer(playerid, killerid);

        // Knife deaths are humiliating, and demote the player.
       if(reason == WEAPON_KNIFE)
        {
            GameTextForPlayer(killerid, "~r~Humiliation!~n~~y~You demoted someone!", 1650, 6);
            GameTextForPlayer(playerid, "~r~Humiliated~n~~y~You got demoted!", 1650, 6);
            if(gPlayerStatus[playerid][e_LEVEL] != 0) gPlayerStatus[playerid][e_LEVEL]--;
            gPlayerStatus[playerid][e_KILLS_AT_LEVEL] = 0;

        }
        gPlayerStatus[killerid][e_KILLS_AT_LEVEL]++;
        if(gPlayerStatus[killerid][e_KILLS_AT_LEVEL] == KILLS_PER_LEVEL)
        {
            gPlayerStatus[killerid][e_KILLS_AT_LEVEL] = 0;
            gPlayerStatus[killerid][e_LEVEL]++;

            //Player has won the game.
            if(gPlayerStatus[killerid][e_LEVEL] == sizeof gWeaponList)
            {
                EndRound();
            }
            else
            {
                GameTextForPlayer(killerid, "~r~Player Killed!~n~~y~Advanced to the next tier!", 1650, 6);
                SetPlayerScore(killerid, gPlayerStatus[killerid][e_LEVEL] + 1);
                ResetPlayerWeapons(killerid);
                GivePlayerWeapon(killerid, WEAPON_KNIFE, 1);
                GivePlayerWeapon(killerid, gWeaponList[gPlayerStatus[killerid][e_LEVEL]], 65535);
            }
        }
        else
        {
            //ShowKillsTillNextLevel(killerid);
        }
    }*/
	return 1;
}

public OnPlayerTakeDamage(playerid, issuerid, Float:amount, WEAPON:weaponid, bodypart)
{
    new Float:health;
    GetPlayerHealth(playerid, health);

    // Take twice the damage as you normally would, to increase the pace in the game.
    new Float:multiplier = 2.0;

    // Make headshots do 50% more damage over the regular multiplier
    if(bodypart == BODYPART_HEAD)
    {
        multiplier = 3.0;
    }
    SetPlayerHealth(playerid, health - amount * multiplier);
}

public OnPlayerUpdate(playerid)
{
    /*if(gPlayerStatus[playerid][e_DEAD] == false)
    {
        if(!GetPlayerWeapon(playerid))
        {
            if(gPlayerStatus[playerid][e_HOLDING_PRIMARY] == true)
            {
    //            SetPlayerArmedWeapon(playerid, WEAPON_KNIFE);
                gPlayerStatus[playerid][e_HOLDING_PRIMARY] = false;
            }
            else
            {
      //          SetPlayerArmedWeapon(playerid, gWeaponList[gPlayerStatus[playerid][e_LEVEL]]);
                gPlayerStatus[playerid][e_HOLDING_PRIMARY] = true;
            }
        }
        else
        {
            gPlayerStatus[playerid][e_HOLDING_PRIMARY] = !(GetPlayerWeapon(playerid) == WEAPON_KNIFE);
        }

    }
    else
    {
        SetPlayerCameraPos(playerid, -1251.1089, 2551.7546, 104.6863);
        SetPlayerCameraLookAt(playerid, -1302.1554, 2533.4226, 93.8427);
        new KEY:keys[3];
        GetPlayerKeys(playerid, keys[0], keys[1], keys[2]);
        if(keys[0] & KEY_SECONDARY_ATTACK && gGameInProgress == true)
        {
            TogglePlayerSpectating(playerid, false);
            SpawnPlayer(playerid);
            TextDrawHideForPlayer(playerid, gRespawn);
            gPlayerStatus[playerid][e_DEAD] = false;
        }
    }*/
    return 1;
}

public Restart()
{
    SendRconCommand("gmx");
}

public OnPlayerDisconnect(playerid, reason)
{
    Conta_OnPlayerDisconnect(playerid);   // salva a conta (DOF2)
    Inv_OnPlayerDisconnect(playerid);
    Look_OnPlayerDisconnect(playerid);
    return 1;
}

CMD:inv(playerid, params[])
{
    Inv_Open(playerid);
    return 1;
}

CMD:inventario(playerid, params[])
{
    Inv_Open(playerid);
    return 1;
}

CMD:negociar(playerid, params[])
{
    new t = -1;
    sscanf(params, "I(-1)", t);
    Inv_Open(playerid);
    Inv_Report(playerid, Inv_TradeStart(playerid, t));
    return 1;
}

// usado pela interface do cliente (acoes: use, drop, move, pickup, trade, tadd...). Tudo validado em Inv_Command.
CMD:invx(playerid, params[])
{
    return Inv_Command(playerid, params);
}

// ---- Visual por slots (maximo_look.inc) ----
CMD:lookx(playerid, params[])      // oculto: o cliente manda /lookx set|base|clear|sync (o servidor valida e retransmite)
{
    return Look_Command(playerid, params);
}

CMD:atualizarvisual(playerid, params[])   // recarrega o visual do arquivo e reenvia: para voce e para os jogadores proximos
{
    return Look_RefreshCommand(playerid);
}

CMD:attvisual(playerid, params[])          // atalho de /atualizarvisual
{
    return Look_RefreshCommand(playerid);
}

CMD:setlook(playerid, params[])    // admin (RCON): /setlook [id] [slot|limpar|info] [estado] [variante]
{
    return Look_AdminCommand(playerid, params);
}

// troca de visual que vale para TODOS: o cliente atual trata /rosto /cabelo /pele /bota /calca /camisa /intima /corp /visual so localmente
CMD:look(playerid, params[])       // /look rosto|pele|cabelo|bota|calca|camisa|intima|corp|limpar|info
{
    return Look_TextCommand(playerid, params);
}

// quem acabou de aparecer para forplayerid: manda o visual dele (o cliente solta o visual no stream-out)
public OnPlayerStreamIn(playerid, forplayerid)
{
    Look_OnPlayerStreamIn(playerid, forplayerid);
    return 1;
}

// ---- administracao (RCON admin: /rcon login) ----
CMD:criaritem(playerid, params[])
{
    //if (!IsPlayerAdmin(playerid)) return SendClientMessage(playerid, 0xFF5555FF, "Apenas administradores RCON.");
    SendClientMessage(playerid, 0xFFFF00FF, "Icones: (8 caixa,9 kit,10 dinheiro,11 comida,12 agua,13 ferramenta,14 chave,15 celular)");
    new peso, icone, nome[INV_NAME_LEN];
    if (sscanf(params, "iis[24]", peso, icone, nome))
        return SendClientMessage(playerid, 0xFFFF00FF, "Uso: /criaritem [peso em gramas] [icone 0-15] [nome]");
    new id = Inv_CreateDef(nome, peso, icone);
    if (!id) return SendClientMessage(playerid, 0xFF5555FF, "Nao foi possivel criar (nome repetido/invalido, peso/icone fora do limite, limite de itens ou falha ao salvar).");
    new msg[96];
    format(msg, sizeof msg, "Item criado: id %d (%s). Use /daritem [jogador] %d [qtd].", id, nome, id);
    return SendClientMessage(playerid, 0x55FF55FF, msg);
}

CMD:daritem(playerid, params[])
{
    if (!IsPlayerAdmin(playerid)) return SendClientMessage(playerid, 0xFF5555FF, "Apenas administradores RCON.");
    new target, itemid, qtd = 1;
    if (sscanf(params, "uiI(1)", target, itemid, qtd)) return SendClientMessage(playerid, 0xFFFF00FF, "Uso: /daritem [jogador] [itemid] [qtd=1]  (veja /itens)");
    if (target == INVALID_PLAYER_ID || !IsPlayerConnected(target)) return SendClientMessage(playerid, 0xFF5555FF, "Jogador nao conectado.");
    new r = Inv_Give(target, itemid, qtd);
    if (r != INV_OK) { new e[64]; Inv_ErrText(r, e); return SendClientMessage(playerid, 0xFF5555FF, e); }
    SendClientMessage(playerid, 0x55FF55FF, "Item entregue (salvo no DOF2).");
    return SendClientMessage(target, 0x55FF55FF, "Voce recebeu um item. Abra com /inv.");
}

CMD:tiraritem(playerid, params[])
{
    if (!IsPlayerAdmin(playerid)) return SendClientMessage(playerid, 0xFF5555FF, "Apenas administradores RCON.");
    new target, itemid, qtd;
    if (sscanf(params, "uii", target, itemid, qtd)) return SendClientMessage(playerid, 0xFFFF00FF, "Uso: /tiraritem [jogador] [itemid] [qtd]");
    if (target == INVALID_PLAYER_ID || !IsPlayerConnected(target)) return SendClientMessage(playerid, 0xFF5555FF, "Jogador nao conectado.");
    new r = Inv_Take(target, itemid, qtd);
    if (r != INV_OK) { new e[64]; Inv_ErrText(r, e); return SendClientMessage(playerid, 0xFF5555FF, e); }
    return SendClientMessage(playerid, 0x55FF55FF, "Item removido (salvo no DOF2).");
}

// /cartaoitem [0-7] [jogador]  -> entrega o ITEM do cartao no inventario (o /cartao teste original continua mostrando o 3D)
CMD:cartaoitem(playerid, params[])
{
    if (!IsPlayerAdmin(playerid)) return SendClientMessage(playerid, 0xFF5555FF, "Apenas administradores RCON.");
    new tipo, target = playerid;
    if (sscanf(params, "iU(-1)", tipo, target)) return SendClientMessage(playerid, 0xFFFF00FF, "Uso: /cartaoitem [0-7] [jogador]  0 Civil,1 Policial,2 Delegado,3 Medico,4 Admin,5 Militar,6 CNH,7 Funcional");
    if (target == -1) target = playerid;
    if (tipo < 0 || tipo > 7) return SendClientMessage(playerid, 0xFF5555FF, "Indice invalido: use 0 a 7.");
    if (target == INVALID_PLAYER_ID || !IsPlayerConnected(target)) return SendClientMessage(playerid, 0xFF5555FF, "Jogador nao conectado.");
    new r = Inv_Give(target, INV_ID_CARD_BASE + tipo, 1);
    if (r != INV_OK) { new e[64]; Inv_ErrText(r, e); return SendClientMessage(playerid, 0xFF5555FF, e); }
    return SendClientMessage(playerid, 0x55FF55FF, "Cartao entregue no inventario (salvo no DOF2).");
}

CMD:itens(playerid, params[])
{
    new msg[96];
    SendClientMessage(playerid, 0xB48CFFFF, "--- Itens cadastrados (id: nome | peso g | icone) ---");
    for (new i = 0; i < gIDefCount; i++)
    {
        format(msg, sizeof msg, "%d: %s | %d g | %d", gIDef[i][idId], gIDef[i][idName], gIDef[i][idWeight], gIDef[i][idIcon]);
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
    }
    return 1;
}

// /mudarsenha [senha atual] [nova senha]
CMD:mudarsenha(playerid, params[])
{
    new atual[CONTA_PASS_MAX + 1], nova[CONTA_PASS_MAX + 1];
    if (sscanf(params, "s[33]s[33]", atual, nova))
        return SendClientMessage(playerid, -1, "{A9A9A9}[Uso]:{FFFFFF} /mudarsenha [senha atual] [nova senha]");
    if (strlen(nova) < CONTA_PASS_MIN || strlen(nova) > CONTA_PASS_MAX)
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} A nova senha deve ter de 6 a 32 caracteres.");
    if (!Conta_ChangePassword(playerid, atual, nova))
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Senha atual incorreta ou conta indisponivel.");
    return SendClientMessage(playerid, -1, "{00FF00}[Conta]:{FFFFFF} Senha alterada com sucesso.");
}

// /setadmin [id] [nivel 0-5]  (somente RCON)
CMD:setadmin(playerid, params[])
{
    if (!IsPlayerAdmin(playerid))
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Somente o administrador RCON pode usar este comando.");
    new targetid, nivel;
    if (sscanf(params, "ui", targetid, nivel))
        return SendClientMessage(playerid, -1, "{A9A9A9}[Uso]:{FFFFFF} /setadmin [ID / parte do nome] [nivel 0-5]");
    if (targetid == INVALID_PLAYER_ID || !Conta_IsLogged(targetid))
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Jogador nao encontrado ou ainda nao logou.");
    if (nivel < 0 || nivel > 5)
        return SendClientMessage(playerid, -1, "{FF0000}[Erro]:{FFFFFF} Nivel de 0 a 5.");
    gPlayerStatus[targetid][e_Admin] = nivel;
    Conta_Save(targetid);
    new msg[96];
    format(msg, sizeof msg, "{00FF00}[Admin]:{FFFFFF} Nivel de admin do ID %d definido para %d.", targetid, nivel);
    SendClientMessage(playerid, -1, msg);
    return 1;
}

