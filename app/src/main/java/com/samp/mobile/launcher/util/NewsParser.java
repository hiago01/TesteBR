package com.samp.mobile.launcher.util;

import com.samp.mobile.launcher.pack.MiniJson;
import com.samp.mobile.launcher.pack.PackException;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;

/**
 * Le o noticias.json do site (Java puro, testado na JVM). Formato:
 * { "news": [ { "id": "1", "title": "...", "message": "...", "image": "https://...", "url": "https://...",
 *               "type": "news|update|event", "date": "2026-10-30 22:00" } ] }
 * Aceita tambem um array na raiz. "body" vale como "message". "date" = "yyyy-MM-dd HH:mm" ou "yyyy-MM-dd" (horario de
 * Brasilia); "time" = milissegundos desde 1970 (tem prioridade). Itens sem titulo e sem mensagem sao descartados;
 * image/url so http/https. Mais recentes primeiro; no maximo MAX itens.
 */
public final class NewsParser {
    public static final int MAX = 50;
    private NewsParser() {}

    public static final class Item {
        public final String id, title, body, image, url, type;
        public final long time;
        public Item(String id, String title, String body, String image, String url, String type, long time) {
            this.id = id; this.title = title; this.body = body; this.image = image; this.url = url; this.type = type; this.time = time;
        }
    }

    private static boolean safe(String u) {
        if (u == null) return false;
        String l = u.trim().toLowerCase(Locale.ROOT);
        return l.startsWith("https://") || l.startsWith("http://");
    }

    static long parseDate(String s) {
        if (s == null) return 0;
        s = s.trim();
        String[] fmts = { "yyyy-MM-dd HH:mm", "yyyy-MM-dd'T'HH:mm", "yyyy-MM-dd" };
        for (String f : fmts) {
            SimpleDateFormat d = new SimpleDateFormat(f, Locale.ROOT);
            d.setLenient(false);
            d.setTimeZone(TimeZone.getTimeZone("America/Sao_Paulo"));
            try { return d.parse(s).getTime(); } catch (ParseException ignored) { }
        }
        return 0;
    }

    /** Lanca PackException se o texto nao for JSON valido ou nao tiver lista de noticias. */
    public static List<Item> parse(String json) throws PackException {
        Object root = MiniJson.parse(json);
        List<Object> arr = MiniJson.asList(root);
        if (arr == null) {
            Map<String, Object> m = MiniJson.asMap(root);
            if (m != null) arr = MiniJson.asList(m.get("news"));
        }
        if (arr == null) throw new PackException("noticias.json: falta a lista \"news\"");
        List<Item> out = new ArrayList<>();
        for (Object o : arr) {
            Map<String, Object> n = MiniJson.asMap(o);
            if (n == null) continue;
            String title = nz(MiniJson.str(n, "title"));
            String body = nz(MiniJson.str(n, "message"));
            if (body.isEmpty()) body = nz(MiniJson.str(n, "body"));
            if (title.isEmpty() && body.isEmpty()) continue;
            String img = nz(MiniJson.str(n, "image"));
            String url = nz(MiniJson.str(n, "url"));
            String type = nz(MiniJson.str(n, "type"));
            long time = MiniJson.lng(n, "time", 0);
            if (time <= 0) time = parseDate(MiniJson.str(n, "date"));
            out.add(new Item(nz(MiniJson.str(n, "id")), title, body, safe(img) ? img : "", safe(url) ? url : "",
                    type.isEmpty() ? "news" : type, time));
        }
        Collections.sort(out, new Comparator<Item>() {
            @Override public int compare(Item a, Item b) { return Long.compare(b.time, a.time); }
        });
        return out.size() > MAX ? new ArrayList<>(out.subList(0, MAX)) : out;
    }

    private static String nz(String s) { return s == null ? "" : s.trim(); }
}
