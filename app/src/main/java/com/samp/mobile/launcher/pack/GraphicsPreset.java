package com.samp.mobile.launcher.pack;

/**
 * Preset de qualidade grafica escolhido no launcher (0 = Minima .. 4 = Ultra). O launcher NAO escreve o gta_sa.set (binario do
 * jogo): grava no SAMP/settings.ini, secao [maximo], um PEDIDO ("preset_request" + "preset_request_id") e o cliente C++
 * (MaximoMenu) aplica o preset na 1a vez que o jogo abre e anota "preset_done_id" para nao reaplicar. Para reaplicar depois
 * (usuario escolheu outro preset), o launcher so incrementa o id.
 * Logica pura (sem Android) para ser testada no PC.
 */
public final class GraphicsPreset {
    private GraphicsPreset() {}

    public static final int MIN = 0, MAX = 4, DEFAULT = 0;     // padrao: Minima
    public static final String[] NAMES = {"Mínima", "Baixa", "Média", "Alta", "Ultra"};

    public static int clamp(int p) { return Math.max(MIN, Math.min(MAX, p)); }

    public static String name(int p) { return NAMES[clamp(p)]; }

    /** Devolve o texto do ini com o pedido gravado (cria a secao/chaves se faltarem; preserva o resto). */
    public static String request(String iniText, int preset, int requestId) {
        String t = IniFix.enforce(iniText, "maximo", "preset_request", String.valueOf(clamp(preset)));
        return IniFix.enforce(t, "maximo", "preset_request_id", String.valueOf(Math.max(1, requestId)));
    }
}
