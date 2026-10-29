package test;

import com.samp.mobile.launcher.pack.*;
import com.samp.mobile.launcher.pack.ZipPackExtractor.ExtractedFile;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;

import java.io.*;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Testes do instalador por pacotes (JVM pura, sem Android). Uso: java -cp out test.PackTest */
public class PackTest {
    static int pass = 0, fail = 0;

    static void r(String n, boolean g, String why) {
        if (g) pass++; else fail++;
        System.out.println((g ? "PASS " : "FAIL ") + n + (g ? "" : "  -> " + why));
    }

    interface Thrower { void run() throws Exception; }

    /** Espera uma PackException cuja mensagem contenha "contains". */
    static void rejects(String name, String contains, Thrower t) {
        try {
            t.run();
            r(name, false, "aceitou entrada invalida");
        } catch (PackException e) {
            boolean g = e.getMessage() != null && e.getMessage().toLowerCase().contains(contains.toLowerCase());
            r(name, g, g ? "" : "msg=" + e.getMessage());
        } catch (Exception e) {
            r(name, false, "excecao inesperada: " + e);
        }
    }

    static File tmp() throws IOException { return Files.createTempDirectory("packtest").toFile(); }

    static String sha(byte[] b) throws Exception { return hex(MessageDigest.getInstance("SHA-256").digest(b)); }

