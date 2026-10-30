package com.samp.mobile.launcher.util;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;

import com.samp.mobile.R;

import org.ini4j.Wini;

import java.io.File;
import java.io.IOException;
import java.util.regex.Pattern;

/**
 * Nick do jogador. Antes ficava em Configuracoes (campo solto em SAMP/settings.ini); agora e escolhido na
 * tela Servidores: ao clicar em CONECTAR pela primeira vez abre a tela "Escolha seu Nick" (obrigatoria; o nick
 * antigo das Configuracoes e ignorado) e depois o nick pode ser trocado no chip "Nick" da tela Servidores.
 * O nick escolhido e gravado em SharedPreferences e em SAMP/settings.ini ([client] name), que o jogo (C++) le.
 */
public final class NickManager {
    private static final String PREFS = "com.samp.mobile";
    private static final String KEY_NICK = "PLAYER_NICK";
    private static final String KEY_CONFIRMED = "PLAYER_NICK_CONFIRMED";

    public static final int MIN_LEN = 3;
    public static final int MAX_LEN = 20; // limite do SA-MP
    private static final Pattern VALID = Pattern.compile("^[A-Za-z0-9_\\[\\]()$@.=]{" + MIN_LEN + "," + MAX_LEN + "}$");

    private NickManager() {}

    public interface Callback { void onNick(String nick); }

    public static boolean isValid(String nick) {
        return nick != null && VALID.matcher(nick).matches();
    }

    /** null se ainda nao confirmou (primeira vez). */
    public static String getNick(Context c) {
        SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        if (!p.getBoolean(KEY_CONFIRMED, false)) return null;
        String n = p.getString(KEY_NICK, null);
        return isValid(n) ? n : null;
    }

    public static boolean hasNick(Context c) { return getNick(c) != null; }

    /** Grava o nick (prefs + settings.ini se existir). */
    public static void save(Context c, String nick) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY_NICK, nick).putBoolean(KEY_CONFIRMED, true).apply();
        writeToSettings(c, nick);
    }

    /** Copia o nick confirmado para SAMP/settings.ini (chamado de novo antes de abrir o jogo). */
    public static void writeToSettings(Context c, String nick) {
        if (nick == null) return;
        File dir = c.getExternalFilesDir(null);
        if (dir == null) return;
        File f = new File(dir, "SAMP/settings.ini");
        if (!f.exists()) return;
        try {
            Wini w = new Wini(f);
            w.put("client", "name", nick);
            w.store();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    /**
     * Garante um nick confirmado e entao chama proceed. Primeira vez: tela obrigatoria (sem cancelar).
     */
    public static void ensureNick(final Activity a, final Runnable proceed) {
        String n = getNick(a);
        if (n != null) {
            writeToSettings(a, n);
            proceed.run();
            return;
        }
        showDialog(a, true, new Callback() {
            @Override public void onNick(String nick) { proceed.run(); }
        });
    }

    /** Tela "Escolha seu Nick". mandatory=true: sem botao Cancelar e sem fechar ao tocar fora/voltar. */
    public static void showDialog(final Activity a, final boolean mandatory, final Callback done) {
        if (a == null || a.isFinishing() || a.isDestroyed()) return;
        View v = a.getLayoutInflater().inflate(R.layout.mx_dialog_nick, null);
        final EditText input = v.findViewById(R.id.mx_nick_input);
        final TextView error = v.findViewById(R.id.mx_nick_error);
        final Button ok = v.findViewById(R.id.mx_nick_ok);
        Button cancel = v.findViewById(R.id.mx_nick_cancel);
        TextView title = v.findViewById(R.id.mx_nick_title);
        title.setText(mandatory ? R.string.mx_nick_title_first : R.string.mx_nick_title_change);

        input.setFilters(new InputFilter[]{ new InputFilter.LengthFilter(MAX_LEN) });
        String cur = getNick(a);
        if (cur != null) { input.setText(cur); input.setSelection(cur.length()); }

        final AlertDialog d = new AlertDialog.Builder(a).setView(v).setCancelable(!mandatory).create();
        d.setCanceledOnTouchOutside(false);

        final Runnable validate = new Runnable() {
            @Override public void run() {
                String t = input.getText().toString().trim();
                boolean good = isValid(t);
                ok.setEnabled(good);
                ok.setAlpha(good ? 1f : 0.55f);
                if (t.isEmpty()) error.setText("");
                else if (t.length() < MIN_LEN) error.setText(R.string.mx_nick_err_short);
                else if (!good) error.setText(R.string.mx_nick_err_chars);
                else error.setText("");
            }
        };
        input.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int st, int c, int af) { }
            @Override public void onTextChanged(CharSequence s, int st, int b, int c) { }
            @Override public void afterTextChanged(Editable e) { validate.run(); }
        });
        validate.run();

        ok.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                String t = input.getText().toString().trim();
                if (!isValid(t)) { validate.run(); return; }
                save(a, t);
                d.dismiss();
                if (done != null) done.onNick(t);
            }
        });
        if (mandatory) {
            cancel.setVisibility(View.GONE);
        } else {
            cancel.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View view) { d.dismiss(); }
            });
        }
        d.show();
        if (d.getWindow() != null) {
            d.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            d.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM);
            d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE
                    | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            int w = Math.min((int) (420 * a.getResources().getDisplayMetrics().density),
                    (int) (a.getResources().getDisplayMetrics().widthPixels * 0.9f));
            d.getWindow().setLayout(w, ViewGroup.LayoutParams.WRAP_CONTENT);
        }
        input.requestFocus();
    }
}
