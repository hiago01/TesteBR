# Bandeira 3D com física de tecido — Máximo RP (v1)

**Status honesto:** nada disto foi rodado no aparelho. O que foi feito aqui: o código da física, o leitor do DFF e o montador da malha rodam e passam em testes no PC (g++/clang, com AddressSanitizer + UBSan); o `Flag.cpp` (a cola com o jogo) foi compilado contra *cabeçalhos mock* que imitam a API do RenderWare/jogo — **não** contra o NDK. Os nomes e assinaturas usados foram conferidos lendo os cabeçalhos do projeto, mas um erro de compilação no GitHub Actions ainda é possível; se aparecer, me mande o log.

## 1. O que vem no pacote (raiz do repo, sem a pasta TesteBR-free)

| Caminho | O que é |
|---|---|
| `SAMP/models/flag_maximo.dff` | Modelo: mastro + ponteira dourada + base redonda + tecido de pena (frente/verso). 3 atomics, 1.468 vértices, 112 KB |
| `SAMP/models/flag_maximo.txd` | Textura 1024×2048 DXT1 (frente e verso). **O cliente não lê este .txd** (ver §4); é entregue para o seu fluxo/ferramentas |
| `texdb/mapasmaximo/*` | Banco ETC1 **atualizado**: tudo que já tinha (incl. as 5 texturas de estrada do pacote v6) + `flag_maximo` + `flag_maximo_verso` (165 texturas, com mipmaps). **Substitui** o banco do pacote da estrada |
| `SAMP/flags.txt` | Exemplo (comentado). Bandeiras locais lidas ao iniciar |
| `app/.../game/Flag.h/.cpp` | Cola com o jogo: ciclo de vida, LOD, textura do banco, desenho Im3D, `/bandeira`, logs `[FLAG_PHYSICS]` |
| `app/.../game/FlagPhysics.h/.cpp` | **Física** (Verlet, passo fixo). Módulo puro |
| `app/.../game/FlagModel.h/.cpp` | Leitor do DFF (RW 3.7). Módulo puro |
| `app/.../game/FlagMesh.h/.cpp` | Monta vértices/índices e divide em lotes. Módulo puro |
| `hooks.cpp`, `scriptrpc.cpp`, `chat.cpp`, `SAMPRPC.cpp/.h` | Arquivos existentes com **só linhas adicionadas** (66 linhas, 0 removidas — ver `tools/bandeira/bandeira-edits.diff`). Se o seu repo mudou nesses 5 arquivos desde a última vez, aplique o diff em vez de sobrescrever |
| `servidor/maximo_flag.inc` | Include Pawn (RPC 207). Não compilado aqui (só há `pawncc.exe`) |
| `png/` | Texturas (frente/verso), prévia 3D e 2 prévias da simulação |
| `tools/bandeira/` | Geradores (DFF, textura, banco) e os testes |
| `testes/` | Saída dos testes que rodei |

Não cria IMG novo e não mexe em `mapasmaximo.img` (o modelo é lido de `SAMP/models/`, a textura sai do banco `mapasmaximo` que o `extra_imgs.txt` já carrega). CMake: `samp/CMakeLists.txt` usa `file(GLOB_RECURSE …)` com C++20 — os 4 `.cpp` novos entram sozinhos (re-rode o CMake/limpe o cache se o build já estava configurado).

## 2. Como usar

**Teste rápido no jogo:** `/bandeira teste` (cria uma ao seu lado) · `/bandeira vento todas 90 2` (vento para o norte, forte) · `/bandeira info`.

Comandos: `criar [x y z [giro]]`, `teste`, `lista`, `remover <id|todas>`, `vento <id|todas> <graus> <intensidade>`, `girar <id> <graus>`, `mover <id> x y z`, `ativar|desativar <id>`, `luz <0.3–1.5>` (brilho do sombreado fixo; ex.: 0.6 à noite), `parar|seguir`, `recarregar` (relê o flags.txt, o DFF e as texturas), `info`.

**Fixas no mapa:** coloque linhas em `SAMP/flags.txt`: `x y z [giro [dirVento [intensidade [interior]]]]`. `z` é o chão. Graus: 0 = leste (+X), 90 = norte (+Y). Ex.: `1234.5 -987.0 13.2 90 45 1.0`.

**Pelo servidor:** `#include <maximo_flag>` e `MaxFlag_Create(1000, x, y, z, giro, ventoDir, ventoInt, interior)`; `MaxFlag_SetWind(-1, 200.0, 2.0)` muda o vento de todas; `MaxFlag_SyncTo(playerid)` no `OnPlayerSpawn`. IDs do servidor ≥ 1000. Máximo de 24 por cliente. Não usa `CreateObject`.

