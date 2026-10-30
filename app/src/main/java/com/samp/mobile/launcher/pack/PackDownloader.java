package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/**
 * Baixa UM pacote .zip para <downloads>/<nome>-<sha12>.zip usando um {@link FileTransfer} (no app: PRDownloader, que
 * RETOMA de onde parou) e so o entrega depois de conferir TAMANHO e SHA-256.
 * O nome leva parte do SHA-256: um pacote novo (conteudo diferente) nunca reaproveita o parcial de um antigo.
 * Falha de rede: o parcial e MANTIDO (a proxima tentativa retoma). Conteudo invalido (tamanho/SHA errado): tudo
 * apagado e recomeca do zero. Um zip completo e valido deixado por uma execucao anterior e reaproveitado.
 * Maximo de tentativas com backoff; ao final, PackException.
 */
public final class PackDownloader {
    private PackDownloader() {}

    public interface Sleeper { void sleep(long ms) throws InterruptedException; }
    /** delta pode ser NEGATIVO (download recomecou do zero / cancelado). */
    public interface ByteProgress { void add(long delta); }

    public static final int CONNECT_TIMEOUT_MS = 15000;
    public static final int READ_TIMEOUT_MS = 30000;

    /** Nome do arquivo baixado: <nome>-<12 primeiros hex do sha256>.zip. */
    public static String fileNameFor(String name, String sha256) {
        String s = sha256 == null ? "" : sha256.toLowerCase(Locale.ROOT);
        return name + "-" + (s.length() >= 12 ? s.substring(0, 12) : s) + ".zip";
    }

    /** Compatibilidade (testes antigos): usa {@link HttpTransfer}. */
    public static File downloadVerified(String url, File downloadsDir, String name, long expectedSize, String expectedSha256,
                                        int maxAttempts, Sleeper sleeper, ByteProgress progress, CancelToken cancel) throws PackException {
        return downloadVerified(url, downloadsDir, name, expectedSize, expectedSha256, maxAttempts, sleeper, progress, cancel, new HttpTransfer());
    }

    public static File downloadVerified(String url, File downloadsDir, String name, long expectedSize, String expectedSha256,
                                        int maxAttempts, Sleeper sleeper, final ByteProgress progress, CancelToken cancel,
                                        FileTransfer transfer) throws PackException {
        if (!downloadsDir.isDirectory() && !downloadsDir.mkdirs() && !downloadsDir.isDirectory())
            throw new PackException("nao foi possivel criar " + downloadsDir);
        final String fileName = fileNameFor(name, expectedSha256);
        final File zip = new File(downloadsDir, fileName);
        final long[] counted = {0};

        // zip completo de uma execucao anterior (ex.: o app morreu durante a extracao): reaproveita se conferir
        if (zip.isFile()) {
            if (zip.length() == expectedSize && sha256Of(zip, cancel).equalsIgnoreCase(expectedSha256)) {
                if (progress != null) progress.add(expectedSize);
                return zip;
            }
            transfer.discard(downloadsDir, fileName);
        }

        FileTransfer.Progress fp = new FileTransfer.Progress() {
            @Override public void at(long bytes) {
                long d = bytes - counted[0];
                counted[0] = bytes;
                if (progress != null && d != 0) progress.add(d);
            }
        };
        String lastError = "?";
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            try {
                transfer.fetch(url, downloadsDir, fileName, expectedSize, fp, cancel);
                String bad = verify(zip, expectedSize, expectedSha256, cancel);
                if (bad == null) {
                    if (progress != null && counted[0] != expectedSize) progress.add(expectedSize - counted[0]);
                    return zip;
                }
                // conteudo invalido: nao retomar
                transfer.discard(downloadsDir, fileName);
                rollback(progress, counted);
                lastError = bad;
            } catch (CancelToken.Cancelled c) {
                rollback(progress, counted); // o parcial fica no disco: a proxima sessao retoma
                throw c;
            } catch (PackException e) {
                lastError = e.getMessage() == null ? e.toString() : e.getMessage();
                // falha de rede/servidor: o parcial fica para a retomada; so descarta se o motivo for integridade
                if (isIntegrityFailure(lastError)) {
                    transfer.discard(downloadsDir, fileName);
                    rollback(progress, counted);
                }
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
        throw new PackException("pacote " + name + ": falhou apos " + maxAttempts + " tentativas (" + lastError + ")");
    }

    private static boolean isIntegrityFailure(String msg) {
        return msg.contains("SHA-256") || msg.contains("mais bytes") || msg.contains("Content-Length") || msg.contains("Content-Range");
    }

    private static void rollback(ByteProgress progress, long[] counted) {
        if (progress != null && counted[0] != 0) progress.add(-counted[0]);
        counted[0] = 0;
    }

    /** null = ok. */
    private static String verify(File zip, long expectedSize, String expectedSha, CancelToken cancel) throws PackException {
        if (!zip.isFile()) return "arquivo nao foi gravado";
        if (zip.length() != expectedSize) return "tamanho " + zip.length() + " difere do esperado " + expectedSize;
        if (!sha256Of(zip, cancel).equalsIgnoreCase(expectedSha)) return "SHA-256 nao confere";
        return null;
    }

    static String sha256Of(File f, CancelToken cancel) throws PackException {
        MessageDigest md;
        try { md = MessageDigest.getInstance("SHA-256"); } catch (NoSuchAlgorithmException e) { throw new PackException("SHA-256 indisponivel", e); }
        byte[] buf = new byte[256 * 1024];
        try (InputStream in = new FileInputStream(f)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                cancel.check();
                md.update(buf, 0, n);
            }
        } catch (IOException e) {
            throw new PackException("nao foi possivel ler " + f.getName() + ": " + e.getMessage(), e);
        }
        return Hex.of(md.digest());
    }
}
