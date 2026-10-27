package com.samp.mobile.launcher.util;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** Historico local das notificacoes recebidas (mostrado na aba Noticias). Maximo de 50 itens. */
public final class NotificationStore {
    private static final String PREFS = "mx_notifications";
    private static final String KEY = "items";
    private static final int MAX = 50;
    private static final Object LOCK = new Object();

    private NotificationStore() {}

    public static final class Item {
        public final String title;
        public final String body;
        public final String type;
        public final String url;
        public final long time;

        public Item(String title, String body, String type, String url, long time) {
            this.title = title == null ? "" : title;
            this.body = body == null ? "" : body;
            this.type = type == null ? "news" : type;
            this.url = url == null ? "" : url;
            this.time = time;
        }
    }

    public static void add(Context c, Item item) {
        synchronized (LOCK) {
            SharedPreferences sp = c.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            JSONArray old = read(sp);
            JSONArray out = new JSONArray();
            try {
                out.put(toJson(item));
                for (int i = 0; i < old.length() && out.length() < MAX; i++) out.put(old.get(i));
            } catch (JSONException ignored) { }
            sp.edit().putString(KEY, out.toString()).apply();
        }
    }

    /** Mais recentes primeiro. */
    public static List<Item> list(Context c) {
        synchronized (LOCK) {
            SharedPreferences sp = c.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            JSONArray arr = read(sp);
            List<Item> out = new ArrayList<>();
            for (int i = 0; i < arr.length(); i++) {
                JSONObject o = arr.optJSONObject(i);
                if (o == null) continue;
                out.add(new Item(o.optString("t"), o.optString("b"), o.optString("y"), o.optString("u"), o.optLong("w")));
            }
            return out;
        }
    }

    public static void clear(Context c) {
        synchronized (LOCK) {
            c.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(KEY).apply();
        }
    }

    private static JSONArray read(SharedPreferences sp) {
        try {
            return new JSONArray(sp.getString(KEY, "[]"));
        } catch (JSONException e) {
            return new JSONArray();
        }
    }

    private static JSONObject toJson(Item i) throws JSONException {
        JSONObject o = new JSONObject();
        o.put("t", i.title);
        o.put("b", i.body);
        o.put("y", i.type);
        o.put("u", i.url);
        o.put("w", i.time);
        return o;
    }
}