    static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format("%02x", x));
        return s.toString();
    }

    static byte[] read(File f) throws IOException { return Files.readAllBytes(f.toPath()); }

    static void write(File f, byte[] b) throws IOException {
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), b);
    }

    static byte[] data(int seed, int n) {
        byte[] b = new byte[n];
        new Random(seed).nextBytes(b);
        return b;
    }

    // ------------------------------------------------------------------ construcao de zips
    /** Zip com manifest embutido. stored=true usa STORED. */
    static byte[] buildZip(Map<String, byte[]> files, boolean stored, boolean withManifest) throws Exception {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(bo)) {
            if (withManifest) {
                StringBuilder m = new StringBuilder("{\"format\":1,\"name\":\"t\",\"files\":[");
                boolean first = true;
                for (Map.Entry<String, byte[]> e : files.entrySet()) {
                    if (!first) m.append(',');
                    first = false;
                    m.append("{\"path\":").append(MiniJson.write(e.getKey())).append(",\"size\":").append(e.getValue().length)
                            .append(",\"sha256\":\"").append(sha(e.getValue())).append("\"}");
                }
                m.append("]}");
                put(z, PackManifest.ENTRY_NAME, m.toString().getBytes(StandardCharsets.UTF_8), false);
            }
            for (Map.Entry<String, byte[]> e : files.entrySet()) put(z, e.getKey(), e.getValue(), stored);
        }
        return bo.toByteArray();
    }

    static void put(ZipOutputStream z, String name, byte[] b, boolean stored) throws IOException {
        ZipEntry ze = new ZipEntry(name);
        if (stored) {
            CRC32 c = new CRC32();
            c.update(b);
            ze.setMethod(ZipEntry.STORED);
            ze.setSize(b.length);
            ze.setCompressedSize(b.length);
            ze.setCrc(c.getValue());
        }
        z.putNextEntry(ze);
        z.write(b);
        z.closeEntry();
    }

    static int indexOf(byte[] a, byte[] pat, int from) {
        outer:
        for (int i = from; i <= a.length - pat.length; i++) {
            for (int k = 0; k < pat.length; k++) if (a[i + k] != pat[k]) continue outer;
            return i;
        }
        return -1;
    }

    /** Acha o cabecalho central (0x02014b50) da entrada com este nome. */
    static int centralHeader(byte[] z, String name) {
        byte[] nm = name.getBytes(StandardCharsets.UTF_8);
        int p = 0;
        while ((p = indexOf(z, new byte[]{0x50, 0x4b, 0x01, 0x02}, p)) >= 0) {
            int nl = (z[p + 28] & 0xff) | ((z[p + 29] & 0xff) << 8);
            if (nl == nm.length && Arrays.equals(Arrays.copyOfRange(z, p + 46, p + 46 + nl), nm)) return p;
            p += 4;
        }
        return -1;
    }

    static void setLe32(byte[] a, int o, long v) {
        a[o] = (byte) v; a[o + 1] = (byte) (v >> 8); a[o + 2] = (byte) (v >> 16); a[o + 3] = (byte) (v >> 24);
    }

    static CancelToken ct() { return new CancelToken(); }

    static ZipPackExtractor.Result extract(byte[] zip, File staging) throws Exception {
        File z = new File(staging.getParentFile(), "in.zip");
        write(z, zip);
        return ZipPackExtractor.extract(z, staging, null, ct());
    }

    static Map<String, byte[]> sample() {
        Map<String, byte[]> m = new LinkedHashMap<>();
        m.put("data/a.txt", "hello a".getBytes(StandardCharsets.UTF_8));
        m.put("models/big.img", data(1, 300000));
        m.put("texdb/gta3.dxt.txd", data(2, 5000));
        m.put("SAMP/readme.txt", "r".getBytes(StandardCharsets.UTF_8));
        return m;
    }

    // ------------------------------------------------------------------ servidor HTTP de teste
    static HttpServer server(final Map<String, byte[]> routes, final AtomicInteger hits, final int flakyUntil) throws IOException {
        HttpServer s = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        s.createContext("/", new HttpHandler() {
            @Override public void handle(HttpExchange ex) throws IOException {
                String path = ex.getRequestURI().getPath();
                int n = hits == null ? 1 : hits.incrementAndGet();
                byte[] b = routes.get(path);
                if (b == null) { ex.sendResponseHeaders(404, -1); ex.close(); return; }
                if (path.startsWith("/trunc") || (path.startsWith("/flaky") && n <= flakyUntil)) {
                    ex.sendResponseHeaders(200, b.length);
                    ex.getResponseBody().write(b, 0, b.length / 2);
                    ex.getResponseBody().flush();
                    ex.close(); // fecha antes do fim: cliente recebe menos bytes
                    return;
                }
                ex.sendResponseHeaders(200, b.length);
                ex.getResponseBody().write(b);
                ex.close();
            }
        });
        s.start();
        return s;
    }

    static final PackDownloader.Sleeper NO_SLEEP = new PackDownloader.Sleeper() { @Override public void sleep(long ms) { } };

    public static void main(String[] args) throws Exception {
        // ---------------- GPU / politica ----------------
        r("gpu: .dxt. -> 1", GameFilePolicy.gpuOf("texdb/gta3.dxt.txd") == 1, "");
        r("gpu: .etc. -> 2", GameFilePolicy.gpuOf("texdb/gta3.etc.txd") == 2, "");
        r("gpu: .pvr. -> 3", GameFilePolicy.gpuOf("texdb/x.pvr.dat") == 3, "");
        r("gpu: sem marca -> core", GameFilePolicy.gpuOf("data/gta.dat") == 0, "");
        r("gpu: SAMP/ (MAIUSCULO) com .dxt. -> core (filtro antigo errava a caixa)", GameFilePolicy.gpuOf("SAMP/ui.dxt.png") == 0, "");
        r("gpu: Player e Menu (caixa mista) -> core", GameFilePolicy.gpuOf("Menu/a.etc.txd") == 0 && GameFilePolicy.gpuOf("PLAYER/b.pvr.x") == 0, "");
        r("wantedForDevice: aparelho dxt pega core+dxt, nao etc", GameFilePolicy.wantedForDevice("texdb/a.dxt.x", 1)
                && GameFilePolicy.wantedForDevice("a.txt", 1) && !GameFilePolicy.wantedForDevice("texdb/a.etc.x", 1), "");
        r("protegidos: lista do requisito", GameFilePolicy.isProtected("CINFO.BIN") && GameFilePolicy.isProtected("models/MINFO.BIN")
                && GameFilePolicy.isProtected("SAMP/settings.ini") && GameFilePolicy.isProtected("settings.ini")
                && GameFilePolicy.isProtected("SAMP/sampvoice/svconfig.ini") && GameFilePolicy.isProtected("a/b/.DS_Store")
                && GameFilePolicy.isProtected("x/Thumbs.db") && GameFilePolicy.isProtected("samp_log.txt")
                && GameFilePolicy.isProtected("gta_sa.set") && GameFilePolicy.isProtected("gtasatelem.set")
                && GameFilePolicy.isProtected("lista.txt") && GameFilePolicy.isProtected("files.json"), "");
        r("protegidos: arquivo de jogo comum nao e protegido", !GameFilePolicy.isProtected("data/gta.dat")
                && !GameFilePolicy.isProtected("SAMP/other/settings.ini.bak"), "");

        // ---------------- client_config / packs ----------------
        String cfg = "{\"client_config\":{\"version_code\":5,\"version\":\"2026.10.29\",\"packs\":["
                + "{\"name\":\"core\",\"gpu\":0,\"url\":\"http://h/core.zip\",\"size\":100,\"sha256\":\"" + sha(new byte[0]) + "\"},"
                + "{\"name\":\"gpu_dxt\",\"gpu\":1,\"url\":\"http://h/d.zip\",\"size\":10,\"sha256\":\"" + sha(new byte[0]) + "\"},"
                + "{\"name\":\"gpu_etc\",\"gpu\":2,\"url\":\"http://h/e.zip\",\"size\":10,\"sha256\":\"" + sha(new byte[0]) + "\"}]}}";
        PackConfig pc = PackConfig.parse(cfg);
        List<PackSpec> sel1 = pc.selectFor(1), sel2 = pc.selectFor(2), sel3 = pc.selectFor(3);
        r("packs: aparelho dxt -> core + gpu_dxt", sel1 != null && sel1.size() == 2 && sel1.get(0).name.equals("core") && sel1.get(1).name.equals("gpu_dxt"), "");
        r("packs: aparelho etc -> core + gpu_etc", sel2 != null && sel2.size() == 2 && sel2.get(1).name.equals("gpu_etc"), "");
        r("packs: aparelho pvr sem pacote pvr -> null (cai no fluxo antigo)", sel3 == null, "");
        r("packs: version lida como string", "2026.10.29".equals(pc.version), String.valueOf(pc.version));
        r("packs: ausente -> lista vazia", PackConfig.parse("{\"client_config\":{\"version_code\":1}}").packs.isEmpty(), "");
        rejects("packs: sha256 invalido", "sha256", new Thrower() { public void run() throws Exception {
            PackConfig.parse("{\"version\":\"1\",\"packs\":[{\"name\":\"core\",\"gpu\":0,\"url\":\"http://h/c.zip\",\"size\":5,\"sha256\":\"zz\"}]}"); } });
        rejects("packs: sem version", "version", new Thrower() { public void run() throws Exception {
            PackConfig.parse("{\"packs\":[{\"name\":\"core\",\"gpu\":0,\"url\":\"http://h/c.zip\",\"size\":5,\"sha256\":\"" + sha(new byte[0]) + "\"}]}"); } });

        // ---------------- zip-slip & caminhos ----------------
        final String[] bad = {"../evil.txt", "a/../../evil.txt", "/abs/evil.txt", "C:/evil.txt", "a\\b.txt", "a//b.txt", "./x.txt"};
        for (final String name : bad) {
            rejects("zip-slip: entrada '" + name.replace("\\", "\\\\") + "' rejeitada", "", new Thrower() { public void run() throws Exception {
                Map<String, byte[]> m = new LinkedHashMap<>();
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                try (ZipOutputStream z = new ZipOutputStream(bo)) {
                    put(z, PackManifest.ENTRY_NAME, "{\"files\":[]}".getBytes(StandardCharsets.UTF_8), false);
                    put(z, name, "x".getBytes(StandardCharsets.UTF_8), false);
                }
                File d = tmp();
                extract(bo.toByteArray(), new File(d, "stg"));
                if (new File(d, "evil.txt").exists()) throw new PackException("ARQUIVO FORA DO STAGING FOI CRIADO");
            } });
        }
        {
            File d = tmp();
            try {
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                try (ZipOutputStream z = new ZipOutputStream(bo)) {
                    put(z, PackManifest.ENTRY_NAME, "{\"files\":[]}".getBytes(StandardCharsets.UTF_8), false);
                    put(z, "../evil.txt", "x".getBytes(StandardCharsets.UTF_8), false);
                }
                try { extract(bo.toByteArray(), new File(d, "stg")); } catch (PackException ignored) { }
            } catch (Exception ignored) { }
            r("zip-slip: nada criado fora do staging", !new File(d, "evil.txt").exists() && !new File(d.getParentFile(), "evil.txt").exists(), "");
        }
        rejects("manifest: caminho com .. rejeitado", "..", new Thrower() { public void run() throws Exception {
            PackManifest.parse("{\"files\":[{\"path\":\"a/../b\",\"size\":1}]}".getBytes(StandardCharsets.UTF_8)); } });
        rejects("manifest: so diferem pela caixa", "caixa", new Thrower() { public void run() throws Exception {
            PackManifest.parse("{\"files\":[{\"path\":\"a/B.txt\",\"size\":1},{\"path\":\"a/b.TXT\",\"size\":1}]}".getBytes(StandardCharsets.UTF_8)); } });

        // ---------------- link simbolico ----------------
        rejects("symlink: entrada com modo S_IFLNK rejeitada", "simbolico", new Thrower() { public void run() throws Exception {
            byte[] z = buildZip(sample(), false, true);
            int h = centralHeader(z, "data/a.txt");
            z[h + 5] = 3;                      // version made by: host Unix
            setLe32(z, h + 38, 0xA1FFL << 16); // 0120777
            extract(z, new File(tmp(), "stg"));
        } });

        // ---------------- caixa duplicada no zip ----------------
        rejects("zip: duas entradas que diferem so pela caixa", "caixa", new Thrower() { public void run() throws Exception {
            Map<String, byte[]> m = new LinkedHashMap<>();
            m.put("tex/A.txd", data(3, 10));
            m.put("tex/a.txd", data(4, 10));
            extract(buildZip(m, false, false), new File(tmp(), "stg"));
        } });

        // ---------------- truncado / corrompido ----------------
        rejects("zip truncado (sem diretorio central)", "", new Thrower() { public void run() throws Exception {
            byte[] z = buildZip(sample(), true, true);
            extract(Arrays.copyOf(z, z.length / 2), new File(tmp(), "stg"));
        } });
        rejects("zip truncado no fim (EOCD cortado)", "", new Thrower() { public void run() throws Exception {
            byte[] z = buildZip(sample(), true, true);
            extract(Arrays.copyOf(z, z.length - 10), new File(tmp(), "stg"));
        } });
        rejects("arquivo que nao e zip", "", new Thrower() { public void run() throws Exception {
            extract("isto nao e um zip, e so texto com mais de vinte e dois bytes".getBytes(StandardCharsets.UTF_8), new File(tmp(), "stg"));
        } });
        rejects("zip corrompido (byte alterado em entrada STORED) -> CRC/SHA", "", new Thrower() { public void run() throws Exception {
            Map<String, byte[]> m = sample();
            byte[] z = buildZip(m, true, true);
            byte[] marker = Arrays.copyOfRange(m.get("models/big.img"), 1000, 1032);
            int at = indexOf(z, marker, 0);
            if (at < 0) throw new IllegalStateException("fixture");
            z[at + 5] ^= 0x55;
            extract(z, new File(tmp(), "stg"));
        } });
        rejects("zip corrompido (deflate)", "", new Thrower() { public void run() throws Exception {
            Map<String, byte[]> m = new LinkedHashMap<>();
            m.put("t.txt", new String(new char[4000]).replace('\0', 'a').getBytes(StandardCharsets.UTF_8));
            byte[] z = buildZip(m, false, true);
            int h = centralHeader(z, "t.txt");
            // corrompe o meio do bloco comprimido do arquivo (apos o cabecalho local)
            int local = indexOf(z, new byte[]{0x50, 0x4b, 0x03, 0x04}, indexOf(z, "t.txt".getBytes(StandardCharsets.UTF_8), 0) - 40 < 0 ? 0 : 0);
            for (int k = 0; k < z.length; k++) { if (k > 100 && k < h - 5 && (z[k] & 0xff) != 0) { z[k] ^= (byte) 0xFF; k += 3; } }
            extract(z, new File(tmp(), "stg"));
        } });

        // ---------------- CRC errado ----------------
        rejects("CRC errado no diretorio central", "crc", new Thrower() { public void run() throws Exception {
            byte[] z = buildZip(sample(), true, true);
            int h = centralHeader(z, "data/a.txt");
            setLe32(z, h + 16, 0xDEADBEEFL);
            extract(z, new File(tmp(), "stg"));
        } });

        // ---------------- hash/tamanho do arquivo vs manifest ----------------
        rejects("SHA-256 do arquivo difere do manifest", "sha-256", new Thrower() { public void run() throws Exception {
            Map<String, byte[]> m = new LinkedHashMap<>();
            m.put("a.txt", "AAAA".getBytes(StandardCharsets.UTF_8));
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            try (ZipOutputStream z = new ZipOutputStream(bo)) {
                put(z, PackManifest.ENTRY_NAME, ("{\"files\":[{\"path\":\"a.txt\",\"size\":4,\"sha256\":\"" + sha("BBBB".getBytes()) + "\"}]}").getBytes(), false);
                put(z, "a.txt", "AAAA".getBytes(), false);
            }
            extract(bo.toByteArray(), new File(tmp(), "stg"));
        } });
        rejects("arquivo fora do manifest", "manifest", new Thrower() { public void run() throws Exception {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            try (ZipOutputStream z = new ZipOutputStream(bo)) {
                put(z, PackManifest.ENTRY_NAME, "{\"files\":[]}".getBytes(), false);
                put(z, "extra.txt", "x".getBytes(), false);
            }
            extract(bo.toByteArray(), new File(tmp(), "stg"));
        } });
        rejects("arquivo do manifest ausente no zip", "ausente", new Thrower() { public void run() throws Exception {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            try (ZipOutputStream z = new ZipOutputStream(bo)) {
                put(z, PackManifest.ENTRY_NAME, ("{\"files\":[{\"path\":\"falta.txt\",\"size\":1}]}").getBytes(), false);
            }
            extract(bo.toByteArray(), new File(tmp(), "stg"));
        } });
        rejects("zip sem manifest", "manifest", new Thrower() { public void run() throws Exception {
            extract(buildZip(sample(), false, false), new File(tmp(), "stg"));
        } });

        // ---------------- extracao correta (STORED e DEFLATED) ----------------
        for (boolean stored : new boolean[]{true, false}) {
            File d = tmp();
            Map<String, byte[]> m = sample();
            ZipPackExtractor.Result res = extract(buildZip(m, stored, true), new File(d, "stg"));
            boolean ok = res.files.size() == m.size();
            for (ExtractedFile f : res.files) ok &= Arrays.equals(read(f.staged), m.get(f.path)) && f.sha256.equals(sha(m.get(f.path)));
            r("extracao OK (" + (stored ? "STORED" : "DEFLATED") + "), bytes e SHA conferem", ok, "");
        }
        {
            File d = tmp();
            Map<String, byte[]> m = sample();
            m.put("SAMP/settings.ini", "NAO TOCAR".getBytes());
            ZipPackExtractor.Result res = extract(buildZip(m, false, true), new File(d, "stg"));
            r("extracao: arquivo protegido do zip e pulado (nao extraido)", res.skippedProtected.contains("SAMP/settings.ini")
                    && !new File(d, "stg/SAMP/settings.ini").exists(), "");
        }
        {   // include_all: tudo e extraido (so internos do instalador ficam de fora)
            GameFilePolicy.setProtectGenerated(false);
            try {
                File d = tmp();
                Map<String, byte[]> m = sample();
                m.put("SAMP/settings.ini", "DO-ZIP".getBytes());
                m.put("CINFO.BIN", "CI".getBytes());
                ZipPackExtractor.Result res = extract(buildZip(m, false, true), new File(d, "stg"));
                r("include_all: settings.ini e CINFO.BIN SAO extraidos", res.skippedProtected.isEmpty()
                        && new File(d, "stg/SAMP/settings.ini").exists() && new File(d, "stg/CINFO.BIN").exists(), "");
                r("include_all: installed.json continua interno", GameFilePolicy.isProtected("installed.json")
                        && GameFilePolicy.isProtected(".staging/x") && !GameFilePolicy.isProtected("SAMP/settings.ini"), "");
            } finally { GameFilePolicy.setProtectGenerated(true); }
            r("include_all: padrao volta a proteger", GameFilePolicy.isProtected("SAMP/settings.ini"), "");
            try {
                PackConfig pcIA = PackConfig.parse("{\"client_config\":{\"version\":\"1\",\"include_all\":true,\"packs\":[]}}");
                PackConfig pdIA = PackConfig.parse("{\"client_config\":{\"version\":\"1\",\"packs\":[]}}");
                r("include_all lido do client_config (e ausente = false)", pcIA.includeAll && !pdIA.includeAll, "");
            } catch (PackException e) { r("include_all lido do client_config", false, e.getMessage()); }
        }
        {
            File d = tmp();
            Map<String, byte[]> m = new LinkedHashMap<>();
            m.put("Ação/ç é.txt", "utf8".getBytes());
            ZipPackExtractor.Result res = extract(buildZip(m, false, true), new File(d, "stg"));
            r("extracao: nomes UTF-8 e caixa preservados", res.files.size() == 1 && res.files.get(0).path.equals("Ação/ç é.txt")
                    && new File(d, "stg/Ação/ç é.txt").isFile(), "");
        }

        // ---------------- rollback no commit ----------------
        {
            File base = tmp();
            File stg = new File(tmp(), "stg");
            File bak = new File(tmp(), "bak");
            write(new File(base, "data/old1.txt"), "OLD1".getBytes());
            write(new File(base, "data/old2.txt"), "OLD2".getBytes());
            write(new File(base, "SAMP/settings.ini"), "MEU-INI".getBytes());
            final List<ExtractedFile> fs = new ArrayList<>();
            String[][] spec = {{"data/old1.txt", "NEW1"}, {"data/new_dir/a.txt", "NEWA"}, {"data/old2.txt", "NEW2"}, {"SAMP/settings.ini", "DO-ZIP"}};
            for (String[] s : spec) {
                File sf = new File(stg, s[0]);
                write(sf, s[1].getBytes());
                fs.add(new ExtractedFile(s[0], s[1].length(), sha(s[1].getBytes()), sf));
            }
            try {
                AtomicInstaller.commit(fs, base, bak, new AtomicInstaller.FailPoint() {
                    public void beforeMove(int i, String p) throws IOException { if (i == 2) throw new IOException("falha simulada no 3o arquivo"); }
                });
                r("rollback: commit deveria falhar", false, "nao falhou");
            } catch (PackException e) {
                r("rollback: falha propagada", e.getMessage().contains("rollback"), e.getMessage());
            }
            r("rollback: arquivos antigos restaurados", new String(read(new File(base, "data/old1.txt"))).equals("OLD1")
                    && new String(read(new File(base, "data/old2.txt"))).equals("OLD2"), "");
            r("rollback: arquivo novo removido e pasta criada removida", !new File(base, "data/new_dir/a.txt").exists() && !new File(base, "data/new_dir").exists(), "");
            r("rollback: arquivo protegido intacto", new String(read(new File(base, "SAMP/settings.ini"))).equals("MEU-INI"), "");
        }
        {
            File base = tmp();
            File stg = new File(tmp(), "stg");
            write(new File(base, "data/old1.txt"), "OLD1".getBytes());
            write(new File(base, "SAMP/settings.ini"), "MEU-INI".getBytes());
            List<ExtractedFile> fs = new ArrayList<>();
            String[][] spec = {{"data/old1.txt", "NEW1"}, {"data/n/a.txt", "NEWA"}, {"SAMP/settings.ini", "DO-ZIP"}};
            for (String[] s : spec) { File sf = new File(stg, s[0]); write(sf, s[1].getBytes()); fs.add(new ExtractedFile(s[0], s[1].length(), sha(s[1].getBytes()), sf)); }
            AtomicInstaller.Committed c = AtomicInstaller.commit(fs, base, new File(tmp(), "bak"), null);
            r("commit OK: novos no lugar, substituido contado, protegido nao tocado",
                    new String(read(new File(base, "data/old1.txt"))).equals("NEW1") && new String(read(new File(base, "data/n/a.txt"))).equals("NEWA")
                            && c.replaced == 1 && new String(read(new File(base, "SAMP/settings.ini"))).equals("MEU-INI"), "");
        }

        // ---------------- recuperacao no proximo inicio ----------------
        {
            File base = tmp();
            write(new File(base, ".staging/1/core/x.txt"), "meio".getBytes());
            write(new File(base, ".downloads/core.zip.part"), "parcial".getBytes());
            write(new File(base, ".backup/1/data/perdido.txt"), "ORIGINAL".getBytes());
            write(new File(base, ".backup/1/data/existe.txt"), "VELHO".getBytes());
            write(new File(base, "data/existe.txt"), "ATUAL".getBytes());
            AtomicInstaller.recoverOnStartup(base);
            r("recuperacao: apaga .staging/.downloads/.backup", !new File(base, ".staging").exists() && !new File(base, ".downloads").exists() && !new File(base, ".backup").exists(), "");
            r("recuperacao: restaura do backup o que faltava e nao sobrescreve o que existe",
                    new String(read(new File(base, "data/perdido.txt"))).equals("ORIGINAL") && new String(read(new File(base, "data/existe.txt"))).equals("ATUAL"), "");
        }

        // ---------------- installed.json ----------------
        {
            File base = tmp();
            write(new File(base, "a/b.txt"), new byte[10]);
            write(new File(base, "c.bin"), new byte[3]);
            InstalledState.write(base, new InstalledState("v7", 2, Arrays.asList(new InstalledState.Entry("a/b.txt", 10), new InstalledState.Entry("c.bin", 3))));
            r("installed.json: mesma versao+gpu+arquivos -> atualizado", InstalledState.isUpToDate(base, "v7", 2), "");
            r("installed.json: versao diferente -> precisa atualizar", !InstalledState.isUpToDate(base, "v8", 2), "");
            r("installed.json: GPU diferente -> precisa atualizar", !InstalledState.isUpToDate(base, "v7", 1), "");
            write(new File(base, "c.bin"), new byte[4]);
            r("installed.json: tamanho diferente -> precisa atualizar", !InstalledState.isUpToDate(base, "v7", 2), "");
            new File(base, "a/b.txt").delete();
            r("installed.json: arquivo ausente -> precisa atualizar", !InstalledState.isUpToDate(base, "v7", 2), "");
            write(new File(base, "installed.json"), "{ lixo".getBytes());
            r("installed.json corrompido -> nao quebra, precisa atualizar", InstalledState.read(base) == null && !InstalledState.isUpToDate(base, "v7", 2), "");
        }

        // ---------------- espaco ----------------
        {
            List<PackSpec> l = new ArrayList<>();
            l.add(new PackSpec("core", 0, "http://h/c.zip", 1000L * 1024 * 1024, sha(new byte[0]), 1500L * 1024 * 1024));
            long need = DiskSpace.required(l);
            r("espaco: zip + descompactado + 200 MB", need == (1000L + 1500L + 200L) * 1024 * 1024, String.valueOf(need));
            r("espaco: livre insuficiente -> false; suficiente -> true", !DiskSpace.enough(need - 1, l) && DiskSpace.enough(need, l), "");
        }

        // ---------------- downloader (HTTP real local) ----------------
        {
            Map<String, byte[]> m = sample();
            byte[] good = buildZip(m, true, true);
            byte[] evil = good.clone();
            evil[evil.length / 2] ^= 0x01;            // mesmo tamanho, conteudo diferente
            Map<String, byte[]> routes = new HashMap<>();
            routes.put("/ok.zip", good);
            routes.put("/evil.zip", evil);
            routes.put("/trunc.zip", good);
            routes.put("/flaky.zip", good);
            AtomicInteger hits = new AtomicInteger();
            HttpServer srv = server(routes, hits, 2);
            String base = "http://127.0.0.1:" + srv.getAddress().getPort();
            try {
                File dl = new File(tmp(), "dl");
                final long[] bytes = {0};
                PackDownloader.ByteProgress bp = new PackDownloader.ByteProgress() { public void add(long d) { bytes[0] += d; } };
                File ok = PackDownloader.downloadVerified(base + "/ok.zip", dl, "core", good.length, sha(good), 3, NO_SLEEP, bp, ct());
                r("download OK: .zip promovido, .part removido, progresso = tamanho", ok.getName().equals("core.zip") && ok.length() == good.length
                        && !new File(dl, "core.zip.part").exists() && bytes[0] == good.length, "bytes=" + bytes[0]);

                final int[] hb = {hits.get()};
                bytes[0] = 0;
                try {
                    PackDownloader.downloadVerified(base + "/evil.zip", dl, "evil", evil.length, sha(good), 3, NO_SLEEP, bp, ct());
                    r("hash errado: deveria falhar", false, "aceitou");
                } catch (PackException e) {
                    r("hash errado: 3 tentativas, depois erro; nada fica em disco; progresso zerado",
                            e.getMessage().contains("3 tentativas") && e.getMessage().contains("SHA-256") && hits.get() - hb[0] == 3
                                    && !new File(dl, "evil.zip").exists() && !new File(dl, "evil.zip.part").exists() && bytes[0] == 0, e.getMessage() + " hits=" + (hits.get() - hb[0]) + " bytes=" + bytes[0]);
                }
                try {
                    PackDownloader.downloadVerified(base + "/trunc.zip", dl, "trunc", good.length, sha(good), 3, NO_SLEEP, bp, ct());
                    r("conexao cortada: deveria falhar", false, "aceitou");
                } catch (PackException e) {
                    r("conexao cortada: erro apos 3 tentativas e sem .part", e.getMessage().contains("3 tentativas") && !new File(dl, "trunc.zip.part").exists(), e.getMessage());
                }
                hits.set(0);
                File flaky = PackDownloader.downloadVerified(base + "/flaky.zip", dl, "flaky", good.length, sha(good), 3, NO_SLEEP, bp, ct());
                r("falha 2x e sucesso na 3a tentativa (recomeca do zero)", flaky.length() == good.length && hits.get() == 3, "hits=" + hits.get());
                try {
                    PackDownloader.downloadVerified(base + "/nada.zip", dl, "n", 10, sha(good), 3, NO_SLEEP, bp, ct());
                    r("HTTP 404: deveria falhar", false, "aceitou");
                } catch (PackException e) {
                    r("HTTP 404 -> erro", e.getMessage().contains("404"), e.getMessage());
                }
                CancelToken c = ct();
                c.cancel();
                try {
                    PackDownloader.downloadVerified(base + "/ok.zip", dl, "cancel", good.length, sha(good), 3, NO_SLEEP, bp, c);
                    r("cancelamento: deveria lancar", false, "");
                } catch (CancelToken.Cancelled e) {
                    r("cancelamento antes de baixar", true, "");
                }
            } finally {
                srv.stop(0);
            }
        }

        // ---------------- instalacao completa (HTTP real) ----------------
        {
            Map<String, byte[]> core = new LinkedHashMap<>();
            core.put("data/gta.dat", "gta".getBytes());
            core.put("models/gta3.img", data(11, 400000));
            core.put("SAMP/lang.txt", "pt".getBytes());
            core.put("SAMP/settings.ini", "NAO-DEVE-SOBRESCREVER".getBytes());
            Map<String, byte[]> dxt = new LinkedHashMap<>();
            dxt.put("texdb/gta3.dxt.txd", data(12, 90000));
            byte[] zCore = buildZip(core, true, true), zDxt = buildZip(dxt, true, true);
            Map<String, byte[]> routes = new HashMap<>();
            routes.put("/core.zip", zCore);
            routes.put("/dxt.zip", zDxt);
            HttpServer srv = server(routes, null, 0);
            String base = "http://127.0.0.1:" + srv.getAddress().getPort();
            try {
                final File files = tmp();
                write(new File(files, "SAMP/settings.ini"), "MEU-INI".getBytes());
                write(new File(files, "data/gta.dat"), "VELHO".getBytes());
                write(new File(files, ".staging/lixo/x"), "sobra".getBytes());
                write(new File(files, ".downloads/core.zip.part"), "sobra".getBytes());
                PackInstaller.Config c = new PackInstaller.Config();
                c.filesDir = files;
                c.version = "42";
                c.gpu = 1;
                c.packs = new ArrayList<>();
                c.packs.add(new PackSpec("core", 0, base + "/core.zip", zCore.length, sha(zCore), -1));
                c.packs.add(new PackSpec("gpu_dxt", 1, base + "/dxt.zip", zDxt.length, sha(zDxt), -1));
                c.urls = new PackInstaller.UrlResolver() { public String resolve(PackSpec s) { return s.url; } };
                c.sleeper = NO_SLEEP;
                final long[] last = {-1};
                final boolean[] mono = {true};
                final Set<String> stages = new HashSet<>();
                c.listener = new PackInstaller.Listener() {
                    public void onProgress(String stage, String pack, int i, int n, long done, long total) {
                        stages.add(stage);
                        if (done > total) mono[0] = false;
                        last[0] = done;
                    }
                };
                InstalledState st = PackInstaller.install(c);
                boolean ok = new String(read(new File(files, "data/gta.dat"))).equals("gta")
                        && Arrays.equals(read(new File(files, "models/gta3.img")), core.get("models/gta3.img"))
                        && Arrays.equals(read(new File(files, "texdb/gta3.dxt.txd")), dxt.get("texdb/gta3.dxt.txd"));
                r("install completo: arquivos de core + gpu_dxt no destino com bytes corretos", ok, "");
                r("install completo: SAMP/settings.ini do usuario preservado", new String(read(new File(files, "SAMP/settings.ini"))).equals("MEU-INI"), "");
                r("install completo: limpeza (.staging/.backup/.downloads) e sobras antigas apagadas",
                        !new File(files, ".staging").exists() && !new File(files, ".backup").exists() && !new File(files, ".downloads").exists(), "");
                r("install completo: installed.json confere e nao lista arquivo protegido",
                        InstalledState.isUpToDate(files, "42", 1) && !hasEntry(st, "SAMP/settings.ini") && hasEntry(st, "data/gta.dat") && st.files.size() == 4, "n=" + st.files.size());
                r("install completo: etapas download+extract reportadas e progresso <= total", stages.contains("download") && stages.contains("extract") && mono[0], stages.toString());
                // 2a execucao: nada a fazer
                r("depois de instalado, isUpToDate evita novo download", InstalledState.isUpToDate(files, "42", 1), "");
            } finally {
                srv.stop(0);
            }
        }
        // falha no meio da instalacao: pacote 2 com hash errado => pacote 1 ja instalado, SEM installed.json, sem lixo
        {
            Map<String, byte[]> core = new LinkedHashMap<>();
            core.put("a.txt", "a".getBytes());
            Map<String, byte[]> g = new LinkedHashMap<>();
            g.put("texdb/x.dxt.txd", data(5, 2000));
            byte[] zc = buildZip(core, true, true), zg = buildZip(g, true, true);
            byte[] zgBad = zg.clone();
            zgBad[zgBad.length / 3] ^= 1;
            Map<String, byte[]> routes = new HashMap<>();
            routes.put("/core.zip", zc);
            routes.put("/dxt.zip", zgBad);
            HttpServer srv = server(routes, null, 0);
            String base = "http://127.0.0.1:" + srv.getAddress().getPort();
            try {
                File files = tmp();
                PackInstaller.Config c = new PackInstaller.Config();
                c.filesDir = files; c.version = "9"; c.gpu = 1; c.packs = new ArrayList<>();
                c.packs.add(new PackSpec("core", 0, base + "/core.zip", zc.length, sha(zc), -1));
                c.packs.add(new PackSpec("gpu_dxt", 1, base + "/dxt.zip", zg.length, sha(zg), -1));
                c.urls = new PackInstaller.UrlResolver() { public String resolve(PackSpec s) { return s.url; } };
                c.sleeper = NO_SLEEP;
                try {
                    PackInstaller.install(c);
                    r("install com pacote corrompido: deveria falhar", false, "");
                } catch (PackException e) {
                    r("install com pacote corrompido: erro claro (3 tentativas)", e.getMessage().contains("gpu_dxt") && e.getMessage().contains("3 tentativas"), e.getMessage());
                }
                r("install com falha: sem installed.json e sem arquivo pela metade nem lixo",
                        !new File(files, "installed.json").exists() && !new File(files, ".downloads").exists() && !new File(files, ".staging").exists()
                                && !new File(files, "texdb/x.dxt.txd").exists(), "");
                // espaco insuficiente
                PackInstaller.Config c2 = new PackInstaller.Config();
                c2.filesDir = tmp(); c2.version = "9"; c2.gpu = 1; c2.packs = c.packs; c2.urls = c.urls;
                c2.freeSpace = new PackInstaller.FreeSpace() { public long freeBytes() { return 1024; } };
                rejects("install: espaco livre insuficiente -> erro antes de baixar", "espaco insuficiente", new Thrower() { public void run() throws Exception { PackInstaller.install(c2x(c2)); } });
            } finally {
                srv.stop(0);
            }
        }

        System.out.println("\n" + pass + " ok, " + fail + " falhas");
        if (fail > 0) System.exit(1);
    }

    static PackInstaller.Config c2x(PackInstaller.Config c) { return c; }

    static boolean hasEntry(InstalledState st, String p) {
        for (InstalledState.Entry e : st.files) if (e.path.equals(p)) return true;
        return false;
    }
}
