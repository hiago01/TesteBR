package com.samp.mobile.launcher.pack;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Leitura/ajuste TOLERANTE de um .ini como o INIReader (inih) do jogo le: secoes/chaves sem diferenciar caixa,
 * o ULTIMO valor repetido vale, comentarios ';' e '#' no inicio da linha e ';' em linha (precedido de espaco).
 * Serve para garantir "[gui] androidkeyboard = 1" mesmo quando o Wini (ini4j) falha, o arquivo vem do servidor
 * com a chave repetida/ausente ou o arquivo esta quebrado. JVM puro (sem Android).
 */
public final class IniFix {
    private IniFix() {}

    /** Valor efetivo (ultimo) de [section] key, ou null se nao houver. */
    public static String read(String text, String section, String key) {
        String cur = "";
        String found = null;
        for (String raw : lines(text)) {
            String s = raw.trim();
            if (s.isEmpty() || s.charAt(0) == ';' || s.charAt(0) == '#') continue;
            if (s.charAt(0) == '[') {
                int e = s.indexOf(']');
                if (e > 0) cur = s.substring(1, e).trim().toLowerCase(Locale.ROOT);
                continue;
            }
            int eq = indexOfSep(s);
            if (eq <= 0) continue;
            String k = s.substring(0, eq).trim().toLowerCase(Locale.ROOT);
            if (cur.equals(section.toLowerCase(Locale.ROOT)) && k.equals(key.toLowerCase(Locale.ROOT)))
                found = stripInline(s.substring(eq + 1)).trim();
        }
        return found;
    }

    /** Mesma regra do inih GetBoolean: true/yes/on/1 = verdadeiro; false/no/off/0 = falso; outro = default. */
    public static boolean asBool(String v, boolean def) {
        if (v == null) return def;
        String l = v.trim().toLowerCase(Locale.ROOT);
        if (l.equals("true") || l.equals("yes") || l.equals("on") || l.equals("1")) return true;
        if (l.equals("false") || l.equals("no") || l.equals("off") || l.equals("0")) return false;
        return def;
    }

    /**
     * Devolve o texto com [section] key = value garantido: remove TODAS as ocorrencias da chave na secao (qualquer caixa)
     * e grava uma unica no fim da secao (cria a secao se faltar). Preserva o resto, o fim de linha e o BOM.
     */
    public static String enforce(String text, String section, String key, String value) {
        String nl = text.contains("\r\n") ? "\r\n" : "\n";
        String bom = text.startsWith("﻿") ? "﻿" : "";
        List<String> in = lines(bom.isEmpty() ? text : text.substring(1));
        List<String> out = new ArrayList<>();
        String sec = section.toLowerCase(Locale.ROOT), k = key.toLowerCase(Locale.ROOT);
        String cur = "";
        int secEnd = -1;            // indice em 'out' logo apos a ultima linha util da secao alvo
        boolean haveSection = false;
        for (String raw : in) {
            String s = raw.trim();
            if (!s.isEmpty() && s.charAt(0) == '[') {
                int e = s.indexOf(']');
                if (e > 0) { cur = s.substring(1, e).trim().toLowerCase(Locale.ROOT); if (cur.equals(sec)) { haveSection = true; out.add(raw); secEnd = out.size(); continue; } }
            }
            if (cur.equals(sec) && !s.isEmpty() && s.charAt(0) != ';' && s.charAt(0) != '#') {
                int eq = indexOfSep(s);
                if (eq > 0 && s.substring(0, eq).trim().toLowerCase(Locale.ROOT).equals(k)) continue; // descarta a chave antiga
                out.add(raw);
                secEnd = out.size();
                continue;
            }
            out.add(raw);
            if (cur.equals(sec) && !s.isEmpty()) secEnd = out.size();
        }
        String line = key + " = " + value;
        if (haveSection) out.add(secEnd < 0 ? out.size() : secEnd, line);
        else {
            if (!out.isEmpty() && !out.get(out.size() - 1).trim().isEmpty()) out.add("");
            out.add("[" + section + "]");
            out.add(line);
        }
        StringBuilder b = new StringBuilder(bom);
        for (int i = 0; i < out.size(); i++) { b.append(out.get(i)); if (i < out.size() - 1) b.append(nl); }
        b.append(nl);
        return b.toString();
    }

    /** Arquivo plausivel como settings.ini: nao vazio, sem NUL e com pelo menos uma secao [x]. */
    public static boolean looksValid(String text) {
        if (text == null || text.trim().isEmpty() || text.indexOf('\0') >= 0) return false;
        for (String raw : lines(text)) {
            String s = raw.trim();
            if (s.length() > 2 && s.charAt(0) == '[' && s.indexOf(']') > 1) return true;
        }
        return false;
    }

    private static List<String> lines(String text) {
        List<String> l = new ArrayList<>();
        if (text.startsWith("﻿")) text = text.substring(1);
        int start = 0;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '\n' || c == '\r') {
                l.add(text.substring(start, i));
                if (c == '\r' && i + 1 < text.length() && text.charAt(i + 1) == '\n') i++;
                start = i + 1;
            }
        }
        if (start < text.length()) l.add(text.substring(start));
        return l;
    }

    private static int indexOfSep(String s) {
        int a = s.indexOf('='), b = s.indexOf(':');
        if (a < 0) return b;
        if (b < 0) return a;
        return Math.min(a, b);
    }

    private static String stripInline(String v) {
        int i = v.indexOf(" ;");
        if (i < 0) i = v.indexOf("\t;");
        return i >= 0 ? v.substring(0, i) : v;
    }
}
