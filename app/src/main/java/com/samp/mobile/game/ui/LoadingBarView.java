package com.samp.mobile.game.ui;

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
    private static final int TRACK_COLOR = 0x33FFFFFF;
    private static final int FILL_COLOR = 0xFF7BC1C3;
    private static final int ERROR_COLOR = 0xFFFF5C6C;

    private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG);
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
        fill.setColor(FILL_COLOR);
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
        fill.setColor(e ? ERROR_COLOR : FILL_COLOR);
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
        float r = h / 2f;
        rect.set(0, 0, w, h);
        canvas.drawRoundRect(rect, r, r, track);

        float fw = w * fraction;
        if (fw <= 0f) return;
        rect.set(0, 0, Math.max(fw, h), h);
        canvas.drawRoundRect(rect, r, r, fill);

        if (active && !error && fw > h * 2) {
            float sw = Math.min(fw, w * 0.22f);
            float x = -sw + (fw + sw) * shinePos;
            shine.setShader(new LinearGradient(x, 0, x + sw, 0,
                    new int[]{0x00FFFFFF, 0x66FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
            int save = canvas.save();
            rect.set(0, 0, fw, h);
            canvas.clipRect(rect);
            canvas.drawRect(x, 0, x + sw, h, shine);
            canvas.restoreToCount(save);
        }
    }
}
