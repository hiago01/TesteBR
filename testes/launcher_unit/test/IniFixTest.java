package test;

import com.samp.mobile.launcher.pack.IniFix;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Testes do ajuste de settings.ini. Com argumento (pasta), grava as saidas para o conferidor C++ (inih REAL do jogo). */
public class IniFixTest {
    static int pass = 0, fail = 0;
    static void r(String n, boolean g, String why) { if (g) pass++; else fail++; System.out.println((g ? "PASS " : "FAIL ") + n + (g ? "" : "  -> " + why)); }

    public static void main(String[] a) throws Exception {
        Map<String, String> cases = new LinkedHashMap<>();
        cases.put("asset_padrao", "[client]\nhost = \nport = \nname = Nick_Name\n\n[debug]\ndebug = false\n\n[gui]\nFont = Arial.ttf\nandroidkeyboard = 0\n");
        cases.put("sem_chave", "[client]\nname = x\n[gui]\nFont = Arial.ttf\nFontSize = 30\n");
        cases.put("sem_secao_gui", "[client]\nname = x\nhost = 1.2.3.4\n");
        cases.put("chave_repetida", "[gui]\nandroidkeyboard = 1\nFont=a\nandroidkeyboard = 0\n");
        cases.put("caixa_mista", "[GUI]\nAndroidKeyboard = false\n");
        cases.put("crlf_bom", "﻿[gui]\r\nFont = a\r\nandroidkeyboard=0\r\n[x]\r\ny=1\r\n");
        cases.put("outra_secao", "[gui]\nFont=a\n[client]\nandroidkeyboard = 1\n"); // chave na secao errada nao conta
        cases.put("comentarios", "; c\n[gui]\n# androidkeyboard = 1\nandroidkeyboard = 0 ; trocado\n");
        cases.put("secao_no_meio", "[gui]\nFont=a\n\n[debug]\ndebug=true\n");
        cases.put("dois_pontos", "[gui]\nandroidkeyboard: 0\n");

        boolean dump = a.length > 0;
        Path dir = dump ? Paths.get(a[0]) : null;
        if (dump) Files.createDirectories(dir);
        for (Map.Entry<String, String> e : cases.entrySet()) {
            String fixed = IniFix.enforce(e.getValue(), "gui", "androidkeyboard", "1");
            String v = IniFix.read(fixed, "gui", "androidkeyboard");
            r("enforce[" + e.getKey() + "]: valor efetivo = 1", IniFix.asBool(v, false), "v=" + v + "\n" + fixed);
            int n = 0; for (String l : fixed.split("\r?\n")) if (l.trim().toLowerCase().startsWith("androidkeyboard") && !l.trim().startsWith("#")) n++;
            r("enforce[" + e.getKey() + "]: exatamente UMA chave na secao gui", n == (e.getKey().equals("outra_secao") ? 2 : 1), "n=" + n);
            r("enforce[" + e.getKey() + "]: idempotente", fixed.equals(IniFix.enforce(fixed, "gui", "androidkeyboard", "1")), "");
            if (dump) Files.write(dir.resolve(e.getKey() + ".ini"), fixed.getBytes(StandardCharsets.UTF_8));
        }
        String f = IniFix.enforce(cases.get("crlf_bom"), "gui", "androidkeyboard", "1");
        r("preserva BOM e CRLF e as outras secoes", f.startsWith("﻿") && f.contains("\r\n") && !f.replace("\r\n", "").contains("\n") && "1".equals(IniFix.read(f, "x", "y")), f);
        r("chave em OUTRA secao nao e tocada", "1".equals(IniFix.read(IniFix.enforce(cases.get("outra_secao"), "gui", "androidkeyboard", "1"), "client", "androidkeyboard")), "");
        r("read: comentario em linha e ';' inicial ignorados", "0".equals(IniFix.read(cases.get("comentarios"), "gui", "androidkeyboard")), "" + IniFix.read(cases.get("comentarios"), "gui", "androidkeyboard"));
        r("read: ultimo valor repetido vale", "0".equals(IniFix.read(cases.get("chave_repetida"), "gui", "androidkeyboard")), "");
        r("asBool: true/yes/on/1 e false/no/off/0, lixo = default", IniFix.asBool("TRUE", false) && IniFix.asBool("yes", false) && IniFix.asBool("on", false) && IniFix.asBool("1", false)
                && !IniFix.asBool("0", true) && !IniFix.asBool("off", true) && IniFix.asBool("talvez", true) && !IniFix.asBool("talvez", false) && IniFix.asBool(null, true), "");
        r("looksValid", IniFix.looksValid("[a]\nb=1\n") && !IniFix.looksValid("") && !IniFix.looksValid("   \n") && !IniFix.looksValid("b=1\n") && !IniFix.looksValid("[a]\u0000\n"), "");
        System.out.println("\n" + pass + " ok, " + fail + " falhas");
        if (fail > 0) System.exit(1);
    }
}
