# Banco texdb "mapa" (tiles do radar/mapa Google sem PNG solto)

## O que é
`tools_texdb/build_mapa_db.py` transforma `radarNN.png` (NN 00..143) num banco no MESMO formato do `gta3/txd/touch`:
`texdb/mapa/mapa.txt`, `mapa.etc.{dat,toc,tmb}`, `mapa.dxt.{dat,toc,tmb}`, `mapa.unc.tmb`. Tiles 256x256, ETC1 e DXT1 (32 KB cada, GPU lê direto).
144 tiles = 4,7 MB por formato (o PNG decodificado em RGBA custava 256 KB por tile na RAM).

## Uso
```
python3 tools_texdb/build_mapa_db.py <pasta com radarNN.png> <saida>      # gera <saida>/mapa/
python3 tools_texdb/verify_db.py <saida>/mapa mapa --src <pasta png> --strict
./tools_texdb/selftest.sh
```
Copie a pasta `mapa/` para `Android/data/com.samp.mobile/files/texdb/` (ao lado de gta3/, txd/ ...).
`verify_db.py` também lê os bancos originais (txd/mobile) para conferir offsets, hashes, RLE ETC1 e miniaturas.

## Cliente (RadarSkin.cpp)
Ordem por tile: 1) banco `texdb/mapa` (GetDatabase→Register→GetTexture("gmapNNN")→Unregister, refCount+1 como o radar nativo)
2) PNG em `SAMP/radar/tiles/` 3) tile nativo. O formato (ETC/DXT) é o do banco `gta3` do aparelho; se o aparelho for outro (PVR) ou
faltar arquivo, cai para PNG/nativo e registra no log `[RADARSKIN]`. `/radarskin info` mostra `banco=etc|dxt|indisp` e quantos tiles vieram dele.

## O que foi verificado e o que NÃO foi
Verificado (host): leitor/escritor do formato contra os txd/mobile reais (0 inconsistências), decodificador ETC1 contra texturas RLE reais,
codificador ETC1/DXT1 (PSNR mediano 38 dB ETC / 41 dB DXT nos 144 tiles, visualmente idêntico), releitura estrita do banco gerado, selftest.sh.
NÃO verificado: compilação Android, que o motor aceite `Load("mapa")` e devolva as texturas, ausência de lag/crash no aparelho, DXT1 contra GPU real
(decodificador DXT é só auto-consistente), aparelhos PVR. Teste no aparelho: `/radarskin reload` → `/radarskin info` (banco=etc/dxt e contagem > 0).
