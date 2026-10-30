package com.samp.mobile.launcher.util;

/**
 * Gesto secreto da tela inicial (Splash): tocar os 4 CANTOS da tela, nesta ordem e rapido:
 * superior esquerdo -> superior direito -> inferior direito -> inferior esquerdo.
 * Cada toque deve cair numa "zona de canto" (por padrao 18% da largura/altura) e vir ate STEP_MS depois do anterior;
 * a sequencia inteira tem de caber em TOTAL_MS. Toque fora do esperado reinicia (e, se for o canto superior esquerdo,
 * ja conta como o 1o passo). Java puro (sem Android) para ser testado na JVM.
 *
 * Uso: gesto = new SecretGesture(); em cada toque (ACTION_UP): if (gesto.onTap(x, y, largura, altura, agoraMs)) { ... }
 */
public final class SecretGesture {
    public static final long STEP_MS = 3000;     // maximo entre dois toques
    public static final long TOTAL_MS = 8000;    // maximo da sequencia toda
    public static final float ZONE = 0.18f;      // fracao da tela que conta como "canto"

    // 0 = sup. esquerdo, 1 = sup. direito, 2 = inf. direito, 3 = inf. esquerdo
    private static final int[] ORDER = {0, 1, 2, 3};

    private int step = 0;
    private long firstAt = 0, lastAt = 0;

    /** Canto (0..3) em que o toque caiu, ou -1 se no meio. */
    static int corner(float x, float y, float w, float h) {
        if (w <= 0 || h <= 0) return -1;
        final boolean left = x <= w * ZONE, right = x >= w * (1f - ZONE);
        final boolean top = y <= h * ZONE, bottom = y >= h * (1f - ZONE);
        if (top && left) return 0;
        if (top && right) return 1;
        if (bottom && right) return 2;
        if (bottom && left) return 3;
        return -1;
    }

    /** @return true quando a sequencia acabou de ser completada (e reinicia o estado). */
    public boolean onTap(float x, float y, float w, float h, long nowMs) {
        final int c = corner(x, y, w, h);
        if (step > 0 && (nowMs - lastAt > STEP_MS || nowMs - firstAt > TOTAL_MS)) step = 0;   // estourou o tempo
        if (c == ORDER[step]) {
            if (step == 0) firstAt = nowMs;
            lastAt = nowMs;
            step++;
            if (step == ORDER.length) { step = 0; return true; }
            return false;
        }
        // errou: recomeca (se foi o canto inicial, ja e o 1o passo)
        if (c == ORDER[0]) { step = 1; firstAt = lastAt = nowMs; } else step = 0;
        return false;
    }

    public void reset() { step = 0; }
}
