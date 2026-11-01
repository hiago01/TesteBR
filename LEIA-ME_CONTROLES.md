# Controles CLÁSSICO x MODERNO – Máximo RP (SA-MP 2.10 ARM64)

## 1. Auditoria (caminhos reais, sem presumir)
| Item | Onde está | Conclusão |
|---|---|---|
| Joystick "clássico" | **nativo da libGTASA** (`CTouchInterface::m_pWidgets`, `game/Widgets/TouchInterface.{h,cpp}`; ID `WIDGET_PED_MOVE 0xA7`) | Não existe código-fonte dele no cliente: é desenhado/lido pelo jogo. Por isso o clássico **não foi tocado**: só é *desligado* quando o moderno está ativo. |
| Entrada de toque bruta | `game/hooks.cpp` → `AND_TouchEvent_hook` (type 2 push / 3 move / 1 pop, `num` = índice do dedo) | Ponto de captura para o moderno e para o seletor (mesmo padrão de MaximoMenu/JumpButton/WeaponBar). |
| Entrada no motor | `game/pad.cpp`: `CPad::GetPedWalkLeftRight/UpDown`, `GetSprint`, `JumpJustDown`, `GetJump`, `DuckJustDown`, `MeleeAttackJustDown`, `GetWeapon` | Já hookados no ramo "jogador local"; o moderno só devolve seu valor ali quando `ModernInput::Active()`. SA-MP sincroniza `LocalPlayerKeys.*` pelo fluxo normal. |
| Botões nativos a pé | `WidgetVisibility.h` + `WidgetGta.cpp` (`CTouchInterface__Update_hook`, `ProcessFixedWidget`) | Mecanismo já usado para esconder botões; reaproveitado (`WidgetVis::ModernReplaces`). |
| Persistência | `SAMP/settings.ini` (`CSimpleIniA`, como MaximoMenu `[maximo]` e `[movimento]`) | Mesmo mecanismo; seção nova por jogador `[controles_<nick>]`. |
| Menu de configurações | `gui/samp_widgets/maximomenu.cpp`, aba **CONTROLES** (já existia) | Opção "Sistema de controles" e ajustes do moderno adicionados nessa aba. |
| Microfone | `gui/samp_widgets/voicebutton.h` (`VoiceButton`) | O botão moderno aciona o **mesmo** `touchPushEvent/touchPopEvent`. |

## 2. Arquitetura
```
AND_TouchEvent_hook (hooks.cpp)
  ├─ MaximoMenu / Inventário / Mapa / Provador / MaximoOS   (inalterados)
  ├─ ControlsSelector::HandleRawTouch      NOVO – tela de escolha (consome tudo enquanto aberta)
  ├─ weaponbar, passengerbutton, minimapa  (inalterados)
  ├─ ModernControls::onRawTouch            NOVO – só atua no modo MODERNO, a pé, sem UI por cima
  ├─ jumpbutton (clássico)                  (inalterado; no moderno não desenha/processa)
  └─ fluxo original → AND_TouchEvent (joystick/botões nativos = CLÁSSICO)

pad.cpp (hooks CPad, ramo local)  ──  if (ModernInput::Active()) devolve valor moderno; senão, código original.
```
* **ControlMode** (`controlmode.{h,cpp}`): modo, config do moderno, persistência por jogador. Padrão **CLÁSSICO**.
* **ModernControls** (`moderncontrols.{h,cpp}`): desenho + multitouch + modo de edição de posições + `ModernInput` (saída para o pad).
* **ControlsSelector** (`controlsselector.{h,cpp}`): tela "ESCOLHA SEUS CONTROLES".
* **controlmode_logic.h**: toda a lógica pura (analógico, layout, multitouch por ID, seletor, persistência de nomes) – é a parte testada no PC.

