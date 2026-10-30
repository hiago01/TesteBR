# Download refeito com PRDownloader + dominio https

## O que mudou
- **Primeira instalacao (pacotes ZIP)** voltou a usar o **PRDownloader** (`launcher/PrDownloaderTransfer.java`), pelo link
  `https://maximoroleplay.com.br/packs/...`. Antes usava um downloader proprio (HttpURLConnection) que recomecava do zero a cada falha.
  - **Retoma de onde parou** (Range; o Caddy `file_server` atende Range): queda de rede, app fechado ou celular reiniciado nao perdem o que ja foi baixado.
  - O PRDownloader agora roda com o banco ligado (`setDatabaseEnabled(true)`), que e o que guarda o ponto de retomada.
  - O zip baixado se chama `<nome>-<12 primeiros hex do sha256>.zip`: pacote novo (conteudo diferente) nunca reaproveita parcial de pacote antigo.
  - Depois de baixar, **tamanho e SHA-256 sao conferidos**. Se nao conferir, o parcial e apagado e baixa do zero (3 tentativas).
  - Um zip completo e valido deixado por uma execucao anterior (ex.: app morreu na extracao) e reaproveitado sem baixar de novo.
  - Os parciais ficam em `.downloads/` entre execucoes (o `recoverOnStartup` so apaga `*.part`); a pasta e apagada quando a instalacao termina.
- **Atualizacoes (arquivo a arquivo)** continuam no PRDownloader e agora tambem retomam. Novo: o `installed.json` guarda o **SHA-256** de cada
  arquivo; se o `files.json` do servidor tiver `sha256` diferente para um arquivo com o MESMO tamanho, ele e baixado de novo (antes so o tamanho era comparado).
  Os arquivos baixados e conferidos na atualizacao sao gravados no `installed.json` (so se ele ja existir; nunca e criado por uma atualizacao parcial).
- **APK do launcher**: continua no PRDownloader, conferindo tamanho/SHA-256 do `client_config.json`.
- Todos os links vem de `res/values/endpoints.xml` (`https://maximoroleplay.com.br`).

## Arquivos
Novos: `pack/FileTransfer.java`, `pack/HttpTransfer.java` (motor JVM puro dos testes), `launcher/PrDownloaderTransfer.java`.
Alterados: `pack/PackDownloader.java`, `pack/PackInstaller.java`, `pack/AtomicInstaller.java`, `pack/InstalledState.java`,
`UpdateService.java`, `testes/launcher_unit/test/PackTest.java`.

## Servidor
Nada novo: `client_config.json` com `packs` (build_packs.py) e `files.json` **com sha256** (o build_packs.py ja gera).
Para o upgrade por conteudo funcionar, publique o `files.json` novo gerado por `build_packs.py`.

## Verificado x nao verificado
- Rodou: `sh testes/launcher_unit/run.sh` -> PackTest 97 ok, 0 falhas (inclui retomada por Range com servidor local, queda no meio,
  zip reaproveitado, parcial adulterado, installed.json com sha) e os demais testes do launcher.
- Compilou contra stubs: `UpdateService.java` e `PrDownloaderTransfer.java`.
- **NAO** rodou: build Gradle, APK, celular, PRDownloader real (a biblioteca nao esta acessivel aqui; a assinatura `setHeader`/`setOnProgressListener`
  vem da API 1.0.2 e precisa compilar no seu Gradle), Caddy real.
- A tela "Reparar" (`RepairFragment`/`RepairEngine`) continua com o downloader proprio (`FileDownloader`); nao foi alterada.
