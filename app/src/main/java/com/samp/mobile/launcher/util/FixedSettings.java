package com.samp.mobile.launcher.util;

import android.content.Context;

import org.ini4j.Wini;

import java.io.File;
import java.io.IOException;

/** Opcoes travadas: Keyboard e Voice chat sempre ligados; versao sempre a ultima. */
public final class FixedSettings {
    public static final String[] VERSIONS = {"0.3.7", "0.3.7-R1", "0.3.7-R3", "0.3.7-R4", "0.3.7-R5"};
    public static final int LAST_VERSION = VERSIONS.length - 1;

    private FixedSettings() {}

    public static void apply(Context ctx) {
        if (ctx == null) return;
        Context app = ctx.getApplicationContext();
        SharedPreferenceCore p = new SharedPreferenceCore();
        p.setBoolean(app, "ANDROID_KEYBOARD", true);
        p.setBoolean(app, "AIM", true);
        p.setInt(app, "VERSION", LAST_VERSION);
        try {
            File dir = app.getExternalFilesDir(null);
            if (dir == null) return;
            File f = new File(dir, "SAMP/settings.ini");
            if (!f.exists()) return;
            Wini w = new Wini(f);
            w.put("gui", "androidkeyboard", true);
            w.put("gui", "autoaim", true);
            w.put("gui", "VoiceChatEnable", true);
            w.put("client", "autoaim", true);
            w.put("client", "version", VERSIONS[LAST_VERSION]);
            w.store();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
