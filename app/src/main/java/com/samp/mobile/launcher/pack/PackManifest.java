package com.samp.mobile.launcher.pack;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Manifest EMBUTIDO em cada zip (entrada ".pack_manifest.json"): path/size/sha256 de cada arquivo. */
public final class PackManifest {
    public static final String ENTRY_NAME = ".pack_manifest.json";
    public static final int MAX_BYTES = 16 * 1024 * 1024;

    public static final class Item {
        public final String path;
        public final long size;
        public final String sha256; // pode ser null se o manifest nao trouxer
        Item(String path, long size, String sha256) { this.path = path; this.size = size; this.sha256 = sha256; }
    }

    public final String name;
    public final Map<String, Item> files = new LinkedHashMap<>();

    private PackManifest(String name) { this.name = name; }

    public static PackManifest parse(byte[] json) throws PackException {
        Object root = MiniJson.parse(new String(json, java.nio.charset.StandardCharsets.UTF_8));
        Map<String, Object> m = MiniJson.asMap(root);
        if (m == null) throw new PackException("manifest do pacote invalido");
        PackManifest pm = new PackManifest(MiniJson.str(m, "name"));
        List<Object> arr = MiniJson.asList(m.get("files"));
        if (arr == null) throw new PackException("manifest do pacote sem \"files\"");
        Map<String, String> lower = new LinkedHashMap<>();
        for (Object o : arr) {
            Map<String, Object> f = MiniJson.asMap(o);
            if (f == null) throw new PackException("item de manifest invalido");
            String path = PathSafety.validateRelative(MiniJson.str(f, "path"));
            long size = MiniJson.lng(f, "size", -1);
            if (size < 0) throw new PackException("tamanho invalido no manifest: " + path);
            String sha = MiniJson.str(f, "sha256");
            if (sha != null && !Hex.isSha256(sha)) throw new PackException("sha256 invalido no manifest: " + path);
            if (pm.files.containsKey(path)) throw new PackException("caminho repetido no manifest: " + path);
            String lc = path.toLowerCase(Locale.ROOT);
            if (lower.containsKey(lc)) throw new PackException("caminhos que diferem so pela caixa no manifest: " + path + " / " + lower.get(lc));
            lower.put(lc, path);
            pm.files.put(path, new Item(path, size, sha == null ? null : sha.toLowerCase(Locale.ROOT)));
        }
        return pm;
    }

    public long totalBytes() {
        long t = 0;
        for (Item i : files.values()) t += i.size;
        return t;
    }
}
