#pragma once
#include <string>

// Colisao da camera de seguir (a pe e veiculo) com paredes/chao. Ver camguard_logic.h.
//   libGTASA.so arm64 (conferido): CCam::Process_FollowCar_SA @0x49fd38 / Process_FollowPed_SA @0x4a22b8 (retornam void);
//   CCam::Front = +0x168, Source = +0x174 (= Front + 12), Up = +0x18C (GetVectorsReadyForRW @0x4b02b4 so normaliza Front e calcula Up).
namespace CamGuard
{
    void Init();                         // instala os 2 hooks (1x, no InstallHooks)
    void SetEnabled(bool on);
    bool Enabled();
    std::string Status();                // texto para /camera guard
}
