// CarryTuner.h - painel visual (ImGui) para ajustar o /carregar DENTRO do jogo. Abrir/fechar: /carregarhud (chat do cliente).
// Mexe em carrycore::Tune() em tempo real (posicao do carregado, tilt, roll, dobra da coluna). "SALVAR" grava
// SAMP/carry_tuning.ini (carregado sozinho nas proximas vezes) e escreve os valores no samp.log ([CARRY-TUNE]).
#pragma once
namespace CarryTuner {
void Toggle();
bool IsOpen();
void LoadSaved();      // le SAMP/carry_tuning.ini uma unica vez (chamado por PlayerCarry::Process)
void Draw();           // dentro do frame do ImGui (ImGuiWrapper::render), antes do EndFrame
}
