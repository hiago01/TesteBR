# DFF com mais de 65.535 vértices

## Resumo

O jogo (libGTASA arm64 2.10) **não consegue** carregar uma geometria com 65.536 vértices ou mais, e não existe caminho de
índices de 32 bits no renderizador. Então a solução é **dividir** a geometria nos bytes do DFF, antes do `RpClumpStreamRead`
(`DffSplit.h`). DFF com todas as geometrias ≤ 65.535 vértices (todos os antigos) sai **idêntico, byte a byte**.

## Auditoria (endereços = vaddr na libGTASA.so arm64 do projeto; tudo lido do desassembly)

| Item | Limite / comportamento | Onde |
|---|---|---|
| `RpGeometryCreate(numVerts, numTris, fmt)` | `lsr w8,w22,#16 / cbnz`: `numVertices >= 65536` seta erro e **devolve NULL** (o DFF não carrega). Limite útil = **65.535**. `numTriangles < 0` também recusa. | `0x2bc7a8` (`0x2bc7d0..0x2bc818`) |
| Triângulos da Struct (`RpTriangle`) | 4 × `uint16` (`strh`), 8 bytes por triângulo. Só enxerga índices ≤ 65.535. | `RpGeometryStreamRead` `0x2bd4f8` (`0x2bd6d0..0x2bd6e0`) |
| Contadores `numVertices`/`numTriangles` | `uint32` no arquivo; o limite vem do `RpGeometryCreate` acima | idem |
| Morph target | lê 0x18 bytes (esfera + hasVertices + hasNormals) + `numVertices*12` por array | `RpGeometryStreamRead` |
| Bin Mesh PLG (disco) | índices **uint32** no arquivo (o `DffPreflight` já lia assim) | `_rpMeshRead` `0x2bff1c` |
| Bin Mesh PLG (memória) | lê em blocos de `0x100` e grava cada índice com **STRH / XTN = trunca para 16 bits, sem checar faixa**; memória `totalIndices*2` | `0x2c0174..0x2c01a8`, alocação em `0x2bff84` |
| `RpMeshHeader.numMeshes` | `uint16` (`strh [x19,#4]`) | `0x2bfffc` |
| `RpMesh` em memória | 32 bytes por mesh (`add x21,x21,#0x20`), cabeçalho 0x18; o `RpMesh` do `rpworld.h` do projeto tem 24 — **não use esse header para mexer na malha carregada** (o `typedef RwUInt32 RxVertexIndex` de `rwplcore.h` também não reflete o engine: em memória os índices são `uint16`) | `_rpMeshRead` |
| Desenho | `RQ_Command_rqDrawIndexed` termina em `mov w2,#0x1403` (**GL_UNSIGNED_SHORT fixo**); `_rpMeshRead` cria o buffer GL com `0x1403`. O libGTASA não consulta `GL_OES_element_index_uint`. | `0x260f84`/`0x260fac`, `0x2c0200` |
| Camada de emulação GL | `emu_ArraysIndices` sabe calcular tamanho para 0x1401/0x1403/0x1405, mas **nenhum caminho de desenho do RenderWare passa 0x1405** | `emu_ArraysIndices` |
| Skin PLG | `numBones`/`numUsedBones` `uint8`; vértices em arrays por vértice (4 índices u8 + 4 pesos float) | formato (conferido por tamanho e pelo `DffPreflight`) |

Conclusão: **índices de 32 bits não são suportados**; mudar para isso exigiria trocar o tipo em dezenas de pontos nativos
(os callbacks de instância de prédio/carro/skin, `rqDrawIndexed`, `_rpMeshRead`…) sem como validar. Por isso: divisão.

## O que o `DffSplit` faz

1. Lê o DFF (Clump → GeometryList → Geometry → Struct/MaterialList/Extension) com checagem de limites em todo chunk.
2. Se alguma geometria passa de 65.535 vértices, lê os triângulos do **Bin Mesh PLG** (é o que o jogo desenha; a Struct só tem 16 bits)
   e os agrupa em partes de ≤ 65.535 vértices, na ordem original dos meshes (material). Só os vértices de fronteira são duplicados.
