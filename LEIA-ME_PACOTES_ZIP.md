# Primeira instalacao por pacotes ZIP (TesteBR-free / com.samp.mobile)

## O que mudou e por que
O download de ~1.700 arquivos um a um (1,57 GB) deixava o cliente quebrado: so havia comparacao de `size`, sem hash,
sem escrita atomica e sem limpeza de parciais. Agora a **PRIMEIRA instalacao** baixa poucos `.zip` verificados e
extrai com seguranca. **Atualizacoes e verificacao continuam pelo PRDownloader atual** (arquivo a arquivo), como voce pediu.

### Quando usa ZIP e quando usa o fluxo antigo (UpdateService.decidePackMode)
Usa ZIP somente se TODAS forem verdade: o `client_config.json` tem `packs` validos para a GPU do aparelho, **nao existe**
`installed.json`, faltam >= 50 arquivos e faltam >= 50% dos arquivos da lista. Caso contrario (atualizacao pequena,
verificacao, servidor sem `packs`, `packs` invalidos) segue o fluxo antigo, sem mudanca de comportamento.
Se a instalacao por ZIP falha, a Activity recebe a mensagem 6 (erro) e a proxima verificacao tenta de novo; arquivos ja
comitados de pacotes anteriores contam como existentes.

## Arquivos
Novos (`app/src/main/java/com/samp/mobile/launcher/pack/`): PackException, CancelToken, MiniJson, PathSafety, GameFilePolicy,
Hex, PackSpec, PackConfig, PackManifest, ZipPackExtractor, PackDownloader, AtomicInstaller, InstalledState, DiskSpace,
PackInstaller. (Java 8, sem dependencias Android; JVM puro.)
Alterados: `UpdateService.java`, `config/AppEndpoints.java` (so adiciona `packUrl`), `testes/launcher_unit/run.sh`.
Novos fora do app: `build_packs.py` (servidor), `testes/launcher_unit/test/PackTest.java`.
Nao mexi em C++/JNI, nomes de pacote, layouts, IDs, `endpoints.xml` nem `network_security_config`.

## Servidor
```
python build_packs.py --src C:\cache-server\files --out C:\cache-server\packs ^
   --base-url https://maximoroleplay.com.br/packs --version 1
```
Copie os `.zip` para a pasta servida em `/packs/` e cole o trecho impresso dentro de `"client_config"` (mantenha `url_cache_files`):
```json
{ "client_config": { "version_code": 1, "url_launcher": "...", "url_cache_files": "...",
  "version": "1",
  "packs": [ {"name":"core","gpu":0,"url":"http://.../packs/core.zip","size":123,"sha256":"...","unpacked":456},
             {"name":"gpu_dxt","gpu":1,"url":"...","size":0,"sha256":"..."} ] } }
```
- `url` pode ser absoluta ou comecar com `/` (relativa a `endpoint_base_url`, via `AppEndpoints.packUrl`).
- Pacotes > 900 MiB sao divididos em `core`, `core_2`... (todos `gpu:0`); sem Zip64.
- Para publicar conteudo novo suba `version`. O script le a pasta de origem sem altera-la, recusa caminhos que diferem so
  pela caixa, ignora os arquivos gerados pelo jogo, avisa sobre arquivos de 0 bytes e usa STORED em .img/.dat/.png/.jpeg/.jpg/.tmb/.toc.

## Seguranca da instalacao
- Download em `.downloads/<nome>.zip.part` -> so vira `.zip` apos conferir tamanho E SHA-256; 3 tentativas com backoff (1,5 s, 3 s), sempre do zero (sem Range). `Accept-Encoding: identity`, timeouts 15 s/30 s.
- Espaco livre via `StatFs`: zip + descompactado + 200 MB (`unpacked` do JSON; sem ele estima zip + 20%).
- Extracao em `.staging/<versao>/<pacote>/`: rejeita `..`, caminho absoluto, `\`, `//`, letra de drive, symlink e nomes que diferem so pela caixa; confere CRC32, tamanho e SHA-256 contra o manifest embutido (`.pack_manifest.json`); arquivo fora/ausente do manifest = erro.
- Commit atomico por pacote (mesmo volume, `ATOMIC_MOVE`); arquivo existente vai para `.backup/<versao>/` e e restaurado se algo falhar. No fim apaga `.staging`, `.backup`, `.downloads`.
- Se o app morrer no meio: no `onCreate` do servico (na HandlerThread) `recoverOnStartup` apaga staging/downloads e restaura do backup o que faltar.
- Nunca grava nem apaga os arquivos da lista IGNORAR (SAMP/settings.ini, CINFO.BIN, gta_sa.set...). `installed.json` guarda versao + GPU + path/size.
- Filtro corrigido: agora sem diferenciar caixa (antes `"SAMP/..."` nao casava com `contains("samp")`).
- Contrato de mensagens 0/7, 1, 2, 4, 5, 6 mantido. Msg 4: `current/total` = bytes baixados / soma dos .zip (tamanho real do download); quando o download termina, passa a mostrar a extracao (bytes extraidos / total descompactado); `filename` = `core.zip` ou `core (extracting)`; `totalfiles/currentfile` = pacotes.
- Cancelamento: `cancelDownloads()` e `onDestroy()` cancelam o `CancelToken` e encerram a thread; `mSession` descarta callbacks atrasados. A instalacao roda numa thread propria (`pack-install`) e a extracao numa terceira (`pack-extract`), nunca na principal.

