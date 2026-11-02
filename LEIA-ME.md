# CORRER some enquanto o LOCK esta ligado

Mudanca em gui/samp_widgets/moderncontrols.cpp (draw), 1 linha. Se o seu arquivo for diferente do meu, aplique a mao:

    // antes
    g_ctl.disabledMask = 0u;
    // depois
    g_ctl.disabledMask = (!editing && g_runLock.load()) ? (1u << B_RUN) : 0u;

- LOCK ligado: o botao CORRER nao e desenhado e nao recebe toque (a corrida ja e mantida pelo LOCK).
- LOCK desligado: o CORRER volta. No editor de HUD ele continua visivel para poder ser ajustado.
- Teste novo em test_controles.test (botao desativado nao recebe toque). Testes de PC: 326/326 e 29/29 OK.
NAO verificado: compilacao NDK/Gradle e comportamento no aparelho.
