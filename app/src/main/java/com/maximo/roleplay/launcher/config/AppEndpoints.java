package com.maximo.roleplay.launcher.config;

import android.content.Context;

import com.maximo.roleplay.R;

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

    public static String newsUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_news_path).trim();
    }

    public static String maximoConfigUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_maximo_config_path).trim();
    }

    /** files_testes.json: lista (name/size/path/sha256) dos arquivos do jogo. Fallback de url_cache_files do maximo_config.json. */
    public static String filesListUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_files_list_path).trim();
    }

    /** URL completa de um arquivo do jogo, a partir do "path" relativo de files_testes.json. */
    public static String fileUrl(Context c, String relativePath) {
        String p = c.getString(R.string.endpoint_files_path).trim();
        if (!p.endsWith("/")) p = p + "/";
        String rel = relativePath.startsWith("/") ? relativePath.substring(1) : relativePath;
        return base(c) + p + rel;
    }

    /** samp.json: lista (path/size/sha256) da pasta SAMP/. */
    public static String sampJsonUrl(Context c) {
        return base(c) + c.getString(R.string.endpoint_samp_json_path).trim();
    }

    /** URL de um arquivo da pasta SAMP/; relativePath e o "path" do samp.json (comeca com "SAMP/"), ja codificado. */
    public static String sampFileUrl(Context c, String relativePath) {
        String rel = relativePath.startsWith("/") ? relativePath.substring(1) : relativePath;
        return base(c) + "/" + rel;
    }
}
