package com.samp.mobile.launcher.pack;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Download de UM arquivo (ou de um texto pequeno) para a engrenagem de correcoes. Fluxo separado do UpdateService:
 * escreve em arquivo .part, confere TAMANHO e (se informado) SHA-256 e so entao entrega o arquivo.
 * Sem Range (o servidor atual, python http.server, nao suporta): cada tentativa recomeca do zero.
 */
public final class FileDownloader {
    private FileDownloader() {}

    public static final int CONNECT_TIMEOUT_MS = 15000;
    public static final int READ_TIMEOUT_MS = 30000;

    /** delta pode ser negativo (bytes de uma tentativa que falhou sao descontados). */
    public interface Bytes { void add(long delta); }

    /** Baixa um texto (JSON de lista) com limite de tamanho e ate maxAttempts tentativas. */
    public static String fetchText(String url, int maxBytes, int maxAttempts, PackDownloader.Sleeper sleeper, CancelToken cancel)
            throws PackException {
        String last = "?";
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            HttpURLConnection c = null;
            try {
                c = (HttpURLConnection) new URL(url).openConnection();
                c.setConnectTimeout(CONNECT_TIMEOUT_MS);
                c.setReadTimeout(READ_TIMEOUT_MS);
                c.setInstanceFollowRedirects(true);
                c.setRequestProperty("Accept-Encoding", "identity");
                c.setUseCaches(false);
                int code = c.getResponseCode();
                if (code != 200) throw new IOException("HTTP " + code);
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[16 * 1024];
                try (InputStream in = c.getInputStream()) {
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        cancel.check();
                        if (out.size() + n > maxBytes) throw new IOException("resposta maior que " + maxBytes + " bytes");
                        out.write(buf, 0, n);
                    }
                }
                return new String(out.toByteArray(), StandardCharsets.UTF_8);
            } catch (IOException e) {
                last = e.getMessage() == null ? e.toString() : e.getMessage();
            } finally {
                if (c != null) c.disconnect();
            }
            backoff(attempt, maxAttempts, sleeper);
        }
        throw new PackException("nao foi possivel baixar " + url + " (" + last + ")");
    }

    /**
     * Baixa url -> dest (dest so existe se TUDO conferiu). expectedSha pode ser "" (so confere tamanho).
     * Em falha, dest e o .part sao apagados.
     */
    public static void downloadVerified(String url, File dest, long expectedSize, String expectedSha, int maxAttempts,
                                        PackDownloader.Sleeper sleeper, Bytes progress, CancelToken cancel) throws PackException {
        File dir = dest.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory())
            throw new PackException("nao foi possivel criar " + dir);
        File part = new File(dest.getPath() + ".part");
        String last = "?";
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            part.delete();
            long[] counted = {0};
            try {
                once(url, part, expectedSize, expectedSha, progress, counted, cancel);
                dest.delete();
                if (!part.renameTo(dest)) throw new IOException("nao foi possivel renomear " + part.getName());
                return;
            } catch (CancelToken.Cancelled c) {
                part.delete();
                if (progress != null && counted[0] != 0) progress.add(-counted[0]);
                throw c;
            } catch (PackException | IOException e) {
                last = e.getMessage() == null ? e.toString() : e.getMessage();
                part.delete();
                if (progress != null && counted[0] != 0) progress.add(-counted[0]);
            }
            backoff(attempt, maxAttempts, sleeper);
        }
        throw new PackException("falhou apos " + maxAttempts + " tentativas (" + last + ")");
    }

    private static void backoff(int attempt, int max, PackDownloader.Sleeper sleeper) throws CancelToken.Cancelled {
        if (attempt >= max) return;
        try {
            sleeper.sleep(1500L * attempt);
        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
            throw new CancelToken.Cancelled();
        }
    }

    private static void once(String url, File part, long expectedSize, String expectedSha, Bytes progress, long[] counted,
                             CancelToken cancel) throws PackException, IOException {
        MessageDigest md;
        try { md = MessageDigest.getInstance("SHA-256"); } catch (NoSuchAlgorithmException e) { throw new PackException("SHA-256 indisponivel", e); }
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        try {
            c.setConnectTimeout(CONNECT_TIMEOUT_MS);
            c.setReadTimeout(READ_TIMEOUT_MS);
            c.setInstanceFollowRedirects(true);
            c.setRequestProperty("Accept-Encoding", "identity");
            c.setUseCaches(false);
            int code = c.getResponseCode();
            if (code != 200) throw new PackException("HTTP " + code);
            long cl = c.getContentLengthLong();
            if (cl >= 0 && cl != expectedSize) throw new PackException("Content-Length " + cl + " difere do esperado " + expectedSize);
            byte[] buf = new byte[64 * 1024];
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
            if (expectedSha != null && !expectedSha.isEmpty() && !Hex.of(md.digest()).equalsIgnoreCase(expectedSha))
                throw new PackException("SHA-256 nao confere");
        } finally {
            c.disconnect();
        }
    }
}
