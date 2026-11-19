# Minimapa / mapa aberto da cidade: San Fierro colado + rodovia marcada (v1)

NADA foi testado no aparelho (so no PC: a interface roda com ImGui real e uma plataforma falsa; o banco foi decodificado de volta e conferido).

## O que estava errado (visto nos seus prints/video, x ~ -3000, y ~ 1968)
1. Perto da divisa a imagem da cidade acabava em x=-3000 e nao tinha NADA de San Fierro: no minimapa a tela trocava de lilas (imagem da cidade) para azul (radar original) de uma vez e a ponte terminava no vazio. No mapa aberto da cidade a rodovia terminava em "San Fierro >" sobre fundo lilas.
2. Dentro da cidade a etiqueta do local ("Cidade Nova") e a bussola sumiam: a imagem do minimapa e desenhada pelo mapa (WorldMapUI), que vem DEPOIS da HUD, e cobria as duas (no print so sobra uma tira da bussola).
3. Dentro da cidade a rodovia so existia pintada na imagem (larga e palida); a marcacao amarela com contorno branco so aparecia em San Andreas.

## O que mudou
### Dados: texdb/cidade (SUBSTITUI o banco atual; mesmo nome de banco, mesmos nomes de textura + 3 tiles novos)
- Foi colado um pedaco de SAN FIERRO ORIGINAL (tiles gmap000-143 do banco texdb/mapa, o mesmo radar que voce ja usa) em x = -3000 ... -1000, em TODA a altura do mapa da cidade (cn_overview + tiles cn_t_8_0, 8_1, 9_0 alterados; cn_t_8_2, 9_1, 9_2 novos).
- So o "vazio" da imagem foi trocado: tudo que a cidade desenha (a ponte/rodovia com as curvas ate Bayside) continua por cima, com a borda suavizada.
- Conferido: a ponte termina exatamente na enseada de Bayside do radar original (mesma coordenada do IPL: curva em -2615,2107 e MSaidaBayside em -2615,2195).
- Nenhum outro tile foi mexido: os 22 tiles que nao mudaram sao byte a byte identicos ao seu banco (gerados dos mesmos PNG: o CRC guardado no .txt do seu banco bate com os PNG usados).
- Banco: 26 -> 29 texturas, +3 MB por formato (dxt e etc). Nao cria IMG novo.
- Na divisa x=-3000 continua existindo a mudanca lilas (cidade) | azul (SA): e a propria divisa dos dois mundos. Se quiser a cidade toda com o azul do radar original, e so recolorir o "vazio" (me avise).

### Codigo (3 arquivos)
- hud.cpp: dentro da cidade (minimapa visivel) a etiqueta + bussola nao sao desenhadas na HUD; sao redesenhadas por HudDrawLocationTagOver(), chamada pelo mapa DEPOIS da imagem, entao ficam POR CIMA. Fora da cidade nada muda.
- WorldMapUI.h:
  - DrawMiniLink tambem dentro da cidade: a rodovia real (contorno branco + amarelo) e marcada por cima da imagem.
  - A seta do jogador virou DrawMiniArrow e e desenhada por ultimo (depois da rodovia e da rota GPS): a linha amarela nao passa por cima dela.
  - Mapa aberto: DrawExitLink usa o eixo REAL (MiniRoute::LinkWorld(), tirado das pecas mx_rod150 / mx_curva15 / MSaidaBayside do IPL, x=-5499 ate Bayside). No mapa da cidade desenha a rodovia INTEIRA; no mapa de SA continua so o trecho de x >= -3000. A largura segue 14 m do mundo (limitada entre 2,5 e 8 px).
  - Nova virtual IPlatform::drawLocationTagOver (padrao vazio: o teste de PC continua compilando).
- worldmap.cpp: implementa drawLocationTagOver chamando HudDrawLocationTagOver (hud.cpp).

## Instalar
1. Cliente (codigo): descompacte NA RAIZ do repo (app/...), compile pelo GitHub Actions como sempre.
2. Aparelho: descompacte texdb/cidade/* em <pasta do jogo>/texdb/cidade/ (substitui os 8 arquivos). Nao precisa apagar nada.
3. Teste: va de San Fierro para a cidade pela ponte de moto/carro e olhe o minimapa na divisa (x=-3000): SF aparece nos dois lados; a rodovia fica marcada em amarelo; "Cidade Nova" + bussola aparecem. Depois abra o mapa dentro da cidade e arraste para o leste: SF e a rodovia ate Bayside.
   Se algo vier errado, mande o samp_log.txt (linhas [CIDADE] / [MINIMAPA]) e um print.

## Limites (honesto)
- A rodovia na imagem do banco continua com a largura exagerada de 29 m (o gerador engrossa para ler no zoom do minimapa); a marcacao amarela por cima e a que mostra o eixo real. Posso fazer a imagem com a largura real (15 m) se preferir.
- A etiqueta do local continua na posicao em que a HUD a desenha hoje (sobre o minimapa); so mudei a ORDEM de desenho na cidade.
- O mapa aberto de SA (sem a cidade) nao foi alterado alem do eixo da rodovia.

## Ferramentas
tools/cidade/compose_sf.py (cola o radar original nas imagens da cidade), cidade_texdb.py + skin_texdb.py (geram o banco), testes/host_test_minimapa_cidade.cpp (teste de PC).
