# Minimapa da Cidade Nova (cliente)
Descompacte na raiz do repositório (caminhos `app/src/main/cpp/...`). Contém tudo do mapa da cidade + o minimapa, então substitui o Cliente-MapaCidade anterior. Precisa dos arquivos da cidade em `SAMP/radar/cidade/` (Mapa-Cidade-arquivos.zip).

- Dentro da cidade o minimapa do HUD passa a desenhar a imagem da cidade (tiles de 1,6 m/px quando o zoom pede, imagem geral caso contrário), girando com a câmera, recortada no formato real da máscara do radar. Fundo azul-água no disco.
- A seta do jogador é redesenhada (a imagem cobre a nativa), apontando o rumo do veículo; o destino do GPS aparece como ponto; a linha da rota continua a do MiniRoute (reta, sem ruas na cidade).
- O rótulo "San Andreas" acima do minimapa vira **Cidade Nova** dentro da cidade.
- `/mapa minicidade on|off` liga/desliga (padrão ligado). Fora da cidade nada muda.
- GPS/HQs: o cliente aceita até 80 locais do servidor (era 40), combina com o servidor da Cidade Nova.

Verificado: sintaxe (clang, com stubs de Android) de hooks.cpp, hud.cpp, worldmap.cpp, waypoint3d.cpp, imguirenderer.cpp (só restam erros de stub JNI/GL no meu ambiente) e a matemática do recorte/afim em teste de host. **Não testado no aparelho.** Se o minimapa ficar vazio/azul na cidade, mande o samp_log.txt (procure `[MINIMAPA]`) e um print.
