# Editor de HUD (moderno) - HUD 1..4, selecao, tamanho e opacidade por controle

Copie para `app/src/main/cpp/samp/` (originais em `backup/`; este zip substitui o anterior do editor):
- gui/samp_widgets/controlmode_logic.h, controlmode.h, controlmode.cpp, moderncontrols.cpp, maximomenu.cpp
- game/Widgets/WidgetGta.cpp (tick de som do editor; ja inclui o tick da mira)
- testes/controles/test_controles.test, testes/controles/persist/persist_main.test

## Como usa (painel no estilo da referencia)
Menu > CONTROLES > "editar posicoes" (so no modo moderno). Tocar num controle = seleciona (contorno AMARELO); arrastar = move.
Painel (topo, semi-transparente):
- Linha 1: [sair = descarta tudo] [<  HUD n  >] [nuvem = salva e continua editando]
- Linha 2: [compartilhar = copia o HUD atual para os 4 HUDs] [restaurar = so o HUD atual] [salvar = salva e sai; amarelo quando ha alteracao, cinza/desativado sem alteracao]
- "Ajuste Inteligente": ao arrastar, alinha (x/y) com outros controles proximos.
- Opacidade e Tamanho do controle selecionado (barras com %) e setas ◄ ▲ ▼ ► que movem o selecionado em passos finos.
- Aba v/^ embaixo: recolhe o painel (fica so a linha 1) e expande.
- O painel pode ser ARRASTADO pelo fundo (nao pelos botoes); fica sempre inteiro na tela.
- SOM em cada toque de botao do editor (sons de interface 1052/1053 do jogo, via CGame::PlaySound, tocados na thread do jogo).
- Em edicao nenhum toque vai ao jogo. Ao sair tudo volta ao normal.

## Salvamento (SAMP/settings.ini, secao [controles_<nick>], so chaves proprias)
hud_ativo=1..4; para cada N: hudN_joy_x/_y/_tam/_opac e hudN_<botao>_x/_y/_tam/_opac
(botao = corrida, pulo, agachar, ataque, microfone, corrida_lock). Ajustes globais (sensibilidade, zona morta, mira...) seguem como antes.
Migracao: INI antigo (b_<botao>_x, joy_x, transparencia) vira o HUD 1; HUD 2..4 comecam no padrao. Outras secoes do INI nao sao tocadas.
O slider "transparencia" do menu aplica a todos os controles do HUD ativo.

## Nao incluido / limites
- "Entrar em veiculos" e demais controles nativos continuam sendo os widgets nativos (nao fazem parte do HUD moderno).
- Sem miniaturas dos 4 layouts: a pre-visualizacao e o proprio HUD ao vivo ao trocar < >.
- Posicao do painel e estado de recolhido/Ajuste Inteligente valem so na sessao de edicao (nao sao salvos).
- Som: so nos botoes do editor (nao nos botoes do HUD em jogo). Ids de som 1052/1053 sao os ja usados pelo cliente na selecao de classe; nao foram ouvidos no aparelho.
- NAO verificado: compilacao NDK, desenho do painel e toque no aparelho, crash ao abrir/fechar o editor no aparelho.
