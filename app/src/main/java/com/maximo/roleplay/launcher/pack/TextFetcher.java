package com.maximo.roleplay.launcher.pack;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/** Baixa um TEXTO pequeno (maximo_config.json / lista de arquivos) para a aba CORRECOES. Arquivos do jogo: so PRDownloader. */
public final class TextFetcher {
    private TextFetcher() {}

    public static final int CONNECT_TIMEOUT_MS = 15000;
    public static final int READ_TIMEOUT_MS = 30000;

    public static String fetch(String url, int maxBytes, int maxAttempts, RepairEngine.Sleeper sleeper, CancelToken cancel)
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
            if (attempt < maxAttempts) {
                try {
                    sleeper.sleep(1500L * attempt);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    throw new CancelToken.Cancelled();
                }
            }
        }
        throw new PackException("nao foi possivel baixar " + url + " (" + last + ")");
    }
}