## Mais rapido? (cliente de 1,57 GB)
O gargalo real e a rede, nao o formato. O que ja ajuda: 3 requisicoes grandes no lugar de ~1.700 (some o custo de
conexao/handshake por arquivo); extracao em paralelo ao proximo download; STORED nos arquivos ja comprimidos (sem CPU).
O que ajuda mais e esta no servidor: o `python -m http.server` e de thread unica e lento — **nginx/Caddy ou CDN** costuma
render varias vezes mais. Se o servidor suportar Range, um proximo passo seria baixar 2 pacotes em paralelo ou retomar; **nao
implementei** (o app nao depende de Range). Diminuir o proprio conteudo (texturas .dxt/.etc/.pvr so da GPU do aparelho — ja e feito pelos pacotes gpu_*) e a maior economia.

## O que foi compilado/executado — e o que NAO foi
- Executado: `sh testes/launcher_unit/run.sh` -> **72 verificacoes de PackTest, 0 falhas**, alem dos testes antigos (zip-slip e variantes, symlink, caixa duplicada, zip truncado/corrompido, CRC errado, SHA errado, manifest invalido, rollback e recuperacao, installed.json, espaco, download com servidor HTTP local real: ok/adulterado/cortado/instavel/404/cancelado, instalacao completa, falha no meio, espaco insuficiente, filtro por GPU).
- Compilado contra stubs: `UpdateService.java` e `AppEndpoints.java` compilam contra stubs minimos de Android/Volley/PRDownloader (valida sintaxe e tipos das minhas chamadas, **nao** o APK).
- `build_packs.py` rodado numa arvore de teste; os zips gerados foram extraidos e verificados pelo extrator Java.
- **NAO feito:** nao ha Android SDK/NDK aqui, entao o **APK nao foi compilado** e a integracao no `UpdateService` **nao foi testada em aparelho**. Compilei com o JDK atual (`--release 8` indisponivel aqui); o codigo evita APIs > Java 8, mas confirme no seu build. `StatFs.getAvailableBytes` exige API 18 (seu minSdk e 26).

## Suposicoes
1. `version` do conteudo e texto `[A-Za-z0-9._-]` (numero tambem serve). 2. Se o `client_config` tem `packs` mas nenhum serve a GPU do aparelho, usa o fluxo antigo. 3. ZIP so na primeira instalacao (limiares 50 arquivos / 50%); ajuste `PACK_MIN_MISSING_FILES`. 4. `installed.json` so e gravado pela via ZIP; apos atualizacoes pelo fluxo antigo ele fica com a versao anterior e a verificacao segue comparando tamanhos da lista de arquivos.

## Teste em aparelho
1. Aparelho limpo, servidor com packs: deve baixar core+gpu e abrir o jogo; confira `installed.json`. 2. Mate o app no meio do download e da extracao: reabrir deve limpar `.staging/.downloads` e refazer. 3. Corrompa 1 byte do zip no servidor: erro apos 3 tentativas, nada em `files/`. 4. Encha o armazenamento: erro de espaco antes de baixar. 5. Edite `SAMP/settings.ini` no celular e reinstale: permanece. 6. Remova `packs` do JSON: fluxo antigo.

## Modo "sem excecoes" (padrao do build_packs.py)
`build_packs.py` agora copia TUDO da pasta de origem (so `installed.json` e `.staging/.backup/.downloads` ficam de fora) e
imprime `"include_all": true`, que o app le do `client_config` e entao extrai tudo, inclusive `SAMP/settings.ini`, `CINFO.BIN`,
`gta_sa.set` etc. Sem esse campo o app mantem a protecao antiga. `--com-excecoes` volta ao modo antigo.
Atencao: so vale para a PRIMEIRA instalacao por pacotes; o fluxo antigo (atualizacao/verificacao) continua sem baixar os arquivos
gerados pelo jogo, para nao sobrescrever as configuracoes do jogador a cada abertura.