## 3. Como a física funciona (`FlagPhysics.cpp`)

- **Malha de simulação:** grade retangular regular 13×41 (533 partículas) que cobre o tecido. A forma de pena do DFF é só a malha desenhada, ligada à grade por "pele" bilinear (`FlagModel::skin`). Motivo: na malha de pena as células ficam com 7 mm × 60 mm e as restrições ficavam mal condicionadas (medi estiramento de 3,8× na primeira versão; na grade regular caiu para ~1,1×).
- **Verlet** com passo **fixo de 1/60 s**, no máximo **4 passos por quadro**; o tempo que sobra é descartado (sem espiral em FPS baixo); `dt` real cortado em 0,1 s. Mesmo resultado de 20 a 144 FPS.
- **Restrições:** horizontais, verticais, diagonais (cisalhamento) e flexão fraca; 8 iterações (4 e sem flexão a mais de 35 m). Compressão resiste só 20% (tecido enruga em vez de ficar rígido). **Limite de estiramento** 6% (4 passes duros).
- **Pinos por máscara:** coluna junto ao mastro fixa; as 2 seguintes "macias" (puxam 12% e 4% por passo).
- **Vento:** direção + intensidade (0 = pende, 1 = ~6 m/s, até 4); rajadas senoidais; ondas transversais/verticais que **viajam do mastro à ponta** com fase própria por partícula e amplitude maior na ponta; semente por bandeira (cada uma balança diferente). Força aerodinâmica por triângulo sobre a velocidade **relativa** (pressão na normal + arrasto tangencial) → amortece sozinha.
- **Cata-vento:** o quadro gira em volta do mastro até o tecido apontar a favor do vento (mola amortecida, caminho curto), com leve oscilação.
- **Colisão:** cilindro do mastro e piso da base (só empurra para fora). Sem autocolisão e sem colisão com o mundo.
- **Guarda:** NaN/explosão (>8 m) → volta ao repouso e loga.
- **Local por cliente:** nada trafega por quadro; o servidor só manda criar/mover/vento.

## 4. Integração com o RenderWare (só símbolos confirmados)

Desenho em **RwIm3D**, a mesma API já usada em produção pela mangueira e pelo cartão 3D (`DefinedState()` em `libGTASA+0x6F4EC4`, `RwRenderStateSet`, `RwIm3DTransform/RenderIndexedPrimitive/End`, índice de 16 bits, câmera em `libGTASA+0xBBA8D0`). Chamado no `RenderEffects()` logo após o cartão 3D: `Flag_Update()` e `Flag_Render()`, na thread do jogo — **nenhuma thread paralela escreve em dados do RW**; os dados compartilhados (comando/RPC) ficam atrás de mutex.

Investiguei também `RpGeometryLock/Unlock` (exportados pela libGTASA; semântica conferida por desmontagem). **Não usei**: exigiria carregar o DFF como clump pelo carregador de modelos do jogo (ID de modelo novo, offsets de `CClumpModelInfo` não confirmados) — mais risco de quebrar o jogo e nada a ganhar frente ao Im3D que já roda no seu projeto.

- **Textura:** `CUtil::LoadTextureFromDB("mapasmaximo", "flag_maximo" / "flag_maximo_verso")` (Register/GetTexture/Unregister, refCount++). Se faltar, tenta 5× (a cada 3 s), loga e **não desenha** (não aparece mastro "pelado").
- **Frente/verso:** cada triângulo é desenhado **uma vez**, pelo lado voltado para a câmera → sem z-fighting e sem depender da convenção de culling.
- **Lotes** de ≤240 vértices (a mangueira usa 102) para ficar longe de qualquer limite do buffer Im3D. Medi 6 a 8 chamadas Im3D por bandeira no teste (mastro+base 2, tecido 3 a 5).
- **Sombreado fixo** (o Im3D do jogo não é iluminado): ambiente + luz fixa, dupla face. Sem ciclo dia/noite (não achei relógio do jogo confirmado no projeto) → use `/bandeira luz`.
- **LOD/ativa só quando:** habilitada, mesmo interior do jogador, a < 120 m (simula) / 150 m (desenha), fora do "bem atrás da câmera". Filtro bilinear sem mip (seguro), então pode cintilar de longe.
- Z-test e Z-write ligados (some atrás de prédios); sem transparência.

## 5. Textura e modelo

