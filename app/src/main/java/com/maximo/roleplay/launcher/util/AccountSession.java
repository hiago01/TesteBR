package com.maximo.roleplay.launcher.util;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import com.maximo.roleplay.launcher.LoginActivity;

import org.ini4j.Wini;

import java.io.File;
import java.io.IOException;

/**
 * Login salvo do launcher. Guarda so nick, e-mail e o token de sessao (nunca a senha). O nick da conta e o nick do
 * jogo (NickManager). Antes de abrir o jogo pede ao servico um token de uso unico e grava em SAMP/settings.ini
 * ([maximo] token); o cliente manda esse token ao servidor, que entra na conta sem pedir a senha.
 */
public final class AccountSession {
    private static final String PREFS = "com.maximo.roleplay.account";
    private static final String K_NICK = "nick", K_EMAIL = "email", K_SESSAO = "sessao";

    private AccountSession() {}

    private static SharedPreferences p(Context c) { return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE); }

    public static boolean isLoggedIn(Context c) {
        return getNick(c) != null && getSessao(c) != null;
    }
    public static String getNick(Context c) { String s = p(c).getString(K_NICK, null); return s == null || s.isEmpty() ? null : s; }
    public static String getEmail(Context c) { return p(c).getString(K_EMAIL, ""); }
    private static String getSessao(Context c) { String s = p(c).getString(K_SESSAO, null); return s == null || s.isEmpty() ? null : s; }

    /** Salva o login e passa o nick da conta a ser o nick do jogo. */
    public static void save(Context c, String nick, String email, String sessao) {
        p(c).edit().putString(K_NICK, nick).putString(K_EMAIL, email == null ? "" : email).putString(K_SESSAO, sessao).apply();
        NickManager.save(c, nick);
    }

    /** Sai da conta: avisa o servico (melhor esforco), apaga o login e o token do settings.ini. */
    public static void logout(Context c) {
        String nick = getNick(c), sessao = getSessao(c);
        if (nick != null && sessao != null) AccountApi.logout(c, nick, sessao);
        p(c).edit().clear().apply();
        writeToken(c, "");
    }

    /** Volta para a tela de login (apaga a sessao local). */
    public static void forceLogin(Activity a) {
        p(a).edit().remove(K_SESSAO).apply();
        writeToken(a, "");
        Intent i = new Intent(a, LoginActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        a.startActivity(i);
        a.finish();
    }

    /**
     * Pede o token de entrada e chama proceed. Sem internet: segue sem token (o servidor pede a senha no jogo).
     * Sessao recusada (401): volta ao login.
     */
    public static void prepareLaunch(final Activity a, final Runnable proceed) {
        final String nick = getNick(a), sessao = getSessao(a);
        if (nick == null || sessao == null) { forceLogin(a); return; }
        NickManager.writeToSettings(a, nick);
        AccountApi.play(a, nick, sessao, new AccountApi.Callback() {
            @Override public void onResult(AccountApi.Result r) {
                if (a.isFinishing() || a.isDestroyed()) return;
                if (r.sessionInvalid()) { forceLogin(a); return; }
                writeToken(a, r.ok ? r.token : "");
                proceed.run();
            }
        });
    }

    /** Grava (ou apaga, com "") [maximo] token em SAMP/settings.ini. */
    public static void writeToken(Context c, String token) {
        File dir = c.getExternalFilesDir(null);
        if (dir == null) return;
        File f = new File(dir, "SAMP/settings.ini");
        if (!f.exists()) return;
        try {
            Wini w = new Wini(f);
            w.put("maximo", "token", token == null ? "" : token);
            w.store();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
