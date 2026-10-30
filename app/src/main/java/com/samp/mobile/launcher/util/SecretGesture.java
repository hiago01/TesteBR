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
    public static final int TAPS = 4;
    public static final long STEP_MS = 1200;     // maximo entre dois toques
    public static final long TOTAL_MS = 4000;    // maximo da sequencia toda

    private int count = 0;
    private long firstAt = 0, lastAt = 0;

    /** @return true quando o 4o toque acabou de acontecer (e reinicia o estado). */
    public boolean onTap(float x, float y, float w, float h, long nowMs) {
        if (count > 0 && (nowMs - lastAt > STEP_MS || nowMs - firstAt > TOTAL_MS)) count = 0;   // estourou o tempo
        if (count == 0) firstAt = nowMs;
        lastAt = nowMs;
        if (++count >= TAPS) { count = 0; return true; }
        return false;
    }

    public void reset() { count = 0; }
}
