package com.samp.mobile.launcher.util;

import android.content.Context;
import android.graphics.Matrix;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;

import androidx.appcompat.widget.AppCompatImageView;

/**
 * Fundo cinematografico do launcher. Preenche a tela inteira (como centerCrop) mas ancora a
 * imagem no topo-direita, para que o personagem nunca seja cortado em telas mais largas
 * (18:9, 20:9...) ou mais quadradas.
 */
public class HeroImageView extends AppCompatImageView {

    public HeroImageView(Context context) {
        super(context);
        init();
    }

    public HeroImageView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public HeroImageView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        setScaleType(ScaleType.MATRIX);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        applyMatrix();
    }

    @Override
    public void setImageDrawable(Drawable drawable) {
        super.setImageDrawable(drawable);
        applyMatrix();
    }

    private void applyMatrix() {
        Drawable d = getDrawable();
        int vw = getWidth();
        int vh = getHeight();
        if (d == null || vw <= 0 || vh <= 0) return;
        int dw = d.getIntrinsicWidth();
        int dh = d.getIntrinsicHeight();
        if (dw <= 0 || dh <= 0) return;
        float scale = Math.max((float) vw / dw, (float) vh / dh);
        Matrix m = new Matrix();
        m.setScale(scale, scale);
        m.postTranslate(vw - dw * scale, 0f); // alinha a direita e ao topo
        setImageMatrix(m);
    }
}
