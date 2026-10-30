package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
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
 * Motor da engrenagem de correcoes (JVM puro, sem Android). Escopo: files/SAMP e files/texdb.
 *  - verify:  confere cada arquivo da lista (samp.json + files.json) por tamanho e SHA-256 (quando a lista traz).
 *  - repair:  rebaixa so os arquivos escolhidos, um a um, para .downloads/repair e troca o arquivo SO depois de conferir
 *             (o arquivo antigo nunca e apagado antes do novo estar verificado).
 *  - report:  texto para o jogador mandar ao suporte.
 * Arquivos que o jogo/launcher regrava no celular (settings.ini, favoritos, CINFO...) so sao repostos se FALTAREM.
 * Nada aqui apaga arquivo do jogo; arquivos fora da lista apenas sao listados.
 */
public final class RepairEngine {
    private RepairEngine() {}

    public static final String[] SCOPE = {"samp/", "texdb/"};
    public static final String REPAIR_TMP = GameFilePolicy.DIR_DOWNLOADS + "/repair";

    /** Nomes que o UpdateService nunca baixa, em qualquer pasta. */
    private static final Set<String> NEVER = new HashSet<>(Arrays.asList(
            "samp_log.txt", "svlog.txt", "gtasatelem.set", "gtasamp10.b", ".htaccess", "gta_sa.set", "settings.ini"));

    public static final class Entry {
        public final String path;
        public final long size;
        public final String sha256; // "" = lista sem hash
        Entry(String path, long size, String sha256) { this.path = path; this.size = size; this.sha256 = sha256 == null ? "" : sha256.toLowerCase(Locale.ROOT); }
        /** Arquivo regravado pelo jogo: so repor se faltar. */
        public boolean isVolatile() { return SampList.isVolatile(path) || GameFilePolicy.isGenerated(path); }
    }

    public static final class Problem {
        public final Entry entry;
        public final String why;
        Problem(Entry e, String why) { this.entry = e; this.why = why; }
    }

    public static final class VerifyResult {
        public int total, ok, sizeOnly;
        public long checkedBytes;
        public final List<Problem> bad = new ArrayList<>();
        public final List<String> extras = new ArrayList<>();
        public final List<String> warnings = new ArrayList<>();
        public boolean clean() { return bad.isEmpty(); }
    }

    public static final class RepairResult {
        public int fixed;
        public final List<String> failed = new ArrayList<>();
        public long downloadedBytes;
        /** Uma linha por arquivo baixado: tamanho, tempo, velocidade (para achar o que trava). */
        public final List<String> log = new ArrayList<>();
    }

    public interface Progress { void onStep(String what, int done, int total); }
    /** Progresso por BYTE do arquivo atual e do total (a tela mostra MB e velocidade). */
    public interface ByteProgress {
        void onBytes(int idx, int total, String path, long fileDone, long fileTotal, long allDone, long allTotal);
    }
    public interface UrlResolver { String urlFor(String relPath); }
    public interface FreeSpace { long freeBytes(); }

    public static boolean inScope(String path) {
        if (path == null) return false;
        String l = path.toLowerCase(Locale.ROOT);
        for (String s : SCOPE) if (l.startsWith(s)) return true;
        return false;
    }

    // ------------------------------------------------------------------ listas
    public static List<Entry> fromSampJson(String body) throws PackException {
        List<Entry> out = new ArrayList<>();
        for (SampList.Entry e : SampList.parse(body).files) if (inScope(e.path)) out.add(new Entry(e.path, e.size, e.sha256));
        return out;
    }

    /** files.json: {"files":[{"name","size","path","sha256"?}]}. Mesmas exclusoes do UpdateService. */
    public static List<Entry> fromFilesJson(String body, List<String> warnings) throws PackException {
        Map<String, Object> m = MiniJson.asMap(MiniJson.parse(body));
        List<Object> arr = m == null ? null : MiniJson.asList(m.get("files"));
        if (arr == null) throw new PackException("lista de arquivos sem \"files\"");
        List<Entry> out = new ArrayList<>();
        Map<String, String> lower = new HashMap<>();
        for (Object o : arr) {
            Map<String, Object> f = MiniJson.asMap(o);
            if (f == null) throw new PackException("item de files.json invalido");
            String path = MiniJson.str(f, "path");
            if (path == null) throw new PackException("item de files.json sem path");
            PathSafety.validateRelative(path);
            if (!inScope(path) || GameFilePolicy.isInternal(path)) continue;
            String name = path.substring(path.lastIndexOf('/') + 1);
            if (NEVER.contains(name.toLowerCase(Locale.ROOT))) continue;
            long size = MiniJson.lng(f, "size", -1);
            if (size < 0) throw new PackException("files.json: tamanho invalido: " + path);
            String sha = MiniJson.str(f, "sha256");
            if (sha != null && !sha.isEmpty() && !Hex.isSha256(sha)) sha = ""; // hash malformado: so tamanho
            String lc = path.toLowerCase(Locale.ROOT);
            String prev = lower.put(lc, path);
            if (prev != null && warnings != null)
                warnings.add("servidor: dois caminhos so diferem pela caixa (Android diferencia): " + prev + " | " + path);
            out.add(new Entry(path, size, sha));
        }
        return out;
    }

