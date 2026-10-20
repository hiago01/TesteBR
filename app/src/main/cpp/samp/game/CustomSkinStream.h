#pragma once

// Skin customizada do modelo 7 lida PELO STREAMING DO PROPRIO JOGO (sem DeleteRwObject/SetClump manual).
// Chame CustomSkinStream::Install() no INICIO de InstallHooks() (antes de o modelo 7 ser carregado).
namespace DffPreflight { struct Info; }

namespace CustomSkinStream
{
    void Install();
    // Inventario do skin.dff (materiais na ordem do motor). nullptr ate o modelo 7 ser convertido 1x ou se o preflight reprovou.
    const DffPreflight::Info* GetInfo();
}
