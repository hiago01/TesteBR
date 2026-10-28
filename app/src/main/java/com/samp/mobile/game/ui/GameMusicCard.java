package com.samp.mobile.game.ui;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;

import com.samp.mobile.R;
import com.samp.mobile.launcher.util.MaximoMusic;

import java.util.Locale;

/**
 * Card "Tocando agora": mostrado sobre a tela de loading e mantido dentro do jogo enquanto a musica
 * do launcher continua tocando. Some SOMENTE quando a musica termina ou quando o jogador toca no X.
 * Depois do loading o card encolhe para o canto superior direito para nao cobrir os controles do jogo.
 *
 * Fonte unica de estado: {@link MaximoMusic} (nada de segundo MediaPlayer). Thread principal.
 */
public class GameMusicCard {
    private static final float COMPACT_SCALE = 0.6f;

    private final Activity activity;
    private final MaximoMusic music;
    private final ConstraintLayout overlay;
    private final View card;
    private final ImageView play, volume;
    private final SeekBar seek;
    private final TextView cur, total;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private boolean dragging = false;
    private boolean dismissed = false;
    private boolean compact = false;

    private final MaximoMusic.Listener listener = new MaximoMusic.Listener() {
        @Override public void onMusicStateChanged(boolean playing, int volumePercent) { refresh(); }
    };

    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (dismissed) return;
            refresh();
            handler.postDelayed(this, 500);
        }
    };

    public GameMusicCard(Activity a) {
        activity = a;
        music = MaximoMusic.get(a);

        overlay = (ConstraintLayout) a.getLayoutInflater().inflate(R.layout.game_music_card, null);
        card = overlay.findViewById(R.id.mx_gm_card);
        play = overlay.findViewById(R.id.mx_gm_play);
        volume = overlay.findViewById(R.id.mx_gm_volume);
        seek = overlay.findViewById(R.id.mx_gm_seek);
        cur = overlay.findViewById(R.id.mx_gm_time_cur);
        total = overlay.findViewById(R.id.mx_gm_time_total);

        if (!music.hasGameSession()) { // nada tocando ao abrir o jogo: nao exibe o player
            dismissed = true;
            return;
        }
        a.addContentView(overlay, new ConstraintLayout.LayoutParams(-1, -1));

        overlay.findViewById(R.id.mx_gm_close).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { music.closeGamePlayer(); }
        });
        play.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { music.toggle(); }
        });
        // So existe uma faixa: anterior/proxima reiniciam a musica.
        View.OnClickListener restart = new View.OnClickListener() {
            @Override public void onClick(View v) { music.seekToMs(0); refresh(); }
        };
        overlay.findViewById(R.id.mx_gm_prev).setOnClickListener(restart);
        overlay.findViewById(R.id.mx_gm_next).setOnClickListener(restart);
        volume.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                music.setVolumePercent(music.getVolumePercent() == 0 ? 55 : 0);
                refresh();
            }
        });
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (fromUser) cur.setText(fmt((int) ((long) music.getDurationMs() * progress / 1000L)));
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { dragging = true; }
            @Override public void onStopTrackingTouch(SeekBar sb) {
                music.seekToMs((int) ((long) music.getDurationMs() * sb.getProgress() / 1000L));
                dragging = false;
                refresh();
            }
        });

        music.addListener(listener);
        refresh();
        handler.postDelayed(ticker, 500);
    }

    /** true enquanto o card esta na tela. */
    public boolean isShowing() { return !dismissed; }

    /** Mantem o card acima da tela de loading (que usa bringToFront ao reconectar). */
    public void bringToFront() {
        if (!dismissed) overlay.bringToFront();
    }

    /** Depois do loading: canto superior direito, menor, sem cobrir os controles do jogo. */
    public void compact() {
        if (dismissed || compact) return;
        compact = true;
        applyLayoutAnimation(true);
    }

    /** Ao reconectar (loading de novo): volta ao tamanho e posicao da tela de loading. */
    public void expand() {
        if (dismissed || !compact) return;
        compact = false;
        applyLayoutAnimation(false);
    }

    private void applyLayoutAnimation(final boolean toCompact) {
        card.post(new Runnable() {
            @Override public void run() {
                if (dismissed) return;
                float margin = activity.getResources().getDisplayMetrics().density * 8f;
                card.setPivotX(card.getWidth());
                card.setPivotY(0f);
                if (toCompact) {
                    float tx = (overlay.getWidth() - card.getRight()) - margin;
                    float ty = -card.getTop() + margin;
                    card.animate().scaleX(COMPACT_SCALE).scaleY(COMPACT_SCALE)
                            .translationX(tx).translationY(ty).setDuration(350).start();
                } else {
                    card.animate().scaleX(1f).scaleY(1f).translationX(0f).translationY(0f).setDuration(350).start();
                }
            }
        });
    }

    private void refresh() {
        if (dismissed) return;
        if (!music.hasGameSession()) { // musica terminou ou foi fechada no X
            dismiss();
            return;
        }
        int dur = music.getDurationMs();
        int pos = Math.min(music.getPositionMs(), dur > 0 ? dur : Integer.MAX_VALUE);
        if (!dragging) {
            seek.setProgress(dur > 0 ? (int) ((long) pos * 1000L / dur) : 0);
            cur.setText(fmt(pos));
        }
        total.setText(fmt(dur));
        play.setImageResource(music.isPlaying() ? R.drawable.mx_ic_pause : R.drawable.mx_ic_play);
        volume.setImageResource(music.getVolumePercent() == 0 ? R.drawable.mx_ic_volume_off : R.drawable.mx_ic_volume);
    }

    /** Remove o card (fim da musica, X ou fechamento da Activity). Nao para a musica por conta propria. */
    public void dismiss() {
        if (dismissed) return;
        dismissed = true;
        handler.removeCallbacks(ticker);
        music.removeListener(listener);
        card.animate().cancel();
        overlay.setVisibility(View.GONE);
    }

    private static String fmt(int ms) {
        int s = Math.max(0, ms / 1000);
        return String.format(Locale.US, "%02d:%02d", s / 60, s % 60);
    }
}
