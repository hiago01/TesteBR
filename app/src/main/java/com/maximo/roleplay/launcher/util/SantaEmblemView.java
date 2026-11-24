package com.maximo.roleplay.launcher.util;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.View;

import com.maximo.roleplay.R;

/**
 * Banner animado: brasao do servidor flutuando enquanto as maos do Papai Noel o seguram e mexem.
 * Anima so enquanto esta anexada e visivel (~30 fps). Nao e interativa.
 */
public class SantaEmblemView extends View {
    private static final long FRAME_MS = 33L;

    private Bitmap emblem, handL, handR;
    private final Paint bmp = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint spark = new Paint(Paint.ANTI_ALIAS_FLAG);
    private long t0;
    private boolean running;
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!running) return;
            invalidate();
            postDelayed(this, FRAME_MS);
        }
    };

    public SantaEmblemView(Context c) { super(c); init(); }
    public SantaEmblemView(Context c, AttributeSet a) { super(c, a); init(); }
    public SantaEmblemView(Context c, AttributeSet a, int s) { super(c, a, s); init(); }

    private void init() {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inScaled = false;
        emblem = BitmapFactory.decodeResource(getResources(), R.drawable.mx_santa_emblem, o);
        handL = BitmapFactory.decodeResource(getResources(), R.drawable.mx_santa_hand_l, o);
        handR = BitmapFactory.decodeResource(getResources(), R.drawable.mx_santa_hand_r, o);
        t0 = System.currentTimeMillis();
        setWillNotDraw(false);
    }

    private void update() {
        boolean run = isAttachedToWindow() && getVisibility() == VISIBLE && getWindowVisibility() == VISIBLE;
        if (run == running) return;
        running = run;
        removeCallbacks(tick);
        if (run) post(tick);
    }

    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); update(); }
    @Override protected void onDetachedFromWindow() { running = false; removeCallbacks(tick); super.onDetachedFromWindow(); }
    @Override protected void onVisibilityChanged(View v, int vis) { super.onVisibilityChanged(v, vis); update(); }
    @Override protected void onWindowVisibilityChanged(int vis) { super.onWindowVisibilityChanged(vis); update(); }

    @Override
    protected void onDraw(Canvas canvas) {
        if (emblem == null || handL == null || handR == null) return;
        float w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        float t = (System.currentTimeMillis() - t0) / 1000f;

        // brasao: ocupa ~86% da altura, centralizado, flutua e balanca de leve
        float eh = h * 0.86f;
        float es = eh / emblem.getHeight();
        float ew = emblem.getWidth() * es;
        float cx = w / 2f;
        float cy = h * 0.46f + (float) Math.sin(t * 1.8f) * h * 0.025f;
        float rot = (float) Math.sin(t * 1.2f) * 3.2f;

        // brilho pulsando atras
        float gr = eh * (0.78f + 0.06f * (float) Math.sin(t * 2.4f));
        glow.setShader(new RadialGradient(cx, cy, gr, new int[]{0x663D9BFF, 0x1A3D9BFF, 0x003D9BFF},
                new float[]{0f, 0.6f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, gr, glow);

        int sv = canvas.save();
        canvas.rotate(rot, cx, cy);
        canvas.translate(cx - ew / 2f, cy - eh / 2f);
        canvas.scale(es, es);
        canvas.drawBitmap(emblem, 0, 0, bmp);
        canvas.restoreToCount(sv);

        // maos: altura ~46% da altura do banner; chegam e saem do brasao como se o ajeitassem
        float hh = h * 0.46f;
        float ls = hh / handL.getHeight();
        float rs = hh / handR.getHeight();
        float pushL = (float) Math.sin(t * 2.2f);          // -1..1
        float pushR = (float) Math.sin(t * 2.2f + 1.9f);

        // mao esquerda (indicador apontando p/ direita): base no canto inferior esquerdo do brasao
        float lw = handL.getWidth() * ls;
        float lx = cx - ew * 0.5f - lw * 0.30f + pushL * w * 0.018f;
        float ly = h - hh - h * 0.02f - pushL * h * 0.02f;
        sv = canvas.save();
        canvas.rotate(-6f * pushL, lx + lw * 0.2f, ly + hh);
        canvas.translate(lx, ly);
        canvas.scale(ls, ls);
        canvas.drawBitmap(handL, 0, 0, bmp);
        canvas.restoreToCount(sv);

        // mao direita (palma aberta): canto inferior direito
        float rw = handR.getWidth() * rs;
        float rx = cx + ew * 0.5f - rw * 0.72f - pushR * w * 0.018f;
        float ry = h - hh - h * 0.02f - pushR * h * 0.02f;
        sv = canvas.save();
        canvas.rotate(6f * pushR, rx + rw * 0.8f, ry + hh);
        canvas.translate(rx, ry);
        canvas.scale(rs, rs);
        canvas.drawBitmap(handR, 0, 0, bmp);
        canvas.restoreToCount(sv);

        // brilhos piscando ao redor do brasao
        for (int i = 0; i < 6; i++) {
            float ph = t * 1.6f + i * 1.05f;
            float a = (float) (0.5 + 0.5 * Math.sin(ph * 2.0));
            float ang = i * 1.047f + t * 0.3f;
            float sx = cx + (float) Math.cos(ang) * ew * 0.52f;
            float sy = cy + (float) Math.sin(ang) * eh * 0.42f;
            spark.setColor(((int) (a * 235) << 24) | 0x00FFF2B0);
            float r = h * 0.014f * (0.6f + a);
            canvas.drawCircle(sx, sy, r, spark);
            spark.setColor(((int) (a * 140) << 24) | 0x00FFFFFF);
            canvas.drawRect(sx - r * 3.2f, sy - r * 0.25f, sx + r * 3.2f, sy + r * 0.25f, spark);
            canvas.drawRect(sx - r * 0.25f, sy - r * 3.2f, sx + r * 0.25f, sy + r * 3.2f, spark);
        }
    }
}
