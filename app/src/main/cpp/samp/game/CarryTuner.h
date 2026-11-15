// CarryTuner.h - painel visual para ajustar o /carregar DENTRO do jogo. Abrir/fechar: /carregarhud (chat do cliente).
// Mexe em carrycore::Tune() em tempo real (posicao do carregado, tilt, roll, dobra da coluna). "SALVAR" grava
// SAMP/carry_tuning.ini (carregado sozinho nas proximas vezes) e escreve os valores no samp.log ([CARRY-TUNE]).
// Toque proprio (mesmo esquema do /celofs): HandleRawTouch so captura o dedo que COMECA dentro do painel; o resto segue p/ o jogo.
#pragma once
namespace CarryTuner {
void Toggle();
bool IsOpen();
void LoadSaved();                                          // le SAMP/carry_tuning.ini uma unica vez (PlayerCarry::Process)
void ApplyPending();                                       // reaplica os bracos do carregador se mudaram (PlayerCarry::Process)
void Draw();                                               // dentro do frame do ImGui (ImGuiWrapper::render), antes do EndFrame
bool HandleRawTouch(int type, int num, int x, int y);      // hooks.cpp (type: 2 push, 3 move, 1 pop). true = consumido
}
