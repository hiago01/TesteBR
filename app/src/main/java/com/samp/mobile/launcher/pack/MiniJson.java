package com.samp.mobile.launcher.pack;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * JSON minimo sem dependencias (o pacote "pack" roda na JVM nos testes, sem org.json do Android).
 * Tipos: Map (LinkedHashMap), List, String, Long, Double, Boolean, null.
 */
public final class MiniJson {
    private final String s;
    private int i;

    private MiniJson(String s) { this.s = s; }

    public static Object parse(String text) throws PackException {
        if (text == null) throw new PackException("JSON vazio");
        MiniJson p = new MiniJson(text);
        p.ws();
        Object v = p.value(0);
        p.ws();
        if (p.i != p.s.length()) throw p.err("texto apos o fim do JSON");
        return v;
    }

    private PackException err(String m) { return new PackException("JSON invalido (pos " + i + "): " + m); }

    private void ws() {
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\uFEFF') i++; else break;
        }
    }

    private Object value(int depth) throws PackException {
        if (depth > 64) throw err("aninhamento excessivo");
        if (i >= s.length()) throw err("fim inesperado");
        char c = s.charAt(i);
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') return string();
        if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
        if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
        if (s.startsWith("null", i)) { i += 4; return null; }
        return number();
    }

    private Map<String, Object> object(int depth) throws PackException {
        Map<String, Object> m = new LinkedHashMap<>();
        i++; ws();
        if (i < s.length() && s.charAt(i) == '}') { i++; return m; }
        while (true) {
            ws();
            if (i >= s.length() || s.charAt(i) != '"') throw err("chave esperada");
            String k = string();
            ws();
            if (i >= s.length() || s.charAt(i) != ':') throw err("':' esperado");
            i++; ws();
            m.put(k, value(depth + 1));
            ws();
            if (i >= s.length()) throw err("fim inesperado");
            char c = s.charAt(i++);
            if (c == '}') return m;
            if (c != ',') throw err("',' ou '}' esperado");
        }
    }

    private List<Object> array(int depth) throws PackException {
        List<Object> l = new ArrayList<>();
        i++; ws();
        if (i < s.length() && s.charAt(i) == ']') { i++; return l; }
        while (true) {
            ws();
            l.add(value(depth + 1));
            ws();
            if (i >= s.length()) throw err("fim inesperado");
            char c = s.charAt(i++);
            if (c == ']') return l;
            if (c != ',') throw err("',' ou ']' esperado");
        }
    }

    private String string() throws PackException {
        StringBuilder b = new StringBuilder();
        i++;
        while (true) {
            if (i >= s.length()) throw err("string nao terminada");
            char c = s.charAt(i++);
            if (c == '"') return b.toString();
            if (c == '\\') {
                if (i >= s.length()) throw err("escape incompleto");
                char e = s.charAt(i++);
                switch (e) {
                    case '"': b.append('"'); break;
                    case '\\': b.append('\\'); break;
                    case '/': b.append('/'); break;
                    case 'b': b.append('\b'); break;
                    case 'f': b.append('\f'); break;
                    case 'n': b.append('\n'); break;
                    case 'r': b.append('\r'); break;
                    case 't': b.append('\t'); break;
                    case 'u':
                        if (i + 4 > s.length()) throw err("\\u incompleto");
                        try { b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); }
                        catch (NumberFormatException ex) { throw err("\\u invalido"); }
                        i += 4;
                        break;
                    default: throw err("escape desconhecido");
                }
            } else {
                b.append(c);
            }
        }
    }

    private Object number() throws PackException {
        int st = i;
        if (i < s.length() && s.charAt(i) == '-') i++;
        boolean frac = false;
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c >= '0' && c <= '9') i++;
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') { frac = true; i++; }
            else break;
        }
        String t = s.substring(st, i);
        if (t.isEmpty() || t.equals("-")) throw err("valor inesperado");
        try {
            if (!frac) return Long.valueOf(Long.parseLong(t));
            return Double.valueOf(Double.parseDouble(t));
        } catch (NumberFormatException e) { throw err("numero invalido"); }
    }

    // ---------------------------------------------------------------- escrita
    public static String write(Object v) {
        StringBuilder b = new StringBuilder();
        w(b, v);
        return b.toString();
    }

    @SuppressWarnings("unchecked")
    private static void w(StringBuilder b, Object v) {
        if (v == null) b.append("null");
        else if (v instanceof String) q(b, (String) v);
        else if (v instanceof Number || v instanceof Boolean) b.append(v);
        else if (v instanceof Map) {
            b.append('{');
            boolean first = true;
            for (Map.Entry<String, Object> e : ((Map<String, Object>) v).entrySet()) {
                if (!first) b.append(',');
                first = false;
                q(b, e.getKey()); b.append(':'); w(b, e.getValue());
            }
            b.append('}');
        } else if (v instanceof List) {
            b.append('[');
            boolean first = true;
            for (Object o : (List<Object>) v) {
                if (!first) b.append(',');
                first = false;
                w(b, o);
            }
            b.append(']');
        } else q(b, String.valueOf(v));
    }

    private static void q(StringBuilder b, String s) {
        b.append('"');
        for (int k = 0; k < s.length(); k++) {
            char c = s.charAt(k);
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c)); else b.append(c);
            }
        }
        b.append('"');
    }

    // ---------------------------------------------------------------- helpers de leitura
    @SuppressWarnings("unchecked")
    public static Map<String, Object> asMap(Object o) { return o instanceof Map ? (Map<String, Object>) o : null; }
    @SuppressWarnings("unchecked")
    public static List<Object> asList(Object o) { return o instanceof List ? (List<Object>) o : null; }
    public static String str(Map<String, Object> m, String k) {
        Object o = m.get(k);
        if (o == null) return null;
        if (o instanceof String) return (String) o;
        if (o instanceof Long || o instanceof Double || o instanceof Boolean) return String.valueOf(o);
        return null;
    }
    public static long lng(Map<String, Object> m, String k, long def) {
        Object o = m.get(k);
        if (o instanceof Long) return (Long) o;
        if (o instanceof Double) return (long) ((Double) o).doubleValue();
        if (o instanceof String) { try { return Long.parseLong(((String) o).trim()); } catch (NumberFormatException e) { return def; } }
        return def;
    }
}
