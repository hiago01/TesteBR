package com.maximo.roleplay.launcher.util;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import com.maximo.roleplay.R;

/**
 * Enfeite de Natal do launcher: fio de PISCA-PISCA (lampadas coloridas com brilho que piscam em varios
 * padroes: aleatorio, corrida, alternado e tudo aceso) e NEVE caindo. Nao recebe toque (os toques passam
 * para as telas por baixo). Anima so enquanto estiver na tela (anexada e visivel).
 */
public class XmasLightsView extends View {

    private static final int[] BULB = {
            0xFFFF3B4E, // vermelho
            0xFFFFC83D, // dourado
            0xFF3DDC84, // verde
            0xFF3D9BFF, // azul
            0xFFE94BD8  // rosa
    };
    private static final long FRAME_MS = 33L; // ~30 quadros por segundo (leve para a bateria)

    private boolean garland = true;
    private boolean snow = true;
    private int swags = 7;
    private int flakeCount = 60;

    private final Paint wirePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint capPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bulbPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint flakePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path wirePath = new Path();
    private final Shader[] glowShaders = new Shader[BULB.length];

    private float density;
    private float glowR;
    // lampadas
    private float[] bx = new float[0], by = new float[0], bAng = new float[0], bPhase = new float[0];
    private int[] bCol = new int[0];
    // flocos
    private float[] fx = new float[0], fy = new float[0], fSpeed = new float[0], fDrift = new float[0], fSize = new float[0], fPhase = new float[0];
    private int[] fAlpha = new int[0];
    private long lastFrame;
    private boolean running;
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!running) return;
            invalidate();
            postDelayed(this, FRAME_MS);
        }
    };

    public XmasLightsView(Context c) { super(c); init(c, null); }
    public XmasLightsView(Context c, AttributeSet a) { super(c, a); init(c, a); }
    public XmasLightsView(Context c, AttributeSet a, int s) { super(c, a, s); init(c, a); }

    private void init(Context c, AttributeSet a) {
        density = c.getResources().getDisplayMetrics().density;
        if (a != null) {
            TypedArray t = c.obtainStyledAttributes(a, R.styleable.XmasLightsView);
            try {
                garland = t.getBoolean(R.styleable.XmasLightsView_xmasGarland, true);
                snow = t.getBoolean(R.styleable.XmasLightsView_xmasSnow, true);
                swags = Math.max(2, t.getInt(R.styleable.XmasLightsView_xmasSwags, 7));
                flakeCount = Math.max(0, t.getInt(R.styleable.XmasLightsView_xmasFlakes, 60));
            } finally { t.recycle(); }
        }
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        wirePaint.setStyle(Paint.Style.STROKE);
        wirePaint.setStrokeWidth(1.6f * density);
        wirePaint.setColor(0xFF0F2A1D);
        wirePaint.setStrokeCap(Paint.Cap.ROUND);
        capPaint.setStyle(Paint.Style.FILL);
        capPaint.setColor(0xFF2A1F14);
        flakePaint.setStyle(Paint.Style.FILL);
        flakePaint.setColor(Color.WHITE);
        glowR = 13f * density;
        for (int i = 0; i < BULB.length; i++) {
            int col = BULB[i];
            glowShaders[i] = new RadialGradient(0f, 0f, glowR,
                    new int[]{(col & 0x00FFFFFF) | 0xCC000000, (col & 0x00FFFFFF) | 0x55000000, (col & 0x00FFFFFF)},
                    new float[]{0f, 0.45f, 1f}, Shader.TileMode.CLAMP);
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        buildGarland(w);
        buildSnow(w, h);
    }

    // ---- fio: varios "swags" (curvas) de ponta a ponta, com lampadas penduradas
    private void buildGarland(int w) {
        if (!garland || w <= 0) { bx = by = bAng = bPhase = new float[0]; bCol = new int[0]; wirePath.reset(); return; }
        float y0 = 2f * density, sag = Math.min(16f * density, getHeight() * 0.5f);
        float step = w / (float) swags;
        wirePath.reset();
        int perSwag = Math.max(3, Math.round(step / (30f * density)));
        int total = swags * perSwag;
        bx = new float[total]; by = new float[total]; bAng = new float[total]; bPhase = new float[total]; bCol = new int[total];
        java.util.Random rnd = new java.util.Random(7);
        int n = 0;
        for (int s = 0; s < swags; s++) {
            float x0 = s * step, x1 = x0 + step, xm = (x0 + x1) * 0.5f;
            wirePath.moveTo(x0, y0);
            wirePath.quadTo(xm, y0 + sag * 2f, x1, y0);
            for (int i = 0; i < perSwag; i++) {
                float t = (i + 0.5f) / perSwag;
                float u = 1f - t;
                float x = u * u * x0 + 2f * u * t * xm + t * t * x1;
                float y = u * u * y0 + 2f * u * t * (y0 + sag * 2f) + t * t * y0;
                float dx = 2f * u * (xm - x0) + 2f * t * (x1 - xm);
                float dy = 2f * u * (y0 + sag * 2f - y0) + 2f * t * (y0 - (y0 + sag * 2f));
                bx[n] = x; by[n] = y;
                bAng[n] = (float) Math.toDegrees(Math.atan2(dy, dx));
                bPhase[n] = rnd.nextFloat() * 6.2831f;
                bCol[n] = n % BULB.length;
                n++;
            }
        }
    }

    private void buildSnow(int w, int h) {
        if (!snow || w <= 0 || h <= 0 || flakeCount == 0) { fx = fy = fSpeed = fDrift = fSize = fPhase = new float[0]; fAlpha = new int[0]; return; }
        fx = new float[flakeCount]; fy = new float[flakeCount]; fSpeed = new float[flakeCount]; fDrift = new float[flakeCount];
        fSize = new float[flakeCount]; fPhase = new float[flakeCount]; fAlpha = new int[flakeCount];
        java.util.Random rnd = new java.util.Random(11);
        for (int i = 0; i < flakeCount; i++) {
            fx[i] = rnd.nextFloat() * w; fy[i] = rnd.nextFloat() * h;
            fSpeed[i] = (14f + rnd.nextFloat() * 30f) * density;   // px/s
            fDrift[i] = (6f + rnd.nextFloat() * 14f) * density;
            fSize[i] = (0.9f + rnd.nextFloat() * 1.9f) * density;
            fPhase[i] = rnd.nextFloat() * 6.2831f;
            fAlpha[i] = 90 + rnd.nextInt(130);
        }
    }

    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); start(); }
    @Override protected void onDetachedFromWindow() { stop(); super.onDetachedFromWindow(); }
    @Override protected void onVisibilityChanged(View v, int vis) { super.onVisibilityChanged(v, vis); if (vis == VISIBLE && isAttachedToWindow()) start(); else stop(); }
    @Override public void onWindowFocusChanged(boolean hasFocus) { super.onWindowFocusChanged(hasFocus); if (hasFocus && isShown()) start(); }

    private void start() {
        if (running) return;
        running = true;
        lastFrame = SystemClock.uptimeMillis();
        post(tick);
    }

    private void stop() {
        running = false;
        removeCallbacks(tick);
    }

    /** Brilho de 0..1 da lampada i no instante t (s), conforme o padrao da vez (troca a cada 8 s). */
    private float level(int i, float t) {
        int pat = ((int) (t / 8f)) % 4;
        switch (pat) {
            case 0: // piscando de forma independente
                return 0.30f + 0.70f * (0.5f + 0.5f * (float) Math.sin(t * 2.6f + bPhase[i]));
            case 1: { // corrida: uma onda de luz percorre o fio
                float w = (float) Math.sin(t * 3.2f - i * 0.55f);
                return 0.22f + 0.78f * Math.max(0f, w);
            }
            case 2: // alternado: pares e impares revezam
                return (((int) (t * 1.8f) + i) & 1) == 0 ? 1.0f : 0.22f;
            default: // tudo aceso, respirando devagar
                return 0.80f + 0.20f * (float) Math.sin(t * 1.5f + bPhase[i]);
        }
    }

    @Override
    protected void onDraw(Canvas c) {
        long now = SystemClock.uptimeMillis();
        float dt = Math.min(0.1f, (now - lastFrame) / 1000f);
        lastFrame = now;
        float t = now / 1000f;
        int w = getWidth(), h = getHeight();

        if (snow && fx.length > 0) {
            for (int i = 0; i < fx.length; i++) {
                fy[i] += fSpeed[i] * dt;
                float x = fx[i] + (float) Math.sin(t * 0.9f + fPhase[i]) * fDrift[i];
                if (fy[i] > h + 6f * density) { fy[i] = -6f * density; fx[i] = (float) (Math.random() * w); }
                flakePaint.setAlpha(fAlpha[i]);
                c.drawCircle(x, fy[i], fSize[i], flakePaint);
            }
        }

        if (garland && bx.length > 0) {
            c.drawPath(wirePath, wirePaint);
            for (int i = 0; i < bx.length; i++) {
                float lv = level(i, t);
                int ci = bCol[i];
                float rot = bAng[i];
                c.save();
                c.translate(bx[i], by[i]);
                // brilho (halo) proporcional ao nivel
                glowPaint.setShader(glowShaders[ci]);
                glowPaint.setAlpha((int) (255 * lv));
                c.drawCircle(0f, 6f * density, glowR * (0.65f + 0.35f * lv), glowPaint);
                glowPaint.setShader(null);
                // soquete + lampada pendurada
                c.rotate(rot * 0.15f);
                c.drawRoundRect(-2.2f * density, -1f * density, 2.2f * density, 3f * density, 1f * density, 1f * density, capPaint);
                int col = BULB[ci];
                int dim = 0xFF000000 | ((int) (((col >> 16) & 0xFF) * (0.35f + 0.65f * lv)) << 16)
                        | ((int) (((col >> 8) & 0xFF) * (0.35f + 0.65f * lv)) << 8)
                        | (int) ((col & 0xFF) * (0.35f + 0.65f * lv));
                bulbPaint.setColor(dim);
                c.drawOval(-2.6f * density, 2.4f * density, 2.6f * density, 9.4f * density, bulbPaint);
                bulbPaint.setColor(Color.argb((int) (170 * lv), 255, 255, 255));
                c.drawOval(-1.2f * density, 3.6f * density, 0.2f * density, 6.0f * density, bulbPaint);
                c.restore();
            }
        }
    }
}
