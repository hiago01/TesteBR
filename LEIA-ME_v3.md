# Cabelo 5 v3 — correção do cabelo BRANCO (Máximo RP)

## O que o seu vídeo mostrou
Cabelo5 aparecendo **branco/platinado**. A textura estava sendo aplicada (dá para ver o pontilhado do degradê), mas a **cor preta
vinha só do material** do skin.dff (RGB 40,35,33) e a textura era clara. O skin.dff usa geometria com flags 0x1001E (sem o bit
"modular cor do material"), e o jogo estava iluminando bastante o ped → a cor do material não chegou e o cabelo ficou claro.
**Causa provável, NÃO confirmada no aparelho** (não tenho como rodar o jogo aqui).

## O que mudou (não depende mais da cor do material)
1. **Textura nova (hair/5/1,2,3)**: preto natural EMBUTIDO (média ~12% de luminância, grão fino com dither ordenado para o RGBA4444 não
   formar faixas no escuro). Mesmo que o motor ignore a cor do material, o cabelo nasce preto.
2. **skin.dff**: idêntico ao v2 (mesma geometria/ossos/vértices; só 3 bytes diferem: cor do material hair5 agora branca = "sem tinta").
3. **Cliente**: tinta padrão do slot = branco (0xFFFFFFFF); o painel COR (RGBA) do provador abre em branco = cabelo natural.
   Cores escolhidas MULTIPLICAM a textura escura -> **só escurecem/matizam** (marrom escuro, azul-petróleo, vinho). Loiro/ruivo claro NÃO é possível
   com textura escura; isso exige tingir a textura em tempo de execução (próximo passo, depende de eu saber se o motor honra a cor do material).
4. **Log de diagnóstico** (novo) em CorpTex: `[CORPTEX] jogador N: material 'fantasy_m_hair5' recebe cor ARGB=... (cor atual do material r,g,b,a)`.
   Mande o samp_log depois de entrar com o cabelo: confirma se o caminho da cor roda.

## Instalar
- `Cabelo5-v3-Assets-COMPLETO.zip` → raiz (SAMP/skins/skin.dff + texdb/skins/*). **Banco COMPLETO** (skins.etc.dat 260.722.576 B): substitua os 6 arquivos juntos.
  Apague o cache do jogo se existir.
- `Cabelo5-v3-Cliente.zip` → raiz do repo (sobrescreve 11 arquivos + testes). Servidor: **nada muda** (continua o Cabelo5-Servidor.zip).

## Verificado aqui
- Banco: `skin_texdb.verify` OK (1241 registros, toc/tmb/unc batem); prefixo original de 259.936.096 B intacto (cmp).
- Teste C++20 de banco (protecao): TUDO OK. Teste C++20 de slots/RGBA/protocolo/provador: TUDO OK (inclui tinta padrão nova).
- skin.dff regerado e relido pelo próprio script (90 materiais, 65.259 vértices <= 65.535, material hair5 255,255,255,255).

## NÃO verificado
- Nenhum NDK aqui: CorpTex.cpp/provador.cpp não foram compilados; nada foi testado no aparelho.
- Se mesmo assim ficar claro/cinza, mande log + vídeo: o log novo diz se a cor chegou ao material.
- hair6 (cabelo longo de tecido) está PAUSADO e não entra neste pacote.
