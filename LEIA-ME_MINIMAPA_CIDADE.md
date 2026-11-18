# Minimapa da Cidade Nova (cliente)
Descompacte na raiz do repositório (caminhos `app/src/main/cpp/...`). Contém tudo do mapa da cidade + o minimapa, então substitui o Cliente-MapaCidade anterior. Precisa dos arquivos da cidade em `SAMP/radar/cidade/` (Mapa-Cidade-arquivos.zip).

- Dentro da cidade o minimapa do HUD passa a desenhar a imagem da cidade (tiles de 1,6 m/px quando o zoom pede, imagem geral caso contrário), girando com a câmera, **preenchendo a moldura QUADRADA** do minimapa (v2: antes saía num círculo dentro do quadrado). Tamanho do quadrado = 0,962 do raio do radar (medido no seu print); se sobrar/faltar borda: `/mapa minicidade moldura 0.95` (aceita 0.80–1.20), sem recompilar.
- **Filtros por zona (v2):** as pílulas Local/Cat só oferecem opções de grupos da zona atual. Fora da cidade "Cidade Nova" não aparece em Local e nenhum local/grupo dela aparece; dentro da cidade só aparecem os de lá (os de fora somem). Ao entrar/sair da cidade os filtros voltam para Todos/Todas.
- A seta do jogador é redesenhada (a imagem cobre a nativa), apontando o rumo do veículo; o destino do GPS aparece como ponto; a linha da rota continua a do MiniRoute (reta, sem ruas na cidade).
- O rótulo "San Andreas" acima do minimapa vira **Cidade Nova** dentro da cidade.
- `/mapa minicidade on|off` liga/desliga (padrão ligado). Fora da cidade nada muda.
- GPS/HQs: o cliente aceita até 80 locais do servidor (era 40), combina com o servidor da Cidade Nova.

Verificado: sintaxe (clang, com stubs de Android) de hooks.cpp, hud.cpp, worldmap.cpp, waypoint3d.cpp, imguirenderer.cpp (só restam erros de stub JNI/GL no meu ambiente) e a matemática do recorte/afim em teste de host. **Não testado no aparelho.** Se o minimapa ficar vazio/azul na cidade, mande o samp_log.txt (procure `[MINIMAPA]`) e um print.

## v4 — menu do mapa: ocultar/mostrar e mover

- Com o **mapa grande aberto**, o minimapa do HUD (e a etiqueta "Cidade Nova"/local) **some**; volta ao fechar o mapa.
- Na **esquerda da lista** (lado direito da tela) ficam 2 botões redondos:
  - **Olho**: com o menu visível mostra o olho cortado (toque = ocultar); com o menu oculto mostra o olho aberto (toque = mostrar).
  - **Mão com setas**: arraste para **mover o menu** pela tela (limitado à área do mapa). Funciona **também com o menu oculto** (ficam só os dois botões).
- Ícones embutidos no código (MapIcons.h, 64x64) — sem arquivo novo, sem IMG novo. Se a textura falhar, desenha com formas simples.
- Arquivos: WorldMapUI.h, worldmap.cpp, MapIcons.h (novo), MiniCity.h, hooks.cpp, hud.cpp.
- NÃO testado no aparelho; só compilação de sintaxe e teste lógico do layout/toques (botões acertáveis, oculto, limite de arrasto).

## v5 — locais mais visíveis, ruas no zoom de 1 km e "Saída da Cidade"

- **Locais (icones + nomes)**: na cidade nova os marcadores ficam ~45% maiores e os nomes passam a ter pílula escura (fonte 13,5). Os nomes são colocados por prioridade (selecionado > locais > grupos), tentando direita, esquerda, acima e abaixo; se não couber sem encostar em outro nome/ícone, o nome é omitido (o ícone continua) — some a "sopa de letras" do zoom de 1 km. O local selecionado sempre mostra o nome.
- **Ruas**: as imagens da cidade foram refeitas (tools/citymap.py): as ruas reais (~15 m) eram finas demais e sumiam em 1 km. Agora têm largura mínima (visão geral ~40 m, rodovia ~52 m; tiles ~22/29 m) e contorno escuro. Instalar `SAMP/radar/cidade/*` (Mapa-Cidade-arquivos.zip) — são só PNG soltos, sem IMG novo.
- **Saída da Cidade**: é a rodovia (MRoad150m) que sai da cidade por y≈1967, faz a curva e termina ao norte em x≈-2615, y≈2277. Entrada nova em `locais.txt`: `8|-2615.0|2270.0|13.0|Saida da Cidade` (aparece no mapa e na lista/GPS dentro da cidade). Para mover, edite essa linha.
- NÃO testado no aparelho (só compilação de sintaxe do WorldMapUI.h + testes de layout).