Frente e verso 1024×2048 (853 px/m), logo **sem esticar e sem espelhar** nos dois lados (o verso tem a borda espelhada e o logo normal), sem transparência. O TXD usa DXT1 (padrão mobile); o banco usa ETC1 com mipmaps (igual ao seu `mapasmaximo`). **Cor:** a spec pedia preto/dourado com o logo; o logo que você mandou é azul/ciano/branco, então fiz fundo preto com borda e detalhes **dourados** e o logo **original** no centro. Se preferir outra cor de borda é só dizer.

## 6. Testes que rodei (resultado em `testes/`)

| Teste | Resultado |
|---|---|
| Leitor do DFF: 3 partes, grade 13×41, 2 materiais, frente/verso espelhados; arquivo truncado/vazio rejeitado; fuzz com 4.000 DFFs corrompidos (ASan+UBSan) | ok, **sem crash** (achou 1 acesso desalinhado, corrigido) |
| Estabilidade a 10/20/30/60/90/144 FPS (20 s) | sem NaN, 0 resets; mesmo resultado de 20 a 144 FPS |
| Pinos da coluna do mastro | exatamente na posição (<0,1 mm) |
| Mastro não é atravessado | menor distância 0,126 m (limite 0,034) |
| Estiramento | pico ~10% com vento 1,0 (limite nominal 6%); 31% só com vento 4 (tempestade) |
| Vento 0 → 4 | 0: pende (0,24 m); 0,3: 0,76 m; 1: 1,13 m de 1,24 m; forma coerente |
| Mudança de vento 0°→90° e 0°→−172° | cata-vento convergiu (1,54 rad de 1,57; −3,03 de −3,00), pelo caminho curto |
| dt = 0/−1/NaN/5 s, vento NaN/∞/50 | sem crash, sem NaN |
| NaN injetado | guarda dispara, reset e recupera |
| Malha: cada triângulo do tecido desenhado exatamente 1×, repouso: câmera −Y só frente, +Y só verso, lotes ≤240 | ok |
| Cola (mock): flags.txt, comandos, RPC, interior diferente, atrás da câmera, >150 m, limite de 24, sem textura | ok |
| Custo | **0,39 ms por passo no PC (x86, -O2)**. No celular ARM64 deve ser de ~3 a 6× isso — é **estimativa, não medida**; o normal é 1 passo/quadro por bandeira próxima. Pior caso (4 passos) ~1,5 ms no PC |

Para repetir: `sh tools/bandeira/testes/build_e_rodar.sh` na raiz do repo.

## 7. Limites e o que olhar no aparelho

- **Primeiro teste:** `/bandeira teste`, depois `/bandeira info` e o `samp_log.txt` (`[FLAG_PHYSICS]`: modelo carregado, texturas carregadas, a cada 30 s ms/quadro). Se a bandeira não aparecer: veja se o log diz "modelo nao encontrado" (arquivo em `SAMP/models/`), "texturas ausentes" (banco `mapasmaximo` não atualizado) ou nada (hook não chamado).
- Pontos que **só o aparelho confirma**: orientação/UV das texturas (se vier de cabeça para baixo ou espelhada, me diga), cor/brilho (sombreado fixo), custo real, e se o filtro sem mip incomoda.
- FPS abaixo de ~15: o tecido anda em câmera lenta (limite de 4 passos por quadro, de propósito).
- Sem autocolisão do tecido, sem colisão com o mundo, sem vento sincronizado com o clima do servidor (use `MaxFlag_SetWind`).
- Banco: +2,8 MB no `.dat` e ~2,8 MB de memória de textura para as duas faces.

## 8. Mapa da spec (TXT) → onde está

DFF mastro+base+tecido subdividido curvo → `flag_maximo.dff` · TXD 1024×2048 frente/verso sem transparência → `flag_maximo.txd` + banco · `FlagPhysics.cpp/.h` Verlet, restrições H/V/diagonais, limite de estiramento, substeps fixos, amortecimento, vento senoidal com fase/amplitude variáveis, pinos por máscara → `FlagPhysics` · `CFlagObject/CFlagPhysics/CFlagManager` → `FlagObj` + `CFlagPhysics` + gerenciador em `Flag.cpp` (criar, posição, rotação, vento, ativar/desativar, remover, limpar) · update só quando carregada/ativa/próxima → `Flag_Update` · sem escrita paralela em buffers RW → seção 4 · simulação local por cliente → seção 3 · logs `[FLAG_PHYSICS]` → `Flag.cpp` · CMake → seção 1 · testes → seção 6.
