# COL v3 — mistura por entrada + índice em cache

## Instalar
Substitua só **`app/src/main/cpp/samp/game/CustomCol.h`** (o `CustomColParse.h` é o mesmo do v2; Streaming.cpp/game.cpp não mudam).

## Onde colocar os .col  (dentro de `Android/data/com.samp.mobile/files/`)
- `texdb/col/*.col` e `SAMP/col/*.col` → **qualquer nome**, cada arquivo só com os modelos que você alterou.
- Legado (continua funcionando): `texdb/countrye_10.col`, `SAMP/countrye_10.col`, `countrye_10.col` na raiz.
- Mesmo nome de modelo em 2 arquivos: o **último vence** (legados → texdb/col → SAMP/col, alfabético). Vai para o log.
- As pastas `col/` e `models/` do v2.1 **não são mais lidas**.

## Como funciona
Hook em `CColStore::LoadCol(slot, buf, size)` (símbolo exportado do libGTASA). Para o buffer do IMG, cada entrada cujo
**nome** está no índice é trocada pela sua; as outras ficam byte a byte como vieram do IMG. O buffer misturado vai ao
loader nativo com o **mesmo slot**. Antes de entregar: Parse + ValidateAll no buffer final. Qualquer dúvida → entrega o IMG original.
- Entrada sua com nome que **não existe naquele slot** não é usada (o jogo só acha nomes dentro do intervalo do slot).
- Índice: `SAMP/col_index.txt` (só nomes/offsets, validado por caminho+mtime+tamanho). Os dados do .col só são lidos do disco
  quando um LoadCol casa com um nome e são liberados logo depois.
- Entrada com erro (índice de triângulo fora, offset fora, face group ruim...) é ignorada; as boas do mesmo arquivo continuam.
- Se um arquivo mudar depois de indexado, a mistura daquela chamada é abortada (IMG original) e reindexa em até 2 s.
- `CINFO.BIN` é invalidado quando a lista/tamanho/mtime dos .col muda (1ª execução com o v3 regenera uma vez).
- Marcadores em `SAMP/`: `col_off.on` (desliga tudo), `cinfo_keep.on`.

## O que procurar no samp_log.txt
```
[COL] 3 arquivo(s) .col externo(s):            <- por arquivo: dono/modo/R_OK, entradas validas/reprovadas
[COL] indice: 40 nomes validos em 3 arquivo(s)
[COL] MISTURA slot=N: IMG 38 entradas (90112 bytes) -> 3 trocada(s), buffer final 122796 bytes
[COL]   'CE_brewery': id=12931 slot=N triangulos esperados=3035 no jogo=3035 -> DADOS PERSONALIZADOS EM USO
```
No 2º boot, sem mexer nos arquivos: `indice carregado do CACHE ... nenhum .col foi lido do disco`.
`DIVERGENTE` ou `MISTURA abortada/reprovada` = me mande as linhas `[COL]`.

## Testado no host (12 cenários, todos OK) / NÃO testado
Feito: sem arquivos; só o celeiro (41 KB) → 37 entradas idênticas ao IMG; vários arquivos; "último vence"; coleção sem nomes
do índice; entrada corrompida; cache (hit e invalidação por mtime); legado inteiro; arquivo editado após indexar; `col_off.on`;
sobra não-zerada no buffer do IMG. Reproduzir: `python3 testes/make_fixtures.py` e compilar `testes/sim_v3.cpp` (ver o comando no topo do arquivo).
**Não feito:** compilar para ARM64 e rodar no aparelho. O host não prova o que o loader nativo faz com o buffer misturado — isso só o log do aparelho mostra.
Modelos NOVOS (ID novo) precisam estar no IDE e ter colisão em algum IMG; este mecanismo só troca colisão de nomes que já existem no slot.
