package com.maximo.roleplay.launcher.util;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Modo de download escolhido pelo jogador na tela "Atualizacao obrigatoria".
 *  - LEGACY (padrao, nada escolhido): comportamento anterior (arquivo a arquivo, com tudo).
 *  - SAFE   (DOWNLOAD SEGURO): so o jogo; as skins (texdb/skins e SAMP/skins) ficam de fora. Arquivo a arquivo.
 *  - FULL   (DOWNLOAD COMPLETO): tudo, inclusive skins, pelos pacotes (packs) do maximo_config.json.
 * A escolha fica gravada para que as proximas verificacoes sigam o mesmo criterio (sem pedir skins de novo no modo seguro).
 */
public final class DownloadMode {
    public static final String LEGACY = "legacy";
    public static final String SAFE = "safe";
    public static final String FULL = "full";

    private static final String PREFS = "com.maximo.roleplay";
    private static final String KEY = "download_mode";

    private DownloadMode() {}

    public static String get(Context c) {
        try {
            String v = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, LEGACY);
            return SAFE.equals(v) || FULL.equals(v) ? v : LEGACY;
        } catch (Throwable t) {
            return LEGACY;
        }
    }

    /** commit() (nao apply): o servico le logo em seguida, em outro processo/thread. */
    public static void set(Context c, String mode) {
        try {
            SharedPreferences.Editor e = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
            e.putString(KEY, SAFE.equals(mode) || FULL.equals(mode) ? mode : LEGACY);
            e.commit();
        } catch (Throwable ignored) { }
    }

    /** Caminho (relativo a files/) que pertence as skins: texdb/skins/... e SAMP/skins/... (sem diferenciar caixa). */
    public static boolean isSkinPath(String relPath) {
        return com.maximo.roleplay.launcher.pack.GameFilePolicy.isSkin(relPath);
    }
}
