#pragma once

// Skins customizadas (modelo 7 = FemininaCorp, modelo 9 = FemininaNormal) lidas PELO STREAMING DO PROPRIO JOGO
// (sem DeleteRwObject/SetClump manual). Chame CustomSkinStream::Install() no INICIO de InstallHooks().
namespace DffPreflight { struct Info; }

namespace CustomSkinStream
{
    void Install();
    // texdb/DFF: relatorio no log (chamar em CGame::Initialize, depois dos IDEs/IMGs)
    void AuditDff(const char* quando);
    // Inventario do DFF do modelo (materiais na ordem do motor). nullptr ate o modelo ser convertido 1x, se nao e um modelo
    // substituido, ou se o preflight reprovou.
    const DffPreflight::Info* GetInfo(int model);
}
