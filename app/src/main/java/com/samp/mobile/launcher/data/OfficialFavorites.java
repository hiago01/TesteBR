package com.samp.mobile.launcher.data;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Locale;

/**
 * Favoritos dos servidores OFICIAIS (independente de {@link FavoritesInfo}, que guarda os servidores
 * adicionados manualmente por IP/porta).
 *
 * Persistencia: SharedPreferences privado ("mx_official_favorites"), chave "list" = JSON array de
 * objetos {key, host, port}. Cada servidor e identificado por host+porta normalizados (minusculas,
 * sem espacos), nunca pelo nome exibido. Host e porta reais ficam guardados internamente (nao sao
 * exibidos em nenhuma tela) para identificar o servidor mesmo quando ele estiver offline.
 *
 * Erros de leitura/gravacao nunca propagam excecao: um JSON corrompido vira lista vazia (e o valor
 * ruim e descartado na proxima gravacao).
 */
public final class OfficialFavorites {
    private static final String TAG = "OfficialFavorites";
    private static final String PREFS = "mx_official_favorites";
    private static final String KEY_LIST = "list";

    private static final Object LOCK = new Object();
    /** Cache em memoria (ordem de insercao preservada). Protegido por LOCK. */
    private static ArrayList<Entry> sCache;

    private OfficialFavorites() { }

    public static final class Entry {
        public final String key;
        public final String host;
        public final int port;
        Entry(String key, String host, int port) { this.key = key; this.host = host; this.port = port; }
    }

    /** Chave normalizada "host:porta" (host em minusculas e sem espacos). */
    public static String keyOf(String host, int port) {
        String h = host == null ? "" : host.trim().toLowerCase(Locale.ROOT);
        return h + ":" + port;
    }

    public static boolean isFavorite(Context ctx, String host, int port) {
        String key = keyOf(host, port);
        synchronized (LOCK) {
            ensureLoaded(ctx);
            for (Entry e : sCache) if (e.key.equals(key)) return true;
            return false;
        }
    }

    /** Copia da lista salva (mesmo de servidores que no momento nao estao na lista oficial). */
    public static ArrayList<Entry> list(Context ctx) {
        synchronized (LOCK) {
            ensureLoaded(ctx);
            return new ArrayList<>(sCache);
        }
    }

    /**
     * Alterna o favorito. Nunca duplica. Retorna o NOVO estado (true = agora e favorito). Se a
     * gravacao falhar, o estado em memoria ainda reflete a acao do usuario nesta sessao.
     */
    public static boolean toggle(Context ctx, String host, int port) {
        if (host == null || host.trim().isEmpty() || port < 1 || port > 65535) return isFavorite(ctx, host, port);
        String key = keyOf(host, port);
        synchronized (LOCK) {
            ensureLoaded(ctx);
            for (int i = 0; i < sCache.size(); i++) {
                if (sCache.get(i).key.equals(key)) {
                    sCache.remove(i);
                    persist(ctx);
                    return false;
                }
            }
            sCache.add(new Entry(key, host.trim(), port));
            persist(ctx);
            return true;
        }
    }

    // --------------------------------------------------------------------------------

    private static void ensureLoaded(Context ctx) {
        if (sCache != null) return;
        ArrayList<Entry> out = new ArrayList<>();
        try {
            SharedPreferences sp = ctx.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            String raw = sp.getString(KEY_LIST, null);
            if (raw != null && !raw.isEmpty()) {
                JSONArray arr = new JSONArray(raw);
                for (int i = 0; i < arr.length(); i++) {
                    JSONObject o = arr.optJSONObject(i);
                    if (o == null) continue;
                    String host = o.optString("host", "");
                    int port = o.optInt("port", 0);
                    if (host.isEmpty() || port < 1 || port > 65535) continue;
                    String key = keyOf(host, port);
                    boolean dup = false;
                    for (Entry e : out) if (e.key.equals(key)) { dup = true; break; }
                    if (!dup) out.add(new Entry(key, host, port));
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "could not read official favorites: " + e.getClass().getSimpleName());
            out.clear();
        }
        sCache = out;
    }

    private static void persist(Context ctx) {
        try {
            JSONArray arr = new JSONArray();
            for (Entry e : sCache) {
                JSONObject o = new JSONObject();
                o.put("key", e.key);
                o.put("host", e.host);
                o.put("port", e.port);
                arr.put(o);
            }
            ctx.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                    .edit().putString(KEY_LIST, arr.toString()).apply();
        } catch (Exception e) {
            Log.w(TAG, "could not save official favorites: " + e.getClass().getSimpleName());
        }
    }
}
