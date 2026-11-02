package com.maximo.roleplay.launcher.util;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.SoundPool;

import com.maximo.roleplay.R;

/**
 * Sons curtos de toque do launcher (SoundPool). Tudo na thread principal.
 * Nunca lanca excecao: se o som nao carregar, a tela funciona do mesmo jeito, em silencio.
 */
public final class UiSounds {
    public static final int CLICK = 0;    // selecionar uma opcao
    public static final int CONFIRM = 1;  // confirmar

    private static SoundPool pool;
    private static final int[] ids = new int[2];
    private static final boolean[] ready = new boolean[2];

    private UiSounds() {}

    /** Carrega os sons (idempotente). */
    public static void init(Context ctx) {
        if (pool != null || ctx == null) return;
        try {
            Context app = ctx.getApplicationContext() != null ? ctx.getApplicationContext() : ctx;
            AudioAttributes attrs = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build();
            final SoundPool p = new SoundPool.Builder().setMaxStreams(2).setAudioAttributes(attrs).build();
            p.setOnLoadCompleteListener(new SoundPool.OnLoadCompleteListener() {
                @Override
                public void onLoadComplete(SoundPool sp, int sampleId, int status) {
                    if (status != 0) return;
                    for (int i = 0; i < ids.length; i++) if (ids[i] == sampleId) ready[i] = true;
                }
            });
            ids[CLICK] = p.load(app, R.raw.mx_click, 1);
            ids[CONFIRM] = p.load(app, R.raw.mx_confirm, 1);
            pool = p;
        } catch (Throwable ignored) {
            pool = null;
        }
    }

    public static void play(int which) {
        try {
            if (pool == null || which < 0 || which >= ids.length || !ready[which]) return;
            pool.play(ids[which], 0.8f, 0.8f, 1, 0, 1f);
        } catch (Throwable ignored) { }
    }

    /** Libera o SoundPool (ao fechar a tela). */
    public static void release() {
        try { if (pool != null) pool.release(); } catch (Throwable ignored) { }
        pool = null;
        ready[0] = ready[1] = false;
    }
}
