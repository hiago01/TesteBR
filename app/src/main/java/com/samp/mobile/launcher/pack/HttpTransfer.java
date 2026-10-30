package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** {@link FileTransfer} em JVM puro (HttpURLConnection). Sem retomada: cada chamada recomeca do zero. Usada nos testes. */
public final class HttpTransfer implements FileTransfer {
    public static final int CONNECT_TIMEOUT_MS = 15000;
    public static final int READ_TIMEOUT_MS = 30000;

    @Override
    public void fetch(String url, File dir, String fileName, long expectedSize, Progress progress, CancelToken cancel) throws PackException {
        if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) throw new PackException("nao foi possivel criar " + dir);
        File part = new File(dir, fileName + ".part");
        File dest = new File(dir, fileName);
        part.delete();
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(CONNECT_TIMEOUT_MS);
            c.setReadTimeout(READ_TIMEOUT_MS);
            c.setInstanceFollowRedirects(true);
            c.setRequestProperty("Accept-Encoding", "identity");
            c.setUseCaches(false);
            int code = c.getResponseCode();
            if (code != 200) throw new PackException("HTTP " + code);
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
                    out.write(buf, 0, n);
                    if (progress != null) progress.at(total);
                }
                out.getFD().sync();
            }
            if (total != expectedSize) throw new PackException("download incompleto (" + total + " de " + expectedSize + " bytes)");
            Files.move(part.toPath(), dest.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            part.delete();
            throw new PackException(e.getMessage() == null ? e.toString() : e.getMessage(), e);
        } catch (PackException e) {
            part.delete();
            throw e;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    @Override
    public void discard(File dir, String fileName) {
        new File(dir, fileName).delete();
        new File(dir, fileName + ".part").delete();
        new File(dir, fileName + ".temp").delete();
    }
}
