# Teleporte de admin pelo mapa

## O que faz
Admin (mesmo criterio do `/ir`: nivel de admin >= 1 ou RCON, e conta logada) que **marca um ponto no mapa** e se teleporta para la:
- toque num ponto livre do mapa aberto (`/mapa`)  -> `/mapax pos x y` (ja enviado pelo cliente);
- toque num local da lista / sede de grupo         -> `/mapax sel id`;
- mapa nativo do jogo (marcar destino no menu)     -> `OnPlayerClickMap` (se esse caminho existir no seu cliente).
Jogador comum continua so com o GPS, exatamente como antes. Ao teleportar, o mapa fecha para o admin ver o mundo carregando.

`/tpmapa` (so admin) liga/desliga. Ligado e o padrao a cada login — senao o admin nunca poderia usar o GPS normal.

## Por que o cliente tambem muda (precisa de novo APK)
O servidor **nao conhece a altura do chao** (sem MapAndreas) e `SetPlayerPosFindZ` nao funciona longe do jogador. Entao o servidor so autoriza e manda `x, y` (+ dica de altura quando existe, ex.: local da lista); o **cliente**:
1. leva o ped para cima do ponto e o **segura no ar** (sem cair) enquanto o jogo carrega a area (min. 2,2 s);
2. acha o chao com uma linha vertical de colisao e espera o valor ficar estavel (6 consultas seguidas);
3. posiciona o ped (chao + 1,3 m) e, por 2 s, se carregar algo mais alto (rua/ponte), sobe para cima;
4. se a linha vertical nunca responder (a Cidade Nova fica **fora** do mapa original; nao da para garantir que essa consulta funcione la), apos 4 s **solta o ped** e deixa a fisica do jogo pousar (a colisao de ped funciona na cidade); quando para, avisa `/mapax tpok <z>`;
5. sem chao nenhum (mar aberto/vazio): **devolve o ped ao ponto de origem** e avisa `/mapax tpfalha` (admin ve a mensagem). Nunca fica preso no ar.
Admin dentro de veiculo: sai do carro no destino (o carro fica onde estava). Interior: vai para o interior 0 (o mapa e o mundo de fora).
O log do cliente tem linhas `[MAPTP] ...` (inicio, chao achado, pousou pela fisica, falhou) para diagnostico.

## Arquivos
Servidor (raiz do servidor): `gamemodes/maximorp.pwn` + `maximorp.amx` (recompilado: 0 erros, mesmos 5 avisos de antes) e `qawno/include/maximo_mapa.inc` (op5, `Mapa_SendTp`, `tpok/tpfalha`).
Cliente (raiz do projeto): `game/MapTeleport.h/.cpp` (novo), `game/hooks.cpp` (1 linha em RenderEffects), `gui/samp_widgets/worldmap.cpp` (trata op5 do RPC 204), `net/scriptrpc.cpp` (Reset na conexao/desconexao).
Protocolo: RPC 204, `u8 5 | f32 x | f32 y | f32 zDica` (zDica < -900 = sem dica). Cliente antigo ignora (op desconhecida) e nada acontece.

## Se voce alterou o maximorp.pwn depois de mim
Em vez de substituir o arquivo, aplique so isto: (1) `new bool:gTpMapaOff[MAX_PLAYERS];` perto de `IDS_IS_ADMIN`; (2) `gTpMapaOff[playerid] = false;` em OnPlayerConnect; (3) copie o bloco "Teleporte de admin pelo mapa" (public `Mapa_PodeTp`, `OnPlayerClickMap`, `CMD:tpmapa`) que fica logo abaixo de `CMD:mapax`; (4) troque o `maximo_mapa.inc`.

## Honestidade / limites
- Servidor compila; a maquina de estados do cliente foi testada em simulacao (chao que aparece tarde, ponte que carrega depois, sem chao, pouso por fisica). **Nada disso foi testado no aparelho** e o cliente nao foi compilado aqui (sem NDK): o proximo build do GitHub Actions e o primeiro compile — se der erro, me mande o log.
- A consulta de chao da Cidade Nova e a incognita principal (fora do mapa original). Se o log mostrar "soltando o ped", o pouso foi pela fisica; se mostrar "falhou", me mande o trecho `[MAPTP]` e a coordenada.
- Pousar em cima de telhado e possivel: a linha vertical pega o ponto mais alto com colisao.
- Marcar o mapa agora TELEPORTA o admin sempre que `/tpmapa` estiver ligado (inclusive ao tocar numa sede/local da lista). Use `/tpmapa` para voltar ao GPS.
