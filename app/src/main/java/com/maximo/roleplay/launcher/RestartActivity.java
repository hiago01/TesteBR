package com.maximo.roleplay.launcher;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/**
 * Reabre o aplicativo do zero (fecha o processo e abre de novo). Roda em OUTRO processo (":restart", ver manifesto):
 * assim ele sobrevive a morte do processo principal e inicia a tela inicial num processo limpo.
 */
public class RestartActivity extends Activity {
    private static final String EXTRA_NEXT = "next";

    /** Fecha o app e abre de novo. Chamar com o app visivel (restricao de abrir atividade em segundo plano). */
    public static void restart(Activity a) {
        Intent next = a.getPackageManager().getLaunchIntentForPackage(a.getPackageName());
        if (next == null) throw new IllegalStateException("sem intent de abertura");
        next.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        Intent i = new Intent(a, RestartActivity.class);
        i.putExtra(EXTRA_NEXT, next);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION);
        a.startActivity(i);
        a.finishAffinity();
        Runtime.getRuntime().exit(0);   // encerra o processo principal; a RestartActivity (outro processo) abre o app
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent next = getIntent() != null ? (Intent) getIntent().getParcelableExtra(EXTRA_NEXT) : null;
        if (next != null) startActivity(next);
        finish();
        Process.killProcess(Process.myPid());
    }
}
