# Relatório de auditoria – controles (fase 1)

Vídeos: o jogo de referência tem joystick analógico translúcido fixo embaixo à esquerda, ícone de corrida, botão grande de soco, botões Pulo/Agachar empilhados e ícones de arma; o jogo atual tem o anel do joystick clássico, soco, corrida e microfone.

Arquivos lidos e conclusão (caminho em `app/src/main/cpp/samp/`):
* `game/Widgets/TouchInterface.{h,cpp}`, `WidgetGta.{h,cpp}`, `WidgetVisibility.h` – HUD de toque nativo (libGTASA); joystick = widget nativo `WIDGET_PED_MOVE (0xA7)`; `m_pWidgets = *(void**)(g_libGTASA+0x850910)`.
* `game/hooks.cpp` (`AND_TouchEvent_hook`, ~1047–1150; hook instalado ~3836) – entrada de toque bruta por dedo.
* `game/pad.cpp` (1–260, 380–530, 955–1000) – hooks de CPad: caminhar L/R, U/D, sprint, pulo, agachar, golpe, arma.
* `game/movemode.{h,cpp}` – modos WALK/JOG/SPRINT sobre o stick nativo; persistência `[movimento]`.
* `gui/gui.{h,cpp}`, `gui/widget.{h,cpp}` – árvore de widgets e despacho de toque.
* `gui/samp_widgets/{jumpbutton,voicebutton,maximomenu}.*` – botão de pulo próprio, microfone, menu com aba CONTROLES.
* `settings.{h,cpp}` – `szNickName`, `bVoiceChatEnable`.

Arquivos originais alterados (backup em `backup_originais_controles/`): `gui/gui.h`, `gui/gui.cpp`, `game/hooks.cpp`, `game/pad.cpp`, `game/Widgets/WidgetGta.cpp`, `game/Widgets/WidgetVisibility.h`, `gui/samp_widgets/jumpbutton.cpp`, `gui/samp_widgets/voicebutton.h`, `gui/samp_widgets/maximomenu.cpp`.
Arquivos novos: `gui/samp_widgets/{controlmode_logic.h, controlmode.h, controlmode.cpp, moderncontrols.h, moderncontrols.cpp, controlsselector.h, controlsselector.cpp}`, `testes/controles/*`.

Nada foi apagado do sistema clássico. O que não existe/não foi comprovado (Interação por hook, ícones, PED_MOVE como anel) está listado em `LEIA-ME_CONTROLES.md`, seção 4/6.
