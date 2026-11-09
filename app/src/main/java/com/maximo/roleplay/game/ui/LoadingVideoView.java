package com.maximo.roleplay.game.ui;

import android.content.Context;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
import android.media.MediaPlayer;
import android.util.AttributeSet;
import android.view.Surface;
import android.view.TextureView;

import com.maximo.roleplay.R;

/**
 * Video de fundo da tela de carregamento: tela cheia (center-crop), em loop, com audio.
 * Qualquer falha de reproducao e engolida: a view fica transparente e o ImageView de fundo aparece.
 */
public class LoadingVideoView extends TextureView implements TextureView.SurfaceTextureListener {

    private MediaPlayer player;
    private Surface surface;
    private int videoW = 0, videoH = 0;
    private boolean paused = false;

    public LoadingVideoView(Context c) { super(c); init(); }
    public LoadingVideoView(Context c, AttributeSet a) { super(c, a); init(); }
    public LoadingVideoView(Context c, AttributeSet a, int d) { super(c, a, d); init(); }

    private void init() {
        setOpaque(false);
        setSurfaceTextureListener(this);
    }

    private void start(SurfaceTexture st) {
        stop();
        try {
            surface = new Surface(st);
            player = MediaPlayer.create(getContext(), R.raw.loading_video);
            if (player == null) return;
            player.setSurface(surface);
            player.setVolume(0f, 0f); // video sem audio: a musica vem do MaximoMusic
            player.setLooping(true);
            videoW = player.getVideoWidth();
            videoH = player.getVideoHeight();
            applyCenterCrop();
            if (!paused) player.start();
        } catch (Throwable t) {
            stop();
        }
    }

    private void stop() {
        try { if (player != null) { player.release(); } } catch (Throwable ignored) {}
        player = null;
        try { if (surface != null) surface.release(); } catch (Throwable ignored) {}
        surface = null;
    }

    /** Escala o video para cobrir toda a view (aspect-fill), cortando o excesso. */
    private void applyCenterCrop() {
        int vw = getWidth(), vh = getHeight();
        if (vw == 0 || vh == 0 || videoW == 0 || videoH == 0) return;
        float sx = (float) vw / videoW, sy = (float) vh / videoH;
        float s = Math.max(sx, sy);
        float w = videoW * s, h = videoH * s;
        Matrix m = new Matrix();
        m.setScale(w / vw, h / vh, vw / 2f, vh / 2f);
        setTransform(m);
    }

    public void pauseVideo() {
        paused = true;
        try { if (player != null && player.isPlaying()) player.pause(); } catch (Throwable ignored) {}
    }

    public void resumeVideo() {
        paused = false;
        try { if (player != null && !player.isPlaying()) player.start(); } catch (Throwable ignored) {}
    }

    public void releaseVideo() { stop(); }

    @Override public void onSurfaceTextureAvailable(SurfaceTexture st, int w, int h) { start(st); }
    @Override public void onSurfaceTextureSizeChanged(SurfaceTexture st, int w, int h) { applyCenterCrop(); }
    @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture st) { stop(); return true; }
    @Override public void onSurfaceTextureUpdated(SurfaceTexture st) {}
}
