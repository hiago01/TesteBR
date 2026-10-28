#pragma once

/*
 * Etapas REAIS da inicializacao reportadas a tela de loading Java (SAMP.setLoadingStage).
 * Os valores DEVEM coincidir com LoadingScreen.STAGE_* (game/ui/LoadingScreen.java).
 * O Java ja registra a etapa 0 (onCreate) e a 1 (initializeSAMP retornou); as demais partem do nativo.
 */
enum LoadingStage
{
    LOADING_STAGE_ENGINE_RUNNING    = 2, // 1o quadro do loop principal rodou (motor do GTA ja iniciado)
    LOADING_STAGE_GAME_INIT         = 3, // CGame::Initialize concluido
    LOADING_STAGE_NET_CREATED       = 4, // CNetGame criado
    LOADING_STAGE_CONNECTING        = 5, // tentativa de conexao enviada ao servidor
    LOADING_STAGE_RETRY_NO_RESPONSE = 6, // somente texto: servidor sem resposta
    LOADING_STAGE_CONNECTED         = 7, // reservado: o 100% vem de HideLoadingScreen (conexao aceita)
    LOADING_STAGE_RETRY_FULL        = 8, // somente texto: servidor cheio
    LOADING_STAGE_REFUSED           = 9  // somente texto: banido / senha invalida
};
