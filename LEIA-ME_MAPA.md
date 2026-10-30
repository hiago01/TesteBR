# Mapa aberto (Maximo RP)

Painel escuro com o mapa a esquerda e a lista de locais a direita (estilo do print). Imagem = radar nativo do jogo.

## Como abrir
- `/mapa` no chat (cliente) | aba **MAPA** do menu | servidor: `Mapa_Open(playerid)` (RPC 204 op2).
- Voltar do Android fecha so o mapa. `/mapa grade` mostra o indice de cada tile (para conferir orientacao); `/mapa info` mostra contadores.

## Toques
- Arrastar = mover | pinca = zoom | botoes + - O (centralizar em voce)
- Tocar num marcador ou na lista = GPS no local (+ centraliza, + avisa o servidor `/mapax sel <id>`)
- Tocar no mapa vazio = GPS livre (`/mapax pos x y`) | LIMPAR GPS (`/mapax clear`) | chegou a 25 m: `/mapax arrived <id>`
- GPS tambem cria blip no radar do jogo (sprite 41).

## Linha do caminho
- Se o servidor mandar a rota (op4, `Mapa_SendRota`) a linha azul segue esses pontos ("pela rua").
- Sem rota: linha reta tracejada ate o destino. O SA-MP puro nao tem os nos de rua; para seguir ruas use um plugin de pathfinding e chame `Mapa_SendRota` (gancho `Mapa_OnRoute`).

## Arquivos
Cliente: `gui/samp_widgets/{WorldMapCore.h,WorldMapUI.h,worldmap.h,worldmap.cpp}`; alterados: `SAMPRPC.cpp/.h`, `scriptrpc.cpp`, `gui.cpp/.h`, `hooks.cpp`, `main.cpp`, `chat.cpp`, `maximomenu.cpp`.
Servidor: `servidor/qawno/include/maximo_mapa.inc` (troque os locais de exemplo em `Mapa_OnGameModeInit`; precisa `Mapa_Params` num `CMD:mapax`).
Opcional: `SAMP/radar/mapa.png` (imagem quadrada do mundo todo, norte em cima) substitui os tiles.

## O que foi testado / nao
- Testado no PC: logica (49 verificacoes) e interface real com ImGui sem tela (67 verificacoes x 5 resolucoes): `bash testes/run_host_worldmap.sh`.
- NAO testado: build Android (Gradle/NDK), no aparelho, nome/banco das texturas `radarNN` (o cliente tenta bancos gta3, gta_int, samp, menu, txd, radar e registra `[MAPA]` no log), sprite 41 do blip, compilacao do .inc (sem pawncc aqui).
- Se aparecer "Imagem do mapa indisponivel", me mande as linhas `[MAPA]` do samp_log.txt.
