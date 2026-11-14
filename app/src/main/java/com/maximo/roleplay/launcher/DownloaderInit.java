package com.maximo.roleplay.launcher;

import android.content.Context;

import com.downloader.PRDownloader;
import com.downloader.PRDownloaderConfig;

/**
 * Liga o PRDownloader uma unica vez por processo (o UpdateService e a aba CORRECOES usam o mesmo). Banco ligado: um
 * download interrompido e RETOMADO de onde parou (o servidor atende Range).
 */
final class DownloaderInit {
    private static boolean done = false;

    private DownloaderInit() {}

    static synchronized void ensure(Context c) {
        if (done) return;
        PRDownloader.initialize(c.getApplicationContext(), PRDownloaderConfig.newBuilder().setDatabaseEnabled(true).setReadTimeout(30000).setConnectTimeout(30000).build());
        done = true;
    }
}
