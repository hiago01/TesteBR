package com.samp.mobile.launcher.pack;

import java.io.File;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Limpeza dos arquivos de clientes ANTIGOS antes de instalar os novos (roda 1x por APK novo, ver SplashActivity).
 * Apaga tudo dentro de files/ (jogo, caches, installed.json, .downloads/.staging/.backup) EXCETO o que e do jogador/teste:
 * configuracoes, favoritos e log (o diag.ini NAO e mantido: vem do servidor). Depois disso o fluxo normal do launcher ve "sem instalacao" e baixa tudo de novo
 * (PRDownloader). Java puro (sem Android) para ser testado na JVM.
 */
public final class ClientWipe {
    private ClientWipe() {}

    /** Caminhos (relativos a files/, minusculos) que NUNCA sao apagados. */
    private static final Set<String> KEEP = new HashSet<>(Arrays.asList(
            "settings.ini", "samp/settings.ini", "gta_sa.set", "gtasatelem.set",
            "samp/favorites.json", "samp/sampvoice/svconfig.ini",
            "samp_log.txt", "chat_log.txt", "samp/svlog.txt"));

    public static final class Result {
        public int deleted, kept, failed;
        @Override public String toString() { return "apagados=" + deleted + " mantidos=" + kept + " falhas=" + failed; }
    }

    public static boolean isKept(String relPath) {
        return relPath != null && KEEP.contains(relPath.toLowerCase(Locale.ROOT));
    }

    /** @param filesDir a pasta files/ do app (getExternalFilesDir). Nao segue links simbolicos para fora dela. */
    public static Result wipe(File filesDir) {
        Result r = new Result();
        if (filesDir == null || !filesDir.isDirectory()) return r;
        walk(filesDir, "", r);
        return r;
    }

    /** @return true se a pasta ficou vazia (e pode ser apagada) */
    private static boolean walk(File dir, String rel, Result r) {
        File[] kids = dir.listFiles();
        if (kids == null) return false;
        boolean empty = true;
        for (File k : kids) {
            String childRel = rel.isEmpty() ? k.getName() : rel + "/" + k.getName();
            if (isSymlink(k)) { if (!k.delete()) { r.failed++; empty = false; } else r.deleted++; continue; }
            if (k.isDirectory()) {
                if (walk(k, childRel, r)) { if (k.delete()) { /* pasta vazia removida */ } else { empty = false; } }
                else empty = false;
            } else if (isKept(childRel)) {
                r.kept++; empty = false;
            } else if (k.delete()) {
                r.deleted++;
            } else {
                r.failed++; empty = false;
            }
        }
        return empty;
    }

    private static boolean isSymlink(File f) {
        try { return java.nio.file.Files.isSymbolicLink(f.toPath()); } catch (Throwable t) { return false; }
    }
}
