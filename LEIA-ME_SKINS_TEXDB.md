# Banco de texturas dos personagens (texdb/skins)

Mesmo formato binario do banco do mapa (`.txt` + `.dxt/.etc .dat/.tmb/.toc` + `.unc.tmb`), lido pelo motor nativo.
So personagens: `CustomImg::kCustomContent` NAO foi alterado.

## Gerar (no PC)
    pip install numpy pillow
    python3 tools/skin_texdb.py build <pasta pack: heads, hair, tshirts, pants, boot, ...> <saida> --name skins
    python3 tools/skin_texdb.py verify <saida>/skins
Aceita png/jpg/jpeg (jpeg tambem funciona aqui). Texturas com transparencia viram DXT5 / RGBA4444. Teto 512 px (`--max`).

## Instalar no aparelho
Copie a pasta `skins` (gerada) para `Android/data/com.maximo.roleplay/files/texdb/skins/`
(arquivos: skins.txt, skins.idx, skins.dxt.*, skins.etc.*, skins.unc.tmb).

## Uso
- Nome da textura no banco = caminho do pack: `tshirts/1/34` -> `tshirts_1_34` (indice em skins.idx).
- `/skin7 texdb` mostra se o banco carregou; `/skin7 texdb tshirts/1/34` diz se a textura existe.
- `/skin7 slot <nome> <n|+|-|pasta/arq>` e `/skin7 set` leem SO o banco; a leitura de PNG do pack foi removida (sem fallback).
  CorpTex (texturas de aparencia e slots) e /provador idem. Ainda leem PNG: pecas de corporacao (pack/corp) e textures/ do /skin7 apply.
- Log: linhas `[SKINTEXDB]` ao iniciar.

## Nao verificado (precisa do aparelho)
Compilacao NDK; carga do banco pelo motor; DXT5/RGBA4444 no `.etc`; aparelhos PowerVR (sem `.pvr`); conversao com o pack real.

## Pack real convertido (1237 PNG)
Entrada: so PNG (os 70 JPEG duplicados e as pecas "(off)"/"(naofunc)"/template ficaram fora).
Instalar: extraia as DUAS partes (parte1 e parte2) e deixe tudo em
`Android/data/com.maximo.roleplay/files/texdb/skins/` (skins.txt, skins.idx, skins.unc.tmb, skins.dxt.*, skins.etc.*).
Depois de instalado, os PNG de SAMP/skins/pack nao sao mais lidos (podem ser apagados). Mantenha `textures/272/aparencia/roupa/mapa.txt`.
