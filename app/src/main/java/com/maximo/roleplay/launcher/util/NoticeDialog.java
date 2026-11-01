package com.maximo.roleplay.launcher.util;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;

import com.maximo.roleplay.R;

/** Aviso do launcher (layout mx_dialog_notice): titulo, mensagem, barra de progresso opcional e um botao opcional. */
public final class NoticeDialog {
    private final AlertDialog dialog;
    private final View root;

    private NoticeDialog(AlertDialog d, View root) { this.dialog = d; this.root = root; }

    public static NoticeDialog show(Activity a, String title, String msg, boolean progress, String btnText, final Runnable onBtn) {
        View v = a.getLayoutInflater().inflate(R.layout.mx_dialog_notice, null);
        ((TextView) v.findViewById(R.id.mx_nt_title)).setText(title);
        ((TextView) v.findViewById(R.id.mx_nt_msg)).setText(msg);
        v.findViewById(R.id.mx_nt_progress).setVisibility(progress ? View.VISIBLE : View.GONE);
        final AlertDialog dlg = new AlertDialog.Builder(a).setView(v).setCancelable(false).create();
        dlg.setCanceledOnTouchOutside(false);
        NoticeDialog nd = new NoticeDialog(dlg, v);
        nd.setButton(btnText, onBtn);
        if (!a.isFinishing() && !a.isDestroyed()) {
            dlg.show();
            if (dlg.getWindow() != null) {
                dlg.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                int w = Math.min((int) (400 * a.getResources().getDisplayMetrics().density),
                        (int) (a.getResources().getDisplayMetrics().widthPixels * 0.9f));
                dlg.getWindow().setLayout(w, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
            }
        }
        return nd;
    }

    public void update(String title, String msg, boolean progress, String btnText, Runnable onBtn) {
        if (title != null) ((TextView) root.findViewById(R.id.mx_nt_title)).setText(title);
        if (msg != null) ((TextView) root.findViewById(R.id.mx_nt_msg)).setText(msg);
        root.findViewById(R.id.mx_nt_progress).setVisibility(progress ? View.VISIBLE : View.GONE);
        setButton(btnText, onBtn);
    }

    private void setButton(String text, final Runnable onClick) {
        View b = root.findViewById(R.id.mx_nt_btn);
        if (text == null) { b.setVisibility(View.GONE); return; }
        ((TextView) root.findViewById(R.id.mx_nt_btn_text)).setText(text);
        b.setVisibility(View.VISIBLE);
        b.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { dialog.dismiss(); if (onClick != null) onClick.run(); }
        });
    }

    public void dismiss() { try { dialog.dismiss(); } catch (RuntimeException ignored) { } }
}
