package com.maximo.roleplay.launcher.pack;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Regras sobre QUAIS arquivos o instalador toca.
 *  - isProtected: arquivos gerados pelo jogo/launcher no celular e lixo de SO. Nunca sao sobrescritos
 *    nem apagados (nem entram nos pacotes).
 *  - gpuOf: a qual pacote de GPU o caminho pertence (0 = core). MESMA regra do build_packs.py.
 */
public final class GameFilePolicy {
    private GameFilePolicy() {}

    /** Nomes ignorados em qualquer pasta (lixo de SO / servidor). */
    private static final Set<String> JUNK_BASENAMES = new HashSet<>(Arrays.asList(
            ".ds_store", "thumbs.db", "desktop.ini", ".htaccess"));

    /** Caminhos exatos (relativos a files/), comparados sem diferenciar caixa para PROTEGER mais. */
    private static final Set<String> PROTECTED_PATHS = new HashSet<>(Arrays.asList(
            "lista.txt", "files_testes.json",
            "cinfo.bin", "models/minfo.bin",
            "samp/cinfo.sig", "samp/favorites.json", "samp/svlog.txt",
            "settings.ini", "samp/settings.ini",
            "samp_log.txt", "gta_sa.set", "gtasatelem.set"));

    /**
     * Arquivos que o JOGO recria sozinho (indices em cache): CINFO.BIN, MINFO.BIN e CINFO.SIG, em qualquer pasta.
     * Nunca sao baixados nem sobrescritos e a falta deles NAO e problema (ver RepairEngine.verify).
     */
    private static final Set<String> GENERATED_BASENAMES = new HashSet<>(Arrays.asList(
            "cinfo.bin", "minfo.bin", "cinfo.sig"));

    /** Pastas internas do instalador (nunca fazem parte do jogo). */
    public static final String DIR_STAGING = ".staging";
    public static final String DIR_BACKUP = ".backup";
    public static final String DIR_DOWNLOADS = ".downloads";

    /**
     * true (padrao) = arquivos gerados pelo jogo NUNCA sao extraidos/sobrescritos.
     * false = o maximo_config.json diz "include_all": true -> o pacote traz TUDO e tudo e extraido
     * (so os arquivos internos do instalador continuam de fora). Ajustado pelo UpdateService
     * antes de cada instalacao.
     */
    private static volatile boolean protectGenerated = true;
    public static void setProtectGenerated(boolean v) { protectGenerated = v; }

    /** Arquivos internos do instalador: nunca fazem parte do jogo, em nenhum modo. */
    public static boolean isInternal(String relPath) {
        if (relPath == null) return true;
        String l = relPath.toLowerCase(Locale.ROOT);
        return l.startsWith(DIR_STAGING + "/") || l.startsWith(DIR_BACKUP + "/") || l.startsWith(DIR_DOWNLOADS + "/");
    }

    /** Lixo de SO/servidor ou arquivo gerado pelo jogo/launcher no celular (lista do requisito). */
    public static boolean isGenerated(String relPath) {
        if (relPath == null) return true;
        String l = relPath.toLowerCase(Locale.ROOT);
        if (PROTECTED_PATHS.contains(l)) return true;
        int slash = l.lastIndexOf('/');
        String base = slash >= 0 ? l.substring(slash + 1) : l;
        return JUNK_BASENAMES.contains(base) || GENERATED_BASENAMES.contains(base);
    }

    /** Lixo de SO que nunca deve ser instalado (.DS_Store, Thumbs.db, desktop.ini, .htaccess). */
    public static boolean isJunk(String relPath) {
        if (relPath == null) return true;
        String l = relPath.toLowerCase(Locale.ROOT);
        int slash = l.lastIndexOf('/');
        return JUNK_BASENAMES.contains(slash >= 0 ? l.substring(slash + 1) : l);
    }

    /**
     * true = as SKINS (texdb/skins/ e SAMP/skins/) NAO sao baixadas nem extraidas dos pacotes (opcao "download_skins": false
     * no maximo_config.json, ou DOWNLOAD SEGURO). Ajustado pelo UpdateService antes de cada instalacao; padrao false.
     */
    private static volatile boolean skipSkins = false;
    public static void setSkipSkins(boolean v) { skipSkins = v; }
    public static boolean isSkipSkins() { return skipSkins; }

    /**
     * Decide se as skins ficam de fora. "download_skins" no maximo_config.json:
     * true = baixa as skins SEMPRE (ate no DOWNLOAD SEGURO); false = nunca; "auto"/ausente/outro = escolha do jogador
     * (SEGURO sem skins, COMPLETO com skins).
     * @param cfg valor do JSON (Boolean, String ou null)  @param safeMode jogador escolheu DOWNLOAD SEGURO
     */
    public static boolean shouldSkipSkins(Object cfg, boolean safeMode) {
        if (cfg != null) {
            String s = String.valueOf(cfg).trim().toLowerCase(Locale.ROOT);
            if (s.equals("true")) return false;
            if (s.equals("false")) return true;
        }
        return safeMode;
    }

    /** Caminho das skins: texdb/skins/... e SAMP/skins/... (sem diferenciar caixa). */
    public static boolean isSkin(String relPath) {
        if (relPath == null) return false;
        String p = relPath.replace('\\', '/').toLowerCase(Locale.ROOT);
        while (p.startsWith("/")) p = p.substring(1);
        return p.startsWith("texdb/skins/") || p.startsWith("samp/skins/");
    }

    public static boolean isProtected(String relPath) {
        return isInternal(relPath) || (protectGenerated && isGenerated(relPath)) || (skipSkins && isSkin(relPath));
    }

    /**
     * 0 = core; 1 = dxt; 2 = etc; 3 = pvr.
     * Mantem a intencao do filtro antigo: caminhos com "samp", "player" ou "menu" sao SEMPRE core
     * (mesmo contendo ".dxt."), agora SEM diferenciar caixa (o original so pegava minusculas e
     * por isso "SAMP/..." caia no filtro de GPU).
     */
    public static int gpuOf(String relPath) {
        String l = relPath.toLowerCase(Locale.ROOT);
        if (l.contains("samp") || l.contains("player") || l.contains("menu")) return 0;
        if (l.contains(".dxt.")) return 1;
        if (l.contains(".etc.")) return 2;
        if (l.contains(".pvr.")) return 3;
        return 0;
    }

    public static String packNameForGpu(int gpu) {
        switch (gpu) {
            case 1: return "gpu_dxt";
            case 2: return "gpu_etc";
            case 3: return "gpu_pvr";
            default: return "core";
        }
    }

    /** Arquivo pertence ao conjunto instalado em um aparelho com esta GPU (core + gpu do aparelho)? */
    public static boolean wantedForDevice(String relPath, int deviceGpu) {
        int g = gpuOf(relPath);
        return g == 0 || g == deviceGpu;
    }
}
