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
    private static final int TRACK_COLOR = 0x990A1A33;
    private static final int TRACK_STROKE = 0xCC4DA3FF;
    private static final int FILL_START = 0xFF1E7CFF;
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
        float r = h / 2f;
        float inset = trackStroke.getStrokeWidth() / 2f;
        rect.set(inset, inset, w - inset, h - inset);
        canvas.drawRoundRect(rect, r, r, track);
        canvas.drawRoundRect(rect, r, r, trackStroke);

        float pad = trackStroke.getStrokeWidth() * 1.6f;
        float inner = w - 2 * pad;
        float fw = inner * fraction;
        if (fw <= 0f) return;
        float ih = h - 2 * pad;
        rect.set(pad, pad, pad + Math.max(fw, ih), h - pad);
        canvas.drawRoundRect(rect, ih / 2f, ih / 2f, fill);

        if (active && !error && fw > h * 2) {
            float sw = Math.min(fw, w * 0.22f);
            float x = -sw + (fw + sw) * shinePos;
            shine.setShader(new LinearGradient(x, 0, x + sw, 0,
                    new int[]{0x00FFFFFF, 0x66FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
            int save = canvas.save();
            rect.set(pad, pad, pad + fw, h - pad);
            canvas.clipRect(rect);
            canvas.drawRect(pad + x, pad, pad + x + sw, h - pad, shine);
            canvas.restoreToCount(save);
        }
    }
}
