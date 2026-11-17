# Sleek (recriação do SleekMovement) — câmera + movimento + postura + foot IK + câmera de ombro

O `SleekMovement.asi` é x86 do PC e não roda no Android. Tudo aqui é **recriado em C++ ARM64**, usando o `SleekMovement.ini` como especificação (mesmos nomes de seção/chave).

## Módulos
| Módulo | Como funciona |
|---|---|
| Tremor de impacto / tremor ocioso / FOV corrida-sprint-pitch | (entrega anterior) hook em `CCamera::Process` + `CDraw::SetFOV` |
| **Inércia completa** | aceleração/frenagem, giro limitado, freada antes de inverter, **LateralGrip** (curvar perde velocidade), **BlockedSpeedDecay** (empurrando parede >0,25 s a velocidade interna some), **teleporte** (>3 m num quadro zera inércia, FOV, câmera, postura e IK) |
| **Câmera de ombro** (`EnableModernCamera`) | só na câmera de seguir a pé: âncora suavizada com atrasos por eixo (lateral/aproximar/afastar/vertical), offset de ombro, 3 níveis de zoom, troca de ombro suave, colisão por raycast (raio+folga, encosta na hora e solta devagar) |
| **Lean + pitch do corpo** | inclinação lateral (entrada + giro + velocidade angular) e para frente/trás (acelerar/frear), repartida entre `SPINE`/`SPINE1`; `HeadFollow` mantém a cabeça parcialmente nivelada |
| **Head tracking** | tronco/pescoço/cabeça olham para onde a câmera olha (limites 75°/30°/25°, ×0,7 andando, some ao olhar para trás) |
| **Foot IK** | sonda o chão sob cada pé (raycast), IK de 2 ossos (coxa/canela) + pelve desce, pé acompanha a inclinação do chão |
| Agachado | `KeepCrouchedWithSprint` (sprint não levanta) |

### Como a postura/IK funciona (mecanismo)
Hook em `RpHAnimHierarchyUpdateMatrices`, **só para o esqueleto do jogador local**: salva as matrizes locais dos ossos → gira os ossos (mesma técnica do `CPedIK` do jogo: editar os `RwFrame` antes do cálculo) → chama o original → (foot IK) 2ª passada → **restaura** as matrizes (idempotente, nunca acumula). Se o hook não instalar ou o esqueleto for incompatível, tudo vira no-op e o jogo segue normal (log `[SLEEK-POSE]`).

## Comandos
`/sleek` (status) · `/sleek <módulo> on|off` com módulos: `tremor ocioso corrida pitch inercia agachado lean inclina cabeca pe ombro` · `/sleek ombro` (troca o lado) · `/sleek zoom perto|medio|longe` · `/sleek fov 0.5` · `/sleek off` · `/sleek reload` · `/sleek reset`

## Config
`SAMP/sleek.ini` (criado só se não existir; **nunca sobrescrito** — chaves novas ausentes usam os padrões do código). Seção extra `[Client]`: `FovScale`, `ShakeGain`, `ShakeDurationSeconds`, `AlsoRotateCamVectors`, `LeanSign`/`PitchSign` (-1 inverte o lado), `PedGroundOffset`, `ShoulderAnchorBase`.

## O que continua NÃO portado
- **Blends de animação** (AimLag, AimStrafeBlend, AimReloadBlend, AimReleaseBlend e os blends de entrada/saída do CrouchLocomotion): precisam de hooks na camada de `CAnimBlendAssociation` (pesos de blend por animação), que este cliente não expõe hoje. Não inventei isso sem poder testar.
- Foot IK: 1 sonda por pé (sem as sondas frente/trás/lado e sem `MaxFootLateralCorrection`); sem correção de quadril lateral.
- Postura/IK só no **jogador local** (outros jogadores não).
- Botão de troca de ombro/zoom na tela: por enquanto só por comando.

## Honestidade sobre o teste
- Lógica pura (FOV, tremores, inércia, rotação de ossos, IK de 2 ossos, alvo de pé, head tracking, lean, âncora da câmera): **testada no PC** (`tools/sleek_test.cpp`).
- Integração com o jogo: só checada por sintaxe (sem NDK/aparelho). **Nada disso rodou no jogo ainda.**
- Riscos reais a validar no aparelho (o log `[SLEEK-POSE]` diz tudo):
  1. A hierarquia do ped não pode ter a flag `UPDATEMODELLINGMATRICES` (0x1000) — senão o código se desliga sozinho e avisa no log.
  2. A base dos ossos pai é lida de `pMatrixArray` (mundo); se for espaço local, usa o `ltm` do frame.
  3. Câmera/tremor/ombro editam `m_mCameraMatrix` + `UpdateRW` + `Source`; se não aparecer nada, tente `AlsoRotateCamVectors=1`.
  4. Sinais de lean/pitch podem sair invertidos → `LeanSign=-1` / `PitchSign=-1` no `[Client]`.
  5. Se o pé "flutuar" ou afundar no plano: ajuste `PedGroundOffset` (padrão 1.0).
- Qualquer módulo problemático: `/sleek <módulo> off`; `/sleek off` desliga tudo.

## Arquivos (a partir da raiz do projeto)
`app/src/main/cpp/samp/game/` → `sleek_logic.h`, `sleek_pose_logic.h`, `sleek_priv.h`, `sleek.h/.cpp`, `sleek_pose.cpp`, `hooks.cpp`, `pad.cpp`; `gui/samp_widgets/chat.cpp`; `tools/sleek_test.cpp`. (CMake usa `GLOB_RECURSE`: arquivos novos entram sozinhos.)
