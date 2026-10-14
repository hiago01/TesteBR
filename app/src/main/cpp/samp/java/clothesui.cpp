// Ponte Java -> servidor da tela de roupas em HTML (ROUPAS.HTML).
// A classe Java ClothesUI converte cada toque do HTML em um comando e chama sendCommand().
#include <jni.h>
#include "main.h"

// A ordem importa: netgame.h usa tipos declarados em game.h (mesma ordem do editobject.cpp)
#include "../game/game.h"
#include "net/netgame.h"
#include "gui/gui.h"

#include <string>

extern CNetGame* pNetGame;

extern "C" {

JNIEXPORT void JNICALL
Java_com_samp_mobile_game_ui_ClothesUI_sendCommand(JNIEnv* env, jobject thiz, jbyteArray command)
{
    if (!command || !pNetGame) return;

    jsize len = env->GetArrayLength(command);
    if (len <= 1 || len > 120) return;               // limite de seguranca

    std::string cmd((size_t)len, '\0');
    env->GetByteArrayRegion(command, 0, len, reinterpret_cast<jbyte*>(&cmd[0]));

    if (cmd[0] != '/') return;                        // so comandos

    pNetGame->SendChatCommand(cmd.c_str());           // mesmo caminho de quem digita "/comando" no chat
}

}
