# MÁXIMO OS — Arquitetura (Etapa 1: infraestrutura)

Tablet interno do Máximo RP, 100% nativo (C++), desenhado com o `ImGuiRenderer` já existente. Sem WebView, sem View Java, sem janela overlay Android, sem segunda engine gráfica.

## Camadas
```
jogo (AND_TouchEvent_hook / Voltar JNI / chat /tablet)
   └─ gui/samp_widgets/maximoos.{h,cpp}   ADAPTADOR (único código que conhece ImGui e o jogo)
         ├─ ImGuiCanvas : Canvas  → ImGuiRenderer
         └─ OSManager (gui/maximoos/, cabeçalhos puros, testados no PC)
               ├─ InputManager        captura/liberação de toque, fila thread-safe
               ├─ GestureTracker      Down/Move/Up → Tap, LongPress, Drag
               ├─ ResolutionManager   Metrics: moldura, tela, escala, fonte mínima, grade
               ├─ AppRegistry         lista de apps (AppInfo + factory)
               ├─ AppManager          ciclo de vida, 1 app em primeiro plano, recentes (máx. 6)
               ├─ HomeScreen / StatusBar / NotificationCenter / painéis (recentes, notificações)
               ├─ Navigation          NavStack + Transition (fade 160 ms)
               ├─ Components.h        Panel, Button, AppIcon, Toggle, Header, ScrollView
               └─ Theme.h             cores, fontes lógicas, ícones por primitivas
```
## Regras
- **Toque:** com o tablet aberto `HandleRawTouch` consome 100% dos eventos (HUD, câmera, joystick e SA-MP não recebem nada). Dedo iniciado com o tablet aberto fica reivindicado até UP/CANCEL, mesmo que feche no meio do gesto. Ao abrir, um *flush* solta os dedos que o jogo já via pressionados (mesmo esquema do MaximoMenu).
- **Threads:** `Open/Close/Toggle/OnBackPressed/PostNotification` só gravam pedidos atômicos; o efeito ocorre na thread de desenho (`draw`).
- **Resolução:** nada usa pixels fixos. Tudo vem de `Metrics` (tela de projeto 1120×720 lu, escala `s = tela.w/1120`, moldura 1,40:1 com margem 3%/4%, fonte mínima 11 px). Texto sempre cortado com "..." (UTF-8 seguro).
- **Navegação:** Voltar = (1) fecha painel aberto, (2) o app trata internamente (`App::onBack`), (3) volta à tela inicial, (4) na tela inicial fecha o tablet. Home = tela inicial (na inicial, fecha). Recentes = cartões; ✕ fecha o app; "Fechar todos". Toque fora da moldura fecha. Android Voltar usa o mesmo caminho.
- **Sem dados falsos:** nenhum RPC, nenhum dado de servidor. Apps previstos são `PlaceholderApp` ("Em breve"). Barra de status: relógio do aparelho + sinal decorativo (4 barras fixas); bateria oculta (sem fonte real).

## Como criar um app novo
1. Classe derivada de `MaximoOS::App` (draw/onTouch/onBack…) usando só `Canvas`, `Metrics` e `Components.h`.
2. `AppInfo` com `id`, `title`, `Icon`, `accent`, `factory`; registrar em `OSManager::registerBuiltinApps()` (ou `registry().add`).
3. Para pedir fechamento: `finish()`. Notificar: `MaximoOSWidget::PostNotification(...)`.
4. Componente novo → criar em `Components.h`, nunca dentro do app.

## Ids/RPC
Nenhum. Quando existirem, ficarão em um único arquivo de registro de handlers (a criar na etapa 2); a etapa 1 não inventa ids.

## Arquivos
Novos: `gui/maximoos/*.h` (Types, Canvas, ResolutionManager, GestureTracker, InputManager, Navigation, Theme, Components, AppModel, NotificationCenter, StatusBar, HomeScreen, BuiltinApps, OSManager), `gui/samp_widgets/maximoos.{h,cpp}`, `testes/maximoos/*`.
Alterados (backups em `backup_originais/`): `gui/gui.h`, `gui/gui.cpp` (widget filho), `game/hooks.cpp` (captura de toque), `main.cpp` (Voltar), `gui/samp_widgets/chat.cpp` (`/tablet`, `/maximoos`).
O CMake usa `GLOB_RECURSE`: os novos .cpp entram sozinhos.
