# IPL por instância — `texdb/ipl/` (gta3.img / gta_int.img)

Mesma ideia do COL v3: o jogo continua carregando o IPL do IMG; o launcher só **mistura** as suas alterações no buffer
antes do `CIplStore::LoadIpl` nativo. Nada é desligado no jogo e qualquer dúvida entrega o buffer original do IMG.

## Instalar
Copie para o projeto (mesmos caminhos):
- `app/src/main/cpp/samp/game/CustomIplParse.h`  (novo, puro, sem dependência do jogo)
- `app/src/main/cpp/samp/game/CustomIpl.h`        (novo, hook + leitura de pastas)
- `app/src/main/cpp/samp/game/Streaming.cpp`      (2 linhas: `#include "CustomIpl.h"` e `CustomIpl::Install();` logo depois do `CustomCol::InstallCinfoGuard();`)

## Onde colocar (dentro de `Android/data/com.samp.mobile/files/`)
`texdb/ipl/<nome_do_ipl>.ipl` e/ou `SAMP/ipl/<nome_do_ipl>.ipl` — ordem: texdb, depois SAMP (alfabética); no mesmo índice **o último vence**.
O nome do arquivo = nome do IPL dentro do IMG (ex.: `countrye_stream0.ipl`, `int_la.ipl`), sem diferenciar maiúsculas.
Na 1ª execução o log lista cada IPL carregado e o nome certo: `[IPL] IPL binario carregado: slot=.. nome='...' instancias=N`.
Marcador (arquivo vazio): `SAMP/ipl_off.on` desliga tudo. Arquivo novo/editado entra em até 2 s (sem reiniciar), mas o IPL só é remisturado na próxima vez que o jogo o carregar (sair/entrar da área).

## Formato do arquivo de ajuste
```
replace                      # troca a instância de índice N (confere o ID original)
N, idOriginal, id, nome, interior, x, y, z, rx, ry, rz, rw, lod
end
remove                       # remove a instância N (confere o ID original)
N, idOriginal
end
inst                         # acrescenta no fim — mesma linha do IPL texto do jogo
id, nome, interior, x, y, z, rx, ry, rz, rw, lod
end
```
- **Índice N** = ordem das linhas `inst` do IPL **decodificado** (a mesma ordem do binário; os do tópico do fórum seguem essa ordem). O `idOriginal` evita aplicar no lugar errado: se não bater, a linha é ignorada e vai pro log.
- **lod** (replace/inst) = índice da linha `inst` no **IPL principal** relacionado (ex.: `countrye_stream4` → `countrye.ipl` de data/maps), **não** desta tabela; `-1` = sem LOD. Passa direto, sem remapear: remover instâncias daqui **não mexe** nos lods das outras (o jogo faz `entityArrays[staticIdx][lod]`, confirmado em `LoadIpl` 0x33b744–0x33b80c e `GetIplEntityIndexArray` 0x33c024).
- **interior** = campo de 32 bits `m_nInstanceType` (byte baixo = área; bits altos = flags), igual ao decodificado.
- Modelo resolvido pelo **nome** (como o jogo); se o nome não existir (ex.: `dummy`), vale o **ID numérico** — desde que exista `ModelInfo` (IDE carregado). Modelo inexistente ⇒ linha ignorada (ver “por quê” abaixo). Modelos novos precisam estar no IDE e ter DFF/TXD/COL em algum IMG.
- Quaternion: igual ao IPL texto, sem conversão (o jogo faz `sscanf` direto na struct).

### Gerar o arquivo automaticamente
```
python3 tools_ipl_delta.py original_decodificado.ipl editado.ipl -o countrye_stream0.ipl
```
Compara os dois, gera `replace/remove/inst`. Não renumere os `lod` do editado (use a numeração do original).

## Evidência (libGTASA.so arm64 do projeto; endereços = `g_libGTASA + off`, todos símbolos exportados)
| O quê | Endereço | Confirmado |
|---|---|---|
| `CStreaming::ConvertBufferToObject` | 0x395114 | chama `CIplStore::LoadIpl` em 0x395270 |
| `CIplStore::LoadIpl(int,uchar*,int)` | 0x33b5f4 | retorna sempre `true`; **é o hook** |
| `CIplStore::LoadIplBoundingBox` | 0x33bb34 | chamado por `LoadIpl` só se o bb do IplDef estiver inválido (1ª vez, com o mesmo buffer) |
| `CIplStore::GetIplName(int)` | 0x33b504 | `&IplDef+0x10` (pool, stride 0x34) → nome do slot |
| `CFileLoader::LoadObjectInstance(CFileObjectInstance*,const char*)` | 0x4d20fc | modelo sem ModelInfo → retorna `nullptr` |
| `CFileLoader::LoadObjectInstance(const char*)` | 0x4d206c | `sscanf("%d %s %d %f %f %f %f %f %f %f %d")` direto na struct; **sem** conversão de quaternion |
| string `"bnry"` / formato do sscanf | 0x7390c0 / 0x7501fe | lidos do .so |

