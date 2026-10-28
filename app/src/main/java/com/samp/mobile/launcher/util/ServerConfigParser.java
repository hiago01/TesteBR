package com.samp.mobile.launcher.util;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.regex.Pattern;

/**
 * Le e valida o servers.json. Regra: apenas o PRIMEIRO elemento de "query" e usado;
 * os demais sao ignorados (e nao validados).
 *
 * Obrigatorios: name (texto), ip (IPv4 ou hostname), port (1..65535).
 * Opcionais: number (padrao 1), online (0), maxplayers (0), password (false).
 * "online"/"maxplayers" do JSON sao so configuracao: a UI so exibe contagem real vinda da consulta UDP.
 */
public final class ServerConfigParser {

    /** Erro de configuracao com mensagem pronta para o usuario. */
    public static final class ConfigException extends Exception {
        public ConfigException(String message) { super(message); }
    }

    /** Resultado imutavel da leitura. */
    public static final class ServerConfig {
        public final int number;
        public final String name;
        public final String ip;
        public final int port;
        public final int maxPlayers;
        public final boolean password;

        ServerConfig(int number, String name, String ip, int port, int maxPlayers, boolean password) {
            this.number = number; this.name = name; this.ip = ip; this.port = port;
            this.maxPlayers = maxPlayers; this.password = password;
        }
    }

    private static final Pattern IPV4 = Pattern.compile("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$");
    private static final Pattern HOST_LABEL = Pattern.compile("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$");
    private static final int MAX_NAME = 128;

    private ServerConfigParser() {}

    public static ServerConfig parse(String body) throws ConfigException {
        if (body == null || body.trim().isEmpty()) throw new ConfigException("servers.json is empty");
        JSONObject root;
        try {
            root = new JSONObject(body.trim());
        } catch (JSONException e) {
            throw new ConfigException("servers.json is not valid JSON");
        }
        JSONArray query = root.optJSONArray("query");
        if (query == null) throw new ConfigException("servers.json: missing \"query\" array");
        if (query.length() == 0) throw new ConfigException("servers.json: \"query\" is empty");
        JSONObject o = query.optJSONObject(0);
        if (o == null) throw new ConfigException("servers.json: first entry is not an object");

        String name = requireString(o, "name").trim();
        if (name.isEmpty()) throw new ConfigException("servers.json: \"name\" is empty");
        if (name.length() > MAX_NAME) name = name.substring(0, MAX_NAME);

        String ip = requireString(o, "ip").trim();
        if (!isValidAddress(ip)) throw new ConfigException("servers.json: invalid \"ip\"");

        int port = requireInt(o, "port");
        if (port < 1 || port > 65535) throw new ConfigException("servers.json: \"port\" must be 1-65535");

        int number = optInt(o, "number", 1);
        int max = optInt(o, "maxplayers", 0);
        if (max < 0) throw new ConfigException("servers.json: \"maxplayers\" must be >= 0");
        boolean password = o.optBoolean("password", false);

        return new ServerConfig(number, name, ip, port, max, password);
    }

    static boolean isValidAddress(String s) {
        if (s == null || s.isEmpty() || s.length() > 253) return false;
        java.util.regex.Matcher m = IPV4.matcher(s);
        if (m.matches()) {
            int sum = 0;
            for (int i = 1; i <= 4; i++) {
                int v = Integer.parseInt(m.group(i));
                if (v > 255) return false;
                sum += v;
            }
            return sum != 0; // rejeita 0.0.0.0
        }
        if (s.endsWith(".")) return false;
        String[] labels = s.split("\\.", -1);
        for (String l : labels) if (!HOST_LABEL.matcher(l).matches()) return false;
        // hostname todo numerico (ex.: "999.1.1.1") nao e hostname valido
        return !Character.isDigit(labels[labels.length - 1].charAt(0)) || labels.length == 1;
    }

    private static String requireString(JSONObject o, String key) throws ConfigException {
        if (!o.has(key) || o.isNull(key)) throw new ConfigException("servers.json: missing \"" + key + "\"");
        Object v = o.opt(key);
        if (!(v instanceof String)) throw new ConfigException("servers.json: \"" + key + "\" must be text");
        return (String) v;
    }

    private static int requireInt(JSONObject o, String key) throws ConfigException {
        if (!o.has(key) || o.isNull(key)) throw new ConfigException("servers.json: missing \"" + key + "\"");
        return toInt(o.opt(key), key);
    }

    private static int optInt(JSONObject o, String key, int def) throws ConfigException {
        if (!o.has(key) || o.isNull(key)) return def;
        return toInt(o.opt(key), key);
    }

    private static int toInt(Object v, String key) throws ConfigException {
        if (v instanceof Integer) return (Integer) v;
        if (v instanceof Long) {
            long l = (Long) v;
            if (l < Integer.MIN_VALUE || l > Integer.MAX_VALUE) throw new ConfigException("servers.json: \"" + key + "\" out of range");
            return (int) l;
        }
        if (v instanceof Double) {
            double d = (Double) v;
            if (d != Math.rint(d) || Math.abs(d) > Integer.MAX_VALUE) throw new ConfigException("servers.json: \"" + key + "\" must be an integer");
            return (int) d;
        }
        if (v instanceof String) {
            try { return Integer.parseInt(((String) v).trim()); } catch (NumberFormatException ignored) { }
        }
        throw new ConfigException("servers.json: \"" + key + "\" must be a number");
    }

    private static String shorten(String s) { return s.length() > 40 ? s.substring(0, 40) + "..." : s; }
}
