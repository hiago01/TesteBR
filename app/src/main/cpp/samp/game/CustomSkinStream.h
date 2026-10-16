#pragma once

// Skin customizada do modelo 7 lida PELO STREAMING DO PROPRIO JOGO (sem DeleteRwObject/SetClump manual).
// Chame CustomSkinStream::Install() no INICIO de InstallHooks() (antes de o modelo 7 ser carregado).
namespace CustomSkinStream
{
    void Install();
}
