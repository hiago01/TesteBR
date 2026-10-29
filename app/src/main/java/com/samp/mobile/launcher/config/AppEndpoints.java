package com.samp.mobile.launcher.config;

import android.content.Context;

import com.samp.mobile.R;

/**
 * Ponto unico para os enderecos HTTP do launcher. Os valores vem de res/values/endpoints.xml
 * (pode ser sobrescrito por build type/flavor), nunca de literais espalhados no codigo.
 */
public final class AppEndpoints {

    /** Timeout (ms) de conexao e de leitura para toda requisicao HTTP do launcher. */
    public static final int HTTP_TIMEOUT_MS = 8000;
    /** Tentativas extras apos a primeira falha (total = 1 + HTTP_MAX_RETRIES). */
    public static final int HTTP_MAX_RETRIES = 2;

    private AppEndpoints() {}

    private static String base(Context c) {
        String b = c.getString(R.string.endpoint_base_url).trim();
        while (b.endsWith("/")) b = b.substring(0, b.length() - 1);
        return b;
    }

    public static String serversUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_servers_path).trim();
    }

    public static String clientConfigUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_client_config_path).trim();
    }

    /** URL completa de um arquivo do jogo, a partir do "path" relativo do client_config. */
    public static String fileUrl(Context c, String relativePath) {
        String p = c.getString(R.string.endpoint_files_path).trim();
        if (!p.endsWith("/")) p = p + "/";
        String rel = relativePath.startsWith("/") ? relativePath.substring(1) : relativePath;
        return base(c) + p + rel;
    }

    /**
     * URL final de um pacote ZIP do client_config ("packs"[].url): absoluta (http/https) fica como esta;
     * iniciada por "/" e relativa a base do servidor. Qualquer outra forma e rejeitada (null).
     */
    public static String packUrl(Context c, String url) {
        if (url == null) return null;
        String u = url.trim();
        if (u.startsWith("http://") || u.startsWith("https://")) return u;
        if (u.startsWith("/")) return base(c) + u;
        return null;
    }
}
