# Editor de HUD (moderno) - HUD 1..4, selecao, tamanho e opacidade por controle

Copie para `app/src/main/cpp/samp/` (originais em `backup/`):
- gui/samp_widgets/controlmode_logic.h, controlmode.h, controlmode.cpp, moderncontrols.cpp, maximomenu.cpp
- testes/controles/test_controles.test, testes/controles/persist/persist_main.test

## Como usa
Menu > CONTROLES > "editar posicoes" (so no modo moderno). Abre o editor com o HUD visivel:
- Tocar num controle = seleciona (contorno AMARELO) e arrastar = move (mantem o ponto de pegada).
- Painel no topo: `<  HUD n  >` troca de HUD; Tamanho e Opacidade do controle selecionado (setas -/+ e barra);
  CANCELAR (descarta tudo), PADRAO (restaura so o HUD atual), OK (grava).
- Controles editaveis: joystick, Correr, Pulo, Agachar, Ataque, Mic, Lock.
- Em edicao nenhum toque vai ao jogo (o Ataque nao ataca ao ser arrastado). Ao sair tudo volta ao normal.

## Salvamento (SAMP/settings.ini, secao [controles_<nick>], so chaves proprias)
hud_ativo=1..4; para cada N: hudN_joy_x/_y/_tam/_opac e hudN_<botao>_x/_y/_tam/_opac
(botao = corrida, pulo, agachar, ataque, microfone, corrida_lock). Ajustes globais (sensibilidade, zona morta, mira...) seguem como antes.
Migracao: INI antigo (b_<botao>_x, joy_x, transparencia) vira o HUD 1; HUD 2..4 comecam no padrao. Outras secoes do INI nao sao tocadas.
O slider "transparencia" do menu aplica a todos os controles do HUD ativo.

## Nao incluido / limites
- "Entrar em veiculos" e demais controles nativos continuam sendo os widgets nativos (nao fazem parte do HUD moderno).
- Sem miniaturas dos 4 layouts: a pre-visualizacao e o proprio HUD ao vivo ao trocar < >.
- NAO verificado: compilacao NDK, desenho do painel e toque no aparelho, crash ao abrir/fechar o editor no aparelho.
