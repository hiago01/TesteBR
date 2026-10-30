package com.samp.mobile.launcher.util;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.ScrollView;

/** ScrollView com altura maxima (px). Usado no cartao de texto do dialogo do jogo (DialogManager). */
public class MaxHeightScrollView extends ScrollView {
    private int mMaxHeightPx = 0;

    public MaxHeightScrollView(Context context) { super(context); }
    public MaxHeightScrollView(Context context, AttributeSet attrs) { super(context, attrs); }
    public MaxHeightScrollView(Context context, AttributeSet attrs, int defStyleAttr) { super(context, attrs, defStyleAttr); }

    public void setMaxHeightPx(int px) {
        mMaxHeightPx = px;
        requestLayout();
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        if (mMaxHeightPx > 0) {
            int mode = View.MeasureSpec.getMode(heightMeasureSpec);
            int size = View.MeasureSpec.getSize(heightMeasureSpec);
            if (mode == View.MeasureSpec.UNSPECIFIED || size > mMaxHeightPx) {
                heightMeasureSpec = View.MeasureSpec.makeMeasureSpec(mMaxHeightPx, View.MeasureSpec.AT_MOST);
            }
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }
}
