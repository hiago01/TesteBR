# Radar/mapa estilo Google Maps (troca dos tiles nativos)

## O que o pacote tem e o que o jogo usa
- `Radar/radarNN.txd` x144: 512x512 DXT1 (D3D9/PC). NN = linha*12+coluna, linha 0 = norte (igual ao jogo). O nome da textura dentro
  (`radar1` no radar00.txd) nao importa: vale o nome do arquivo.
- `Radar Map Color/CLEO`: script CLEO de PC que muda 3 cores (mapa 255,255,255 | oceano fora do mundo | "fog"). No Android nao existe CLEO.
  Equivalente feito: o oceano fora do mundo (111,137,170 no jogo) vira 143,189,240 (o oceano dos tiles) para nao haver emenda na borda.
  "Fog" e recurso do PC (nao ha area nao explorada no SA-MP): nao usado. Cor do mapa fica branca (sem tint).
- `models/fronten2.txd`: fundo/mapa do menu do PC. NAO usado (o menu do Android desenha o mapa pelos mesmos tiles do radar).

## Como o jogo carrega o radar (conferido no desassembly do libGTASA.so desta source)
`CRadar::StreamRadarSections` -> `RequestMapSection(x,y)`: se `gRadarTextures[y*12+x]` esta vazio, pega o banco "gta3", textura `radar%02d`,
guarda em `gRadarTextures` (simbolo exportado, 144 ponteiros, refCount+1). `RemoveMapSection` destroi e zera.
`DrawRadarSection` desenha esse array; o MESMO array serve o minimapa e o mapa do menu. Fora do mundo pinta 111,137,170.

## O que foi feito (nativo preservado)
- `game/RadarSkin.cpp/.h`: hook so em `RequestMapSection`. Se existe `SAMP/radar/tiles/radarNN.png` cria um RwTexture (linear, sem mipmap,
  clamp) e poe no array; se NAO existe, chama o ORIGINAL (tile nativo). Blips, rotacao, zoom, mascara, mapa do menu = tudo do jogo.
- 3 instrucoes MOVZ da cor do oceano (bytes conferidos: originais e1 0d 80 52 / 22 11 80 52 / 43 15 80 52).
- `/radarskin on|off|reload|info` (ao vivo; `off` volta ao nativo e restaura a cor do oceano). Config em `SAMP/radar/radarskin.ini`.
- Mapa aberto (/mapa) usa os mesmos PNG (reduzidos a 256x256, ate 48 na memoria) e tira o escurecimento quando os tiles estao ligados.

## Instalar
1. `python3 tools_radar_convert.py <pasta Radar do pacote> <saida>` (ja feito: `radar-tiles-google.zip`, 144 PNG, 3,1 MB).
2. Colocar em `files/SAMP/radar/tiles/` (entre no pack SAMP para baixar pelo launcher) + `radarskin.ini` (opcional).
3. Compilar com os arquivos de `radar-google-codigo.zip`.

## Testado / nao testado
- PC: conversao DXT1->PNG conferida visualmente (mosaico 12x12 igual ao mapa de referencia); encoding dos MOVZ; testes do mapa aberto (67 ok).
- NAO testado: build Android, no aparelho (textura, desempenho ao carregar 512x512 RGBA na thread do jogo, memoria), `RemoveRadarSections` ao vivo.
  Se algo estranho aparecer: `/radarskin off` e me mande as linhas `[RADARSKIN]` do samp_log.txt.
