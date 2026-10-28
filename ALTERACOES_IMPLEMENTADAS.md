# ALTERAÇÕES IMPLEMENTADAS — Máximo RP (TesteBR-free)

Backup: `backup_pre_alteracoes/` guarda os arquivos originais alterados (o ZIP original também é a base).

## O que NÃO foi verificado (leia primeiro)
- **Não compilei com Gradle/NDK** (sem Android SDK/NDK/rede neste ambiente) e **não testei em aparelho**.
- Feito: verificação de recursos Android (nenhum `@string/@color/@drawable/@id/R.*` faltando ou duplicado), XML bem-formado, e passagem do `javac` só para erros de **sintaxe** nos arquivos alterados (nenhum). O C++ foi revisado por diff, **não compilado**.
- Os testes de 4–13 da lista (persistência, conexão, loading em jogo) precisam ser feitos no aparelho.

## 1. Categoria antiga de favoritos (desativada, lógica preservada)
- `Config.SHOW_LEGACY_FAVORITES = false` (ligar = volta a aba "Manuais").
- `ServersFragment`: abas montadas por **id de página** (`PAGE_*`), não por posição; sem aba antiga e sem aba vazia.
- `MainActivity.getFavoriteServersInfo()` não consulta mais os servidores manuais enquanto oculta. `FavoritesInfo`, `FavoriteServerData`, adapters, `ServerAddFragment` e `SAMP/favorites.json` **intactos**.

## 2. Favoritos dos servidores oficiais (implementado, DESATIVADO na UI por `Config.SHOW_OFFICIAL_FAVORITES=false`; só a lista oficial aparece)
- `data/OfficialFavorites.java`: SharedPreferences `mx_official_favorites`, JSON `{key,host,port}`; chave normalizada `host(minúsculo):porta`; sem duplicatas; JSON corrompido → lista vazia sem crash.
- Estrela em `server_item.xml` (`mx_star`, `GONE` nos manuais), ícones `mx_ic_star(_filled)`.
- Aba **Favoritos** = filtro da lista oficial atual (`MainActivity.getOfficialFavoriteList`). Se o servidor sumir/ficar offline a marcação **continua salva**; mensagem de lista vazia/indisponível em `fragment_official_favorites.xml`.
- Selecionar/conectar usa o mesmo objeto `SAMPServerInfo` (endereço/porta reais). Fonte oficial e parser não foram alterados quanto aos dados.

## 3. IP/porta ocultos (só apresentação)
- `ServerCardBinder.bindOfficial`: linha de endereço `GONE` (não reserva espaço).
- `ServerInformationFragment`: IP/Porta e rótulos `GONE`; linhas restantes reposicionadas (sem buraco). `launchGame()` segue usando `getAddress()/getPort()`.
- `ServerConfigParser`: mensagem de erro não ecoa mais o IP.
- Favoritos manuais (do próprio jogador) continuam mostrando o que ele digitou.
- Fora do escopo: o chat nativo do jogo não imprime o IP (linhas já comentadas).

## 4. Loading com progresso real
- Vídeo: a 2ª tela é o `loadingscreen` (preto, logo, barra) exibido na Activity `SAMP` após CONECTAR.
- Etapas reais (Java→JNI `SAMP.setLoadingStage`): 0 onCreate (0%), 1 `initializeSAMP` (15%), 2 1º quadro do motor (40%), 3 `CGame::Initialize` (60%), 4 `CNetGame` criado (75%), 5 conexão enviada (85%), conexão aceita → 100% e some.
- Sem temporizador. Entre etapas a barra fica parada e um brilho discreto indica atividade; o motor do GTA não expõe % interno, então os valores são das **etapas concluídas**, não estimativa de tempo.
- Só texto (não movem a barra): sem resposta, servidor cheio, recusado. Perda de conexão reexibe a tela ("Reconectando…") até nova aceitação. Erro de JNI mostra mensagem em vermelho. `onDestroy` cancela animações.
- Arquivos: `LoadingScreen`, `LoadingBarView`, `loadingscreen.xml`, `SAMP.java`, `jniutil.*`, `loadingstage.h`, `main.cpp`, `netgame.cpp`.

## Arquivos alterados/criados
Java: MainActivity, SAMP, LoadingScreen (+LoadingBarView), ServerAdapter, ServerCardBinder, ServerInformationFragment, Config, ServersFragment, ServerPagesItemFragment, ServerConfigParser, OfficialFavorites (novo).
Res: loadingscreen.xml, server_item.xml, fragment_official_favorites.xml (novo), mx_ic_star*.xml (novos), maximo_compat.xml (strings/cor novas).
C++: jniutil.h/.cpp, loadingstage.h (novo), main.cpp, netgame.cpp.

## Limitações conhecidas
- O parser continua lendo só o 1º servidor do `servers.json` (fonte não alterada); a lógica de favoritos já suporta vários.
- Se o IP oficial mudar no servers.json, o favorito antigo fica salvo mas não aparece (chave diferente).
- IPs continuam visíveis por inspeção do app/tráfego.

## 5. Loading com a arte nova + música contínua (atualização)
- `loadingscreen.xml` refeito como a arte enviada: fundo `drawable-nodpi/mx_loading_bg.jpg` (a arte com a barra/texto/player embutidos foi **limpa** por inpainting; a área do card ficou como uma névoa escura, coberta pelo card real), título "CARREGANDO O CLIENTE...", barra azul com a porcentagem real à direita e a etapa atual abaixo. Mesmo progresso por etapas reais da seção 4.
- `GameMusicCard` + `game_music_card.xml`: card "TOCANDO AGORA" (capa, título, barra de tempo com seek, anterior/próxima = reinicia, play/pause, volume e X). Mesmo `MaximoMusic` do launcher (nenhum 2º MediaPlayer).
- `MaximoMusic.enterGameMode()` (chamado ao abrir o jogo no lugar de `release()`): a música continua, **toca uma vez até o fim** (sem loop) e o card só some quando a música **acaba** ou no **X**. Se estava pausada ao abrir o jogo, o card não aparece. Pausa se o jogo for para segundo plano e retoma ao voltar. `SAMP.onDestroy` libera o player.
- Após o loading o card encolhe (60%) para o canto superior direito para não cobrir os botões do jogo; ao reconectar volta ao tamanho do loading.
- Não testado em aparelho: posição/proporção do card sobre a arte foi calculada para telas 20:9 (a arte é 1870x841).
