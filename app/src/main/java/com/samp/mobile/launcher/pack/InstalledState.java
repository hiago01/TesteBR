package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** installed.json: versao do conteudo + lista path/size do que foi instalado (para decidir se precisa atualizar). */
public final class InstalledState {
    public static final String FILE_NAME = "installed.json";

    public static final class Entry {
        public final String path;
        public final long size;
        /** SHA-256 (hex minusculo) do conteudo instalado; "" = desconhecido (installed.json antigo). */
        public final String sha256;
        public Entry(String path, long size) { this(path, size, ""); }
        public Entry(String path, long size, String sha256) {
            this.path = path; this.size = size; this.sha256 = sha256 == null ? "" : sha256.toLowerCase(java.util.Locale.ROOT);
        }
    }

    public final String version;
    public final int gpu;
    public final List<Entry> files;

    public InstalledState(String version, int gpu, List<Entry> files) {
        this.version = version;
        this.gpu = gpu;
        this.files = files;
    }

    /** Escrita atomica (tmp + rename). */
    public static void write(File filesDir, InstalledState st) throws IOException {
        Map<String, Object> root = new LinkedHashMap<>();
        root.put("format", 1L);
        root.put("version", st.version);
        root.put("gpu", (long) st.gpu);
        List<Object> arr = new ArrayList<>();
        for (Entry e : st.files) {
            Map<String, Object> o = new LinkedHashMap<>();
            o.put("path", e.path);
            o.put("size", e.size);
            if (!e.sha256.isEmpty()) o.put("sha256", e.sha256);
            arr.add(o);
        }
        root.put("files", arr);
        File tmp = new File(filesDir, FILE_NAME + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(MiniJson.write(root).getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        Files.move(tmp.toPath(), new File(filesDir, FILE_NAME).toPath(), StandardCopyOption.REPLACE_EXISTING);
    }

    /** @return null se nao existe ou esta corrompido. */
    public static InstalledState read(File filesDir) {
        File f = new File(filesDir, FILE_NAME);
        if (!f.isFile()) return null;
        try {
            Object root = MiniJson.parse(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8));
            Map<String, Object> m = MiniJson.asMap(root);
            if (m == null) return null;
            String version = MiniJson.str(m, "version");
            if (version == null) return null;
            List<Object> arr = MiniJson.asList(m.get("files"));
            if (arr == null) return null;
            List<Entry> list = new ArrayList<>();
            for (Object o : arr) {
                Map<String, Object> e = MiniJson.asMap(o);
                if (e == null) return null;
                String p = MiniJson.str(e, "path");
                long s = MiniJson.lng(e, "size", -1);
                if (p == null || s < 0) return null;
                String sh = MiniJson.str(e, "sha256");
                list.add(new Entry(p, s, sh == null ? "" : sh));
            }
            return new InstalledState(version, (int) MiniJson.lng(m, "gpu", -1), list);
        } catch (IOException | PackException e) {
            return null;
        }
    }

    /**
     * Atualiza (ou acrescenta) entradas em um installed.json JA existente, depois de arquivos baixados e conferidos
     * um a um (atualizacao). Se nao existir installed.json, NAO cria: um arquivo parcial faria o app achar que a
     * instalacao inteira foi verificada.
     * @return true se gravou.
     */
    public static boolean record(File filesDir, List<Entry> updated) {
        if (updated == null || updated.isEmpty()) return false;
        InstalledState st = read(filesDir);
        if (st == null) return false;
        Map<String, Entry> byPath = new LinkedHashMap<>();
        for (Entry e : st.files) byPath.put(e.path, e);
        for (Entry e : updated) byPath.put(e.path, e);
        try {
            write(filesDir, new InstalledState(st.version, st.gpu, new ArrayList<>(byPath.values())));
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /** Mesma versao, mesma GPU e TODOS os arquivos existem com o tamanho registrado. */
    public static boolean isUpToDate(File filesDir, String version, int gpu) {
        InstalledState st = read(filesDir);
        if (st == null || version == null || !version.equals(st.version) || st.gpu != gpu || st.files.isEmpty()) return false;
        for (Entry e : st.files) {
            try {
                File f = PathSafety.resolveInside(filesDir, e.path);
                if (!f.isFile() || f.length() != e.size) return false;
            } catch (PackException ex) {
                return false;
            }
        }
        return true;
    }
}
