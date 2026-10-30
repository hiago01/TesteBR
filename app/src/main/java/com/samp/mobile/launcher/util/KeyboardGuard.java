package com.samp.mobile.launcher.util;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.provider.Settings;
import android.view.inputmethod.InputMethodInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import com.samp.mobile.launcher.pack.IniFix;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * Analise de teclado ANTES de abrir o jogo (chamada nos 3 caminhos que iniciam a SAMP.class).
 * O cliente nativo le "SAMP/settings.ini" uma vez na partida (settings.cpp):
 *   - arquivo ausente  -> std::terminate()  (o jogo fecha);
 *   - chave "[gui] androidkeyboard" ausente/0/arquivo quebrado -> usa o teclado antigo do jogo (ENG/RUS), nao o do Android.
 * Aqui: (1) garante que o settings.ini existe e e legivel (restaura do asset, guardando o quebrado como .bad),
 * (2) grava [gui] androidkeyboard = 1 por edicao de texto tolerante (nao depende do Wini) e CONFERE relendo,
 * (3) confere se ha teclado do Android ativo. SEM nenhum teclado ativo o teclado do Android nao digita nada, entao a chave
 *     vai para 0 (teclado interno do jogo) ate o jogador ativar um; com teclado ativo (ou lista indisponivel) fica 1.
 *     Nunca bloqueia a entrada no jogo.
 */
public final class KeyboardGuard {
    private KeyboardGuard() {}

    public static final class Report {
        public boolean iniRestored, iniFixed, iniOk;
        public final List<String> notes = new ArrayList<>();
        public final List<String> keyboards = new ArrayList<>();
        public String defaultIme = "";
        public boolean imeOk = true;
        public boolean imeKnown = false;     // true = a lista de teclados ativos foi obtida (vazia ou nao)
        public String wanted = "1";          // valor gravado em [gui] androidkeyboard
        @Override public String toString() {
            StringBuilder b = new StringBuilder();
            b.append("settings.ini: ").append(iniOk ? "OK (androidkeyboard=" + wanted + ")" : "PROBLEMA")
                    .append(iniRestored ? " [restaurado do APK]" : "").append(iniFixed ? " [corrigido]" : "").append('\n');
            b.append("teclado padrao do Android: ").append(defaultIme.isEmpty() ? "(nenhum)" : defaultIme).append('\n');
            b.append("teclados ativos: ").append(keyboards.isEmpty() ? "(nenhum)" : keyboards.toString()).append('\n');
            for (String n : notes) b.append("  - ").append(n).append('\n');
            return b.toString();
        }
    }

    private static volatile Report last;
    public static Report last() { return last; }

    /** Chamar logo antes de startActivity(SAMP). Mostra um aviso curto se nao houver teclado do Android. */
    public static void beforeLaunch(Activity a) {
        try {
            Report r = check(a);
            last = r;
            if (!r.iniOk) {
                Toast.makeText(a, "Não foi possível gravar o settings.ini. Use CORREÇÕES > Exportar relatório.", Toast.LENGTH_LONG).show();
            }
        } catch (RuntimeException e) {
            // a analise nunca pode impedir o jogador de entrar
        }
    }

    /** Analise + correcao (pode ser chamada varias vezes; e idempotente). */
    public static Report check(Context ctx) {
        Report r = new Report();
        Context app = ctx.getApplicationContext();
        analyzeIme(app, r);                                    // antes do ini: decide se a chave vai para 1 ou 0
        r.wanted = "0";                                       // o jogo usa SEMPRE o teclado nativo (nao depende do Android)
        File dir = app.getExternalFilesDir(null);
        if (dir == null) { r.notes.add("armazenamento do app indisponivel"); r.iniOk = false; return r; }
        File ini = new File(dir, "SAMP/settings.ini");

        String text = read(ini);
        if (text == null || !IniFix.looksValid(text)) {
            // ausente, vazio ou quebrado: guarda o quebrado e restaura o padrao do APK
            if (ini.exists()) {
                File bad = new File(ini.getPath() + ".bad");
                bad.delete();
                if (ini.renameTo(bad)) r.notes.add("settings.ini quebrado guardado como settings.ini.bad");
                else ini.delete();
            }
            ConfigValidator.validateConfigFiles(app); // copia assets/settings.ini
            text = read(ini);
            r.iniRestored = true;
            r.notes.add("settings.ini ausente/quebrado: restaurado do APK");
        }
        if (text == null || !IniFix.looksValid(text)) {
            r.iniOk = false;
            r.notes.add("nao foi possivel restaurar settings.ini");
        } else {
            final boolean want = "1".equals(r.wanted);
            if (IniFix.asBool(IniFix.read(text, "gui", "androidkeyboard"), false) != want) {
                String fixed = IniFix.enforce(text, "gui", "androidkeyboard", r.wanted);
                if (write(ini, fixed)) { r.iniFixed = true; r.notes.add("androidkeyboard ajustado para " + r.wanted + " (teclado nativo do jogo)"); }
                else r.notes.add("falha ao gravar settings.ini");
                text = read(ini);
            }
            r.iniOk = text != null && IniFix.asBool(IniFix.read(text, "gui", "androidkeyboard"), false) == want;
        }
        return r;
    }

    private static void analyzeIme(Context app, Report r) {
        try {
            InputMethodManager imm = (InputMethodManager) app.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                List<InputMethodInfo> list = imm.getEnabledInputMethodList();
                if (list != null) { for (InputMethodInfo i : list) r.keyboards.add(i.getPackageName()); r.imeKnown = true; }
            }
            String def = Settings.Secure.getString(app.getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD);
            r.defaultIme = def == null ? "" : def;
            // So a lista de teclados ATIVOS decide. NAO se confere o pacote do teclado padrao com PackageManager: no Android 11+
            // (targetSdk 34, sem QUERY_ALL_PACKAGES) o pacote do teclado fica invisivel e dava "nao instalado" para quem TEM Gboard.
            if (r.imeKnown && r.keyboards.isEmpty()) { r.imeOk = false; r.notes.add("nenhum teclado do Android ativo"); }
        } catch (RuntimeException e) {
            r.notes.add("nao foi possivel listar teclados: " + e);
        }
    }

    /** Abre os ajustes de teclado do Android (para o botao de ajuda). */
    public static void openInputSettings(Activity a) {
        try { a.startActivity(new Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)); } catch (RuntimeException ignored) { }
    }

    private static String read(File f) {
        try { return f.isFile() ? new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8) : null; }
        catch (IOException | RuntimeException e) { return null; }
    }

    /** Escrita atomica: .tmp -> rename (nunca deixa o settings.ini pela metade). */
    private static boolean write(File f, String text) {
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) {
            o.write(text.getBytes(StandardCharsets.UTF_8));
            o.getFD().sync();
        } catch (IOException e) { tmp.delete(); return false; }
        f.delete();
        if (tmp.renameTo(f)) return true;
        tmp.delete();
        return false;
    }
}
