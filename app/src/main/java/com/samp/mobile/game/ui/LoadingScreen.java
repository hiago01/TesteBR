package com.samp.mobile.game.ui;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.app.Activity;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;

import com.samp.mobile.R;

/**
 * Segunda tela de loading (logo + barra). O progresso e DIRIGIDO POR EVENTOS REAIS do processo de
 * inicializacao; nao existe temporizador que "encha" a barra:
 *
 *  0  STARTING            SAMP.onCreate (Java) - tela criada
 *  1  NATIVE_READY        initializeSAMP() retornou (ponte JNI pronta)
 *  2  ENGINE_RUNNING      libGTASA terminou de subir e o 1o quadro do loop principal rodou (nativo)
 *  3  GAME_INIT           CGame::Initialize concluido, SA-MP carregado no motor (nativo)
 *  4  NET_CREATED         CNetGame criado (nativo)
 *  5  CONNECTING          1a tentativa de conexao RakNet enviada (nativo)
 *  6/8/9                  somente texto: sem resposta / servidor cheio / recusado (nao andam a barra)
 *  7  CONNECTED           conexao aceita pelo servidor -> 100% e a tela e encerrada (nativo)
 *
 * As porcentagens abaixo sao os valores atribuidos as etapas JA CONCLUIDAS (nao uma estimativa de
 * tempo). Entre duas etapas a barra permanece no valor da ultima concluida e um brilho discreto indica
 * que o processo continua; o motor do GTA nao informa a porcentagem interna de seu proprio carregamento.
 * A barra nunca recua (exceto ao reconectar) e so chega a 100% quando a conexao realmente conclui.
 *
 * Todos os metodos devem ser chamados na thread principal.
 */
public class LoadingScreen {

    public static final int STAGE_STARTING = 0;
    public static final int STAGE_NATIVE_READY = 1;
    public static final int STAGE_ENGINE_RUNNING = 2;
    public static final int STAGE_GAME_INIT = 3;
    public static final int STAGE_NET_CREATED = 4;
    public static final int STAGE_CONNECTING = 5;
    public static final int STAGE_RETRY_NO_RESPONSE = 6;
    public static final int STAGE_CONNECTED = 7;
    public static final int STAGE_RETRY_FULL = 8;
    public static final int STAGE_REFUSED = 9;

    /** Progresso (0..100) atribuido a cada etapa concluida. -1 = etapa so de texto. */
    private static final int[] PERCENT = { 0, 15, 40, 60, 75, 85, -1, 100, -1, -1 };

    private final Activity activity;
    private final ConstraintLayout mainLayout;
    private final LoadingBarView bar;
    private final TextView title;
    private final TextView step;
    private final TextView pctView;

    /** Avisos para quem precisa reagir a tela aparecer/sumir (ex.: card de musica). */
    public interface Callback { void onLoadingShown(); void onLoadingHidden(); }
    private Callback callback;
    public void setCallback(Callback c) { callback = c; }

    private float displayed = 0f;     // 0..1 exibido
    private float target = 0f;        // 0..1 da ultima etapa concluida
    private ValueAnimator anim;
    private boolean finishing = false;
    private boolean released = false;

    public LoadingScreen(Activity activity)
    {
        this.activity = activity;

        mainLayout = (ConstraintLayout) activity.getLayoutInflater().inflate(R.layout.loadingscreen, null);
        bar = mainLayout.findViewById(R.id.progressBar2);
        title = mainLayout.findViewById(R.id.mx_loading_text);
        step = mainLayout.findViewById(R.id.mx_loading_step);
        pctView = mainLayout.findViewById(R.id.mx_loading_pct);
        activity.addContentView(mainLayout, new ConstraintLayout.LayoutParams(-1, -1));
        resetVisual();
    }

    private void resetVisual() {
        cancelAnim();
        finishing = false;
        displayed = 0f;
        target = 0f;
        mainLayout.animate().cancel();
        mainLayout.setAlpha(1f);
        mainLayout.setVisibility(View.VISIBLE);
        bar.setError(false);
        bar.setFraction(0f);
        bar.setActive(true);
        title.setTextColor(0xFFFFFFFF);
        pctView.setTextColor(0xFFFFFFFF);
        render(0f);
        step.setText(R.string.mx_loading_step_start);
    }