Cabeçalho binário usado pelo jogo: `+0x00 "bnry"`, `+0x04 int16 contagem`, `+0x14 int16 car generators`, `+0x1C offset da tabela de instâncias`
(passo `0x28`: pos[3], quat[4], id, tipo, lod), `+0x3C offset dos car generators` (passo `0x30`).
**Por que validar o ID:** nos dois laços binários (`LoadIpl` 0x33b9d4 e `LoadIplBoundingBox` 0x33be68) o retorno de `LoadObjectInstance` é usado sem teste de nulo.

## Como a mistura é feita
Copia o buffer, **anexa a tabela nova no fim** e altera só `+0x04` (contagem) e `+0x1C` (offset). Tudo que é relativo ao início (car generators etc.) fica intacto e válido. Buffer final passa por `Validate` (cabeçalho, lod entre -1 e 32767, floats finitos); se reprovar, usa o original.
Limites: ≤ 32767 instâncias (o jogo lê 16 bits), 2 MB por arquivo de ajuste, 32 MB por IPL.

## Log (`samp_log.txt`, prefixo `[IPL]`)
```
[IPL] hook instalado: CIplStore::LoadIpl ...
[IPL] 2 arquivo(s) de ajuste:  /...texdb/ipl/countrye_stream0.ipl -> IPL 'countrye_stream0': replace=3 remove=1 inst=2 | linhas ok=6 ruins=0
[IPL] IPL binario carregado: slot=41 nome='countrye_stream0' instancias=312 (13160 bytes) [com ajustes]
[IPL] MISTURA 'countrye_stream0' slot=41: 312 instancias no IMG -> 313 (trocadas=3 removidas=1 acrescentadas=2 ignoradas=0 lod-corrigidos=1), buffer 13160 -> 13632 bytes
```
`ignoradas`, `AVISO ... a mais de 400 unidades` (a caixa de streaming do IPL cresce) e `sem mistura (...)` explicam por que algo não entrou.

## Testado / NÃO testado
Feito (host, `g++`): `testes/host_test_customipl.cpp` — 55 verificações (parser, replace, remove + remapeamento de lod, inst, vários arquivos/“último vence”, buffers inválidos, limites, aviso de distância, texto→binário 1:1) também com ASan/UBSan; ponta a ponta de `tools_ipl_delta.py` com `testes/ipl_apply_host.cpp`; `CustomIpl.h` compila contra stubs com `-Wall -Wextra`.
Os testes usam IPLs binários **sintéticos** montados com o layout lido do desassembly — **não havia IPL real do IMG** para testar.
**Não feito:** compilar para ARM64 no NDK e rodar no aparelho. O host não prova o que o `LoadIpl` nativo faz com o buffer misturado; só o log do aparelho mostra. Confirme no 1º teste: a linha `MISTURA ...` aparece, o objeto trocado/novo aparece no mapa e o jogo não fecha ao entrar na área.

## Limites conhecidos
- Só IPL **binário em streaming** (`*_stream*.ipl` do gta3.img e IPLs do gta_int.img). IPL texto do `gta.dat` (`data/maps`) passa por `CFileLoader::LoadScene` e não é tocado.
- Só instâncias (`inst`). Car generators (+0x3C) permanecem como no IMG. Entradas/saídas (`enex`), garagens, zonas etc. não estão no IPL binário de streaming.
- O LOD de uma casa removida **continua no IPL principal** (texto, `data/maps`): este sistema não o toca, então ele continua aparecendo de longe até ser tratado lá.
- Se o `GetIplName` devolver um nome diferente do esperado, use o que o log mostrar (`nome='...'`).


## V2 - seção `hide` (esconder LOD/instância do IPL principal)

O LOD de uma instância de streaming fica no IPL **principal** (texto, `data/maps`), na linha cujo número é o campo `lod`. Apagar a linha
deslocaria todos os lods seguintes; por isso `hide` **não apaga**: troca só o modelo por um invisível (19300) e a instância continua na mesma
posição da lista, então o índice das outras não muda.

```
hide
13292, 166.62, -106.58, 4.45, 60    # id, x, y, z, raio
end
```
- Só esconde se o **id for igual** e a instância estiver a **menos de `raio` unidades** do ponto (0 < raio <= 500). Outro id, ou o mesmo id longe, não é tocado.
- Vale em qualquer arquivo `.ipl` de `SAMP/ipl/` ou `texdb/ipl/` (pode estar junto com `remove`). Regras de todos os arquivos somam.
- Ponto de ligação: `CFileLoader::LoadObjectInstance(const char*)` (a única chamada de `LoadScene` 0x553dbc para linhas `inst`).
- Se o modelo 19300 não existir no cliente, **nada é escondido** e o log avisa (`hide: modelo invisivel 19300 NAO existe`).
- Log ao esconder: `[IPL] hide: instancia do IPL principal escondida: id=... pos=(...) -> modelo 19300`.
- `SAMP/ipl_off.on` desliga também o hide.
- Para voltar ao normal: apague a seção/arquivo (o jogo relê em ~2 s; o LOD só volta ao recarregar o IPL principal, isto é, reiniciando o jogo).

Observação: se `texdb/ipl/` der `errno=13 Permission denied`, o arquivo foi copiado com outro dono; use `SAMP/ipl/` (funcionou).
