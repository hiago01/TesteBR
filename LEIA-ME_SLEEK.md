# Sleek (recriação do SleekMovement) — câmera + movimento

O `SleekMovement.asi` é um binário **x86 do PC (GTA SA 1.0 US)**: não roda no Android. Esta entrega **recria em C++ ARM64** os módulos que você escolheu (câmera + movimento), usando o `SleekMovement.ini` como especificação (mesmos nomes de seção/chave).

## O que foi recriado
| Módulo | Como funciona no cliente |
|---|---|
| Tremor de impacto reescrito (`EnableCameraShakeRewrite`) | Zera `m_fCamShakeForce` só durante o `CCamera::Process` original (o motor não aplica o tremor dele), restaura depois, e aplica tremor suave rotacional + deslocamento mínimo. |
| Tremor ocioso (`EnableCameraIdleShake`) | Pitch/yaw/roll orgânicos (senos incomensuráveis), x1,5 no sprint. Desliga ao mirar / de carro. |
| FOV de corrida/sprint (`EnableCameraRunFov`) | +15° correndo, +30° no sprint, com subida/descida suave (via `CDraw__SetFOV_hook`). Andar devagar não abre o FOV. |
| FOV por pitch (`EnableCameraPitchFov`) | Olhar para cima abre até +15°. Total limitado a `MaxTotalChange`. |
| Inércia (`EnableMomentum`) | Aceleração/frenagem progressivas, giro limitado por velocidade, freada antes de inverter a direção. Atua no stick local (`pad.cpp`), depois do modo WALK/JOG/SPRINT. |
| Agachado (`EnableCrouchLocomotion`) | `KeepCrouchedWithSprint`: sprint apertado agachado não levanta o personagem. |

## NÃO portado (precisa de hooks de animação/esqueleto)
Câmera de ombro (ModernCamera), lean/pitch do corpo, head tracking, foot IK, blends de mira, e os *blends de animação* do CrouchLocomotion. Também não portados dentro da inércia: `LateralGrip`, `BlockedSpeedDecay`, `TeleportResetDistance` (o estado zera após pausa >0,25 s).

## Arquivos (a partir da raiz do projeto)
- `app/src/main/cpp/samp/game/sleek_logic.h` — lógica pura (testada no PC)
- `app/src/main/cpp/samp/game/sleek.h/.cpp` — integração, INI, comando
- `app/src/main/cpp/samp/game/hooks.cpp` — `CCamera__Process_hook` chama `SleekMove::CameraProcess`; `CDraw__SetFOV_hook` soma `FovDelta()`
- `app/src/main/cpp/samp/game/pad.cpp` — `SleekMove::ApplyStick` (4 pontos) e `FilterSprint`
- `app/src/main/cpp/samp/gui/samp_widgets/chat.cpp` — comando `/sleek`
- `tools/sleek_test.cpp` — `g++ -std=c++17 tools/sleek_test.cpp && ./a.out` → "TODOS OS TESTES OK"

(O CMake já usa `GLOB_RECURSE`: os arquivos novos entram sozinhos.)

## Config e teste
`SAMP/sleek.ini` é criado com os padrões na 1ª execução e **nunca sobrescrito**. Seção extra `[Client]`: `FovScale` (1.0 = INI; 0.5 = metade), `ShakeGain`, `ShakeDurationSeconds`, `AlsoRotateCamVectors`.

Comandos: `/sleek` (status) · `/sleek tremor|ocioso|corrida|pitch|inercia|agachado on|off` · `/sleek fov 0.5` · `/sleek off` · `/sleek reload` · `/sleek reset`.

## Honestidade sobre o teste
- Lógica pura: testada no PC (FOV, rotação da câmera, tremores, inércia).
- Código de integração: checado só por sintaxe (sem NDK/aparelho aqui). **Não rodou no jogo.**
- Incertezas a validar no aparelho: (1) o tremor ocioso edita a matriz da câmera **depois** do `CCamera::Process` + `UpdateRW` + `RwFrameUpdateObjects`; se não aparecer nada, teste `AlsoRotateCamVectors=1` e me avise; (2) a unidade/decaimento de `m_fCamShakeForce` foi estimada (5 s linear; ajuste com `ShakeGain`/`ShakeDurationSeconds`); (3) o sinal do eixo do stick (frente = −128) é simétrico na inércia, mas o giro pode precisar de ajuste fino.
- Se o FOV ficar exagerado: `/sleek fov 0.5`. Qualquer módulo problemático: `/sleek <modulo> off`.
