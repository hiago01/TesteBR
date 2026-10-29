package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Baixa UM pacote .zip para <downloads>/<nome>.zip.part e so o promove a .zip depois de conferir
 * TAMANHO e SHA-256. Sem Range/resume (o servidor http.server nao suporta): cada tentativa recomeca
 * do zero. Maximo de tentativas com backoff; ao final, PackException.
 */
public final class PackDownloader {
    private PackDownloader() {}

    public interface Sleeper { void sleep(long ms) throws InterruptedException; }
    /** delta pode ser NEGATIVO (descartar o que foi contado numa tentativa que falhou). */
    public interface ByteProgress { void add(long delta); }

    public static final int CONNECT_TIMEOUT_MS = 15000;
    public static final int READ_TIMEOUT_MS = 30000;

    public static File downloadVerified(String url, File downloadsDir, String name, long expectedSize, String expectedSha256,
                                        int maxAttempts, Sleeper sleeper, ByteProgress progress, CancelToken cancel) throws PackException {
        if (!downloadsDir.isDirectory() && !downloadsDir.mkdirs() && !downloadsDir.isDirectory())
            throw new PackException("nao foi possivel criar " + downloadsDir);
        File part = new File(downloadsDir, name + ".zip.part");
        File zip = new File(downloadsDir, name + ".zip");
        String lastError = "?";
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            part.delete();
            zip.delete();
            long[] counted = {0};
            try {
                attemptOnce(url, part, expectedSize, expectedSha256, progress, counted, cancel);
                Files.move(part.toPath(), zip.toPath(), StandardCopyOption.REPLACE_EXISTING);
                return zip;
            } catch (CancelToken.Cancelled c) {
                part.delete();
                if (progress != null && counted[0] != 0) progress.add(-counted[0]);
                throw c;
            } catch (PackException | IOException e) {
                lastError = e.getMessage();
                part.delete();
                if (progress != null && counted[0] != 0) progress.add(-counted[0]);
                if (attempt < maxAttempts) {
                    try {
                        sleeper.sleep(1500L * attempt);
                    } catch (InterruptedException ie) {
                        Thread.currentThread().interrupt();
                        throw new CancelToken.Cancelled();
                    }
                }
            }
        }
        throw new PackException("pacote " + name + ": falhou apos " + maxAttempts + " tentativas (" + lastError + ")");
    }

    private static void attemptOnce(String url, File part, long expectedSize, String expectedSha, ByteProgress progress,
                                    long[] counted, CancelToken cancel) throws PackException, IOException {
        MessageDigest md;
        try { md = MessageDigest.getInstance("SHA-256"); } catch (NoSuchAlgorithmException e) { throw new PackException("SHA-256 indisponivel", e); }
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        try {
            c.setConnectTimeout(CONNECT_TIMEOUT_MS);
            c.setReadTimeout(READ_TIMEOUT_MS);
            c.setInstanceFollowRedirects(true);
            c.setRequestProperty("Accept-Encoding", "identity"); // bytes contados == bytes do zip
            c.setUseCaches(false);
            int code = c.getResponseCode();
            if (code != 200) throw new PackException("HTTP " + code + " (esperado 200; Range nao e usado)");
            long cl = c.getContentLengthLong();
            if (cl >= 0 && cl != expectedSize) throw new PackException("Content-Length " + cl + " difere do esperado " + expectedSize);
            byte[] buf = new byte[256 * 1024];
            long total = 0;
            try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(part)) {
                int n;
                while ((n = in.read(buf)) > 0) {
                    cancel.check();
                    total += n;
                    if (total > expectedSize) throw new PackException("o servidor enviou mais bytes que o esperado");
                    md.update(buf, 0, n);
                    out.write(buf, 0, n);
                    counted[0] += n;
                    if (progress != null) progress.add(n);
                }
                out.getFD().sync();
            }
            if (total != expectedSize) throw new PackException("download incompleto (" + total + " de " + expectedSize + " bytes)");
            String got = Hex.of(md.digest());
            if (!got.equalsIgnoreCase(expectedSha)) throw new PackException("SHA-256 nao confere");
        } finally {
            c.disconnect();
        }
    }
}
