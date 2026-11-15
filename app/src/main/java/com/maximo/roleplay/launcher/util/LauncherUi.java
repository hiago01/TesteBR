package com.maximo.roleplay.launcher.util;

import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

/**
 * Tela cheia imersiva em landscape e margens seguras para notch/recortes.
 * Android 15/16 (targetSdk 35+): a janela e sempre borda a borda; as barras do sistema ficam ocultas (deslizar mostra
 * por instantes) e o conteudo respeita so o recorte (notch) da tela.
 */
public final class LauncherUi {
    private LauncherUi() {}

    /** Chamar ANTES de setContentView. */
    public static void prepareWindow(Activity activity) {
        Window w = activity.getWindow();
        WindowCompat.setDecorFitsSystemWindows(w, false);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }
    }

    public static void applyImmersive(Activity activity) {
        Window w = activity.getWindow();
        View decor = w.getDecorView();
        WindowInsetsControllerCompat c = WindowCompat.getInsetsController(w, decor);
        c.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        c.hide(WindowInsetsCompat.Type.systemBars());
    }

    /** Aplica como padding do container as maiores margens entre barras de sistema e recorte. */
    public static void applySafePadding(final View container) {
        ViewCompat.setOnApplyWindowInsetsListener(container, (v, insets) -> {
            Insets i = insets.getInsets(WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
            v.setPadding(i.left, i.top, i.right, 0); // rodape encosta embaixo (barra de navegacao fica oculta)
            return insets;
        });
        ViewCompat.requestApplyInsets(container);
    }
}
