# Linhas do mapa + GPS pelas ruas (NODES0..63.DAT, A*)

Base analisada: `Source_Atual_Sab.zip`. Nada de Android foi compilado nem testado em aparelho (ver "Verificado / NÃO verificado").

## 1. Diagnóstico da source (o que existia)

| Peça | Situação encontrada |
|---|---|
| Mapa aberto (`worldmap.cpp`, `WorldMapUI.h`, `WorldMapCore.h`) | Funciona com tiles `radarNN` / PNG. A rota vinha **só do servidor** (RPC 204 op 4, `maximo_mapa.inc`); sem ela, **reta tracejada** atravessando prédios. A polyline era desenhada com espessura fixa (7 px contorno / 3,6 px linha). |
| `RadarSkin.cpp/.h` | Só troca os 144 tiles (PNG/Google Maps). Não desenha linhas. **Não foi alterado.** |
| `WidgetRadar.cpp/.h` | Só um hook, hoje comentado. **Não foi alterado.** |
| `maximo_mapa.inc` (servidor) | **Não está no zip** (o `.rar` do servidor não pôde ser aberto aqui). O protocolo foi lido em `WorldMapCore.h`. O servidor **não precisa mudar**: a rota dele continua tendo prioridade. |
| NODES0..63.DAT | **Não estão no zip** nem nos zips SAMP enviados. Mas a source e o libGTASA dizem onde estão e qual é o formato (abaixo). |

### NODES: onde estão e formato (conferido no desassembly do libGTASA 2.10 arm64)
* O jogo reserva **64 ids de streaming "DAT"** (`RESOURCE_ID_DAT = 25511`, `Streaming.h`) — são os `nodesN.dat`, entradas dentro dos IMG (`texdb/*.IMG`). O libGTASA também tem a string `data\paths\nodes%d.dat`.
* `CPathFind::LoadPathFindData(RwStream*, int)` (0x3db4ec): 5 × int32 (nós, nós de veículo, nós de pedestre, nós de navegação, links) → nós de **28 bytes** → navegação de **14 bytes** → links de **4 bytes** (+ 0x300 de enchimento) → u16 × links → u8 × (links+192) × 2.
* `CPathFind::DoPathSearch` (0x3dbc30) confirma: nº de links do nó = `flags & 0xF`; `baseLinkId` no offset 16; palavra de link = `{u16 área, u16 nó}` (área nos 16 bits baixos).
* Os nós de **veículo vêm primeiro**; só eles entram no grafo (GPS de carro). Coordenadas = int16 / 8.

### Limite importante descoberto
`ImDrawIdx` é de **16 bits** (`imconfig.h`) e **toda a interface sai em uma só ImDrawList** (`GetBackgroundDrawList`): no máximo **65 536 vértices por quadro**. Uma linha grossa custa 8 vértices. Por isso a camada de ruas tem **orçamento de vértices** (24 000 por padrão). Sem isso, passou de 65 536 e o ImGui estoura (no teste do PC o `assert` do ImGui disparou com 6 000 segmentos).

## 2. O que foi feito

| Arquivo | Tipo | O que faz |
|---|---|---|
| `gui/samp_widgets/RoadGraph.h` | **novo** (lógica pura) | Leitor de NODESn.DAT (validado), leitor de IMG VER2, grafo de ruas compacto (CSR, só nós de veículo, int16), vizinho mais próximo por grade, **A\*** (heurística euclidiana, direcionado; se não houver caminho, tenta ignorando mão única), carga de 64 áreas. |
| `gui/samp_widgets/MapLines.h` | **novo** (lógica pura) | Categorias de linha, estilos (cor, contorno, espessura mín/máx), leitor do `linhas.ini`, simplificação (Douglas-Peucker), **cantos arredondados com desvio limitado**, espessura pelo zoom, trechos visíveis. |
| `gui/samp_widgets/WorldMapUI.h` | alterado | `IPlatform` ganhou `roads()` e `lineStyles()` (com padrão: sem eles tudo funciona como antes). GPS local quando o servidor não manda rota; linha com contorno escuro + linha azul, espessura pelo zoom, só trechos visíveis, pontas arredondadas, parte já percorrida não é desenhada; camada opcional de ruas. |
| `gui/samp_widgets/worldmap.cpp` | alterado | `GamePlatform` carrega o grafo **em segundo plano e só quando o mapa precisa** (GPS ligado ou camada de ruas); lê/cria `SAMP/radar/linhas.ini`; comandos novos. |
| `gui/samp_widgets/chat.cpp`, `worldmap.h` | alterados (1 linha + comentário) | Liga os subcomandos `/mapa ruas|nodes|linhas`. |
| `testes/*` | novos | 3 testes de PC (abaixo). |
| `SAMP/radar/linhas.ini` | novo (modelo) | Todas as categorias. Se não existir, o jogo grava este modelo sozinho. |
| `backup_pre_linhas/` | backup | Os 4 arquivos originais, intactos. |

