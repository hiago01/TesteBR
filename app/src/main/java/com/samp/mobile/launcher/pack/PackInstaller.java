package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Instalacao completa por pacotes (rodar em thread de fundo; NUNCA na thread principal):
 *   espaco livre -> [download verificado do pacote i -> extracao segura em .staging do pacote i (em outra thread,
 *   enquanto baixa o i+1) -> commit atomico com backup/rollback] -> installed.json -> limpeza.
 * Progresso em bytes: baixados + extraidos (total = soma dos zips + soma dos descompactados).
 */
public final class PackInstaller {

    public interface Listener {
        /** stage: "download" ou "extract". */
        void onProgress(String stage, String packName, int packIndex, int packCount, long doneBytes, long totalBytes);
    }

    public interface FreeSpace { long freeBytes(); }

    public static final class Config {
        public File filesDir;
        public String version;
        public int gpu;
        public List<PackSpec> packs;
        /** Resolve a URL final de cada pacote (absoluta ou relativa ao endpoint base). */
        public UrlResolver urls;
        public int maxAttempts = 3;
        public PackDownloader.Sleeper sleeper = new PackDownloader.Sleeper() {
            @Override public void sleep(long ms) throws InterruptedException { Thread.sleep(ms); }
        };
        public FreeSpace freeSpace;
        public Listener listener;
        public CancelToken cancel = new CancelToken();
        public AtomicInstaller.FailPoint failPoint; // testes
    }

    public interface UrlResolver { String resolve(PackSpec spec); }

    private PackInstaller() {}

