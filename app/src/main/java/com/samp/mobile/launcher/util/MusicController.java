package com.samp.mobile.launcher.util;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.MediaPlayer;

/**
 * Musica local do launcher. Procura a faixa em res/raw/launcher_music.(mp3|ogg|wav|m4a).
 * Se o arquivo NAO existir, hasTrack() e false e a interface desabilita os controles: nada e
 * simulado. Nao toca sozinha: so quando o usuario aperta play. Pausa quando o launcher sai de
 * primeiro plano e libera o MediaPlayer ao fechar (inclusive ao iniciar o jogo).
 */
public final class MusicController {

    public interface Listener { void onMusicStateChanged(); }

    private static final String PREF_VOLUME = "mx_music_volume";

    private final Context ctx;
    private final int resId;
    private final SharedPreferences prefs;
    private MediaPlayer player;
    private boolean userWantsPlay = false;
    private boolean hostPaused = false;
    private int volumePct;
    private Listener listener;

    public MusicController(Context context) {
        ctx = context.getApplicationContext();
        resId = ctx.getResources().getIdentifier("launcher_music", "raw", ctx.getPackageName());
        prefs = ctx.getSharedPreferences(SharedPreferenceCore.APP_PREFERENCES, Context.MODE_PRIVATE);
        volumePct = clamp(prefs.getInt(PREF_VOLUME, 60));
    }

    public void setListener(Listener l) { listener = l; }
    public boolean hasTrack() { return resId != 0; }
    public int getVolumePercent() { return volumePct; }

    public boolean isPlaying() {
        try {
            return player != null && player.isPlaying();
        } catch (IllegalStateException e) {
            return false;
        }
    }

    public void toggle() {
        if (!hasTrack()) return;
        if (isPlaying()) {
            userWantsPlay = false;
            try { player.pause(); } catch (IllegalStateException ignored) { }
        } else {
            userWantsPlay = true;
            startPlayback();
        }
        notifyChanged();
    }

    public void setVolumePercent(int pct) {
        volumePct = clamp(pct);
        applyVolume();
        prefs.edit().putInt(PREF_VOLUME, volumePct).apply();
    }

    /** Chamar em Activity.onPause(). */
    public void onHostPause() {
        hostPaused = true;
        if (isPlaying()) {
            try { player.pause(); } catch (IllegalStateException ignored) { }
            notifyChanged();
        }
    }

    /** Chamar em Activity.onResume(): retoma so se o usuario tinha deixado tocando. */
    public void onHostResume() {
        hostPaused = false;
        if (userWantsPlay && hasTrack() && !isPlaying()) {
            startPlayback();
            notifyChanged();
        }
    }

    public void release() {
        userWantsPlay = false;
        if (player != null) {
            try { player.release(); } catch (Exception ignored) { }
            player = null;
        }
    }

    private void startPlayback() {
        if (hostPaused) return;
        try {
            if (player == null) {
                player = MediaPlayer.create(ctx, resId);
                if (player == null) { userWantsPlay = false; return; } // faixa invalida
                player.setLooping(true);
            }
            applyVolume();
            player.start();
        } catch (Exception e) {
            userWantsPlay = false;
            release();
        }
    }

    private void applyVolume() {
        if (player == null) return;
        float v = volumePct / 100f;
        try { player.setVolume(v, v); } catch (IllegalStateException ignored) { }
    }

    private void notifyChanged() {
        if (listener != null) listener.onMusicStateChanged();
    }

    private static int clamp(int v) { return Math.max(0, Math.min(100, v)); }
}