**Reaproveitado, sem duplicar:** `RouteRemaining/RouteLength/Dist`, o protocolo RPC 204, `setGps` (blip no minimapa), o desenho de pinos/jogador, `m_route` (rota do servidor). **Preservado:** tiles/mapas originais, `RadarSkin`, `WidgetRadar`, GPS do servidor.

### Como o GPS decide
1. Rota do servidor (op 4) com ≥ 2 pontos → usa (com cantos arredondados).
2. Senão, grafo pronto → A\* do jogador ao destino. Recalcula se o destino mudar ou se o jogador sair > 45 un. da rota (no máx. 1 cálculo a cada 1,2 s).
3. Senão (grafo carregando/indisponível, origem/destino a > 600 un. de qualquer rua, sem caminho) → **reta tracejada**, como antes. O mapa nunca quebra por falta de dados.

### Categorias
| Categoria | Estado |
|---|---|
| `rota` (GPS) | implementada |
| `rua`, `avenida`, `rodovia` (camada de ruas) | implementadas — **classificadas pela largura do nó (heurística)**; o log imprime o histograma de larguras para você ajustar `rua_ate` / `avenida_ate` |
| `terra`, `pedestre`, `agua`, `ar`, `interior` | **SEM implementação**: já existem no `.ini` e no código (estilo, cor, liga/desliga), mas nunca são desenhadas — prontas para configuração posterior |

## 3. Instalação
1. Faça backup do projeto (os 4 originais já estão em `backup_pre_linhas/`).
2. Extraia o zip **na raiz do projeto** (os caminhos já começam em `app/src/main/...`), substituindo.
3. Android Studio: **Refresh Linked C++ Projects** (o CMake usa GLOB; os dois `.h` novos entram só por `#include`, mas faça um build limpo).
4. (Opcional) copie `SAMP/radar/linhas.ini` para `files/SAMP/radar/` no aparelho. Se não copiar, o jogo grava o modelo na primeira vez.
5. Nenhuma mudança de servidor nem de dados do jogo. Os NODES são lidos **dos IMG que o jogo já tem** (`texdb/*.IMG`). Para testar com outro conjunto: arquivo solto `SAMP/paths/nodesN.dat` (tem prioridade).

## 4. Comandos (no chat do jogo)
* `/mapa` abre o mapa. Toque num ponto/local = GPS (como antes). Com o mapa aberto o GPS desenha a rota pelas ruas.
* `/mapa nodes` — estado do grafo (`pronto: N nós, N ligações, áreas X/64`).
* `/mapa ruas on|off` — camada com todas as ruas (desligada por padrão).
* `/mapa linhas` — relê o `linhas.ini` sem reiniciar.
* Logs no `samp_log.txt`: linhas `[NODES]` e `[LINHAS]`.

## 5. Testes

