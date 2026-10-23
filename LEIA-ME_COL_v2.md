# COL v2 — replace direto do countrye_10.col (CE_brewery)

## Instalar (só 2 arquivos; Streaming.cpp e game.cpp do patch v1 ficam como estão)
Copie por cima, em `app/src/main/cpp/samp/game/`:
- `CustomCol.h`        (reescrito)
- `CustomColParse.h`   (ganhou validação profunda)

Coloque o `countrye_10.col` novo em `files/SAMP/col/` (ou `files/SAMP/`, ou `files/texdb/`).
Recompile e rode. Para testar o original: crie o arquivo vazio `files/SAMP/col_off.on`.

## O que estava errado no patch v1 (provado no libGTASA.so arm64 do projeto)
1. **Ponto de troca errado.** O jogo carrega o countrye_10 do IMG por
   `CStreaming::ConvertBufferToObject → CColStore::LoadCol(int slot, uchar* buf, int size) → CFileLoader::LoadCollisionFile[FirstTime](buf,size,slot)`.
   O v1 nunca tocou nisso.
2. **Slot errado.** O v1 chamava `LoadCollisionFile(buf,size,0)`. O loader procura o nome só dentro do intervalo de
   modelos do slot (`CModelInfo::GetModelInfo(nome,min,max)`), então o slot 0 não acha `CE_brewery`.
3. **Era opt-in e de uma vez só.** Só rodava com `col_reload.on`, uma vez, em `CGame::Initialize`. O streaming
   recarrega o slot do IMG depois (RemoveCol/LoadCol) e a colisão original voltava.
4. O `CINFO.BIN` guard estava certo no conceito (a ordem de hooks está ok: `InstallHooks` roda antes do
   `CGame::Initialise` original, que chama `startCache`), mas sem o replace não tinha efeito.

## Seu arquivo novo NÃO é inválido
`countrye_10.col`: 38 entradas COL3, mesmos nomes do original (menos 1 de padding), CE_brewery 41 408 bytes /
3 035 triângulos / 2 247 vértices / 126 face groups. Validado: offsets dentro do size, índices < nº de vértices,
sem triângulos degenerados, face groups cobrindo 0..3034, bounds do cabeçalho = bounds reais dos vértices.
Diferenças em relação ao original (todas legais): sem caixas (48 → 0), materiais só 0 (antes 0/7/73), luz 255,
bounding sphere raio 228 → 177. As outras 37 entradas diferem só em 6 bytes de lixo após o nome (nome tem 22 bytes).
Maior que o original não é problema por si: o buffer do IMG (44 → 60 setores) é tratado pelo streaming como "big model" se passar do buffer.

## Como o v2 funciona
Hook em `CColStore::LoadCol` (símbolo exportado `_ZN9CColStore7LoadColEiPhi`). Se o buffer do IMG é o countrye_10
(1ª entrada com o mesmo nome + ≥ metade dos nomes em comum), entrega ao loader nativo uma **cópia** do seu arquivo,
**com o mesmo slot**. Todo o resto nativo roda normal (validação de versão, AllocateData, SetColModel,
IncludeModelIndex, addCacheCol). O arquivo só é entregue se passar em Parse + ValidateAll; senão o IMG segue e o motivo vai pro log.
Reedita o .col → vale na próxima vez que o slot for streamado (checa mtime a cada 2 s).

## O que procurar no samp_log.txt
```
[COL] .../countrye_10.col: 122796 bytes, 38 entradas, estrutura=OK, validacao profunda=OK
[COL] REPLACE slot=N: buffer do IMG (90112 bytes) -> ... (122796 bytes)
[COL] REPLACE slot=N: LoadCol retornou 1 | triangulos esperados (arquivo)=3035, no jogo=3035 -> DADOS PERSONALIZADOS EM USO
[CINFO] ... depois do startCache: m_iCacheState=.. m_iNumColItems=..
```
Se aparecer `DIVERGENTE`, ou nenhuma linha `REPLACE` depois de chegar perto do celeiro, me mande as linhas `[COL]` e `[CINFO]`.

## Testes feitos / NÃO feitos
- Feito: parser + validação nos seus 2 arquivos reais; simulação no host do hook (sem arquivo, arquivo válido, outro .col, arquivo cortado, `col_off.on`) — tudo OK.
- **Não feito:** compilar para ARM64 e rodar no aparelho (sem NDK/aparelho aqui).
- **Não provado:** a causa exata da quebra com o arquivo dentro do gta3.img. Não tive disassembler; só mapeei chamadas (BL) e assinaturas.
  Hipóteses ainda abertas: CINFO.BIN velho com os bounds do original; limite no tamanho da entrada do IMG. O v2 contorna as duas
  (não depende do IMG e invalida o CINFO). Se ainda quebrar COM o `[COL] ... DADOS PERSONALIZADOS EM USO`, o problema é físico/geométrico, não de carregamento.
- Aviso: o loader por **arquivo** (`LoadCollisionFile(const char*,uchar)`, usado para COLFILE do gta.dat) tem um `tempBuffer` estático de 32 768 bytes
  (símbolo do libGTASA). CE_brewery tem 41 408 bytes. Por isso **não** coloque este .col no gta.dat; o caminho do IMG/streaming não usa esse buffer.
