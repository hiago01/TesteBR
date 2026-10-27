package com.samp.mobile.launcher.util;

import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.DisplayCutoutCompat;

/** Tela cheia imersiva em landscape e margens seguras para notch/recortes. */
public final class LauncherUi {
    private LauncherUi() {}

    /** Chamar ANTES de setContentView. */
    public static void prepareWindow(Activity activity) {
        Window w = activity.getWindow();
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }
    }

    @SuppressWarnings("deprecation")
    public static void applyImmersive(Activity activity) {
        activity.getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    /** Aplica como padding do container as maiores margens entre barras de sistema e recorte. */
    @SuppressWarnings("deprecation")
    public static void applySafePadding(final View container) {
        ViewCompat.setOnApplyWindowInsetsListener(container, (v, insets) -> {
            int l = insets.getSystemWindowInsetLeft();
            int t = insets.getSystemWindowInsetTop();
            int r = insets.getSystemWindowInsetRight();
            DisplayCutoutCompat cut = insets.getDisplayCutout();
            if (cut != null) {
                l = Math.max(l, cut.getSafeInsetLeft());
                t = Math.max(t, cut.getSafeInsetTop());
                r = Math.max(r, cut.getSafeInsetRight());
            }
            v.setPadding(l, t, r, 0); // rodape encosta embaixo (barra de navegacao fica oculta)
            return insets;
        });
        ViewCompat.requestApplyInsets(container);
    }
}
