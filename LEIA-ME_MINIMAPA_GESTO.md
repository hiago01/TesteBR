# 1) Loop de 2 s do ícone do botão de pulo
Só rodava se o ícone falhasse ao carregar (uma leitura do texdb a cada 2 s, para sempre). Agora: no máximo 3 tentativas (espaçadas 3 s) e desiste — depois não há mais NENHUMA leitura em disco durante o jogo (fica o botão simples "PULO"). Se o ícone carregar de primeira (caso normal) não há loop nenhum. (O botão P2 de passageiro, que já era seu, tem o mesmo padrão de retry; não mexi.)

# 2) Tocar no minimapa abre o /mapa
- sprite2d.cpp guarda o retângulo do disco do radar a cada frame desenhado; hooks.cpp (MiniMapTouch) trata o toque.
- Conta como toque: soltar dentro do minimapa, mover < 40 px, durar < 600 ms. Arrasto e toque longo não abrem. O dedo que começa no minimapa não gira a câmera.
- Não funciona com diálogo/teclado/lista/spawn/splash abertos; com o mapa/menu/inventário abertos esses já consomem os toques antes.
- Lógica pura testada: testes/minimaptap_check.cpp

# 3) Gesto secreto (launcher, tela Splash)
- 4 toques rápidos ATIVAM o modo sem verificação; com ele ligado, 3 toques DESLIGAM (o alvo muda conforme o estado, então 4 toques nunca "passam" por 3 e desligam sem querer).
- Ao ativar, o launcher espera 3,5 s antes de abrir o menu, para dar tempo dos 3 toques.
- endpoints.xml: secret_gesture_enabled = true. O modo vale só na abertura atual e NUNCA fica gravado (corrige o bug antigo de pular o download nas aberturas seguintes).
- Testes JVM: SecretGestureTest (11 ok).
NAO verificado: compilação Android e comportamento no aparelho (posição real do retângulo do minimapa; se o toque do minimapa conflita com outro botão).
