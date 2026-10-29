package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.IOException;

/** Validacao de caminhos relativos vindos de zip/manifest (zip-slip). */
public final class PathSafety {
    private PathSafety() {}

    /**
     * Valida um caminho relativo "a/b/c.ext". Rejeita: vazio, absoluto, letra de unidade ("C:"),
     * barra invertida, segmentos "", "." e "..", caracteres de controle/NUL e nomes enormes.
     * @return o proprio caminho (inalterado: a caixa e preservada) ou lanca PackException.
     */
    public static String validateRelative(String p) throws PackException {
        if (p == null || p.isEmpty()) throw new PackException("caminho vazio");
        if (p.length() > 1024) throw new PackException("caminho longo demais");
        if (p.charAt(0) == '/') throw new PackException("caminho absoluto: " + show(p));
        if (p.indexOf('\\') >= 0) throw new PackException("barra invertida no caminho: " + show(p));
        for (int k = 0; k < p.length(); k++) {
            if (p.charAt(k) < 0x20 || p.charAt(k) == 0x7f) throw new PackException("caractere de controle no caminho: " + show(p));
        }
        if (p.length() >= 2 && p.charAt(1) == ':') throw new PackException("letra de unidade no caminho: " + show(p));
        String[] seg = p.split("/", -1);
        for (int k = 0; k < seg.length; k++) {
            String s = seg[k];
            if (s.isEmpty()) {
                if (k == seg.length - 1 && k > 0) continue; // barra final (diretorio) tratada por quem chama
                throw new PackException("segmento vazio no caminho: " + show(p));
            }
            if (s.equals(".") || s.equals("..")) throw new PackException("segmento '" + s + "' no caminho: " + show(p));
        }
        return p;
    }

    /** Garante que o caminho canonico de (base, rel) fica DENTRO de base. */
    public static File resolveInside(File base, String rel) throws PackException {
        validateRelative(rel);
        try {
            File f = new File(base, rel);
            String root = base.getCanonicalPath();
            String c = f.getCanonicalPath();
            if (!c.startsWith(root + File.separator)) throw new PackException("fora do diretorio de destino: " + show(rel));
            return f;
        } catch (IOException e) {
            throw new PackException("caminho invalido: " + show(rel), e);
        }
    }

    private static String show(String p) {
        StringBuilder b = new StringBuilder();
        for (int k = 0; k < Math.min(p.length(), 120); k++) {
            char c = p.charAt(k);
            b.append(c < 0x20 ? '?' : c);
        }
        return b.toString();
    }
}
