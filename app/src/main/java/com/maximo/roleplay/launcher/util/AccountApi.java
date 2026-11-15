package com.maximo.roleplay.launcher.util;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import com.maximo.roleplay.R;
import com.maximo.roleplay.launcher.config.AppEndpoints;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * Contas fora do servidor (HTTPS; o site repassa para o contas-service). Rotas relativas a AppEndpoints.contaUrl:
 * "" criar, /login, /sessao, /jogar, /logout. POST JSON, sem redirecionamento (reenviaria a senha para outro endereco)
 * e sem retentativa. Senha e token nunca vao para log.
 */
public final class AccountApi {
    private AccountApi() {}

    /** Resultado de qualquer chamada. status: codigo HTTP (0 = sem rede). */
    public static final class Result {
        public boolean ok;
        public int status;
        public String message = "";   // ja pronta para mostrar ao jogador (vazia se ok)
        public String nick = "";
        public String email = "";
        public String sessao = "";
        public String token = "";
        public boolean sessionInvalid() { return status == 401; }
        public boolean networkError() { return status == 0; }
    }

    public interface Callback {
        /** Sempre na thread principal. */
        void onResult(Result r);
    }

    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final int MAX_BODY = 4096;

    public static void create(Context ctx, String nick, String senha, String email, Callback cb) {
        JSONObject b = new JSONObject();
        put(b, "nick", nick); put(b, "senha", senha); put(b, "email", email);
        run(ctx, "", b, cb);
    }

    public static void login(Context ctx, String nick, String senha, Callback cb) {
        JSONObject b = new JSONObject();
        put(b, "nick", nick); put(b, "senha", senha);
        run(ctx, "/login", b, cb);
    }

    /** Confere a sessao salva (e renova). */
    public static void checkSession(Context ctx, String nick, String sessao, Callback cb) {
        JSONObject b = new JSONObject();
        put(b, "nick", nick); put(b, "sessao", sessao);
        run(ctx, "/sessao", b, cb);
    }

    /** Pede o token de uso unico para entrar no servidor sem digitar a senha. */
    public static void play(Context ctx, String nick, String sessao, Callback cb) {
        JSONObject b = new JSONObject();
        put(b, "nick", nick); put(b, "sessao", sessao);
        run(ctx, "/jogar", b, cb);
    }

    public static void logout(Context ctx, String nick, String sessao) {
        JSONObject b = new JSONObject();
        put(b, "nick", nick); put(b, "sessao", sessao);
        run(ctx, "/logout", b, null);
    }

    private static void put(JSONObject o, String k, String v) {
        try { o.put(k, v == null ? "" : v); } catch (Exception ignored) { }
    }

    private static void run(Context ctx, final String route, final JSONObject body, final Callback cb) {
        final Context app = ctx.getApplicationContext();
        new Thread(new Runnable() {
            @Override public void run() {
                Result r = new Result();
                try {
                    post(AppEndpoints.contaUrl(app) + route, body, r);
                    if (!r.ok && r.message.isEmpty()) {
                        r.message = app.getString(r.status == 429 ? R.string.mx_acc_err_many : R.string.mx_acc_err_generic);
                    }
                } catch (IOException e) {
                    r.ok = false; r.status = 0;
                    r.message = app.getString(R.string.mx_acc_err_net);
                } catch (Exception e) {
                    r.ok = false;
                    r.message = app.getString(R.string.mx_acc_err_generic);
                }
                if (cb == null) return;
                final Result fr = r;
                MAIN.post(new Runnable() { @Override public void run() { cb.onResult(fr); } });
            }
        }, "account-api").start();
    }

    private static void post(String url, JSONObject body, Result out) throws Exception {
        byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        try {
            c.setConnectTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
            c.setReadTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setUseCaches(false);
            c.setInstanceFollowRedirects(false);
            c.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            c.setFixedLengthStreamingMode(data.length);
            OutputStream os = c.getOutputStream();
            try { os.write(data); } finally { os.close(); }

            int code = c.getResponseCode();
            InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            String txt = in == null ? "" : read(in);
            out.status = code;
            try {
                JSONObject j = new JSONObject(txt);
                out.ok = code == 200 && j.optBoolean("ok", false);
                out.message = out.ok ? "" : j.optString("msg", "");
                out.nick = j.optString("nick", "");
                out.email = j.optString("email", "");
                out.sessao = j.optString("sessao", "");
                out.token = j.optString("token", "");
            } catch (Exception notJson) {
                out.ok = false;
            }
        } finally {
            c.disconnect();
        }
    }

    private static String read(InputStream in) throws IOException {
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[1024];
            int n;
            while ((n = in.read(buf)) > 0 && out.size() < MAX_BODY) out.write(buf, 0, n);
            return out.toString("UTF-8");
        } finally {
            in.close();
        }
    }
}
