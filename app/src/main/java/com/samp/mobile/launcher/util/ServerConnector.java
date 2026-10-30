package com.samp.mobile.launcher.util;

import android.app.Activity;
import android.content.Intent;
import android.widget.Toast;

import com.samp.mobile.game.SAMP;

import org.ini4j.Wini;

import java.io.File;
import java.io.IOException;

/**
 * Fluxo de conexao do SA-MP Mobile, extraido SEM alteracao de logica de
 * ServerInformationFragment (botao Connect original): grava host/port em SAMP/settings.ini,
 * confere os arquivos obrigatorios do jogo e so entao abre a Activity SAMP (GTA SA + SA-MP).
 * O cartao novo e o dialogo antigo chamam este mesmo metodo.
 */
public final class ServerConnector {

    private ServerConnector() {}

    /** @return true se o jogo foi iniciado; false se faltam arquivos (o usuario e avisado). */
    public static boolean connect(Activity activity, SAMPServerInfo info) {
        KeyboardGuard.beforeLaunch(activity); // ANTES de gravar host/port: pode restaurar o settings.ini
        File file = new File(activity.getExternalFilesDir(null) + "/SAMP/settings.ini");
        if (file.exists()) {
            try {
                Wini wini = new Wini(file);
                wini.put("client", "host", info.getAddress());
                wini.put("client", "port", info.getPort());
                wini.store();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        File file1 = new File(activity.getExternalFilesDir(null) + "/Text/american.dxt");
        if (!file1.exists()) {
            File file2 = new File(activity.getExternalFilesDir(null) + "/Textures/fonts/RussianFont.png");
            if (!file2.exists()) {
                Toast.makeText(activity, "Some important files in your modified data are missing, such as \"Text\" and \"Textures\"" +
                        "Please, fix it and after try again. ( You can get that files in my discord channel )", Toast.LENGTH_LONG).show();
                return false;
            }
        }
        MaximoMusic.get(activity).enterGameMode();
        activity.startActivity(new Intent(activity, SAMP.class));
        activity.finish();
        return true;
    }
}