    /** samp.json manda na pasta SAMP/; files.json completa o resto. Sem repetidos. */
    public static List<Entry> merge(List<Entry> samp, List<Entry> files) {
        List<Entry> out = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        if (samp != null) for (Entry e : samp) if (seen.add(e.path.toLowerCase(Locale.ROOT))) out.add(e);
        boolean haveSamp = samp != null && !samp.isEmpty();
        if (files != null) for (Entry e : files) {
            if (haveSamp && SampList.isSampPath(e.path)) continue;
            if (seen.add(e.path.toLowerCase(Locale.ROOT))) out.add(e);
        }
        return out;
    }

    // ------------------------------------------------------------------ GPU
    /**
     * Mantem so o que o aparelho usa (MESMA regra do fluxo antigo: GameFilePolicy.wantedForDevice).
     * gpu 1=dxt 2=etc 3=pvr. gpu desconhecida (0): so os arquivos sem marca de GPU + os de GPU que JA existem no aparelho
     * (nunca marca como "ausente" nem baixa textura de outra GPU).
     */
    public static List<Entry> forGpu(File base, List<Entry> entries, int gpu, List<String> warnings) throws PackException {
        List<Entry> out = new ArrayList<>();
        int skipped = 0;
        for (Entry e : entries) {
            int g = GameFilePolicy.gpuOf(e.path);
            boolean keep;
            if (g == 0) keep = true;
            else if (gpu >= 1 && gpu <= 3) keep = g == gpu;
            else keep = PathSafety.resolveInside(base, e.path).isFile();
            if (keep) out.add(e); else skipped++;
        }
        if (warnings != null) {
            if (gpu < 1 || gpu > 3) warnings.add("GPU do aparelho ainda nao registrada (abra o launcher de novo): texturas de GPU so sao conferidas se ja existirem");
            if (skipped > 0) warnings.add(skipped + " arquivo(s) de outra GPU ignorados (nao sao usados neste aparelho)");
        }
        return out;
    }

    // ------------------------------------------------------------------ verificar
    public static VerifyResult verify(File base, List<Entry> entries, Progress progress, CancelToken cancel) throws PackException {
        VerifyResult r = new VerifyResult();
        Set<String> known = new HashSet<>();
        int i = 0;
        for (Entry e : entries) {
            cancel.check();
            known.add(e.path.toLowerCase(Locale.ROOT));
            r.total++;
            if (progress != null) progress.onStep(e.path, ++i, entries.size());
            File f = PathSafety.resolveInside(base, e.path);
            if (!f.isFile()) {
                String alt = caseVariant(f);
                r.bad.add(new Problem(e, alt == null ? "ausente" : "ausente (existe com outra caixa: " + alt + ")"));
                continue;
            }
            if (e.isVolatile()) { r.ok++; continue; }
            if (f.length() != e.size) { r.bad.add(new Problem(e, "tamanho " + f.length() + " != " + e.size)); continue; }
            if (e.sha256.isEmpty()) { r.ok++; r.sizeOnly++; continue; }
            String h = sha256(f, cancel);
            r.checkedBytes += f.length();
            if (h == null) r.bad.add(new Problem(e, "nao foi possivel ler o arquivo (I/O)"));
            else if (!h.equals(e.sha256)) r.bad.add(new Problem(e, "sha256 diferente (conteudo corrompido)"));
            else r.ok++;
        }
        for (String s : SCOPE) listExtras(new File(base, s.substring(0, s.length() - 1)), base, known, r.extras);
        // pastas com caixa diferente (ex.: "SAMP" x "samp") sao um problema classico entre Windows e Android
        File[] top = base.listFiles();
        if (top != null) {
            Map<String, String> dirs = new HashMap<>();
            for (File t : top) if (t.isDirectory()) {
                String prev = dirs.put(t.getName().toLowerCase(Locale.ROOT), t.getName());
                if (prev != null) r.warnings.add("duas pastas so diferem pela caixa em files/: " + prev + " e " + t.getName());
            }
        }
        return r;
    }

