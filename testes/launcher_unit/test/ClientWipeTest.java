package test;

import com.samp.mobile.launcher.pack.ClientWipe;
import java.io.File;
import java.nio.file.Files;

public class ClientWipeTest {
    static int fail = 0, n = 0;
    static void check(boolean c, String m) { n++; if (!c) { fail++; System.out.println("FALHOU: " + m); } else System.out.println("PASS " + m); }
    static void put(File root, String rel) throws Exception { File f = new File(root, rel); f.getParentFile().mkdirs(); Files.write(f.toPath(), new byte[]{1, 2, 3}); }

    public static void main(String[] a) throws Exception {
        File root = Files.createTempDirectory("wipe").toFile();
        String[] gone = { "texdb/gta3/gta3.txt", "texdb/touch.img", "SAMP/dff/x.dff", "SAMP/col/y.col", "CINFO.BIN", "CINFO.BIN.bak-1", "cinfo.bin", "models/minfo.bin",
                "SAMP/cinfo.sig", "SAMP/col_index.txt", "installed.json", ".downloads/core.zip.part", ".staging/a/b.bin", ".backup/c", "lista.txt", "files.json", "gta3.img" };
        String[] kept = { "settings.ini", "SAMP/settings.ini", "gta_sa.set", "gtasatelem.set", "SAMP/favorites.json", "SAMP/sampvoice/svconfig.ini", "samp_log.txt", "SAMP/SVLOG.TXT" };
        for (String s : gone) put(root, s);
        for (String s : kept) put(root, s);
        ClientWipe.Result r = ClientWipe.wipe(root);
        boolean allGone = true; for (String s : gone) if (new File(root, s).exists()) { allGone = false; System.out.println("  restou: " + s); }
        boolean allKept = true; for (String s : kept) if (!new File(root, s).exists()) { allKept = false; System.out.println("  sumiu: " + s); }
        check(allGone, "apaga jogo, caches, installed.json e pastas do instalador");
        check(allKept, "mantem configuracoes, favoritos e log (sem diferenciar caixa)");
        check(!ClientWipe.isKept("SAMP/diag.ini"), "diag.ini NAO e mantido (vem do servidor)");
        check(r.deleted == gone.length && r.kept == kept.length && r.failed == 0, "contagem " + r);
        check(!new File(root, "texdb").exists() && !new File(root, ".downloads").exists(), "pastas vazias removidas");
        check(new File(root, "SAMP").isDirectory(), "pasta com arquivo mantido continua");
        check(ClientWipe.wipe(new File(root, "nao-existe")).deleted == 0, "pasta inexistente nao quebra");
        check(ClientWipe.wipe(null).deleted == 0, "null nao quebra");
        ClientWipe.Result r2 = ClientWipe.wipe(root);
        check(r2.deleted == 0 && r2.kept == kept.length, "2a execucao nao apaga mais nada (idempotente)");
        System.out.println(n + " verificacoes, " + fail + " falhas");
        if (fail != 0) System.exit(1);
    }
}
