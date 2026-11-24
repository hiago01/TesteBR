package com.maximo.roleplay.launcher.util;

import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.util.DisplayMetrics;
import android.view.Window;
import android.view.WindowManager;

/** Deixa um Dialog no tema de Natal: janela transparente (o layout desenha o proprio cartao), largura confortavel e fundo escurecido. */
public final class XmasDialogStyle {
    private XmasDialogStyle() {}

    public static void apply(Dialog d, int maxWidthDp) {
        Window w = d.getWindow();
        if (w == null) return;
        w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        DisplayMetrics dm = d.getContext().getResources().getDisplayMetrics();
        int maxPx = Math.round(maxWidthDp * dm.density);
        int width = Math.min(maxPx, Math.round(dm.widthPixels * 0.92f));
        w.setLayout(width, WindowManager.LayoutParams.WRAP_CONTENT);
        w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        w.setDimAmount(0.62f);
    }
}
