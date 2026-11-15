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
 * Criacao de conta fora do servidor: POST JSON {nick,senha} para AppEndpoints.contaUrl (HTTPS; o site repassa para o
 * contas-service do servidor). Sem retentativa: repetir um POST de criacao so geraria "nick ja existe". A senha nunca
 * e gravada no aparelho nem escrita em log.
 */
public final class AccountApi {
    private AccountApi() {}

    public interface Callback {
        /** Sempre na thread principal. message ja vem pronta para mostrar ao jogador. */
        void onResult(boolean ok, String message);
    }

    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final int MAX_BODY = 4096;

    public static void create(final Context ctx, final String nickWire, final String senha, final Callback cb) {
        final Context app = ctx.getApplicationContext();
        new Thread(new Runnable() {
            @Override public void run() {
                boolean ok = false;
                String msg;
                try {
                    String[] r = post(AppEndpoints.contaUrl(app), nickWire, senha);
                    ok = "1".equals(r[0]);
                    msg = ok ? null : (r[1] != null && !r[1].isEmpty() ? r[1] : app.getString("429".equals(r[2]) ? R.string.mx_acc_err_many : R.string.mx_acc_err_generic));
                } catch (IOException e) {
                    msg = app.getString(R.string.mx_acc_err_net);
                } catch (Exception e) {
                    msg = app.getString(R.string.mx_acc_err_generic);
                }
                final boolean fok = ok; final String fmsg = msg;
                MAIN.post(new Runnable() { @Override public void run() { cb.onResult(fok, fmsg); } });
            }
        }, "account-create").start();
    }

    /** Retorna {ok("1"/"0"), msg, httpStatus}. */
    private static String[] post(String url, String nick, String senha) throws Exception {
        JSONObject body = new JSONObject();
        body.put("nick", nick);
        body.put("senha", senha);
        byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);

        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        try {
            c.setConnectTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
            c.setReadTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setUseCaches(false);
            c.setInstanceFollowRedirects(false);   // um redirecionamento reenviaria a senha para outro endereco
            c.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            c.setFixedLengthStreamingMode(data.length);
            OutputStream os = c.getOutputStream();
            try { os.write(data); } finally { os.close(); }

            int code = c.getResponseCode();
            InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            String txt = in == null ? "" : read(in);
            try {
                JSONObject j = new JSONObject(txt);
                return new String[]{ j.optBoolean("ok", false) && code == 200 ? "1" : "0", j.optString("msg", ""), String.valueOf(code) };
            } catch (Exception notJson) {
                return new String[]{ "0", "", String.valueOf(code) };
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
