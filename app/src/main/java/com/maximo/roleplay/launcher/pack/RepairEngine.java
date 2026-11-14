package com.maximo.roleplay.launcher.pack;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Motor da aba CORRECOES (JVM puro, sem Android). Trabalha sobre a MESMA lista de arquivos do launcher ({@link FileList}).
 *  - verify:  confere cada arquivo da lista por tamanho e SHA-256 (quando a lista traz e o arquivo e "verify").
 *  - repair:  rebaixa so os arquivos escolhidos, um a um, para uma pasta temporaria e troca o arquivo SO depois de conferir
 *             (o arquivo antigo nunca e apagado antes do novo estar verificado). O download e feito pelo {@link FileTransfer}
 *             recebido (no aplicativo: PRDownloader).
 *  - report:  texto para o jogador mandar ao suporte.
 * Arquivos com "verify": false so sao repostos se FALTAREM. Nada aqui apaga arquivo do jogo; arquivos fora da lista apenas
 * sao listados.
 */
public final class RepairEngine {
    private RepairEngine() {}

    /** Pastas varridas so para listar arquivos que estao no aparelho e nao estao na lista (informativo). */
    private static final String[] EXTRAS_SCOPE = {"samp/", "texdb/"};
    /** Pasta temporaria do reparo, dentro de files/. */
    public static final String REPAIR_TMP = ".repair";

    public interface Sleeper { void sleep(long ms) throws InterruptedException; }

    public static final class Problem {
        public final FileList.Entry entry;
        public final String why;
        Problem(FileList.Entry e, String why) { this.entry = e; this.why = why; }
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

