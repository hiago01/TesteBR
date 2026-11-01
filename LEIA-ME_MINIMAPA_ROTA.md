# Rota (GPS) no MINIMAPA

## O que faz
Com o GPS ligado e o mapa grande fechado, a rota (pelas ruas, A* dos NODES; ou a do servidor; ou reta se não houver grafo) aparece no
minimapa nativo, começando no jogador, com contorno + linha da mesma cor da categoria `rota` do `SAMP/radar/linhas.ini`.
Abrir o mapa grande apaga a linha do minimapa; fechar de novo ela volta. A parte já percorrida não é desenhada.

## Como funciona (e por que usa as funções do próprio jogo)
1. `Ui::Draw` (WorldMapUI.h) publica a rota em coordenadas do MUNDO (`MiniRoute::PublishWorld`).
2. `game/hooks.cpp`: logo depois de `CHud::DrawRadar()` (ainda com o retângulo/transformação do radar já deslocados/escalados por `BeginRadarMove`)
   converte cada ponto com `CRadar::TransformRealWorldPointToRadarSpace` e `CRadar::TransformRadarPointToScreenSpace` (símbolos exportados do
   libGTASA, via `dlsym`; conferido no desassembly que `ScreenSpace` usa o mesmo retângulo que o projeto altera). Assim a rotação do radar
   com a câmera, o alcance (zoom pela velocidade) e a posição/escala do projeto já vêm certos, sem reimplementar nada.
3. A linha é recortada no formato REAL do minimapa: o projeto deforma a máscara do radar para um "quadrado arredondado" (8 vértices em
   `CSprite2d__SetMaskVertices_hook`); esses 8 vértices são gravados e usados como polígono de recorte (com 3% de margem). Se a máscara
   não for plausível (outro espaço de coordenadas), recorta num círculo (0,965 do raio do radar).
4. O ImGui desenha as polilinhas por cima do radar (`Ui::DrawMiniRoute`). A linha só aparece se o radar foi desenhado há < 300 ms
   (radar oculto/HUD desligado = sem linha). O começo da linha pula ~16% do raio para não cobrir a seta do jogador.

## Arquivos
Novo: `gui/samp_widgets/MiniRoute.h` (lógica pura). Alterados: `gui/samp_widgets/WorldMapUI.h`, `gui/samp_widgets/worldmap.cpp`,
`gui/samp_widgets/chat.cpp`, `game/hooks.cpp`. Testes: `testes/test_miniroute.cpp`, `testes/host_test_minimapa.cpp`, `testes/rodar_testes_minimapa.sh`.
Instalação: extrair na raiz do projeto (caminhos começam em `app/src/main/...`), substituindo; build limpo.

## Comando (chat do jogo)
`/mapa minimapa on|off` liga/desliga a linha no minimapa (padrão: ligada).

## No aparelho: o que olhar (primeiro teste)
1. Ligue um GPS (toque num destino no `/mapa`), feche o mapa. A linha deve sair do centro do minimapa, seguir as ruas e girar junto com o radar.
2. `samp_log.txt`: linha `[MINIMAPA] rota: TransformRealWorld...=0x... TransformRadarPoint...=0x...` (os dois endereços devem ser diferentes de zero;
   se algum for 0 a linha não aparece) e `[MINIMAPA] centro=(x,y) raio=R px, recorte=mascara|circulo`.
3. Se a linha aparecer deslocada ou em escala errada, me mande essas linhas do log e um print: a hipótese que precisa ser confirmada é que as
   unidades do retângulo do radar do jogo são os mesmos pixels do ImGui (o toque no minimapa já parte dessa premissa).

## Verificado x NÃO verificado
Executado no PC (`sh testes/rodar_testes_minimapa.sh`): lógica (41 verificações, 0 falhas, com AddressSanitizer/UBSan: recorte exato no círculo e no
polígono convexo, casco convexo com vértices fora de ordem, validação da máscara, 9 mil polilinhas aleatórias sempre dentro do recorte) e a
interface com o ImGui real + radar FALSO (15 verificações, 0 falhas: sem GPS não publica; rota publicada e desenhada; recomeça no jogador ao andar;
radar parado = sem desenho; mapa aberto limpa; sem grafo = reta; `/mapa minimapa off`; GPS limpo apaga). Conferido a olho nos PPM.
O trecho do hook foi compilado com stubs (sintaxe/tipos).
NÃO verificado: compilação NDK/Gradle; o jogo real (as duas funções do radar e o formato da máscara só foram lidos no desassembly/código);
posição/escala/rotação da linha no aparelho; desempenho; o que acontece com o radar em interiores ou com o HUD desligado.
## Limitações
- A linha é do ImGui, desenhada por cima do radar (fica sobre os blips do jogo; só o começo é recuado para não cobrir a seta).
- Até 1500 pontos da rota por quadro.
