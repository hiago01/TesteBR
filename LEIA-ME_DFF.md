# DFF por entrada — `texdb/DFF/`

## Instalar
- Novo: `app/src/main/cpp/samp/game/CustomDff.h`
- Alterados: `CustomSkinStream.cpp` (+ `.h`) e `game.cpp` (1 include + 1 linha de auditoria). O CMake já pega tudo por glob.
- No aparelho, copie a pasta `texdb/DFF/` para `Android/data/com.samp.mobile/files/texdb/DFF/`
  (já vai com `laehospital1.dff` = modelo 5403).

## Como funciona
O nome do arquivo (sem `.dff`) é o nome do modelo no IDE. No `ConvertBufferToObject` (mesmo hook das skins), se existe
`texdb/DFF/<nome>.dff`, o jogo converte o **seu** arquivo, alocado com o tamanho real (stat), em vez dos bytes do IMG.
Só aquela entrada muda; o resto vem do IMG. Ordem de precedência (o último vence): `texdb/DFF/` → `texdb/dff/` → `SAMP/dff/`.
Reprovou na validação estrutural → o jogo usa o modelo original e o motivo vai ao log. `SAMP/dff_off.on` desliga tudo.

## Limites (auditoria)
| Onde | Limite | Observação |
|---|---|---|
| `CustomDff.h` | 32 MB por arquivo, 4096 arquivos | constantes `kMaxDffBytes` / `kMaxFiles` |
| `CustomSkinStream.cpp` (skins 7/9/271) | 64 MB | inalterado |
| `CustomCol.h` | 64 MB por .col, 96 MB misturado | inalterado |
| IMG v2 (diretório) | tamanho em 16 bits de setores ≈ 128 MB | formato do IMG; não afeta `texdb/DFF` |
| `ms_streamingBufferSize` (libGTASA) | valor definido pelo jogo, em setores de 2048 B | **não dá para ler do código-fonte**; vai no log `[DFF][AUDIT]` |
| Modelo original "big" (> buffer) | não é trocado | o jogo lê em 2 etapas; o log avisa |

## O que procurar no samp_log.txt
```
[DFF] encontrado: .../texdb/DFF/laehospital1.dff (820820 bytes)
[DFF][AUDIT:CGame::Initialize] buffer de streaming=N setores ...
[DFF][AUDIT] laehospital1  id=5403 OK | seu arquivo=820820 bytes (401 setores) | original no IMG=M setores
[DFF] modelo 5403: DFF externo ... | convertido: ok=1
```
Linhas com `SEM MODELO`, `REPROVADO`, `sem entrada no IMG` ou `'big'` explicam por que o arquivo não foi usado.

## Testado / NÃO testado
Testado no host (`testes/host_test_customdff.sh <dff>`, 8 cenários OK): índice, nome em maiúscula, precedência, arquivo truncado,
vazio, chunk corrompido, `dff_off.on`, arquivo de 3,9 MB lido inteiro, e o seu `laehospital1.dff` real.
**Não feito:** compilar para ARM64 e rodar no aparelho. Se o modelo aparecer branco/invisível, as texturas
(`lh_*`, `hospwin*_LAe`...) precisam existir no TXD/texdb do modelo.