    // ------------------------------------------------------------------ GPU
    /**
     * Mantem so o que o aparelho usa (MESMA regra do fluxo antigo: GameFilePolicy.wantedForDevice).
     * gpu 1=dxt 2=etc 3=pvr. gpu desconhecida (0): so os arquivos sem marca de GPU + os de GPU que JA existem no aparelho
     * (nunca marca como "ausente" nem baixa textura de outra GPU).
     */
    public static List<FileList.Entry> forGpu(File base, List<FileList.Entry> entries, int gpu, List<String> warnings) throws PackException {
        List<FileList.Entry> out = new ArrayList<>();
        int skipped = 0;
        for (FileList.Entry e : entries) {
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
    public static VerifyResult verify(File base, List<FileList.Entry> entries, Progress progress, CancelToken cancel) throws PackException {
        VerifyResult r = new VerifyResult();
        Set<String> known = new HashSet<>();
        int i = 0;
        for (FileList.Entry e : entries) {
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
            if (!e.verify) { r.ok++; continue; }          // o jogo regrava: so conta se faltar
            if (f.length() != e.size) { r.bad.add(new Problem(e, "tamanho " + f.length() + " != " + e.size)); continue; }
            if (!e.hasSha()) { r.ok++; r.sizeOnly++; continue; }
            String h = sha256(f, cancel);
            r.checkedBytes += f.length();
            if (h == null) r.bad.add(new Problem(e, "nao foi possivel ler o arquivo (I/O)"));
            else if (!h.equals(e.sha256)) r.bad.add(new Problem(e, "sha256 diferente (conteudo corrompido)"));
            else r.ok++;
        }
        for (String s : EXTRAS_SCOPE) listExtras(new File(base, s.substring(0, s.length() - 1)), base, known, r.extras);
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
            if (known.contains(lc)) continue;
            out.add(rel + (isStray(k.getName()) ? "  [temporario/incompleto]" : ""));
        }
    }

    private static boolean isStray(String name) {
        String l = name.toLowerCase(Locale.ROOT);
        return l.endsWith(".temp") || l.endsWith(".part") || l.endsWith(".tmp") || l.endsWith(".download") || l.endsWith(".crdownload");
    }

    // ------------------------------------------------------------------ reparar
    /** Alvos de "reparar do zero": tudo da lista, exceto arquivos "verify": false que ja existem. */
    public static List<FileList.Entry> allTargets(File base, List<FileList.Entry> entries) throws PackException {
        List<FileList.Entry> t = new ArrayList<>();
        for (FileList.Entry e : entries) {
            if (!e.verify && PathSafety.resolveInside(base, e.path).isFile()) continue;
            t.add(e);
        }
        return t;
    }

    public static List<FileList.Entry> targetsOf(VerifyResult v) {
        List<FileList.Entry> t = new ArrayList<>();
        for (Problem p : v.bad) t.add(p.entry);
        return t;
    }

    /**
     * Rebaixa os alvos, um por vez. Cada arquivo vai para {@link #REPAIR_TMP}, e conferido (tamanho + SHA-256) e so entao
     * substitui o do jogo; falhou = o arquivo antigo continua la.
     */
    public static RepairResult repair(File base, List<FileList.Entry> targets, UrlResolver urls, FileTransfer transfer, int maxAttempts,
                                      Sleeper sleeper, FreeSpace free, final ByteProgress bytes, CancelToken cancel) throws PackException {
        final RepairResult res = new RepairResult();
        if (targets.isEmpty()) return res;
        long maxFile = 0, allTotal0 = 0;
        for (FileList.Entry e : targets) { maxFile = Math.max(maxFile, e.size); allTotal0 += e.size; }
        final long allTotal = allTotal0;
        if (free != null) {
            long need = maxFile + 100L * 1024 * 1024; // um arquivo por vez (o antigo e liberado ao trocar) + margem
            long have = free.freeBytes();
            if (have < need)
                throw new PackException("espaco insuficiente: livre " + (have >> 20) + " MB, necessario " + (need >> 20) + " MB");
        }
        File tmpDir = new File(base, REPAIR_TMP);
        final long[] doneBefore = {0}; // soma dos arquivos ja concluidos
        int i = 0;
        try {
            for (FileList.Entry e : targets) {
                cancel.check();
                i++;
                File dest = PathSafety.resolveInside(base, e.path);
                final String tmpName = i + ".tmp";
                final File tmp = new File(tmpDir, tmpName);
                final long[] fileDone = {0};
                final int idx = i, cnt = targets.size();
                final String fpath = e.path;
                final long fsize = e.size;
                long t0 = System.currentTimeMillis();
                try {
                    if (dest.isDirectory()) throw new PackException("o destino e uma pasta");
                    if (bytes != null) bytes.onBytes(idx, cnt, fpath, 0, fsize, doneBefore[0], allTotal);
                    fetchVerified(transfer, e.url.isEmpty() ? urls.urlFor(e.path) : e.url, tmpDir, tmpName, e, maxAttempts, sleeper,
                            new FileTransfer.Progress() {
                                @Override public void at(long fileBytes) {
                                    res.downloadedBytes += fileBytes - fileDone[0];
                                    fileDone[0] = fileBytes;
                                    if (bytes != null) bytes.onBytes(idx, cnt, fpath, fileBytes, fsize, doneBefore[0] + fileBytes, allTotal);
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
                    transfer.discard(tmpDir, tmpName);
                }
            }
        } finally {
            tmpDir.delete(); // so some se estiver vazia
        }
        return res;
    }

    /** Baixa para dir/name e confere tamanho + SHA-256 (se o arquivo tem); ate maxAttempts vezes. Falhando, nada fica em dir. */
    private static void fetchVerified(FileTransfer transfer, String url, File dir, String name, FileList.Entry e, int maxAttempts,
                                      Sleeper sleeper, FileTransfer.Progress progress, CancelToken cancel) throws PackException {
        if (e.size == 0) {   // o PRDownloader nao conclui download sem conteudo: o arquivo vazio e criado aqui
            try {
                if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) throw new IOException("nao foi possivel criar " + dir);
                File f = new File(dir, name);
                if (!f.createNewFile() && !f.isFile()) throw new IOException("createNewFile falhou");
                return;
            } catch (IOException ex) {
                throw new PackException("nao foi possivel criar o arquivo vazio: " + ex.getMessage(), ex);
            }
        }
        String last = "?";
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            try {
                transfer.fetch(url, dir, name, e.size, progress, cancel);
                File f = new File(dir, name);
                if (f.length() != e.size) {
                    last = "tamanho " + f.length() + " != " + e.size;
                } else if (!e.hasSha() || e.sha256.equals(sha256(f, cancel))) {
                    return;
                } else {
                    last = "SHA-256 nao confere";
                }
                transfer.discard(dir, name);   // conteudo errado: nao pode ser retomado
            } catch (PackException ex) {
                if (ex instanceof CancelToken.Cancelled) throw ex;
                last = ex.getMessage() == null ? ex.toString() : ex.getMessage();
            }
            if (attempt < maxAttempts) {
                try {
                    sleeper.sleep(1500L * attempt);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    throw new CancelToken.Cancelled();
                }
            }
        }
        transfer.discard(dir, name);
        throw new PackException("falhou apos " + maxAttempts + " tentativas (" + last + ")");
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
