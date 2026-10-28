# Auditoria e correção do launcher SA-MP Mobile 2.10

> **Aviso de honestidade:** neste ambiente não havia Android SDK/NDK, Gradle nem rede. **O APK NÃO foi compilado e nada foi executado em aparelho/emulador.** O que foi realmente executado está na seção 6. Faça `./gradlew assembleDebug` e os testes de aparelho da seção 7 antes de publicar.
> Nenhum arquivo C++/JNI/hook nativo foi alterado. Nomes de pacote, layouts, IDs e o contrato de mensagens Service↔Activity foram preservados (única adição: mensagem `6` = erro recuperável).

## 1. Diagnóstico (linhas = arquivo ORIGINAL)

| # | Arquivo:linha | Defeito |
|---|---|---|
| 1 | `SplashActivity.java:47` | Pedia `READ/WRITE_EXTERNAL_STORAGE` + `RECORD_AUDIO` no splash. Launcher e jogo só usam `getExternalFilesDir()` (app-specific), que **não exige** permissão de armazenamento. |
| 2 | `SplashActivity.java:247-259` | `isPermissionsGranted()` retornava `true` na **primeira** permissão concedida (lógica de "qualquer uma" em vez de "todas"). |
| 3 | `SplashActivity.java:262-273` | `onRequestPermissionsResult` olhava só `grantResults[0]` (READ_EXTERNAL_STORAGE). Negado → toast "Permissions not granted!" e **nada mais acontecia** (sem retry, sem sair do splash). |
| 4 | `SplashActivity.java:114-137` | `requestPermissions`/`bindService` chamados da **thread GL** (`onSurfaceCreated`); `glGetString` podia ser `null` (NPE); `onResume` com `prefs==null` quando a assinatura é inválida (NPE). |
| 5 | `AndroidManifest.xml:22,23,56` | `MANAGE_EXTERNAL_STORAGE` (desnecessário; permissão especial que bloqueia publicação), `RECORD_AUDIO` exigida no início, `usesCleartextTraffic=true` global. |
| 6 | `MainActivity.java:192-198` e `272-278` | `while(!fragment.isAdded()) Thread.sleep(1000)` dentro de callback do Volley (**thread principal**) e dentro de `AsyncTask` (trava a fila serial dos favoritos). |
| 7 | `MainActivity.java:216` | `onErrorResponse` chamava `getServersInfo()` de novo: **loop infinito de requisições** sem limite, e cada chamada criava um `RequestQueue` novo (5 threads vazadas por tentativa). |
| 8 | `MainActivity.java:170-185` | Adicionava **todos** os itens de `query`, marcava `ONLINE` e `ping=12` fixos, jogadores vindos do JSON (dados fictícios). `throw new RuntimeException` em JSON inválido → crash. |
| 9 | `UpdateService.java:140` (+`MainActivity:165`, `UpdateService:382`) | URLs `http://192.168.1.83:8080/...` espalhadas no código. |
| 10 | `UpdateService.java:154,233-244` | `getFilesInfo` fazia `while(true) sleep(30)` esperando `Util.responseFilesInt` **na thread principal** (callback do Volley). Conexão sem timeout; só pegava `IOException`; qualquer outra exceção na thread deixava o valor em 0 → **laço eterno / ANR**. |
| 11 | `Util.java:40-41` | `responseFilesInt` global nunca voltava a 0: 2ª verificação lia valor velho. Falha na lista (`=2`) era tratada como "Updated" (silenciosa). |
| 12 | `UpdateService.java:363-433` | Download em laço com `do{sleep}while(mDownloadingStatus)` sem timeout; callback que não chega = espera eterna. |
| 13 | `UpdateService.java:366-370` | `new File(getExternalFilesDir(null), "<caminho absoluto>")` → caminho **duplicado** (`.../files/storage/.../files/...`); `string.replace(nome,"")` quebrava se o nome aparecia no diretório. Sem proteção contra `../` vindo do JSON remoto. |
| 14 | `UpdateService.java:508-511` | `onError` chamava `downloadGame()` recursivamente: **tentativas infinitas**. Com o serviço recriado, `mUpdateGameURL==""` → laço eterno. |
| 15 | `UpdateService.java:144-146, 313, 533` | Listas nulas até o config chegar (NPE em `startGameUpdateChecking`); `mUpdateGameDataSize` acumulava entre verificações; `packageInfo` nulo (NPE); `total = size/2` no progresso (>100%). |
| 16 | `SampQueryAPI.java` | `receive()` com timeout era engolido e o buffer zerado era interpretado como resposta; tamanho de string lido da rede sem limite (`new byte[n]`, OOM/NegativeArraySize). |
| 17 | `UpdateActivity.java` | Sem tratamento de erro (ficava em "Updating…"); instalação de APK sem checar `canRequestPackageInstalls()`; `getStringExtra("mode")` nulo → NPE. |