    public static InstalledState install(final Config c) throws PackException {
        if (c.packs == null || c.packs.isEmpty()) throw new PackException("nenhum pacote para instalar");
        if (c.version == null || !c.version.matches("[A-Za-z0-9._-]{1,64}")) throw new PackException("versao invalida");
        final File base = c.filesDir;
        if (base == null || !base.isDirectory()) throw new PackException("pasta de dados indisponivel");

        AtomicInstaller.recoverOnStartup(base);

        if (c.freeSpace != null) {
            long free = c.freeSpace.freeBytes();
            long need = DiskSpace.required(c.packs);
            if (free < need) {
                throw new PackException("espaco insuficiente: livre " + mb(free) + " MB, necessario " + mb(need)
                        + " MB (zip + descompactado + 200 MB de margem)");
            }
        }

        final File downloads = new File(base, GameFilePolicy.DIR_DOWNLOADS);
        final File stagingRoot = new File(new File(base, GameFilePolicy.DIR_STAGING), c.version);
        final File backupRoot = new File(new File(base, GameFilePolicy.DIR_BACKUP), c.version);

        long totalBytes = 0;
        for (PackSpec p : c.packs) totalBytes += p.size + p.estimatedUnpacked();
        final long total = totalBytes;
        final AtomicLong done = new AtomicLong(0);
        final int count = c.packs.size();

        ExecutorService extractor = Executors.newSingleThreadExecutor(new java.util.concurrent.ThreadFactory() {
            @Override public Thread newThread(Runnable r) {
                Thread t = new Thread(r, "pack-extract");
                t.setPriority(Thread.NORM_PRIORITY - 1);
                t.setDaemon(true);
                return t;
            }
        });
        final List<InstalledState.Entry> installedEntries = new ArrayList<>();
        final Set<String> lowerSeen = new HashSet<>();
        Future<ZipPackExtractor.Result> pending = null;
        int pendingIdx = -1;
        try {
            for (int i = 0; i < count; i++) {
                c.cancel.check();
                final PackSpec spec = c.packs.get(i);
                final int idx = i;
                final String stage1 = "download";
                File zip = PackDownloader.downloadVerified(c.urls.resolve(spec), downloads, spec.name, spec.size, spec.sha256,
                        c.maxAttempts, c.sleeper, new PackDownloader.ByteProgress() {
                            @Override public void add(long d) { report(c, stage1, spec.name, idx, count, done.addAndGet(d), total); }
                        }, c.cancel);
                // credita a diferenca entre o tamanho do zip e o contado (igual, pois ja conferido) -> nada a ajustar

                // termina (extrai+commit) o pacote anterior ANTES de lancar o proximo (so 1 extracao por vez)
                if (pending != null) {
                    commitPack(c, pending, pendingIdx, base, backupRoot, installedEntries, lowerSeen);
                    pending = null;
                }
                final File stage = new File(stagingRoot, spec.name);
                final File zipF = zip;
                pending = extractor.submit(new Callable<ZipPackExtractor.Result>() {
                    @Override public ZipPackExtractor.Result call() throws Exception {
                        final long[] est = {0};
                        try {
                            ZipPackExtractor.Result r = ZipPackExtractor.extract(zipF, stage, new ZipPackExtractor.Progress() {
                                @Override public void onBytes(long d) {
                                    est[0] += d;
                                    report(c, "extract", spec.name, idx, count, done.addAndGet(d), total);
                                }
                            }, c.cancel);
                            // acerta a estimativa de "unpacked": completa o que faltou do estimado
                            long expected = spec.estimatedUnpacked();
                            if (est[0] < expected) report(c, "extract", spec.name, idx, count, done.addAndGet(expected - est[0]), total);
                            return r;
                        } finally {
                            zipF.delete(); // zip ja consumido: libera espaco
                        }
                    }
                });
                pendingIdx = i;
            }
            if (pending != null) {
                commitPack(c, pending, pendingIdx, base, backupRoot, installedEntries, lowerSeen);
                pending = null;
            }
        } catch (PackException e) {
            if (pending != null) pending.cancel(true);
            extractor.shutdownNow();
            AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_STAGING));
            AtomicInstaller.deleteTree(downloads);
            // .backup so e apagado pelo AtomicInstaller quando o rollback terminou; o resto fica para recoverOnStartup
            throw e;
        } finally {
            extractor.shutdownNow();
        }

        InstalledState st = new InstalledState(c.version, c.gpu, installedEntries);
        try {
            InstalledState.write(base, st);
        } catch (IOException e) {
            throw new PackException("nao foi possivel gravar installed.json: " + e.getMessage(), e);
        }
        AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_STAGING));
        AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_BACKUP));
        AtomicInstaller.deleteTree(downloads);
        return st;
    }

    private static void commitPack(Config c, Future<ZipPackExtractor.Result> f, int idx, File base, File backupRoot,
                                   List<InstalledState.Entry> installed, Set<String> lowerSeen) throws PackException {
        ZipPackExtractor.Result r;
        try {
            r = f.get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new CancelToken.Cancelled();
        } catch (ExecutionException e) {
            Throwable t = e.getCause();
            if (t instanceof PackException) throw (PackException) t;
            throw new PackException("falha na extracao: " + t, t);
        }
        c.cancel.check();
        for (ZipPackExtractor.ExtractedFile ef : r.files) {
            String lc = ef.path.toLowerCase(Locale.ROOT);
            if (!lowerSeen.add(lc)) throw new PackException("o mesmo caminho (ou so diferente na caixa) aparece em dois pacotes: " + ef.path);
        }
        AtomicInstaller.commit(r.files, base, backupRoot, c.failPoint);
        for (ZipPackExtractor.ExtractedFile ef : r.files) {
            if (!GameFilePolicy.isProtected(ef.path)) installed.add(new InstalledState.Entry(ef.path, ef.size));
        }
        // staging deste pacote ja foi movido arquivo a arquivo; remove o que sobrou (pastas vazias)
        AtomicInstaller.deleteTree(r.files.isEmpty() ? new File(new File(base, GameFilePolicy.DIR_STAGING), "x") : stageDirOf(r));
    }

    private static File stageDirOf(ZipPackExtractor.Result r) {
        // pasta de staging do pacote = staged do primeiro arquivo, subindo ate o diretorio logo abaixo de .staging/<versao>/
        File f = r.files.get(0).staged;
        String path = r.files.get(0).path;
        int depth = path.split("/").length;
        File cur = f;
        for (int k = 0; k < depth; k++) cur = cur.getParentFile();
        return cur;
    }

    private static void report(Config c, String stage, String name, int idx, int count, long done, long total) {
        if (c.listener != null) c.listener.onProgress(stage, name, idx, count, Math.max(0, Math.min(done, total)), total);
    }

    private static long mb(long b) { return b / (1024 * 1024); }
}
