# Cartão de identidade 3D — Máximo RP

**Status: NÃO compilado para arm64-v8a e NÃO testado em aparelho** (este ambiente não tem NDK/CMake Android).
O que foi validado: `g++ -fsyntax-only` (host, com stubs de JNI/GLES) dos arquivos `IdCard3D.cpp`, `scriptrpc.cpp`,
`playerpool.cpp` e `chat.cpp` contra os headers reais do projeto — sem erros (o mesmo teste passa no `Hose.cpp` original).
`hooks.cpp` não passou nesse teste por falta de GLES nos stubs (erros só em código antigo de GL, não na linha nova).
Rode o build real (`./gradlew assembleDebug`, ou Android Studio > Refresh Linked C++ Projects) antes de confiar.

## Como testar (no jogo, conectado a um servidor)
`/cartao teste` (Civil no seu jogador) · `/cartao teste medico` · `/cartao teste militar 12` (no jogador 12) ·
`/cartao anim` (repete a subida) · `/cartao limpar` · `/cartao dist|altura|largura <n>` · `/cartao los on|off` · `/cartao proprio on|off`.
Categorias: civil policial delegado medico administrador militar (ou 0–5). Logs: `[IDCARD]` no samp_log.txt.

## Animação
Na **primeira** vez que o cartão de um jogador é desenhado, ele sobe 0,90 m (ease-out cúbico, 0,85 s) com fade-in (0,30 s)
e **para** na altura final. Não repete ao sair/entrar da distância nem ao mudar o cargo; `/cartao anim` reinicia (teste).
Parâmetros: `riseDist`, `riseTime`, `fadeTime` em `Config` (IdCard3D.cpp).

## O que foi inspecionado (e reutilizado)
- Ciclo de render 3D: `RenderEffects()` em `game/hooks.cpp` (onde o `Hose_Render()` já desenha com `RwIm3D`).
- Contexto gráfico: `RwIm3DTransform/RenderIndexedPrimitive/End` (wrappers em `game/RW/rwcore.cpp`, símbolos conferidos no `libGTASA.so`), `RwRenderStateSet`, `DefinedState()` (+0x6F4EC4, como no Hose).
- Posição da cabeça: `CPlayerPed::GetBonePosition(8)` (igual ao `CPlayerTags`); câmera: `TheCamera` em `libGTASA+0xBBA8D0`.
- Jogadores/nome/ID: `CPlayerPool` (`GetAt`, `GetPlayerName`, `GetLocalPlayerID`). Visibilidade: `GetDistanceFromCamera` + `CWorld::GetIsLineOfSightClear` (mesma chamada do `CPlayerTags`).
- Texturas: PNG embutido → `stb_image` → `RwImage` → `RwRaster` (mesmo caminho do atlas do HUD). Nenhum offset/endereço novo foi inventado.

## Arquivos
Novos: `game/IdCard3D.{h,cpp}`, `game/idcard_assets.h` (gerado), `servidor/maximo_idcard.inc`, `tools_make_idcards.py`.
Modificados (CMake usa GLOB: nada a editar no CMakeLists, só *Refresh Linked C++ Projects*):
`vendor/raknet/SAMP/SAMPRPC.{cpp,h}` (RPC 202), `net/scriptrpc.cpp` (handler, registro, unregister+reset), `net/playerpool.cpp` (limpa ao jogador sair),
`game/hooks.cpp` (1 linha em `RenderEffects`), `gui/samp_widgets/chat.cpp` (`/cartao`). Veja `idcard_3d.patch`.

## Dados do servidor (protocolo)
Nome e ID vêm do pool de jogadores. **Cargo e nº funcional não existiam em nenhum RPC atual**, então foi preciso um RPC novo, no mesmo padrão do 200/201:
**RPC 202** `uint16 id | uint8 flags | uint8 categoria | uint8 len+cargo | uint8 len+nº funcional`. Confirme que 202 está livre no seu servidor.

## Texturas
5 banners recortados da prancha (Civil, Policial, Delegado, Médico, Administrador) + Militar recortado do cartão verde.
No Militar, os dados fictícios (Player / ID 27 / N/A) foram **apagados** da arte. Largura 512 px, proporção original, cantos arredondados, PNG paletizado.
Não usei as artes "REDUTO RP" (outra marca). Texto dinâmico (Nome (ID) / Cargo | Nº) é uma placa gerada em runtime abaixo do banner, na fonte `<storage>/fonts/arial_bold.ttf`; se a fonte não existir, só o banner aparece.

## Limitações / não validado
- Não compilado/testado em aparelho: flag `rwIM3D_VERTEXUV` e estados de blend em Im3D translúcido seguem o padrão do RW, mas não foram vistos rodando.
- Orientação do billboard calculada a partir da posição da câmera (Z do mundo = vertical), não da matriz da câmera; sem roll.
- Um cartão por jogador; sem cache de placas além do raster por jogador; rasters são destruídos na thread de render.
- Não oculta em veículo/spectate; `proprio` (cartão em si mesmo) fica desligado por padrão.