### Causa exata de "Permissions not granted!" e do carregamento infinito
O array pedido era `[READ_EXTERNAL_STORAGE, WRITE_EXTERNAL_STORAGE, RECORD_AUDIO]` com `targetSdk 34`. Pela documentação do Android, a partir do Android 11 `WRITE_EXTERNAL_STORAGE` não tem efeito e, no Android 13+, `READ_EXTERNAL_STORAGE` também deixa de ser concedível (substituída pelas permissões de mídia): o sistema responde "negado" sem mostrar diálogo. Como o código só olhava `grantResults[0]`, mostrava o toast e **nunca fazia `bindService`** → `UpdateService` não iniciava → o splash ficava eternamente em "Loading". *(Conclusão por análise do código e da documentação; não reproduzida em aparelho.)*
Segundo caminho de travamento: com permissão OK, falha de rede em `client_config`/lista de arquivos prendia a thread principal nos laços do item 10.

## 2. Arquivos modificados / criados

| Arquivo | Mudança e motivo |
|---|---|
| `app/src/main/AndroidManifest.xml` | Remove `MANAGE_EXTERNAL_STORAGE` e `usesCleartextTraffic`; READ/WRITE limitadas a `maxSdkVersion=28`; comentário sobre `RECORD_AUDIO` (pedida no jogo). |
| `launcher/SplashActivity.java` | Fluxo de permissões "todas as obrigatórias" (lista vazia, ver comentário no código), negada/cancelada/"não perguntar mais"→configurações, reavaliação em `onResume`, GL→UI thread, watchdog de 30 s com Tentar de novo/Continuar, guardas de nulidade. |
| `game/SAMP.java` | `RECORD_AUDIO` pedida **ao entrar no jogo** (não no splash), uma vez, sem insistir após "não perguntar mais"; negada = jogo segue sem voz. Sem tocar no C++. |
| `launcher/UpdateActivity.java` | Msg 6 (diálogo Tentar de novo/Continuar), `canRequestPackageInstalls()` + retorno das configurações, guardas de nulidade. |
| `launcher/UpdateService.java` | Reescrito sem esperas: Volley com timeout/tentativas, estado só na HandlerThread, `mSession` invalida callbacks, 3 tentativas/arquivo e 3 no APK com backoff, aborta após 2 arquivos seguidos falhando, cancelamento em `onDestroy`, caminho seguro, `mDownloadingStatus` sempre volta a `false`. Contrato 0/1/2/4/5/7 mantido. |
| `launcher/MainActivity.java` | `getServersInfo()` assíncrono, 1 servidor, estados, sem recursão/sleep, fila Volley única e parada em `onDestroy`, consulta UDP real em executor, favoritos sem laços bloqueantes. |
| `launcher/adapters/ServerAdapter.java` | Mostra estados (checking/online/offline/erro) e **toque para tentar novamente**; removido `clear()` que apagaria a lista compartilhada. |
| `launcher/adapters/ServerInformationFragment.java` | Usa `getStatusText()` (sem "0/0" fictício). |
| `launcher/fragments/ServersFragment.java` | `reloadServersIfNeeded()` em `onStart` (só se erro/offline, mín. 5 s). |
| `launcher/util/SAMPServerInfo.java` | `Status` ganhou `LOADING`/`ERROR` (ordinais antigos preservados) e `getStatusText()`. |
| `launcher/util/SampQueryAPI.java` | Timeout deixa de virar "resposta"; limite no tamanho de string; mede ping real (`lastPingMs`). |
| `launcher/util/Util.java` | Remove globais `responseFiles/responseFilesInt`. |
| **novo** `launcher/config/AppEndpoints.java` | Único ponto de URLs + timeout/tentativas. |
| **novo** `launcher/util/ServerConfigParser.java` | Leitura e validação do `servers.json`. |
| **novo** `res/values/endpoints.xml` | Endpoints (valores de DEV). |
| `res/xml/network_security_config.xml` (+ **novo** `app/src/debug/res/xml/...`) | HTTP só no build `debug`; padrão/release = HTTPS apenas. |
| **novos** `docs/servers.example.json`, `testes/launcher_unit/*` | Exemplo e testes de unidade. |

O código completo de cada arquivo está no próprio projeto (ZIP).

