package com.samp.mobile.launcher;

import com.downloader.Error;
import com.downloader.OnDownloadListener;
import com.downloader.OnProgressListener;
import com.downloader.PRDownloader;
import com.samp.mobile.launcher.pack.CancelToken;
import com.samp.mobile.launcher.pack.FileTransfer;
import com.samp.mobile.launcher.pack.PackException;

import java.io.File;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * {@link FileTransfer} sobre o PRDownloader. Bloqueia a thread que chama (a thread "pack-install", NUNCA a principal)
 * ate o download terminar, falhar ou ser cancelado. O PRDownloader grava em <arquivo>.temp e so renomeia para o nome
 * final quando completa; com o banco ligado (PRDownloaderConfig.setDatabaseEnabled(true)) um .temp deixado por falha
 * de rede ou pelo app fechado e RETOMADO com "Range" (o Caddy atende Range). O tamanho/SHA-256 sao conferidos depois,
 * por PackDownloader.
 */
public final class PrDownloaderTransfer implements FileTransfer {

    @Override
    public void fetch(String url, File dir, String fileName, long expectedSize, final FileTransfer.Progress progress, CancelToken cancel) throws PackException {
        if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) throw new PackException("nao foi possivel criar " + dir);
        new File(dir, fileName).delete(); // o PRDownloader nao sobrescreve um arquivo final ja existente; o .temp (parcial) fica
        final CountDownLatch done = new CountDownLatch(1);
        final AtomicReference<String> failure = new AtomicReference<>(null);
        int id;
        try {
            id = PRDownloader.download(url, dir.getAbsolutePath(), fileName)
                    .setHeader("Accept-Encoding", "identity") // bytes baixados == bytes do arquivo
                    .build()
                    .setOnStartOrResumeListener(null).setOnPauseListener(null).setOnCancelListener(null)
                    .setOnProgressListener(new OnProgressListener() {
                        @Override
                        public void onProgress(com.downloader.Progress p) {
                            if (progress != null) progress.at(p.currentBytes);
                        }
                    })
                    .start(new OnDownloadListener() {
                        @Override
                        public void onDownloadComplete() {
                            done.countDown();
                        }

                        @Override
                        public void onError(Error error) {
                            failure.set("download falhou (connection=" + error.isConnectionError() + " server=" + error.isServerError() + ")");
                            done.countDown();
                        }
                    });
        } catch (RuntimeException e) {
            throw new PackException("PRDownloader nao iniciou: " + e, e);
        }
        try {
            while (!done.await(200, TimeUnit.MILLISECONDS)) {
                if (cancel.isCancelled()) {
                    PRDownloader.cancel(id); // mantem o .temp: a proxima sessao retoma
                    throw new CancelToken.Cancelled();
                }
            }
        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
            PRDownloader.cancel(id);
            throw new CancelToken.Cancelled();
        }
        cancel.check();
        String why = failure.get();
        if (why != null) throw new PackException(why);
        File out = new File(dir, fileName);
        if (!out.isFile()) throw new PackException("o PRDownloader terminou mas " + fileName + " nao existe");
    }

    @Override
    public void discard(File dir, String fileName) {
        new File(dir, fileName).delete();
        new File(dir, fileName + ".temp").delete();
        new File(dir, fileName + ".part").delete();
    }
}
