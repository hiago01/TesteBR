package com.samp.mobile.launcher.pack;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
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
     * Se o servidor anunciar "Accept-Ranges: bytes", uma tentativa interrompida RETOMA de onde parou (Range);
     * se ele ignorar o Range (responde 200), recomeca do zero. O python http.server NAO suporta Range.
     * Em falha final, dest e o .part sao apagados.
     */
    public static void downloadVerified(String url, File dest, long expectedSize, String expectedSha, int maxAttempts,
                                        PackDownloader.Sleeper sleeper, Bytes progress, CancelToken cancel) throws PackException {
        File dir = dest.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory())
            throw new PackException("nao foi possivel criar " + dir);
        File part = new File(dest.getPath() + ".part");
        part.delete();
        String last = "?";
        boolean canResume = false;
        long[] counted = {0};
        for (int attempt = 1; attempt <= maxAttempts; attempt++) {
            cancel.check();
            long have = canResume && part.isFile() ? part.length() : 0;
            if (have <= 0 || have >= expectedSize) {
                have = 0;
                part.delete();
                rollback(progress, counted);
            }
            boolean[] ranges = {false};
            try {
                once(url, part, have, expectedSize, expectedSha, progress, counted, cancel, ranges);
                dest.delete();
                if (!part.renameTo(dest)) throw new IOException("nao foi possivel renomear " + part.getName());
                return;
            } catch (CancelToken.Cancelled c) {
                part.delete();
                rollback(progress, counted);
                throw c;
            } catch (PackException | IOException e) {
                last = e.getMessage() == null ? e.toString() : e.getMessage();
                canResume = ranges[0] && !(e instanceof PackException && isIntegrityFailure(last));
                if (!canResume) { part.delete(); rollback(progress, counted); }
            }
            backoff(attempt, maxAttempts, sleeper);
        }
        part.delete();
        rollback(progress, counted);
        throw new PackException("falhou apos " + maxAttempts + " tentativas (" + last + ")");
    }

    /** SHA/tamanho errado = o conteudo do .part nao presta: nao retomar. */
    private static boolean isIntegrityFailure(String msg) {
        return msg.contains("SHA-256") || msg.contains("mais bytes") || msg.contains("Content-Length") || msg.contains("Content-Range");
    }

    private static void rollback(Bytes progress, long[] counted) {
        if (progress != null && counted[0] != 0) progress.add(-counted[0]);
        counted[0] = 0;
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

    private static void once(String url, File part, long have, long expectedSize, String expectedSha, Bytes progress, long[] counted,
                             CancelToken cancel, boolean[] ranges) throws PackException, IOException {
        MessageDigest md;
        try { md = MessageDigest.getInstance("SHA-256"); } catch (NoSuchAlgorithmException e) { throw new PackException("SHA-256 indisponivel", e); }
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        try {
            c.setConnectTimeout(CONNECT_TIMEOUT_MS);
            c.setReadTimeout(READ_TIMEOUT_MS);
            c.setInstanceFollowRedirects(true);
            c.setRequestProperty("Accept-Encoding", "identity");
            if (have > 0) c.setRequestProperty("Range", "bytes=" + have + "-");
            c.setUseCaches(false);
            int code = c.getResponseCode();
            String ar = c.getHeaderField("Accept-Ranges");
            ranges[0] = code == 206 || (ar != null && ar.trim().equalsIgnoreCase("bytes"));
            boolean append = false;
            if (code == 206) {
                String cr = c.getHeaderField("Content-Range"); // bytes N-M/T
                if (cr == null || !cr.trim().toLowerCase().startsWith("bytes " + have + "-"))
                    throw new PackException("Content-Range inesperado: " + cr);
                append = true;
            } else if (code == 200) {
                if (have > 0) { part.delete(); rollback(progress, counted); have = 0; } // servidor ignorou o Range: do zero
            } else {
                throw new PackException("HTTP " + code);
            }
            long cl = c.getContentLengthLong();
            if (cl >= 0 && cl != expectedSize - have) throw new PackException("Content-Length " + cl + " difere do esperado " + (expectedSize - have));
            long total = have;
            if (append) { // reaproveita o que ja esta no .part: so entra no hash
                try (FileInputStream in = new FileInputStream(part)) {
                    byte[] b = new byte[64 * 1024];
                    long left = have;
                    int n;
                    while (left > 0 && (n = in.read(b, 0, (int) Math.min(b.length, left))) > 0) { md.update(b, 0, n); left -= n; }
                    if (left != 0) throw new IOException("parte local menor que o esperado");
                }
            }
            byte[] buf = new byte[64 * 1024];
            try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(part, append)) {
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
            if (total != expectedSize) throw new IOException("download incompleto (" + total + " de " + expectedSize + " bytes)");
            if (expectedSha != null && !expectedSha.isEmpty() && !Hex.of(md.digest()).equalsIgnoreCase(expectedSha))
                throw new PackException("SHA-256 nao confere");
        } finally {
            c.disconnect();
        }
    }
}
