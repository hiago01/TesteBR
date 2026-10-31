package com.samp.mobile.launcher.util;

/**
 * Gesto secreto da tela inicial (Splash): 4 toques em qualquer lugar da tela, em sequencia rapida.
 * Cada toque deve vir ate STEP_MS depois do anterior e os 4 devem caber em TOTAL_MS; se passar do tempo, recomeca
 * (o toque atrasado conta como o 1o da nova sequencia). Java puro (sem Android) para ser testado na JVM.
 *
 * Uso: gesto = new SecretGesture(); em cada toque (ACTION_UP): if (gesto.onTap(x, y, largura, altura, agoraMs)) { ... }
 * (x, y, largura e altura sao ignorados: assinatura mantida para nao mexer no SplashActivity)
 */
public final class SecretGesture {
    public static final int TAPS = 4;            // ATIVAR: 4 toques
    public static final int TAPS_OFF = 3;        // DESLIGAR: 3 toques
    public static final long STEP_MS = 1200;     // maximo entre dois toques
    public static final long TOTAL_MS = 4000;    // maximo da sequencia toda

    private int count = 0;
    private long firstAt = 0, lastAt = 0;

    /** @return true quando o 4o toque acabou de acontecer (e reinicia o estado). */
    public boolean onTap(float x, float y, float w, float h, long nowMs) {
        return onTap(x, y, w, h, nowMs, TAPS);
    }

    /**
     * Igual ao anterior, mas com o numero de toques exigido: o chamador passa TAPS (4) quando o modo esta DESLIGADO
     * (4 toques ativam) e TAPS_OFF (3) quando esta LIGADO (3 toques desligam). Como o alvo muda conforme o estado, uma
     * sequencia de 4 toques nunca "passa" pelos 3 e desliga sem querer: com o modo desligado so o 4o toque conta.
     */
    public boolean onTap(float x, float y, float w, float h, long nowMs, int targetTaps) {
        if (count > 0 && (nowMs - lastAt > STEP_MS || nowMs - firstAt > TOTAL_MS)) count = 0;   // estourou o tempo
        if (count == 0) firstAt = nowMs;
        lastAt = nowMs;
        if (++count >= Math.max(1, targetTaps)) { count = 0; return true; }
        return false;
    }

    public void reset() { count = 0; }
}
