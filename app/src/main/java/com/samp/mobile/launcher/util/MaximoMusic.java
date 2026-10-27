package com.samp.mobile.launcher.util;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.samp.mobile.R;

import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Player da trilha local do launcher (res/raw). Singleton de processo: a musica continua
 * entre Splash -> Update -> Main, pausa quando o app vai para segundo plano e e liberada
 * antes de abrir o jogo. Chamar sempre na thread principal.
 */
public final class MaximoMusic {
    private static final String TAG = "MaximoMusic";
    private static final String PREFS = "mx_music";
    private static final String KEY_ENABLED = "enabled";
    private static final String KEY_VOLUME = "volume";
    private static final int[] TRACKS = { R.raw.maximo_theme };

    public interface Listener {
        void onMusicStateChanged(boolean playing, int volumePercent);
    }

    private static MaximoMusic sInstance;

    public static synchronized MaximoMusic get(Context c) {
        if (sInstance == null) sInstance = new MaximoMusic(c.getApplicationContext());
        return sInstance;
    }

    private final Context app;
    private final SharedPreferences prefs;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private final AudioManager audio;
    private AudioFocusRequest focusRequest;

    private MediaPlayer player;
    private int track = 0;
    private int startedActivities = 0;
    private boolean pausedByFocus = false;
    private boolean failed = false;

    private final Runnable pauseWhenBackground = new Runnable() {
        @Override public void run() {
            if (startedActivities <= 0) pausePlayer();
        }
    };

    private final AudioManager.OnAudioFocusChangeListener focusListener = change -> {
        if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
            if (player != null && player.isPlaying()) {
                pausedByFocus = true;
                pausePlayer();
            }
        } else if (change == AudioManager.AUDIOFOCUS_GAIN) {
            if (pausedByFocus && isEnabled() && startedActivities > 0) {
                pausedByFocus = false;
                startPlayer();
            }
        }
    };

    private MaximoMusic(Context app) {
        this.app = app;
        this.prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        this.audio = (AudioManager) app.getSystemService(Context.AUDIO_SERVICE);
    }

    // ---- estado persistido ----
    /** Preferencia do jogador (padrao: ligada). */
    public boolean isEnabled() { return prefs.getBoolean(KEY_ENABLED, true); }
    public int getVolumePercent() { return prefs.getInt(KEY_VOLUME, 55); }
    public boolean isPlaying() { return player != null && safeIsPlaying(); }
    public boolean hasFailed() { return failed; }

    public void addListener(Listener l) { if (l != null && !listeners.contains(l)) listeners.add(l); }
    public void removeListener(Listener l) { listeners.remove(l); }

    // ---- ciclo de vida das telas do launcher ----
    public void attach() {
        startedActivities++;
        main.removeCallbacks(pauseWhenBackground);
        if (isEnabled()) startPlayer();
        notifyListeners();
    }

    public void detach() {
        startedActivities = Math.max(0, startedActivities - 1);
        if (startedActivities == 0) main.postDelayed(pauseWhenBackground, 700);
    }

    // ---- controles ----
    public void toggle() {
        if (isPlaying()) {
            prefs.edit().putBoolean(KEY_ENABLED, false).apply();
            pausePlayer();
        } else {
            prefs.edit().putBoolean(KEY_ENABLED, true).apply();
            failed = false;
            startPlayer();
        }
        notifyListeners();
    }

    public void next() { changeTrack(1); }
    public void previous() { changeTrack(-1); }

    private void changeTrack(int delta) {
        boolean wasPlaying = isPlaying();
        track = ((track + delta) % TRACKS.length + TRACKS.length) % TRACKS.length;
        destroyPlayer();
        if (wasPlaying || isEnabled()) {
            startPlayer();
        }
        notifyListeners();
    }

    public void setVolumePercent(int percent) {
        int p = Math.max(0, Math.min(100, percent));
        prefs.edit().putInt(KEY_VOLUME, p).apply();
        applyVolume();
        notifyListeners();
    }

    /** Libera tudo (usar antes de abrir o jogo). A proxima tela do launcher recria o player. */
    public void release() {
        main.removeCallbacks(pauseWhenBackground);
        destroyPlayer();
        abandonFocus();
        notifyListeners();
    }

    // ---- internos ----
    private boolean safeIsPlaying() {
        try { return player.isPlaying(); } catch (IllegalStateException e) { return false; }
    }

    private void startPlayer() {
        if (failed) return;
        try {
            if (player == null) {
                AudioAttributes attrs = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
                player = MediaPlayer.create(app, TRACKS[track], attrs, AudioManager.AUDIO_SESSION_ID_GENERATE);
                if (player == null) { fail("MediaPlayer.create devolveu null"); return; }
                player.setLooping(true);
                player.setOnErrorListener((mp, what, extra) -> {
                    fail("MediaPlayer erro " + what + "/" + extra);
                    return true;
                });
            }
            applyVolume();
            if (!safeIsPlaying() && requestFocus()) {
                player.start();
            }
        } catch (RuntimeException e) {
            fail(e.toString());
        }
        notifyListeners();
    }

    private void pausePlayer() {
        try {
            if (player != null && safeIsPlaying()) player.pause();
        } catch (IllegalStateException ignored) { }
        notifyListeners();
    }

    private void destroyPlayer() {
        if (player != null) {
            try { player.stop(); } catch (IllegalStateException ignored) { }
            try { player.release(); } catch (RuntimeException ignored) { }
            player = null;
        }
    }

    private void fail(String why) {
        Log.w(TAG, "Falha na musica: " + why);
        failed = true;
        destroyPlayer();
        notifyListeners();
    }

    private void applyVolume() {
        if (player == null) return;
        float v = getVolumePercent() / 100f;
        v = v * v; // curva perceptual
        try { player.setVolume(v, v); } catch (IllegalStateException ignored) { }
    }

    private boolean requestFocus() {
        if (audio == null) return true;
        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attrs)
                .setOnAudioFocusChangeListener(focusListener, main)
                .build();
        return audio.requestAudioFocus(focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
    }

    private void abandonFocus() {
        if (audio != null && focusRequest != null) {
            audio.abandonAudioFocusRequest(focusRequest);
            focusRequest = null;
        }
    }

    private void notifyListeners() {
        final boolean playing = isPlaying();
        final int vol = getVolumePercent();
        for (Listener l : listeners) l.onMusicStateChanged(playing, vol);
    }
}
