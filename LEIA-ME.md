# Fade dos botoes na freada (HUD moderno)

Copie a pasta `app/` por cima do projeto (mesmos caminhos). Originais em `backup_originais/`.

## O que faz
- Detecta, na thread do jogo, a animacao de freada do ped local: associacao ANIM_ID_RUN_STOP (6) ou RUN_STOPR (7)
  na clump (`RpAnimBlendClumpGetAssociation`, offset ja existente no projeto), rodando e com blend > 0.05.
- Os botoes e o joystick do HUD moderno fazem fade (~120 ms) ao entrar na animacao e voltam (~150 ms) ao terminar.
- Fade e SO DO DESENHO: os toques/funcoes dos botoes continuam ativos.
- Trava de seguranca: nunca esconde por mais de 1,5 s seguidos. Nunca esconde no editor de HUD.
- Opcao no menu: "Moderno: esconder botoes na freada" (padrao: ligado). settings.ini: `fade_freada=1/0`.

## Arquivos
controlmode_logic.h (campo fadeBrake), controlmode.cpp (ini), maximomenu.cpp (linha do menu),
moderncontrols.cpp (deteccao + fade), game/Widgets/WidgetGta.cpp (chama ModernControlsBrakeTick).

## NAO verificado
Compilacao NDK/Gradle; comportamento no aparelho; se a freada do seu video realmente usa RUN_STOP/RUN_STOPR
(e a hipotese; se nao funcionar, o log precisa mostrar o AnimationId ativo na freada).
Testes de PC existentes: 324/324 e 29/29 OK (nao cobrem o fade).