3. Cada parte vira uma Geometry completa: Struct (flags, **todas** as UVs, cor pré-calculada, posições, normais, triângulos com material),
   esfera de contorno **recalculada**, MaterialList **idêntica** (índices de material preservados), Bin Mesh novo, Skin PLG
   (índices de osso e pesos remapeados; matrizes/ossos usados/cauda intactos), Night Vertex Colors remapeado, 2dfx só na parte 0.
4. Cada Atomic que usava a geometria ganha um Atomic por parte (mesmo frame, flags e extensões). Recalcula todos os tamanhos,
   `numAtomics` e `numGeometries`. Tri-strip é convertido em lista (sentido preservado).
5. Vértices que nenhum triângulo usa são descartados nas geometrias divididas (cai em `droppedUnused`).

Recusa com mensagem (em vez de gerar lixo): geometria *native*, mais de 1 morph target, plugin Breakable (`0x253F2FD`), Skin com
split/RLE (`numMeshes`/`numRLE` ≠ 0), Bin Mesh com índice ≥ `numVertices`, layout de Skin/Night inesperado.

## ONDE vale (importante)

A divisão cria **vários atomics no mesmo frame**. O jogo só os mantém em modelos cujo `SetClump` guarda o clump inteiro:

* **Vale:** clump (5), veículo (6), ped (7). `CClumpModelInfo::SetClump` (`0x45b7f8`) percorre todos com `RpClumpForAllAtomics`.
* **Não vale:** objeto/prédio/time/LOD (`CAtomicModelInfo`). `CFileLoader::LoadAtomicFile` (`0x4d046c`) liga **cada atomic a um modelo pelo
  nome do frame** (`FindRelatedModelInfoCB`, `0x5546c8`) e `CAtomicModelInfo::SetAtomic` (`0x45aa6c`) faz `str x20,[x19,#0x40]`,
  **substituindo** o anterior: só a última parte apareceria. Nesses o `CustomDff` recusa e usa o modelo original do IMG
  (`[DFF] modelo N: ... guarda 1 atomic so ...`).
* **Arma (4):** não verificado, tratado como "não vale".
* **skin.dff (CustomSkin):** o `DffPreflight` agora recusa geometria > 65.535 com mensagem clara. O código de skin assume 1 geometria,
  então ela não é dividida sozinha; divida antes com `tools/dffsplit_cli` e confira o resultado no aparelho.

Para objeto/prédio com mais de 65.535 vértices a saída é partir o mapa em **modelos diferentes** (um id por parte + uma instância de IPL
por parte); isso não cabe dentro de um único DFF.

## Como usar

* **Automático:** coloque o DFF em `texdb/DFF/<nome-do-modelo>.dff` (como já era). O log mostra
  `[DFF] <arquivo>: DIVIDIDO (geometrias 1->2 atomics 1->2 verts 100000->100066 ... maxIdx 99999->65534 ...)`.
* **Desligar:** crie o arquivo vazio `SAMP/dff_split_off.on` (geometria > 65.535 passa a ser recusada em vez de dividida).
* **Offline:** `g++ -std=c++17 -O2 -Iapp/src/main/cpp/samp/game tools/dffsplit_cli.cpp -o dffsplit_cli && ./dffsplit_cli in.dff out.dff`

## Testes

`tools/dffsplit_test.cpp` (resultado em `testes/5_dffsplit.txt`): 10.000, 40.000, 65.000, 65.535, 65.536, 100.000 e 150.000 vértices;
skin + night colors + 2 UVs + cor pré-calculada; tri-strip; clump com 3 geometrias e 2 atomics na grande; vértices sem uso;
4 recusas esperadas; fuzz de 1.500 DFFs corrompidos; tudo também sob AddressSanitizer/UBSan. Simula o `RpGeometryCreate` e o `strh` do
`_rpMeshRead`: na entrada de 100.000 vértices 103.389 de 299.994 índices seriam corrompidos; na saída, zero.

**Não medido:** renderização no aparelho. O `allowSplit` por tipo de modelo, os hooks e a integração em `ConvertLoose` não puderam ser
compilados com o NDK aqui (o `CustomDff.h` e o `DffSplit.h` foram compilados no host; o `CustomSkinStream.cpp` só foi conferido por leitura).
