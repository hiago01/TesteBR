package com.maximo.roleplay.launcher.util;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;

import com.maximo.roleplay.R;

/** Tela "Criar conta": nick + senha + confirmacao -> AccountApi. Em caso de sucesso o nick vira o nick do launcher. */
public final class AccountDialog {
    private AccountDialog() {}

    private static final int PASS_MIN = 6;
    private static final int PASS_MAX = 32;

    /** Senha aceita pelo servidor: ASCII imprimivel sem espaco (acentos/UTF-8 gerariam hash diferente no jogo). */
    static boolean passChars(String s) {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c < 0x21 || c > 0x7e) return false;
        }
        return true;
    }

    public static void show(final Activity a) {
        if (a == null || a.isFinishing() || a.isDestroyed()) return;
        View v = a.getLayoutInflater().inflate(R.layout.mx_dialog_account, null);
        final EditText nick = v.findViewById(R.id.mx_acc_nick);
        final EditText pass = v.findViewById(R.id.mx_acc_pass);
        final EditText pass2 = v.findViewById(R.id.mx_acc_pass2);
        final TextView status = v.findViewById(R.id.mx_acc_status);
        final Button ok = v.findViewById(R.id.mx_acc_ok);
        final Button cancel = v.findViewById(R.id.mx_acc_cancel);

        String cur = NickManager.getNick(a);
        if (cur != null) nick.setText(cur);

        final AlertDialog d = new AlertDialog.Builder(a).setView(v).setCancelable(true).create();
        d.setCanceledOnTouchOutside(false);

        final boolean[] busy = { false };
        cancel.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { if (!busy[0]) d.dismiss(); }
        });
        ok.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                if (busy[0]) return;
                final String display = NickRules.normalizeDisplay(nick.getText().toString());
                final String wire = NickRules.toWire(display);
                final String p1 = pass.getText().toString();
                final String p2 = pass2.getText().toString();
                status.setTextColor(a.getColor(R.color.mx_error));
                if (!NickRules.isValidWire(wire)) { status.setText(R.string.mx_nick_err_chars); return; }
                if (p1.length() < PASS_MIN || p1.length() > PASS_MAX) { status.setText(R.string.mx_acc_err_pass_len); return; }
                if (!passChars(p1)) { status.setText(R.string.mx_acc_err_pass_chars); return; }
                if (!p1.equals(p2)) { status.setText(R.string.mx_acc_err_pass_match); return; }

                busy[0] = true;
                ok.setEnabled(false); ok.setAlpha(0.55f); cancel.setEnabled(false);
                status.setTextColor(a.getColor(R.color.mx_text_dim));
                status.setText(R.string.mx_acc_creating);
                AccountApi.create(a, wire, p1, new AccountApi.Callback() {
                    @Override public void onResult(boolean success, String message) {
                        busy[0] = false;
                        if (a.isFinishing() || a.isDestroyed() || !d.isShowing()) return;
                        if (success) {
                            NickManager.save(a, display);   // o nick da conta passa a ser o nick do jogo
                            pass.setText(""); pass2.setText("");
                            status.setTextColor(a.getColor(R.color.mx_text_dim));
                            status.setText(a.getString(R.string.mx_acc_done, wire));
                            ok.setVisibility(View.GONE);
                            cancel.setEnabled(true);
                            cancel.setText(android.R.string.ok);
                        } else {
                            status.setTextColor(a.getColor(R.color.mx_error));
                            status.setText(message);
                            ok.setEnabled(true); ok.setAlpha(1f); cancel.setEnabled(true);
                        }
                    }
                });
            }
        });
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
        (cur == null ? nick : pass).requestFocus();
    }
}
