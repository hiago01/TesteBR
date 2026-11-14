package com.maximo.roleplay.launcher.pack;

import java.util.Locale;

/**
 * Unica regra que o aplicativo mantem sobre a lista de arquivos: texturas de GPU. O resto (o que entra na lista e
 * o que e conferido) e decidido no painel do servidor (campos "verify" e a propria lista).
 */
public final class GameFilePolicy {
    private GameFilePolicy() {}

    /**
     * 0 = comum; 1 = dxt; 2 = etc; 3 = pvr.
     * Caminhos com "samp", "player" ou "menu" sao SEMPRE comuns (mesmo contendo ".dxt."), sem diferenciar caixa.
     */
    public static int gpuOf(String relPath) {
        String l = relPath.toLowerCase(Locale.ROOT);
        if (l.contains("samp") || l.contains("player") || l.contains("menu")) return 0;
        if (l.contains(".dxt.")) return 1;
        if (l.contains(".etc.")) return 2;
        if (l.contains(".pvr.")) return 3;
        return 0;
    }

    /** O arquivo e usado em um aparelho com esta GPU (1=dxt 2=etc 3=pvr)? Arquivos comuns sempre. */
    public static boolean wantedForDevice(String relPath, int deviceGpu) {
        int g = gpuOf(relPath);
        return g == 0 || g == deviceGpu;
    }
}
