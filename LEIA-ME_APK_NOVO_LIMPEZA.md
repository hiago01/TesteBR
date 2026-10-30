# APK novo: apagar arquivos dos clientes antigos e instalar de novo (PRDownloader, /filestestes/)

## O que acontece
1. Na 1ª abertura de cada APK novo (versionCode diferente do último que limpou), a Splash roda `pack/ClientWipe` em segundo plano:
   apaga TUDO em `Android/data/com.samp.mobile/files/` (jogo, texdb, SAMP/dff, SAMP/col, caches CINFO/MINFO, installed.json,
   .downloads/.staging/.backup) **menos** `settings.ini`, `SAMP/settings.ini`, `gta_sa.set`, `gtasatelem.set`, `SAMP/favorites.json`,
   `SAMP/sampvoice/svconfig.ini`, `SAMP/diag.ini`, `samp_log.txt`, `chat_log.txt`, `SAMP/svlog.txt`.
2. Depois segue o fluxo normal: sem `installed.json` o launcher vê "sem instalação" e baixa tudo de novo pelo PRDownloader.
3. O prefixo de download dos arquivos mudou para `https://maximoroleplay.com.br/filestestes/` (`res/values/endpoints.xml`, `endpoint_files_path`).
4. Desligar: `<bool name="wipe_files_on_new_apk">false</bool>` em `endpoints.xml`. Não roda com o "modo sem verificação" (4 toques) ligado.

## Servidor (precisa estar certo, senão o jogo fica sem arquivos depois da limpeza)
- Os arquivos do jogo devem estar em `<site>/filestestes/<mesmo caminho do files.json>` e o `client_config.json` com `url_cache_files` apontando para o `files.json` desse conjunto.
- A pasta `SAMP/` vem do `samp.json` e é baixada de `<site>/SAMP/...` (NÃO de /filestestes/), como antes.
- Se o `client_config.json` tiver `"packs"` (ZIPs), a 1ª instalação baixa pelos ZIPs, com as URLs que estão neles; para forçar /filestestes/ tire `"packs"` do client_config de teste ou aponte as URLs dos packs para lá.
- Pacotes de teste: gere com `build_packs.py` apontando para a sua pasta de teste.

## Cuidados
- É DESTRUTIVO: depois de limpar, se o servidor/URL estiver errado o jogo fica sem dados até baixar. Teste primeiro num aparelho/pasta de teste.
- Só roda 1x por versionCode. Para repetir a limpeza no mesmo APK: limpe os dados do app ou suba o `versionCode` no `app/build.gradle`.

## Verificado x não verificado
- Rodou (JVM): `sh testes/launcher_unit/run.sh` -> `ClientWipeTest` 8 verificações, 0 falhas (apaga o que deve, mantém configs/favoritos/log/diag.ini, idempotente, pasta inexistente/null) e os testes antigos do launcher.
- NÃO rodou: build Gradle, APK, aparelho, PRDownloader real, servidor /filestestes/. O trecho novo do `SplashActivity.java` NÃO foi compilado aqui.