    /** Registra uma etapa REAL concluida. Chamadas fora de ordem/repetidas nao fazem a barra recuar. */
    public void setStage(int stage) {
        if (released || finishing || stage < 0 || stage >= PERCENT.length) return;
        if (mainLayout.getVisibility() != View.VISIBLE) return; // tela ja encerrada: ignora eventos tardios

        switch (stage) {
            case STAGE_STARTING:          step.setText(R.string.mx_loading_step_start); break;
            case STAGE_NATIVE_READY:      step.setText(R.string.mx_loading_step_engine); break;
            case STAGE_ENGINE_RUNNING:    step.setText(R.string.mx_loading_step_samp); break;
            case STAGE_GAME_INIT:         step.setText(R.string.mx_loading_step_prepare); break;
            case STAGE_NET_CREATED:       step.setText(R.string.mx_loading_step_connect); break;
            case STAGE_CONNECTING:        step.setText(R.string.mx_loading_step_wait); break;
            case STAGE_RETRY_NO_RESPONSE: step.setText(R.string.mx_loading_step_retry); break;
            case STAGE_RETRY_FULL:        step.setText(R.string.mx_loading_step_full); break;
            case STAGE_REFUSED:           step.setText(R.string.mx_loading_step_refused); break;
            case STAGE_CONNECTED:         step.setText(R.string.mx_loading_step_connected); break;
            default: break;
        }
        bar.setError(false);
        bar.setActive(true);

        int pct = PERCENT[stage];
        if (pct < 0) return; // so texto
        float t = pct / 100f;
        if (t > target) {
            target = t;
            animateTo(t, stage == STAGE_CONNECTED);
        }
    }

    /**
     * Reconexao apos perder a conexao (ou reinicio do game mode): reexibe a tela no ponto "rede
     * criada". Este e o unico caso em que a barra volta atras, porque o jogo realmente voltou a esperar
     * a conexao.
     */
    public void showReconnecting() {
        if (released) return;
        resetVisual();
        step.setText(R.string.mx_loading_step_reconnect);
        mainLayout.bringToFront();
        if (callback != null) callback.onLoadingShown();
        target = PERCENT[STAGE_NET_CREATED] / 100f;
        displayed = target;
        bar.setFraction(displayed);
        render(displayed);
    }

    /** Falha de inicializacao: mostra o erro (sem porcentagem enganosa) e mantem a tela. */
    public void showError(int messageRes) {
        if (released) return;
        cancelAnim();
        bar.setActive(false);
        bar.setError(true);
        title.setTextColor(0xFFFF5C6C);
        pctView.setTextColor(0xFFFF5C6C);
        title.setText(R.string.mx_loading_title);
        step.setText(messageRes);
        mainLayout.setVisibility(View.VISIBLE);
    }

    /** Chamado quando o jogo confirma a conclusao: completa a barra em 100% e encerra a tela. */
    public void hide()
    {
        if (released || mainLayout.getVisibility() != View.VISIBLE || finishing) return;
        step.setText(R.string.mx_loading_step_connected);
        target = 1f;
        animateTo(1f, true);
    }

    /** Cancela animacoes (onDestroy). */
    public void release() {
        released = true;
        cancelAnim();
        mainLayout.animate().cancel();
        bar.setActive(false);
    }

    // ------------------------------------------------------------------------------------

    private void animateTo(float to, final boolean thenHide) {
        cancelAnim();
        if (thenHide) finishing = true;
        final float from = displayed;
        float delta = Math.abs(to - from);
        long duration = Math.max(180L, Math.min(700L, (long) (900L * delta)));
        anim = ValueAnimator.ofFloat(from, to);
        anim.setDuration(duration);
        anim.setInterpolator(new DecelerateInterpolator());
        anim.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator a) {
                displayed = (float) a.getAnimatedValue();
                bar.setFraction(displayed);
                render(displayed);
            }
        });
        if (thenHide) {
            anim.addListener(new AnimatorListenerAdapter() {
                private boolean canceled = false;
                @Override public void onAnimationCancel(Animator a) { canceled = true; }
                @Override public void onAnimationEnd(Animator a) {
                    if (canceled || released) return;
                    displayed = 1f;
                    bar.setFraction(1f);
                    render(1f);
                    bar.setActive(false);
                    // breve pausa em 100% e some (nao fica na tela depois de concluir)
                    mainLayout.animate().alpha(0f).setStartDelay(250).setDuration(250)
                            .withEndAction(new Runnable() {
                                @Override public void run() {
                                    if (released) return;
                                    mainLayout.setVisibility(View.INVISIBLE);
                                    mainLayout.setAlpha(1f);
                                    finishing = false;
                                    if (callback != null) callback.onLoadingHidden();
                                }
                            }).start();
                }
            });
        }
        anim.start();
    }

    private void cancelAnim() {
        if (anim != null) {
            anim.cancel();
            anim = null;
        }
    }

    private void render(float f) {
        int pct = Math.round(f * 100f);
        pctView.setText(pct + "%");
        title.setText(f >= 1f ? R.string.mx_loading_done : R.string.mx_loading_title);
    }
}
