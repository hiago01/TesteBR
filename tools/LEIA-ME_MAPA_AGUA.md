# Mapa aberto: água azul quase transparente

A água azul do mapa aberto fica com 7% de opacidade (alfa 1/15): o jogo, escurecido, aparece através dela. Terra, parques, ruas e ícones continuam opacos; a borda da costa degrada suave.

## Instalar
1. Cliente: aplique o zip de código e faça o build (RadarSkin.cpp/.h, worldmap.cpp, WorldMapUI.h).
2. Banco novo `mapaui`: extraia `mapaui-texdb.zip` DENTRO da pasta `texdb/` do jogo (fica `texdb/mapaui/mapaui.*`). Nenhum IMG novo.
   - Sem o banco, o mapa cai no comportamento anterior (banco `mapa` opaco) ou, se existirem PNG em `SAMP/radar/tiles/`, faz a água transparente na CPU. Nada quebra.
3. `/radarskin info` mostra `mapaui=1(N)` quando o banco está ativo; o log tem `[RADARSKIN] banco texdb mapaui carregado (etc|dxt)`.

## Peso
- ETC (Mali): ~15 MB de dados (RGBA4444 nos 113 tiles com água/borda; 31 tiles sem água ficam em ETC1 de 32 KB). DXT: ~8 MB.
- O banco só é carregado na 1ª vez que o mapa aberto pede um tile; o radar/minimapa continua com o banco `mapa` (4,7 MB, opaco, inalterado).

## Regerar (se trocar os tiles do mapa)
`python3 tools/mapa_agua_texdb.py mapa saida` (lê `mapa/mapa.etc.dat|toc`, escreve `saida/mapaui/`). Fórmula: f = clamp((B−R−12)/92, 0, 1); alfa = round((1 − f·14/15)·15)/15. A mesma fórmula roda em C++ (`RadarSkin::WaterAlpha`) para PNG e `SAMP/radar/mapa.png`.
