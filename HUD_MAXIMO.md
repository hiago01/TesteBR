# HUD Maximo RP - como testar

1. Copie os arquivos deste pacote por cima do projeto (mesma estrutura de pastas) OU aplique `hud_maximo.patch`
   (na raiz do projeto: `patch -p1 < hud_maximo.patch`).
2. Android Studio: File > Sync / "Refresh Linked C++ Projects" (o CMake usa GLOB; sem isso os arquivos novos nao entram). Rode um build limpo.
3. No jogo: `/hud teste` aplica os valores do print de referencia. Outros: `/hud on|off|reset`, `/hud escala 1.2`,
   `/hud set fome|sede|energia 0-100`, `/hud set dinheiro|extra N`, `/hud set pessoa A [MAX]`.
4. Servidor: inclua `servidor/maximo_hud.inc` e chame `MaxHud_Send(...)` (RPC 201).
5. Logs: procure `[HUD]` e `[PATCH] dinheiro nativo do HUD` no samp_log.txt.

Chaves para desligar: `kHideNativeHealthHud` (hooks.cpp), `kHideNativeMoney` (patches.cpp).
Atlas (logo + icones): `make_atlas.py` regenera `hudatlas.h` (hud_atlas.png e o resultado).
NAO foi compilado para Android nem testado em aparelho.
