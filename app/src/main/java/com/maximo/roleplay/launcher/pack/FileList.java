package com.maximo.roleplay.launcher.pack;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Lista de arquivos do jogo (JVM puro). Formato do servidor (mesmo do samp-mobile.shop/files.json, com 3 campos opcionais):
 * <pre>
 * {"files":[{"name":"x.img","path":"anim/x.img","url":"https://.../x.img","size":123,
 *            "sha256":"64 hex"?, "verify":false?}],
 *  "dirs":["SAMP/skins/fallback_tex"]?}
 * </pre>
 *  - url: link completo do arquivo. Ausente = AppEndpoints.fileUrl(path).
 *  - verify: true (padrao) = o launcher confere tamanho (e SHA-256, se houver) e baixa de novo se divergir.
 *            false = so baixa se o arquivo FALTAR; nunca sobrescreve (arquivos que o proprio jogo regrava).
 *  - dirs: pastas vazias que precisam existir.
 * Quem decide o que entra na lista e o que e conferido e o painel do servidor (PostgreSQL), nao o aplicativo.
 */
public final class FileList {
    private FileList() {}

    public static final class Entry {
        public final String path, name, url, sha256;
        public final long size;
        public final boolean verify;

        Entry(String path, String name, String url, long size, String sha256, boolean verify) {
            this.path = path;
            this.name = name;
            this.url = url;
            this.size = size;
            this.sha256 = sha256;
            this.verify = verify;
        }

        /** SHA-256 so e conferido em arquivo "verify" que traz o hash na lista. */
        public boolean hasSha() { return verify && sha256.length() == 64; }
    }

    public static final class Parsed {
        public final List<Entry> files = new ArrayList<>();
        public final List<String> dirs = new ArrayList<>();
        public final List<String> warnings = new ArrayList<>();
    }

    public static Parsed parse(String body) throws PackException {
        Map<String, Object> root = MiniJson.asMap(MiniJson.parse(body));
        List<Object> arr = root == null ? null : MiniJson.asList(root.get("files"));
        if (arr == null) throw new PackException("lista de arquivos sem \"files\"");
        Parsed out = new Parsed();
        Map<String, String> lower = new HashMap<>();
        for (Object o : arr) {
            Map<String, Object> f = MiniJson.asMap(o);
            if (f == null) throw new PackException("item da lista de arquivos invalido");
            String path = MiniJson.str(f, "path");
            if (path == null) throw new PackException("item da lista sem path");
            PathSafety.validateRelative(path);
            long size = MiniJson.lng(f, "size", -1);
            if (size < 0) throw new PackException("tamanho invalido: " + path);
            String name = MiniJson.str(f, "name");
            if (name == null || name.isEmpty()) name = path.substring(path.lastIndexOf('/') + 1);
            String url = MiniJson.str(f, "url");
            url = url == null ? "" : url.trim();
            if (!url.isEmpty() && !(url.startsWith("http://") || url.startsWith("https://"))) throw new PackException("url invalida: " + path);
            String sha = MiniJson.str(f, "sha256");
            sha = sha == null ? "" : sha.trim().toLowerCase(Locale.ROOT);
            if (!sha.isEmpty() && !Hex.isSha256(sha)) sha = "";   // hash malformado: so tamanho
            Object v = f.get("verify");
            boolean verify = !(v instanceof Boolean) || ((Boolean) v);
            String prev = lower.put(path.toLowerCase(Locale.ROOT), path);
            if (prev != null) out.warnings.add("dois caminhos so diferem pela caixa (Android diferencia): " + prev + " | " + path);
            out.files.add(new Entry(path, name, url, size, sha, verify));
        }
        List<Object> dirs = MiniJson.asList(root.get("dirs"));
        if (dirs != null) {
            for (Object d : dirs) {
                if (!(d instanceof String)) continue;
                String p = (String) d;
                while (p.endsWith("/")) p = p.substring(0, p.length() - 1);
                if (p.isEmpty()) continue;
                PathSafety.validateRelative(p);
                out.dirs.add(p);
            }
        }
        return out;
    }

    /** null = em dia. "ausente" ou "tamanho X != Y". Arquivo com verify=false so conta se faltar. Nao confere SHA-256. */
    public static String whyNeeded(File base, Entry e) throws PackException {
        File f = PathSafety.resolveInside(base, e.path);
        if (!f.isFile()) return "ausente";
        if (!e.verify) return null;
        if (f.length() != e.size) return "tamanho " + f.length() + " != " + e.size;
        return null;
    }

    /** Cria as pastas vazias da lista. Devolve quantas foram criadas. */
    public static int ensureDirs(File base, List<String> dirs) throws PackException {
        int made = 0;
        for (String d : dirs) {
            File f = PathSafety.resolveInside(base, d);
            if (f.isDirectory()) continue;
            if (f.mkdirs() || f.isDirectory()) made++;
        }
        return made;
    }
}
