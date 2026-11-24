package com.maximo.roleplay.game.ui;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.View;
import android.view.animation.LinearInterpolator;

/**
 * Barra fina do loading (mesmo estilo minimalista: trilho cinza escuro + preenchimento azul-esverdeado).
 *
 * O preenchimento mostra SOMENTE o progresso real informado por {@link #setFraction(float)}. Enquanto
 * uma etapa ainda nao terminou, um brilho discreto desliza dentro da parte ja preenchida apenas para
 * indicar que o processo esta ativo; ele nao altera a porcentagem.
 */
public class LoadingBarView extends View {
    private static final int TRACK_COLOR = 0x99140A18;
    private static final int TRACK_STROKE = 0xCC4DA3FF;
    private static final int FILL_START = 0xFFFF3B4E;
    private static final int FILL_END = 0xFF2BD0FF;
    private static final int ERROR_COLOR = 0xFFFF5C6C;

    private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint trackStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();

    private float fraction = 0f;
    private boolean active = false;
    private boolean error = false;
    private float shinePos = 0f; // 0..1
    private ValueAnimator shineAnimator;

    public LoadingBarView(Context c) { this(c, null); }
    public LoadingBarView(Context c, AttributeSet a) { this(c, a, 0); }
    public LoadingBarView(Context c, AttributeSet a, int d) {
        super(c, a, d);
        track.setColor(TRACK_COLOR);
        trackStroke.setStyle(Paint.Style.STROKE);
        trackStroke.setColor(TRACK_STROKE);
        trackStroke.setStrokeWidth(getResources().getDisplayMetrics().density * 1.2f);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        updateFillShader();
    }

    private void updateFillShader() {
        if (error) {
            fill.setShader(null);
            fill.setColor(ERROR_COLOR);
        } else {
            fill.setShader(new LinearGradient(0, 0, Math.max(1, getWidth()), 0, FILL_START, FILL_END, Shader.TileMode.CLAMP));
        }
    }

    /** Fracao real exibida (0..1). */
    public void setFraction(float f) {
        fraction = Math.max(0f, Math.min(1f, f));
        invalidate();
    }

    public float getFraction() { return fraction; }

    /** Liga/desliga o brilho de "em andamento" (nao representa progresso). */
    public void setActive(boolean a) {
        if (active == a) return;
        active = a;
        updateShine();
        invalidate();
    }

    public void setError(boolean e) {
        error = e;
        updateFillShader();
        if (e) setActive(false);
        invalidate();
    }

    private void updateShine() {
        boolean run = active && isAttachedToWindow() && getVisibility() == VISIBLE;
        if (run) {
            if (shineAnimator == null) {
                shineAnimator = ValueAnimator.ofFloat(0f, 1f);
                shineAnimator.setDuration(1400);
                shineAnimator.setRepeatCount(ValueAnimator.INFINITE);
                shineAnimator.setInterpolator(new LinearInterpolator());
                shineAnimator.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                    @Override public void onAnimationUpdate(ValueAnimator a) {
                        shinePos = (float) a.getAnimatedValue();
                        invalidate();
                    }
                });
            }
            if (!shineAnimator.isStarted()) shineAnimator.start();
        } else if (shineAnimator != null) {
            shineAnimator.cancel();
            shineAnimator = null;
        }
    }

    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); updateShine(); }
    @Override protected void onDetachedFromWindow() { if (shineAnimator != null) { shineAnimator.cancel(); shineAnimator = null; } super.onDetachedFromWindow(); }
    @Override protected void onVisibilityChanged(View changed, int vis) { super.onVisibilityChanged(changed, vis); updateShine(); }

    @Override
    protected void onDraw(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float sw = Math.min(w, h) * 0.085f;
        float cx = w / 2f, cy = h / 2f;
        float rad = Math.min(w, h) / 2f - sw;
        rect.set(cx - rad, cy - rad, cx + rad, cy + rad);

        // disco escuro + trilho
        track.setStyle(Paint.Style.FILL);
        canvas.drawCircle(cx, cy, rad + sw * 0.4f, track);
        trackStroke.setStrokeWidth(sw);
        trackStroke.setColor(0x55FFFFFF);
        canvas.drawArc(rect, 0, 360, false, trackStroke);

        // progresso real (comeca no topo, sentido horario)
        float sweep = 360f * fraction;
        if (sweep > 0f) {
            fill.setStyle(Paint.Style.STROKE);
            fill.setStrokeWidth(sw);
            fill.setStrokeCap(Paint.Cap.ROUND);
            if (error) {
                fill.setShader(null);
                fill.setColor(ERROR_COLOR);
            } else {
                fill.setShader(new android.graphics.SweepGradient(cx, cy,
                        new int[]{0xFFFF3B4E, 0xFFFFC83D, 0xFF3DDC84, 0xFFFF3B4E}, null));
            }
            int sv = canvas.save();
            canvas.rotate(-90f, cx, cy);
            canvas.drawArc(rect, 0, sweep, false, fill);
            canvas.restoreToCount(sv);
        }

        // ponto de luz girando enquanto a etapa esta ativa (nao representa progresso)
        if (active && !error) {
            float ang = (float) Math.toRadians(shinePos * 360f - 90f);
            shine.setShader(null);
            shine.setColor(0xCCFFFFFF);
            canvas.drawCircle(cx + (float) Math.cos(ang) * (rad + sw * 1.15f), cy + (float) Math.sin(ang) * (rad + sw * 1.15f), sw * 0.32f, shine);
        }
    }
}
