# Launcher Máximo RP — tela de Servidores (landscape), música, download redondo e notificações

## O que mudou
**Orientação**: MainActivity, SplashActivity e UpdateActivity agora são `sensorLandscape` (o jogo SAMP já era landscape).
Tela cheia imersiva, recortes (notch) respeitados via `LauncherUi`.

**Tela principal (`activity_main.xml`)** – componentes separados, nada de imagem de UI achatada:
| Componente | Arquivo |
|---|---|
| Fundo cinematográfico (personagem + cidade, ancorado topo-direita) | `util/HeroImageView`, `drawable-nodpi/maximo_bg_hero.webp` |
| Cabeçalho + logo | `layout/mx_header.xml`, `drawable-nodpi/maximo_logo.png` |
| Menu lateral (Início, Servidores, Notícias, Configurações, Comunidade, Sair) | `layout/mx_menu_item.xml` + `MainActivity.buildSidebar()` |
| Painel de Servidores (abas Oficial/Favoritos, ATUALIZAR, barra CONECTAR) | `layout/fragment_servers.xml`, `fragments/ServersFragment` |
| Cartão do servidor | `layout/server_item.xml`, `adapters/ServerCardBinder` (usado por `ServerAdapter` e `FavouriteServerAdapter`) |
| Rodapé: player, volume, versão do cliente, estado do servidor, redes | `layout/mx_footer.xml` |
| Notícias / Comunidade / Início / Configurações | `NewsFragment`, `CommunityFragment`, `HomeFragment`, `fragment_settings.xml` (mesmos IDs) |

**Regras atendidas**
- Reaproveitados: Activities, `ViewPagerAdapter`, `ServersFragment`, `ServerPagesItemFragment`, adaptadores, IDs antigos (`textView6`, `online_text`, `password`, `tabLayout_servers`, `viewPager_servers`, `fragment_place`…).
- **Atualizar**: refaz a consulta real (`servers.json` + query UDP SA‑MP) e recarrega favoritos **limpando a lista antes** (sem duplicar).
- **Conectar**: chama `ServerInformationFragment.launchGame()` — o mesmo fluxo original (grava `settings.ini`, confere `Text/` e `Textures/`, abre `SAMP`). Se o servidor estiver OFFLINE pede confirmação; se ainda verificando, avisa.
- **Nada inventado**: jogadores, ping, modalidade, idioma e senha só aparecem quando a consulta respondeu (senão `--`). Status: ONLINE / OFFLINE / ERRO DE CONEXÃO / VERIFICANDO conforme o resultado real. Endereço e porta vêm do `servers.json`.
- Cliente: mostra a `versionName` real; "Atualizado" só se o Splash concluiu sem atualização pendente (`Config.clientUpToDate`).
- Redes sociais: só aparecem as preenchidas em `res/values/maximo_links.xml` (vêm **vazias**). Os links antigos hardcoded do desenvolvedor original (bit.ly/discord/samp-mobile.shop) e o botão "ADD SERVER" (anúncio de venda de vaga) foram removidos.

## Música
`util/MaximoMusic` (singleton): reproduz/pausa, anterior/próxima, volume (SeekBar) e mudo; persiste volume e liga/desliga; foco de áudio; continua entre Splash → Download → Launcher; pausa em segundo plano; **é liberada antes de abrir o jogo** (`launchGame` e diálogo de favoritos).
> A faixa `res/raw/maximo_theme.ogg` é um **placeholder sintetizado por mim** (loop ~44 s). Troque pelo seu tema oficial mantendo o nome `maximo_theme.ogg`. O título exibido está em `mx_music_title`.

## Download/atualização com anel redondo
`activity_update.xml` + `UpdateActivity`: anel de progresso (`mx_ring_progress.xml`, escala 0–1000, porcentagem no centro), anel girando quando o total é desconhecido, música com botão liga/desliga, nome do arquivo e contador. Mantidos os IDs usados pelo código (`download_progress`, `fileProgressPercent`, `fileName`, `fileCount`, `installation_text`, `gpu`). O Splash também usa o anel girando.

## Notificações novas
`NotificationHelper` / `FirebaseMessageReceiver` / `NotificationStore`:
- Canais: Notícias, Atualizações, Eventos, Downloads (o canal antigo `web_app_channel` é removido).
- Visual próprio (RemoteViews escuros `notification.xml` / `notification_big.xml`, ícone `mx_ic_stat`, banner opcional), ação **Abrir** (URL http/https) e **Abrir launcher**.
- `PendingIntent` com `FLAG_IMMUTABLE` (o código antigo usava só `FLAG_ONE_SHOT` e quebra no Android 12+).
- Permissão `POST_NOTIFICATIONS` pedida uma vez no Android 13+.
- Notificação de progresso do download e de "atualização concluída".
- Histórico real na aba **Notícias**.

Payload FCM recomendado (somente `data`, funciona com o app fechado):
`{"title":"…","message":"…","image":"https://…","type":"news|update|event","url":"https://…"}`

## Compilar
```
cd 2.10-OpenSource
./gradlew assembleDebug        # ou: ./gradlew assembleRelease  (arm64-v8a conforme seu build.gradle)
```
Requer Android SDK/JDK do projeto original. Para os links sociais, edite `app/src/main/res/values/maximo_links.xml`.

## ⚠ Estado da validação
Neste ambiente **não há Android SDK nem rede**, então **não compilei o APK nem testei em aparelho**. Foi feito: XML 100% bem-formado, checagem cruzada de todos os recursos (`@drawable/@string/@id/R.*`) e balanço de chaves em todos os `.java` alterados. Faça o primeiro build e me envie qualquer erro. Pontos a conferir em aparelho: diálogos `alertdialog_server`/`addserver` (dimensões em sdp pensadas para retrato), altura do menu em telas de 360 dp, tema do `SettingsFragment`.
O fundo/logo/banner foram **extraídos da sua imagem** (fundo recomposto só com as áreas sem UI; banner do servidor tem resolução baixa, 224×98 px da referência).
