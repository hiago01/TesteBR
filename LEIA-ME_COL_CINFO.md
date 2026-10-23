# countrye_10.col / CE_brewery + CINFO.BIN

## O que foi feito
| Arquivo | Mudança |
|---|---|
| `game/CustomColParse.h` (novo) | leitor puro de arquivo .col (COLL/COL2/COL3/COL4), testado no host |
| `game/CustomCol.h` (novo) | auditoria passo a passo, nova leitura opcional e guard do CINFO.BIN |
| `game/Streaming.cpp` | +1 include, +1 linha: `CustomCol::InstallCinfoGuard()` |
| `game/game.cpp` | +1 include, +1 linha: `CustomCol::Audit(...)` depois do `CustomImg::Audit` |
| `tools_col_audit.py` (novo) | mesma auditoria offline no PC (somente leitura) |
| `testes/host_test_customcol.cpp` (novo) | testes do leitor |

Originais de `Streaming.cpp` e `game.cpp` estão em `backup_originais/`; o diff em `COL_CINFO.patch`.

## Onde colocar o countrye_10.col (ordem de busca)
1. `files/SAMP/col/` (pasta do cliente)
2. `files/SAMP/`
3. `files/texdb/` (pasta do touch.img)

Sem o arquivo em nenhuma delas: 1 linha no log e o jogo segue exatamente como antes.

## Passos no samp_log.txt (prefixo `[COL]`)
A. onde achou · B. cada entrada (ident, nome, size, offset) · C. `CE_brewery` está dentro? · D. o jogo conhece `CE_brewery` com id 12931 e tem CColModel? · E. nova leitura (só com `SAMP/col_reload.on`): antes/depois, e recusa se o arquivo estiver cortado/corrompido.

## CINFO.BIN
Hook em `CColAccel::startCache`. Se não há assinatura gravada (1ª vez), ela mudou (countrye_10.col, touch.img ou *.ide) ou o cache tem 0 bytes: o CINFO.BIN é **renomeado** para `CINFO.BIN.bak-<tempo>` (nunca apagado, nunca sobrescreve backup) e o jogo regera. Assinatura em `SAMP/cinfo.sig`. `SAMP/cinfo_keep.on` desliga tudo. Prefixo no log: `[CINFO]`.
A 1ª abertura depois do update pode demorar mais (cache sendo regerado).

## Não verificado
- Não compilei para ARM64 nem rodei no aparelho. Compilei e simulei no host com stubs (sem erros de compilação, 11 cenários OK).
- A regeração do cache quando falta o arquivo é inferência pela estrutura do `CColAccel`; confirme no 1º teste (o log `[CINFO]` mostra o que aconteceu).