    private static String caseVariant(File missing) {
        File dir = missing.getParentFile();
        String[] names = dir == null ? null : dir.list();
        if (names == null) return null;
        for (String n : names) if (n.equalsIgnoreCase(missing.getName()) && !n.equals(missing.getName())) return n;
        return null;
    }

    private static void listExtras(File dir, File base, Set<String> known, List<String> out) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        for (File k : kids) {
            if (out.size() >= 300) return;
            if (k.isDirectory()) { listExtras(k, base, known, out); continue; }
            String rel = base.toURI().relativize(k.toURI()).getPath();
            String lc = rel.toLowerCase(Locale.ROOT);
            if (known.contains(lc) || GameFilePolicy.isGenerated(rel)) continue;
            out.add(rel + (isStray(k.getName()) ? "  [temporario/incompleto]" : ""));
        }
    }

    private static boolean isStray(String name) {
        String l = name.toLowerCase(Locale.ROOT);
        return l.endsWith(".part") || l.endsWith(".tmp") || l.endsWith(".download") || l.endsWith(".crdownload");
    }

    /** Apaga SO temporarios de download dentro de SAMP/ e texdb/ (*.part, *.tmp...). Retorna quantos. */
    public static int cleanStrayTemp(File base) {
        int n = 0;
        for (String s : SCOPE) n += cleanStray(new File(base, s.substring(0, s.length() - 1)));
        return n;
    }

    private static int cleanStray(File dir) {
        File[] kids = dir.listFiles();
        if (kids == null) return 0;
        int n = 0;
        for (File k : kids) {
            if (k.isDirectory()) n += cleanStray(k);
            else if (isStray(k.getName()) && k.delete()) n++;
        }
        return n;
    }

    // ------------------------------------------------------------------ reparar
    /** Alvos de "reparar do zero": tudo da lista, exceto arquivos regravados pelo jogo que ja existem. */
    public static List<Entry> allTargets(File base, List<Entry> entries) throws PackException {
        List<Entry> t = new ArrayList<>();
        for (Entry e : entries) {
            if (e.isVolatile() && PathSafety.resolveInside(base, e.path).isFile()) continue;
            t.add(e);
        }
        return t;
    }

    public static List<Entry> targetsOf(VerifyResult v) {
        List<Entry> t = new ArrayList<>();
        for (Problem p : v.bad) t.add(p.entry);
        return t;
    }

    public static RepairResult repair(File base, List<Entry> targets, UrlResolver urls, int maxAttempts, PackDownloader.Sleeper sleeper,
                                      FreeSpace free, Progress progress, CancelToken cancel) throws PackException {
        return repair(base, targets, urls, maxAttempts, sleeper, free, progress, null, cancel);
    }

    public static RepairResult repair(File base, List<Entry> targets, UrlResolver urls, int maxAttempts, PackDownloader.Sleeper sleeper,
                                      FreeSpace free, Progress progress, final ByteProgress bytes, CancelToken cancel) throws PackException {
        final RepairResult res = new RepairResult();
        if (targets.isEmpty()) return res;
        long maxFile = 0;
        for (Entry e : targets) maxFile = Math.max(maxFile, e.size);
        if (free != null) {
            long need = maxFile + 100L * 1024 * 1024; // um arquivo por vez (o antigo e liberado ao trocar) + margem
            long have = free.freeBytes();
            if (have < need)
                throw new PackException("espaco insuficiente: livre " + (have >> 20) + " MB, necessario " + (need >> 20) + " MB");
        }
        File tmpDir = new File(base, REPAIR_TMP);
        long allTotal0 = 0;
        for (Entry e : targets) allTotal0 += e.size;
        final long allTotal = allTotal0;
        final long[] doneBefore = {0}; // soma dos arquivos ja concluidos
        int i = 0;
        try {
            for (Entry e : targets) {
                cancel.check();
                if (progress != null) progress.onStep(e.path, ++i, targets.size());
                File dest = PathSafety.resolveInside(base, e.path);
                File tmp = new File(tmpDir, i + ".tmp");
                final long[] fileDone = {0};
                final int idx = i, cnt = targets.size();
                final String fpath = e.path;
                final long fsize = e.size;
                long t0 = System.currentTimeMillis();
                try {
                    if (dest.isDirectory()) throw new PackException("o destino e uma pasta");
                    if (bytes != null) bytes.onBytes(idx, cnt, fpath, 0, fsize, doneBefore[0], allTotal);
                    FileDownloader.downloadVerified(urls.urlFor(e.path), tmp, e.size, e.sha256, maxAttempts, sleeper,
                            new FileDownloader.Bytes() {
                                @Override public void add(long d) {
                                    res.downloadedBytes += d;
                                    fileDone[0] += d;
                                    if (bytes != null) bytes.onBytes(idx, cnt, fpath, fileDone[0], fsize, doneBefore[0] + fileDone[0], allTotal);
                                }
                            }, cancel);
                    File parent = dest.getParentFile();
                    if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory())
                        throw new PackException("nao foi possivel criar a pasta " + parent);
                    replace(tmp, dest);
                    res.fixed++;
                    doneBefore[0] += e.size;
                    long ms = Math.max(1, System.currentTimeMillis() - t0);
                    res.log.add(e.path + ": " + (e.size >> 10) + " KB em " + (ms / 1000.0) + " s (" + String.format(java.util.Locale.ROOT, "%.2f", e.size / 1048.576 / ms) + " MB/s)");
                } catch (CancelToken.Cancelled c) {
                    throw c;
                } catch (PackException | IOException ex) {
                    res.failed.add(e.path + " (" + (e.size >> 10) + " KB, apos " + ((System.currentTimeMillis() - t0) / 1000) + " s): " + ex.getMessage());
                    doneBefore[0] += e.size;
                    tmp.delete();
                }
            }
        } finally {
            AtomicInstaller.deleteTree(tmpDir);
        }
        return res;
    }

    private static void replace(File from, File to) throws IOException {
        java.nio.file.Files.move(from.toPath(), to.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }

    // ------------------------------------------------------------------ util / relatorio
    static String sha256(File f, CancelToken cancel) throws CancelToken.Cancelled {
        try (FileInputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[256 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) { cancel.check(); md.update(buf, 0, n); }
            return Hex.of(md.digest());
        } catch (IOException | NoSuchAlgorithmException e) {
            return null;
        }
    }

    /** Ultimos maxBytes de um arquivo de texto (ou "" se nao existe). */
    public static String tail(File f, int maxBytes) {
        if (f == null || !f.isFile()) return "";
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            long len = raf.length();
            int n = (int) Math.min(len, maxBytes);
            byte[] b = new byte[n];
            raf.seek(len - n);
            raf.readFully(b);
            return new String(b, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "(nao foi possivel ler " + f.getName() + ": " + e.getMessage() + ")";
        }
    }

    public static String formatReport(String device, File base, VerifyResult v, RepairResult rep, String logTail, String note) {
        StringBuilder b = new StringBuilder();
        b.append("=== Relatorio de correcao - Maximo RP ===\n");
        b.append(device).append('\n');
        b.append("pasta: ").append(base).append('\n');
        if (note != null && !note.isEmpty()) b.append(note).append('\n');
        if (v != null) {
            b.append("\n[Verificacao] total=").append(v.total).append(" ok=").append(v.ok).append(" problemas=").append(v.bad.size())
                    .append(" so_tamanho(sem sha na lista)=").append(v.sizeOnly).append('\n');
            int shown = 0;
            for (Problem p : v.bad) { if (shown++ >= 200) { b.append("... (+").append(v.bad.size() - 200).append(")\n"); break; } b.append("  - ").append(p.entry.path).append(": ").append(p.why).append('\n'); }
            for (String w : v.warnings) b.append("  ! ").append(w).append('\n');
            if (!v.extras.isEmpty()) { b.append("  arquivos fora da lista (").append(v.extras.size()).append("):\n"); int k = 0; for (String x : v.extras) { if (k++ >= 60) break; b.append("    ").append(x).append('\n'); } }
        }
        if (rep != null) {
            b.append("\n[Reparo] corrigidos=").append(rep.fixed).append(" falhas=").append(rep.failed.size())
                    .append(" baixado=").append(rep.downloadedBytes >> 10).append(" KB\n");
            for (String f : rep.failed) b.append("  - ").append(f).append('\n');
            int k = 0;
            for (String l : rep.log) { if (k++ >= 60) break; b.append("  ok ").append(l).append('\n'); }
        }
        if (logTail != null && !logTail.isEmpty()) b.append("\n[samp_log.txt - final]\n").append(logTail).append('\n');
        return b.toString();
    }
}