### Executados (no PC, sintéticos — `sh testes/rodar_testes_linhas.sh`)
* `test_roadgraph.cpp` — **64 verificações, 0 falhas**, com AddressSanitizer + UBSan: parse byte-a-byte do layout acima, grafo de 14 400 nós (~700 KB, carga ~7 ms em -O2/x86), A\* atravessando 64 áreas, **desvio de parede com abertura única** (a rota não atravessa a parede), mão única (usa o fallback e marca), destino sem ligação, IMG VER2 (nome em maiúsculas), arquivo solto com prioridade, área corrompida ignorada sem derrubar o resto, **6 000 mutações aleatórias do arquivo sem crash**, `.ini` (ida e volta do modelo), geometria. Na rota diagonal do mapa todo a suavização se afastou no máximo **3,5 un.** da rota original (limite verificado no teste: 6).
* `host_test_linhas_render.cpp` — a `Ui` real com o **ImGui real** e plataforma falsa; rasteriza o desenho num PPM. Conferido a olho: rota pelas ruas contornando a parede, rota do servidor em L com cantos arredondados, reta tracejada sem grafo. Quadro com camada de ruas + rota: **28 026 vértices** (limite 65 536), ~2,7 ms/quadro no PC (sem GPU).
* `host_test_plataforma.cpp` — o trecho novo do `worldmap.cpp` (extraído do arquivo, não copiado à mão): carga em segundo plano, modelo do `linhas.ini` gravado, recarga, falha de dados → `roads()==nullptr`.

### NÃO verificado (preciso do seu teste/log)
* **Compilação Android/NDK** do `worldmap.cpp`, `chat.cpp` e dos headers (só testados com g++ e clang++ de PC, C++17; AddressSanitizer/UBSan só com g++ — o clang deste ambiente não tem o runtime do ASan, e sem sanitizadores passou igual).
* **Dados reais**: o parser foi validado contra o **desassembly** do jogo e contra arquivos **sintéticos** no mesmo layout — **não contra um NODESn.DAT real**. Os nomes das entradas no IMG (`nodesN.dat`) também não foram vistos num IMG real.
* Desempenho e aparência **no aparelho**; antisserrilhamento (as linhas usam o `AntiAliasedLines` padrão do ImGui; não confirmado em aparelho).
* Classificação rua/avenida/rodovia por largura (heurística).
* GPS com o grafo real: qualidade da rota em pontes/viadutos (o grafo é 2D; ver limitações).

**Primeiro teste (2 minutos):** abra o mapa, toque num destino, depois `/mapa nodes`. Me envie as linhas `[NODES]` do `samp_log.txt`. Esperado, algo como:
```
[NODES] texdb/GTA3.IMG: 64 arquivos nodesN.dat no diretório
[NODES] áreas ok=64 ... faltando=0 rejeitadas=0
[NODES] nós de veículo=~12000 ligações=...
[NODES] conferência: id!=esperado=0 fora_da_area=0 nos_ruins=0 links_ruins=0
[NODES] largura do caminho (valor:qtd): ...
```
Se `rejeitadas` ou `nos_ruins` > 0, ou `faltando` > 0, o log diz o motivo e o GPS cai na reta (sem travar).

## 6. Limitações conhecidas (honestas)
* **Linha no minimapa nativo: NÃO implementada.** O minimapa é desenhado pelo `CRadar` do jogo; desenhar a rota nele exigiria hook + a transformação mundo→radar (o libGTASA tem `CRadar::TransformRadarPointToScreenSpace`, mas a conversão inversa/rotação precisa ser mapeada e testada). Próximo passo possível, sob pedido. O blip do destino já aparece no minimapa (como antes).
* O grafo é **2D**: pontes/viadutos sobrepostos podem prender origem/destino ao nível errado; a rota em si segue as ligações reais, então não "pula" de nível.
* Nós de **pedestre, barco e avião** não entram (categorias `pedestre/agua/ar` preparadas, sem implementação). Estradas de **terra** idem.
* Rotas bloqueadas por script em tempo real (`SwitchRoadsOff`) não são consideradas.
* O A\* é **por distância**, sem penalidade de curva/semáforo.
* A camada de ruas é cortada pelo orçamento de vértices (prioridade rodovia > avenida > rua); em zoom afastado só mostra as maiores.