## Modos do build_packs.py
- **seguro (--seguro):** tudo, exceto lixo de SO, lista.txt/files.json e caches do aparelho (CINFO.BIN, models/MINFO.BIN, SAMP/cinfo.sig, SAMP/col_index.txt, logs). settings.ini, favorites.json, gta_sa.set etc. vao no pacote.
- **padrao** (`--sem-excecoes`): absolutamente tudo (so internos do instalador ficam de fora; sobre os caches: os caches sao validados por mtime/tamanho/assinatura no aparelho).
- `--com-excecoes`: modo antigo (ignora tambem as configuracoes do jogador).

## Gerando os pacotes a partir do files.zip
`python build_packs.py --src C:\cache-server\files.zip --out C:\cache-server\packs --base-url https://maximoroleplay.com.br/packs --version 1`
Le direto de dentro do .zip (nada e extraido em disco; testa o CRC do zip antes), remove a pasta raiz unica se houver (`--no-strip` desliga)
e, por padrao, NAO deixa nada de fora. `--seguro` deixa de fora lixo de SO, lista.txt e os caches do aparelho. O `verify_install.py zip`
tambem aceita o .zip em `--src`. O fluxo antigo (atualizacao) continua sem baixar/sobrescrever arquivos gerados pelo jogo.

## Pasta SAMP/ completa (--overlay)
`python build_packs.py --src C:\cache-server\files.zip --overlay C:\cache-server\SAMP.zip --out C:\cache-server\packs --base-url https://maximoroleplay.com.br/packs --version 1`
O `SAMP.zip` passa a ser a verdade para toda a pasta `SAMP/`: o que existia so no `files.zip` (obsoleto) some e tudo do `SAMP.zip` entra,
inclusive arquivos recriados pelo jogo (cinfo.sig, col_index.txt, favorites.json, svlog.txt, settings.ini, svconfig.ini) e as
pastas com espaco/parenteses (`SAMP/dff/New Folder`, `countrye_stream4(edit.txt`). O `files.json` e gerado do MESMO conjunto.

## Pasta SAMP/ pelo PRDownloader (cache-server/SAMP + samp.json)
Fluxo: **1)** pacotes ZIP (tudo menos SAMP/) -> extrai e confere -> **apaga** `.downloads` (zips), `.staging` e `.backup` do aparelho;
**2)** a pasta SAMP/ e baixada arquivo a arquivo pelo PRDownloader, com tamanho e SHA-256 conferidos, nada ignorado.
Nas aberturas seguintes (atualizacao/verificacao) a SAMP/ continua sendo conferida pelo samp.json.

Servidor (C:\cache-server):
```
SAMP\           <- a pasta, na raiz do servidor HTTP (arquivos em http://IP:8080/SAMP/...)
samp.json       <- py gerar_samp_json.py --src C:\cache-server\SAMP --out C:\cache-server\samp.json
packs\*.zip     <- py build_packs.py --src C:\cache-server\files.zip --excluir-pasta SAMP --out C:\cache-server\packs --base-url http://IP:8080/packs --version 1
```
Sem `samp.json` no servidor o launcher cai para o fluxo antigo (SAMP/ pelo files.json). Endpoint novo: `endpoint_samp_json_path` (endpoints.xml / AppEndpoints.sampJsonUrl / sampFileUrl).
**Arquivos que o jogo regrava** (settings.ini, favorites.json, svlog.txt, cinfo.sig, col_index.txt, sampvoice/svconfig.ini): entram no samp.json e sao
baixados SEMPRE que estiverem ausentes; se ja existirem NAO sao sobrescritos (senao toda abertura apagaria configuracoes e favoritos do jogador).
Para rebaixar sempre, `SampList.restoreVolatileOnlyIfMissing = false` (pack/SampList.java).

## Pastas vazias da SAMP/ (ex.: SAMP/skins/fallback_tex)
O `samp.json` agora tem `"dirs": [...]` com as pastas VAZIAS da SAMP/. O launcher cria essas pastas (mkdirs, idempotente)
ao ler o samp.json. Sem `SAMP/skins/fallback_tex` o jogo nao usa a textura `male01` de reserva e a skin fica branca/manchada.
Rode de novo `py gerar_samp_json.py` (ele imprime "pastas vazias incluidas"). A pasta no servidor deve se chamar `skins`.