Regra "um sistema por vez": modo clássico → `ModernInput::Active()` é sempre falso, `ModernControls::onRawTouch` retorna `false` e nada novo desenha. Modo moderno → `JumpButton` clássico e `VoiceButton` original não desenham/processam, e os widgets nativos `ATTACK, BUTTON_SPRINT, BUTTON_CROUCH, SPRINT, PED_MOVE` são desligados **somente a pé**. Nenhum arquivo, função, callback ou config do clássico foi apagado.

## 3. Comportamento
* **Joystick**: base translúcida fixa embaixo à esquerda, centro móvel, 360°, diagonais, intensidade proporcional (zona morta configurável → 0 … 1), volta ao centro ao soltar; dedo próprio (ID).
* **Botões**: Corrida (segurar, com joystick em uso), Pulo, Agachar, Ataque (segurado = fogo/golpe contínuo; toque = golpe), Microfone (segurar). Cada um: área própria, estado pressionado/solto, feedback visual, multitouch; o dedo que cai num botão é dele até soltar, mesmo se arrastar para fora.
* **Seleção**: aparece sozinha **uma vez** (jogador novo, já em jogo, 2,5 s sem login/spawn/diálogo/teclado por cima). Depois: menu → CONTROLES → "Sistema de controles". Troca na hora, sem reiniciar. VOLTAR não altera nada (na 1ª vez só registra "já escolhi", segue CLÁSSICO).
* **Ajustes do moderno** (menu → CONTROLES): tamanho do joystick, tamanho dos botões, transparência, sensibilidade, zona morta, distância máxima, posição H/V do joystick, **Editar posições** (arrastar cada controle; OK/PADRÃO) e **restaurar ajustes**. Não mexem em nada do clássico.
* **Persistência** (`SAMP/settings.ini`, `[controles_<nick>]`): `modo=classico|moderno`, `escolhido`, `joy_x/y`, `joy_escala`, `botoes_escala`, `transparencia`, `sensibilidade`, `zona_morta`, `distancia_max`, `b_<botao>_x/_y`. Chave ausente = CLÁSSICO.

## 4. Limites / decisões (nada inventado)
1. **Interação** (entrar no veículo): não existe hook comprovado para forçá-la; **não criei botão falso**. No moderno o widget nativo "hud_car" (entrar no carro) e o botão P2 continuam como antes.
2. **Veículo/jetpack**: o moderno é só "a pé". Em veículo o HUD nativo de direção volta (e o moderno solta todos os dedos).
3. **Nadar/mergulhar** (`BUTTON_SWIM/DIVE`) continuam nativos no moderno (o moderno não os reimplementa).
4. **Convenção do eixo** (frente = `ud` negativo, direita = `lr` positivo) é a do GTA SA; foi implementada assim, **não verificada no aparelho**. Se o personagem andar invertido, inverta o sinal em `controlmode_logic.h::ToPad` (um ponto só).
5. No moderno o tiro por toque na tela (gesto nativo "shoot look") não existe: só o botão Ataque dispara.
6. Ícones: Pulo/Corrida/Agachar/Ataque usam texto; não usei nomes de textura que eu não pude comprovar.

## 5. Integração
1. Copie o conteúdo do zip por cima do projeto (mesmos caminhos `app/src/main/...`).
2. **Refaça o CMake** (Sync/Reload): os `.cpp` novos entram pelo `file(GLOB_RECURSE ...)` existente.
3. Compile no Android Studio/NDK.

## 6. Testes
Executados **no PC** (g++): `testes/controles/run.sh` → 113/113; `testes/controles/persist_test.sh` → 12/12 (usa o SimpleIni real do projeto com stubs; no PC foi preciso um shim de `std::__binary_function`, só do teste).
Cobrem: modo padrão clássico; clamp de config; analógico (zona morta, proporcional, saturação, diagonal, 360°, sensibilidade); layout sem sobreposição (paisagem e retrato); multitouch (joystick+ataque+pulo simultâneos, dedo arrastado para fora segue dono, soltura individual, CANCEL solta tudo, POINTER_DOWN duplicado); botão desabilitado (mic sem voz); edição de posições; seletor (hit-test, responsivo, VOLTAR não aplica, CONFIRMAR aplica); auto-exibição só 1x e nunca sobre login/diálogo; persistência por jogador (troca de nick não vaza, reabrir mantém, voltar ao clássico mantém os ajustes, restaurar só o moderno, seções `[maximo]` preservadas).

