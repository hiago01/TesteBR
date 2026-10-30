package test;

import com.samp.mobile.launcher.pack.*;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;

import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

/** Testes da engrenagem de correcoes (JVM pura + servidor HTTP local). Uso: java -cp out test.RepairTest */
public class RepairTest {
    static int pass = 0, fail = 0;
    static void r(String n, boolean g, String why) { if (g) pass++; else fail++; System.out.println((g ? "PASS " : "FAIL ") + n + (g ? "" : "  -> " + why)); }

    static String sha(byte[] b) throws Exception {
        StringBuilder s = new StringBuilder();
        for (byte x : MessageDigest.getInstance("SHA-256").digest(b)) s.append(String.format("%02x", x));
        return s.toString();
    }
    static byte[] data(int seed, int n) { byte[] b = new byte[n]; new Random(seed).nextBytes(b); return b; }
    static void put(File base, String rel, byte[] b) throws IOException { File f = new File(base, rel); f.getParentFile().mkdirs(); Files.write(f.toPath(), b); }
    static final PackDownloader.Sleeper NO_SLEEP = new PackDownloader.Sleeper() { @Override public void sleep(long ms) { } };

    static String filesJson(Map<String, byte[]> m, boolean withSha) throws Exception {
        StringBuilder b = new StringBuilder("{\"files\":[");
        boolean first = true;
        for (Map.Entry<String, byte[]> e : m.entrySet()) {
            if (!first) b.append(','); first = false;
            String name = e.getKey().substring(e.getKey().lastIndexOf('/') + 1);
            b.append("{\"name\":\"").append(name).append("\",\"path\":\"").append(e.getKey()).append("\",\"size\":").append(e.getValue().length);
            if (withSha) b.append(",\"sha256\":\"").append(sha(e.getValue())).append("\"");
            b.append('}');
        }
        return b.append("]}").toString();
    }

    static HttpServer server(final Map<String, byte[]> routes, final AtomicInteger hits, final Set<String> flaky, final Set<String> bad) throws IOException {
        HttpServer s = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        s.createContext("/", new HttpHandler() {
            @Override public void handle(HttpExchange ex) throws IOException {
                String path = ex.getRequestURI().getPath();
                int n = hits.incrementAndGet();
                byte[] b = routes.get(path);
                if (b == null) { ex.sendResponseHeaders(404, -1); ex.close(); return; }
                if (flaky.contains(path) && n % 3 != 0) { ex.sendResponseHeaders(503, -1); ex.close(); return; }
                if (bad.contains(path)) { // mesmo tamanho, conteudo adulterado
                    byte[] c = b.clone(); c[0] ^= 0x55; b = c;
                }
                if (path.startsWith("/trunc")) { ex.sendResponseHeaders(200, b.length); ex.getResponseBody().write(b, 0, b.length / 2); ex.close(); return; }
                ex.sendResponseHeaders(200, b.length);
                ex.getResponseBody().write(b);
                ex.close();
            }
        });
        s.start();
        return s;
    }

