# Relatório — Máximo OS, etapa 1

## Entregue
Infraestrutura completa (ver MAXIMO_OS_ARCHITECTURE.md): UIManager→OSManager/adaptador, InputManager, ResolutionManager, AppManager, AppRegistry, HomeScreen, StatusBar, navegação, NotificationCenter, recentes, 10 apps placeholder + 1 app de teste, comando `/tablet` e `/maximoos`.
Dependências: nenhuma nova (usa ImGui e ImGuiRenderer do projeto, C++17).

## Testes
**Executados no PC (g++ -std=c++17, `testes/maximoos/test_maximoos.cpp`): 539 verificações, 0 falhas**
- Layout em 10 resoluções (800×480 … 3200×1440, retrato incluso): moldura/tela/conteúdo dentro do celular, proporção 1,40, fonte ≥ 11 px, grade dentro da área.
- Gestos (tap, arrasto, long-press, cancel, segundo dedo ignorado), InputManager (captura, flush único, fechar no meio do gesto, limite 256), AppRegistry (id duplicado/sem factory), AppManager (ciclo de vida, reaproveitar instância, limite de recentes, closeAll), NavStack/Transition, NotificationCenter, ScrollView, FitText UTF-8.
- Fluxo completo com MockCanvas: abrir app pela grade, Home, Recentes, sino, Voltar em cascata, fechar, toque fora, nenhum evento vaza com o tablet aberto, clips balanceados.
- Renderização aproximada (PIL, fonte DejaVu) conferida visualmente: home, app de teste, placeholder, recentes.

**NÃO verificado**
- Compilação NDK/Gradle de qualquer arquivo (adaptador `maximoos.cpp`, edições em gui/hooks/main/chat nunca foram compilados; só os cabeçalhos puros foram).
- Aparência real com a fonte/ImGui do jogo (tamanhos de texto, círculos por polígono, clip), desempenho/FPS, memória.
- Captura de toque, flush, botão Voltar e `/tablet` no aparelho; interação com HUD/câmera/SA-MP; convivência com MaximoMenu/mapa/inventário abertos.
- Rotação/mudança de resolução em execução; vários dedos; rolagem com inércia no dedo real.

## Limitações conhecidas
- Clip é retangular (cantos arredondados da tela não recortam o conteúdo).
- Sinal da barra de status é decorativo; bateria oculta.
- `Open()` é ignorado se o MaximoMenu estiver aberto.
- Transição só com fade (sem deslocamento).