### NÃO VERIFICADO (impossível sem NDK/aparelho)
* Compilação NDK/Gradle de todos os `.cpp`/`.h` alterados (revisei tipos/nomes à mão; conflitos de nome com `Rect/Circle/Vec` já foram evitados com prefixo `Ctl`).
* Comportamento no aparelho: sentido do movimento, que `WIDGET_PED_MOVE (0xA7)` seja o anel do joystick clássico (se o anel continuar aparecendo no moderno, falta mapear o ID), câmera/mira/combate/moto/passageiro/diálogos/teclado, microfone moderno, desenho/legibilidade dos botões, tela de seleção em tela real.
* Troca clássico↔moderno em jogo (a lógica está testada; o efeito visual/nativo não).
* Que desligar `PED_MOVE` não interfira no gesto de câmera nativo.

---
## 7. Parte 2/3 – Mira (Ataque) e mira ARRASTÁVEL estilo Free Fire

**Mira ao atirar** (`game/pad.cpp`): no moderno, segurar Ataque com arma de fogo (ids 22–38) faz `CPad::GetEnterTargeting` devolver verdadeiro → mira nativa; mira centralizada em `CCamera::m_f3rdPersonCHairMultX/Y = 0.5` (símbolos resolvidos por `dlsym`; se faltarem, só a centralização é pulada e o log avisa).

**Ícones nativos antigos** (`WidgetGta.cpp`, `WidgetVisibility.h`): `SetEnabled(false)` não apagava o desenho; agora `m_Color.a = 0` nos widgets substituídos (+ ENTER/EXIT_TARGETING). O log grava `widgets nativos ligados a pe no moderno: ...`.

**Mira arrastável** (opção independente `mira_arrastavel`, padrão ligada; menu CONTROLES → "Moderno: mira arrastável / sensibilidade / suavização"):
* Descoberta na source: não há função/offset público de "olhar" para chamar; a câmera nativa lê o toque bruto na região de câmera (`WIDGET_LOOK 0xAF`, ver `camerazoom.h`). Por isso **não inventei nenhum offset**: o dedo do Ataque gera um dedo **sintético** (`AND_TouchEvent`, mesmo índice do dedo real, que o moderno consome e o jogo nunca vê) numa âncora livre da tela e o move pelo **deslocamento relativo** × sensibilidade (com suavização opcional). Câmera, mira, limites verticais, recuo, animações, cadência e munição continuam 100% do motor.
* Ao chegar perto da borda o dedo sintético é solto e reposto na âncora (só o delta importa; nada "pula").
* Soltar o Ataque encerra o disparo e o dedo sintético. Outro dedo no joystick esquerdo continua independente (cada dedo tem seu ID).
* Armas automáticas/semiautomáticas/sem munição: decididas pelo motor via `GetWeapon` (segurado), como antes.
* Clássico: nada disso existe (`ModernInput::Active()` falso).
* Desligando a opção, o Ataque volta a só atirar/mirar (botão antigo preservado).

**Testado no PC:** `run.sh` 125/125 (inclui arrasto relativo, sensibilidade, reancoragem sem sair da tela, suavização, soltar); `persist_test.sh` 13/13.
**NÃO verificado:** compilação NDK; que a região de câmera aceite o dedo sintético na âncora (0,58·L, 0,38·A) e mova também a MIRA em modo de mira; sentido/velocidade; `dlsym` dos símbolos de mira; efeito de a pop sintética só sair no próximo toque quando o moderno é desligado no meio do arrasto.
