package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Lista da pasta SAMP/ (samp.json, gerado por gerar_samp_json.py a partir de C:\cache-server\SAMP).
 * Esta pasta NAO vem nos pacotes ZIP: e baixada arquivo a arquivo pelo PRDownloader, com tamanho e SHA-256
 * conferidos, e NENHUM arquivo dela e ignorado (nem os que o jogo recria).
 *
 * Unica regra especial (restoreVolatileOnlyIfMissing = true, padrao): os arquivos que o proprio jogo regrava em
 * tempo de execucao (settings.ini, favorites.json, svlog.txt, cinfo.sig, col_index.txt, sampvoice/svconfig.ini)
 * sao SEMPRE baixados quando estao ausentes, mas nao sao rebaixados so porque o tamanho mudou — senao cada abertura
 * do launcher apagaria as configuracoes/favoritos do jogador. Para rebaixar SEMPRE tudo, ponha false.
 */
public final class SampList {
    public static final String PREFIX = "SAMP/";
    public static volatile boolean restoreVolatileOnlyIfMissing = true;

    private static final Set<String> VOLATILE = new HashSet<>(Arrays.asList(
            "samp/settings.ini", "samp/favorites.json", "samp/svlog.txt", "samp/cinfo.sig",
            "samp/col_index.txt", "samp/sampvoice/svconfig.ini"));

    public static final class Entry {
        public final String path;
        public final long size;
        public final String sha256; // "" se a lista nao trouxer
        Entry(String path, long size, String sha256) { this.path = path; this.size = size; this.sha256 = sha256; }
    }

    public final List<Entry> files;
    /** Pastas VAZIAS da SAMP/ (ex.: SAMP/skins/fallback_tex): o jogo checa so a existencia delas. */
    public final List<String> dirs;

    private SampList(List<Entry> files, List<String> dirs) { this.files = files; this.dirs = dirs; }

    /** Cria (se faltarem) as pastas vazias da lista. Retorna quantas foram criadas. Idempotente. */
    public int ensureDirs(File filesDir) {
        int made = 0;
        for (String d : dirs) {
            try {
                File f = PathSafety.resolveInside(filesDir, d);
                if (f.isDirectory()) continue;
                if (f.exists()) continue; // existe como arquivo: nao mexe
                if (f.mkdirs() || f.isDirectory()) made++;
            } catch (PackException ignored) {
            }
        }
        return made;
    }

    public static boolean isSampPath(String rel) {
        return rel != null && rel.regionMatches(true, 0, PREFIX, 0, PREFIX.length());
    }

    public static boolean isVolatile(String rel) {
        return rel != null && VOLATILE.contains(rel.toLowerCase(Locale.ROOT));
    }

    /** samp.json: {"files":[{"name":..., "path":"SAMP/...", "size":N, "sha256":"..."}], "dirs":["SAMP/pasta/vazia"]} */
    public static SampList parse(String json) throws PackException {
        Map<String, Object> m = MiniJson.asMap(MiniJson.parse(json));
        if (m == null) throw new PackException("samp.json nao e um objeto JSON");
        List<Object> arr = MiniJson.asList(m.get("files"));
        if (arr == null) throw new PackException("samp.json sem \"files\"");
        List<Entry> out = new ArrayList<>();
        Map<String, String> lower = new HashMap<>();
        for (Object o : arr) {
            Map<String, Object> f = MiniJson.asMap(o);
            if (f == null) throw new PackException("item de samp.json invalido");
            String path = PathSafety.validateRelative(MiniJson.str(f, "path"));
            if (!isSampPath(path)) throw new PackException("samp.json: caminho fora de SAMP/: " + path);
            long size = MiniJson.lng(f, "size", -1);
            if (size < 0) throw new PackException("samp.json: tamanho invalido: " + path);
            String sha = MiniJson.str(f, "sha256");
            if (sha != null && !sha.isEmpty() && !Hex.isSha256(sha)) throw new PackException("samp.json: sha256 invalido: " + path);
            String lc = path.toLowerCase(Locale.ROOT);
            if (lower.containsKey(lc)) throw new PackException("samp.json: caminho repetido (ou so difere pela caixa): " + path);
            lower.put(lc, path);
            out.add(new Entry(path, size, sha == null ? "" : sha.toLowerCase(Locale.ROOT)));
        }
        List<String> dirs = new ArrayList<>();
        List<Object> da = MiniJson.asList(m.get("dirs"));
        if (da != null) {
            for (Object o : da) {
                if (!(o instanceof String)) throw new PackException("samp.json: item de \"dirs\" invalido");
                String d = PathSafety.validateRelative((String) o);
                if (!isSampPath(d)) throw new PackException("samp.json: pasta fora de SAMP/: " + d);
                dirs.add(d);
            }
        }
        return new SampList(out, dirs);
    }

    /**
     * null = o arquivo ja esta certo; senao o motivo de precisar baixar.
     * @param deep true = tambem confere o SHA-256 de arquivos de tamanho certo (instalacao ainda nao verificada).
     */
    public static String whyNeeded(File filesDir, Entry e, boolean deep) {
        File f;
        try {
            f = PathSafety.resolveInside(filesDir, e.path);
        } catch (PackException ex) {
            return "caminho invalido";
        }
        if (!f.isFile()) return "ausente";
        if (restoreVolatileOnlyIfMissing && isVolatile(e.path)) return null; // regravado pelo jogo: so repoe se faltar
        if (f.length() != e.size) return "tamanho " + f.length() + " != " + e.size;
        if (deep && !e.sha256.isEmpty()) {
            String h = sha256(f);
            if (h == null || !h.equals(e.sha256)) return "sha256 diferente";
        }
        return null;
    }

    public static String sha256(File f) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            FileInputStream in = new FileInputStream(f);
            try {
                byte[] buf = new byte[256 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            } finally {
                in.close();
            }
            StringBuilder sb = new StringBuilder(64);
            for (byte b : md.digest()) sb.append(String.format(Locale.ROOT, "%02x", b));
            return sb.toString();
        } catch (NoSuchAlgorithmException | IOException ex) {
            return null;
        }
    }
}