## 3. Formato do `servers.json`
```json
{
  "query": [
    {
      "number": 1,
      "name": "Meu Servidor Roleplay",
      "ip": "IP_REAL_DO_SERVIDOR",
      "port": 7777,
      "online": 0,
      "maxplayers": 100,
      "password": false
    }
  ]
}
```
- **Obrigatórios:** `name` (texto), `ip` (IPv4 ou hostname; `0.0.0.0`, URLs, `ip:porta` e o placeholder acima são rejeitados), `port` (1–65535).
- **Opcionais:** `number` (1), `maxplayers` (0), `online` (ignorado: só contagem real da consulta UDP é exibida), `password` (false).
- Só o **primeiro** elemento de `query` é usado; os demais são ignorados sem validação.
- Estados na UI: *Checking…* → `jogadores/máx` (ONLINE, após resposta UDP válida, com ping real) | *Offline* (sem resposta) | *Error* + "Tap to retry" (JSON/HTTP inválidos).
- Favoritos continuam na aba Favorites (lista separada); nunca entram na aba Hosted.

## 4. Como configurar os endereços
Edite `app/src/main/res/values/endpoints.xml` (`endpoint_base_url` + caminhos). Os valores atuais são **de desenvolvimento** (IP privado de LAN, HTTP :8080), idênticos aos do código original; não inventei domínio de produção. Para produção crie `app/src/release/res/values/endpoints.xml` com os mesmos nomes e URLs HTTPS reais.
Dependências: o servidor HTTP precisa servir `/servers.json`, `/client_config.json` e `/files/<path>`. Dentro do `client_config.json` continuam valendo `version_code`, `url_launcher` e `url_cache_files` (esta última e `url_launcher` são URLs completas definidas por você; `AppEndpoints` só centraliza `/files/`).
**Atenção HTTP:** agora o build `release` bloqueia HTTP (Android). Para testar com o IP de LAN use o build **debug**, ou use HTTPS em release. Sem isso o app mostrará erro recuperável de conexão.

## 5. Observações fora do escopo (não alteradas)
Senhas do keystore em `app/build.gradle` e `.jks` versionado; `AsyncTask` deprecado; `jcenter()`; `POST_NOTIFICATIONS` declarada mas não solicitada (só afeta notificações FCM no Android 13+).

## 6. Testes REALMENTE executados
| Teste | Resultado |
|---|---|
| Análise sintática (javac parser) dos 13 arquivos Java alterados/criados | 0 erros |
| `ServerConfigParser` — 33 casos (JSON válido, vários servidores → só o primeiro, inválido/truncado/HTML, campos ausentes, IP/porta inválidos, placeholder) | **33/33** (usa stub de `org.json`; o Android é um pouco mais permissivo no parse) |
| `SampQueryAPI` contra servidor UDP SA-MP falso local — 12 casos (online c/ ping real e parse; sem servidor; servidor mudo; eco errado; info sem resposta; tamanho malicioso; host inválido) | **12/12** |
| Validade XML dos recursos e manifest | OK |

Reproduzir: `sh testes/launcher_unit/run.sh`.

## 7. Testes pedidos e NÃO executados (precisam de aparelho/emulador)
| # | Teste | Situação |
|---|---|---|
| 1–4 | Permissões (todas concedidas / áudio negado / obrigatória negada / retorno das configurações) | **Não executado.** Por projeto: splash não exige nenhuma permissão; áudio é pedido no jogo; fluxo de negação/configurações existe em `SplashActivity` mas está sem teste. |
| 5–7 | JSON com 1 servidor / vários / inválido na UI | Lógica de parse testada (seção 6); **UI não executada**. |
| 8 | HTTP indisponível + recuperação | **Não executado.** Por projeto: 8 s de timeout × (1+2 tentativas), ≈ 38 s no pior caso, depois linha de erro com retry. |
| 9 | Recarregar sem duplicar | **Não executado.** Por projeto: `clear()+add()` de um único item, descarte de respostas de gerações antigas. |
| 10 | Download interrompido/falha de rede | **Não executado.** |
| 11 | Navegação sem congelar | **Não executado.** |
| 12 | Build completo do APK | **Não executado** (sem SDK/NDK/Gradle/rede aqui). Alterações são só Java/XML. Risco conhecido: não pude confirmar contra o JAR real as assinaturas de `com.downloader.Error.isConnectionError()/isServerError()` (PRDownloader 1.0.2) — se o build reclamar, remova esses dois do `Log.w`. |
| — | Logcat | **Não disponível.** Filtros úteis: `adb logcat -s UpdateService MainActivity SplashActivity x1y2z`. |
