# Correção — câmera atravessando parede e chão (a pé e em veículo)

Descompacte na raiz do projeto (sobrescreve): 3 arquivos novos + 2 alterados em app/src/main/cpp/samp/game/
  novos:      camguard.h, camguard.cpp, camguard_logic.h
  alterados:  hooks.cpp (+ #include "camguard.h" e + CamGuard::Init(); logo após CCameraZoom::Init();)
              camerazoom.cpp (+ comando /camera guard on|off|status)
Base: Source-21hrs ORIGINAL (sem cabelo 5+; o hooks.cpp daqui = original + as 2 linhas da câmera, nada mais). Se o seu hooks.cpp for outro, aplique só essas 2 linhas nele em vez de sobrescrever.
v3: a v2 enviada em 21/11 estava ERRADA (trazia chamadas HairCloth_*); apague-a e use esta.

## O que faz
Hook em CCam::Process_FollowCar_SA e Process_FollowPed_SA (câmera de seguir). Depois do cálculo do jogo:
 1) raio do alvo até a câmera (prédios/objetos/dummies; sem veículos/peds; ignora vidro/"camera ignora"); se bater, recua a câmera até o impacto (folga 0,40 m, mínimo 0,60 m do alvo);
 2) se a câmera ficou abaixo do chão, sobe para chão + 0,35 m.
Só mexe na câmera ativa, só desliza a câmera ao longo da linha alvo->câmera (Front/Up continuam válidos) e ignora cinemática (>40 m) e valores inválidos.
O raio começa a 1,8 m do alvo no veículo (a pé 0,35 m) para não bater em giroflex/tuning presos ao carro.
Log: linhas "[CAMGUARD] ..." (as 16 primeiras). /camera guard mostra os contadores (quadros / parede / chão).

## Verificado
- libGTASA.so arm64: Process_FollowCar_SA @0x49fd38, Process_FollowPed_SA @0x4a22b8 (retorno ignorado pelos chamadores), CCam::Front +0x168 / Source +0x174 (static_assert no código).
- Lógica pura testada no host (ASan/UBSan): parede, chão (caso do vídeo), parede colada, valores inválidos, 20.000 câmeras aleatórias.
## NÃO verificado
- Não compilei com o NDK nem testei no aparelho. Se algo piscar/tremer, use /camera guard off e me mande o log.
