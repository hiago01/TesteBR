package com.samp.mobile.launcher.util;

import java.util.regex.Pattern;

/**
 * Regras do nick (JVM puro). Dois formatos:
 *  - DISPLAY: o que o jogador digita e ve no launcher; pode ter ESPACOS ("Jose Luis Silva").
 *  - WIRE: o que vai para o servidor/SAMP/settings.ini; SA-MP nao aceita espaco -> cada grupo de espacos vira UM "_".
 * Qualquer gravacao para o jogo deve usar toWire(); o launcher mostra o display.
 */
public final class NickRules {
    private NickRules() {}

    public static final int MIN_LEN = 3;
    public static final int MAX_LEN = 20; // limite do SA-MP (conta o formato WIRE, que tem o mesmo tamanho do display normalizado)
    private static final Pattern WIRE = Pattern.compile("^[A-Za-z0-9_\\[\\]()$@.=]{" + MIN_LEN + "," + MAX_LEN + "}$");

    /** Tira espacos das pontas e troca qualquer sequencia de espacos (inclui NBSP/tab) por UM espaco. */
    public static String normalizeDisplay(String s) {
        if (s == null) return "";
        StringBuilder b = new StringBuilder(s.length());
        boolean pendingSpace = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (Character.isWhitespace(c) || c == '\u00A0' || c == '\u2007' || c == '\u202F') {
                pendingSpace = b.length() > 0;
            } else {
                if (pendingSpace) { b.append(' '); pendingSpace = false; }
                b.append(c);
            }
        }
        return b.toString();
    }

    /** Nick enviado ao servidor: display normalizado com cada espaco trocado por "_". */
    public static String toWire(String display) {
        return normalizeDisplay(display).replace(' ', '_');
    }

    public static boolean isValidWire(String wire) {
        return wire != null && WIRE.matcher(wire).matches();
    }

    /** Valido se o formato WIRE correspondente for valido (letras, numeros e _ [ ] ( ) $ @ . =, 3..20). */
    public static boolean isValidDisplay(String display) {
        return isValidWire(toWire(display));
    }

    /** true se o display tem espaco (entao o servidor vera um nome diferente do exibido). */
    public static boolean differsOnWire(String display) {
        return !normalizeDisplay(display).equals(toWire(display));
    }
}