    public static void main(String[] a) throws Exception {
        File base = Files.createTempDirectory("repair").toFile();
        Map<String, byte[]> srvFiles = new LinkedHashMap<>();
        srvFiles.put("texdb/a.txd", data(1, 5000));
        srvFiles.put("texdb/b.txd", data(2, 7000));
        srvFiles.put("texdb/c.txd", data(3, 3000));
        srvFiles.put("texdb/Mix.txd", data(4, 1000));
        srvFiles.put("SAMP/skins/x.dff", data(5, 2000));
        srvFiles.put("SAMP/settings.ini", "volatile".getBytes(StandardCharsets.UTF_8));
        srvFiles.put("models/other.dat", data(6, 100)); // fora do escopo

        // ---- listas
        List<String> warns = new ArrayList<>();
        List<RepairEngine.Entry> ents = RepairEngine.fromFilesJson(filesJson(srvFiles, true), warns);
        r("lista: so texdb/ e SAMP/ entram (fora do escopo ignorado)", ents.size() == 5, "n=" + ents.size());
        ents.addAll(RepairEngine.fromSampJson("{\"files\":[{\"path\":\"SAMP/settings.ini\",\"size\":8,\"sha256\":\"" + sha("volatile".getBytes(StandardCharsets.UTF_8)) + "\"}]}")); // regravado pelo jogo
        r("lista: caminho com .. rejeitado", throwsPack(() -> RepairEngine.fromFilesJson("{\"files\":[{\"path\":\"texdb/../x\",\"size\":1}]}", null)), "");
        List<String> w2 = new ArrayList<>();
        RepairEngine.fromFilesJson("{\"files\":[{\"path\":\"texdb/A.txd\",\"size\":1},{\"path\":\"texdb/a.txd\",\"size\":1}]}", w2);
        r("lista: aviso de caminhos que so diferem pela caixa", w2.size() == 1, "" + w2);
        r("lista: nomes nunca baixados (samp_log.txt) ignorados", RepairEngine.fromFilesJson("{\"files\":[{\"path\":\"SAMP/samp_log.txt\",\"size\":1},{\"path\":\"texdb/k.txd\",\"size\":1}]}", null).size() == 1, "");
        RepairEngine.Entry se = RepairEngine.fromSampJson("{\"files\":[{\"path\":\"SAMP/q.txt\",\"size\":3,\"sha256\":\"" + sha(new byte[3]) + "\"}]}").get(0);
        List<RepairEngine.Entry> mg = RepairEngine.merge(Arrays.asList(se), ents);
        int sampCount = 0; for (RepairEngine.Entry e : mg) if (e.path.startsWith("SAMP/")) sampCount++;
        r("merge: com samp.json, SAMP/ do files.json e descartada", sampCount == 1 && mg.size() == 1 + 4, "n=" + mg.size() + " samp=" + sampCount);

        // ---- estado do aparelho
        put(base, "texdb/a.txd", srvFiles.get("texdb/a.txd"));                              // ok
        byte[] corrupt = srvFiles.get("texdb/b.txd").clone(); corrupt[10] ^= 1; put(base, "texdb/b.txd", corrupt); // MESMO tamanho, conteudo ruim
        put(base, "texdb/c.txd", Arrays.copyOf(srvFiles.get("texdb/c.txd"), 100));          // tamanho errado
        put(base, "texdb/mix.txd", srvFiles.get("texdb/Mix.txd"));                          // caixa diferente
        put(base, "SAMP/skins/x.dff", srvFiles.get("SAMP/skins/x.dff"));
        put(base, "SAMP/settings.ini", "o jogador mudou isto".getBytes(StandardCharsets.UTF_8)); // volatil: nao pode ser tocado
        put(base, "texdb/orfao.txd", data(9, 10));                                          // fora da lista
        put(base, "texdb/lixo.txd.part", data(8, 10));                                      // temporario
        new File(base, "Texdb").mkdirs();                                                   // pasta duplicada por caixa

        RepairEngine.VerifyResult v = RepairEngine.verify(base, ents, null, new CancelToken());
        Map<String, String> why = new HashMap<>();
        for (RepairEngine.Problem p : v.bad) why.put(p.entry.path, p.why);
        r("verify: arquivo ok nao e problema", !why.containsKey("texdb/a.txd") && !why.containsKey("SAMP/skins/x.dff"), "" + why);
        r("verify: mesmo tamanho + conteudo corrompido -> detectado por SHA-256", why.containsKey("texdb/b.txd") && why.get("texdb/b.txd").contains("sha256"), "" + why);
        r("verify: tamanho errado detectado", why.containsKey("texdb/c.txd") && why.get("texdb/c.txd").contains("tamanho"), "" + why);
        r("verify: arquivo com outra caixa apontado", why.containsKey("texdb/Mix.txd") && why.get("texdb/Mix.txd").contains("outra caixa"), "" + why);
        r("verify: arquivo regravado pelo jogo (settings.ini) existente NAO e problema", !why.containsKey("SAMP/settings.ini"), "" + why);
        boolean orfao = false, stray = false; for (String x : v.extras) { if (x.contains("orfao.txd")) orfao = true; if (x.contains("lixo.txd.part") && x.contains("temporario")) stray = true; }
        r("verify: lista arquivos fora da lista e temporarios", orfao && stray, "" + v.extras);
        r("verify: aviso de pastas que so diferem pela caixa", !v.warnings.isEmpty(), "" + v.warnings);
        r("verify: contadores", v.total == 6 && v.bad.size() == 3 && v.ok == 3, v.total + "/" + v.bad.size() + "/" + v.ok);

        // sem sha na lista: so tamanho (b.txd corrompido passa) e isso e REPORTADO em sizeOnly
        RepairEngine.VerifyResult vn = RepairEngine.verify(base, RepairEngine.fromFilesJson(filesJson(srvFiles, false), null), null, new CancelToken());
        r("verify sem sha: corrupcao de mesmo tamanho nao detectavel, mas sizeOnly informa", vn.sizeOnly >= 3 && !containsPath(vn, "texdb/b.txd"), "sizeOnly=" + vn.sizeOnly);

        // ---- reparo contra servidor HTTP local
        Map<String, byte[]> routes = new HashMap<>();
        for (Map.Entry<String, byte[]> e : srvFiles.entrySet()) routes.put("/files/" + e.getKey(), e.getValue());
        AtomicInteger hits = new AtomicInteger();
        Set<String> flaky = new HashSet<>(Arrays.asList("/files/texdb/c.txd")), bad = new HashSet<>();
        HttpServer srv = server(routes, hits, flaky, bad);
        try {
            final String root = "http://127.0.0.1:" + srv.getAddress().getPort() + "/files/";
            RepairEngine.UrlResolver urls = new RepairEngine.UrlResolver() { @Override public String urlFor(String p) { return root + p; } };
            byte[] oldB = Files.readAllBytes(new File(base, "texdb/b.txd").toPath());

            // servidor adulterado: a copia boa antiga NAO pode ser trocada por lixo
            bad.add("/files/texdb/b.txd");
            RepairEngine.RepairResult rbad = RepairEngine.repair(base, Arrays.asList(entry(ents, "texdb/b.txd")), urls, 2, NO_SLEEP, null, null, new CancelToken());
            r("reparo: servidor entrega conteudo adulterado -> falha e arquivo local intacto", rbad.failed.size() == 1 && rbad.fixed == 0
                    && Arrays.equals(oldB, Files.readAllBytes(new File(base, "texdb/b.txd").toPath())), "" + rbad.failed);
            bad.clear();

            RepairEngine.RepairResult rr = RepairEngine.repair(base, RepairEngine.targetsOf(v), urls, 3, NO_SLEEP, null, null, new CancelToken());
            r("reparo: corrige os 3 problemas (inclui servidor instavel 503 com retentativa)", rr.fixed == 3 && rr.failed.isEmpty(), "fixed=" + rr.fixed + " " + rr.failed);
            RepairEngine.VerifyResult v2 = RepairEngine.verify(base, ents, null, new CancelToken());
            r("reparo: nova verificacao so tem o problema de caixa (arquivo ja baixado como Mix.txd e o antigo mix.txd fica)", v2.bad.isEmpty(), "" + v2.bad.size());
            r("reparo: settings.ini do jogador intocado", "o jogador mudou isto".equals(new String(Files.readAllBytes(new File(base, "SAMP/settings.ini").toPath()), StandardCharsets.UTF_8)), "");
            r("reparo: sem sobras em .downloads/repair", !new File(base, RepairEngine.REPAIR_TMP).exists(), "");

            // 404 e truncado: falha individual, nao derruba o resto
            routes.put("/files/texdb/nao.txd", new byte[0]);
            RepairEngine.Entry e404 = RepairEngine.fromFilesJson("{\"files\":[{\"path\":\"texdb/ausente_no_servidor.txd\",\"size\":5}]}", null).get(0);
            routes.put("/files/trunc.txd", data(7, 4000));
            RepairEngine.Entry etr = RepairEngine.fromFilesJson("{\"files\":[{\"path\":\"texdb/trunc.txd\",\"size\":4000}]}", null).get(0);
            RepairEngine.UrlResolver urls2 = new RepairEngine.UrlResolver() { @Override public String urlFor(String p) { return p.endsWith("trunc.txd") ? root.replace("/files/", "/trunc") + ".txd" : root + p; } };
            routes.put("/trunc.txd", data(7, 4000));
            RepairEngine.RepairResult r2 = RepairEngine.repair(base, Arrays.asList(e404, etr, entry(ents, "texdb/a.txd")), urls2, 2, NO_SLEEP, null, null, new CancelToken());
            r("reparo: 404 e download truncado falham individualmente, o terceiro arquivo ainda e processado", r2.failed.size() == 2 && r2.fixed == 1, r2.fixed + " " + r2.failed);
            r("reparo: truncado nao deixa arquivo/.part", !new File(base, "texdb/trunc.txd").exists() && !new File(base, "texdb/trunc.txd.part").exists(), "");

            // espaco e cancelamento
            try {
                RepairEngine.repair(base, ents, urls, 1, NO_SLEEP, new RepairEngine.FreeSpace() { @Override public long freeBytes() { return 1; } }, null, new CancelToken());
                r("reparo: espaco insuficiente recusa", false, "nao lancou");
            } catch (PackException e) { r("reparo: espaco insuficiente recusa", e.getMessage().contains("espaco"), e.getMessage()); }
            CancelToken ct = new CancelToken(); ct.cancel();
            try { RepairEngine.repair(base, ents, urls, 1, NO_SLEEP, null, null, ct); r("reparo: cancelado", false, ""); }
            catch (CancelToken.Cancelled e) { r("reparo: cancelado", true, ""); }
        } finally { srv.stop(0); }

        // ---- "do zero" e limpeza
        List<RepairEngine.Entry> all = RepairEngine.allTargets(base, ents);
        r("do zero: tudo da lista, menos arquivo regravado pelo jogo que ja existe", all.size() == 5 && !containsE(all, "SAMP/settings.ini"), "n=" + all.size());
        r("limpeza: remove so *.part/*.tmp de SAMP/ e texdb/", RepairEngine.cleanStrayTemp(base) == 1 && new File(base, "texdb/orfao.txd").exists() && !new File(base, "texdb/lixo.txd.part").exists(), "");
        String rep = RepairEngine.formatReport("modelo X / Android 14", base, v, null, "linha de log", "nota");
        r("relatorio: traz aparelho, problemas e final do log", rep.contains("modelo X") && rep.contains("texdb/b.txd") && rep.contains("linha de log"), rep);
        r("tail: ultimos bytes", RepairEngine.tail(new File(base, "texdb/a.txd"), 10).length() > 0 && RepairEngine.tail(new File(base, "naoexiste"), 10).isEmpty(), "");

        System.out.println("\n" + pass + " ok, " + fail + " falhas");
        if (fail > 0) System.exit(1);
    }

    interface Th { void run() throws Exception; }
    static boolean throwsPack(Th t) { try { t.run(); return false; } catch (PackException e) { return true; } catch (Exception e) { return false; } }
    static RepairEngine.Entry entry(List<RepairEngine.Entry> l, String p) { for (RepairEngine.Entry e : l) if (e.path.equals(p)) return e; throw new IllegalStateException(p); }
    static boolean containsE(List<RepairEngine.Entry> l, String p) { for (RepairEngine.Entry e : l) if (e.path.equals(p)) return true; return false; }
    static boolean containsPath(RepairEngine.VerifyResult v, String p) { for (RepairEngine.Problem x : v.bad) if (x.entry.path.equals(p)) return true; return false; }
}
