package test;

import com.samp.mobile.launcher.util.NickRules;

public class NickRulesTest {
    static int pass = 0, fail = 0;
    static void r(String n, boolean g, String why) { if (g) pass++; else fail++; System.out.println((g ? "PASS " : "FAIL ") + n + (g ? "" : "  -> " + why)); }
    public static void main(String[] a) {
        r("toWire: espaco vira _", NickRules.toWire("Jose Luis").equals("Jose_Luis"), NickRules.toWire("Jose Luis"));
        r("toWire: varios nomes", NickRules.toWire("Jose Luis Silva").equals("Jose_Luis_Silva"), "");
        r("toWire: espacos repetidos viram UM _", NickRules.toWire("Jose    Luis").equals("Jose_Luis"), NickRules.toWire("Jose    Luis"));
        r("toWire: espacos nas pontas sao tirados", NickRules.toWire("  Jose Luis  ").equals("Jose_Luis"), NickRules.toWire("  Jose Luis  "));
        r("toWire: tab e NBSP tambem", NickRules.toWire("Jose\tLuis Silva").equals("Jose_Luis_Silva"), NickRules.toWire("Jose\tLuis Silva"));
        r("toWire: sem espaco nao muda (underscore digitado fica)", NickRules.toWire("Nome_Sobrenome").equals("Nome_Sobrenome"), "");
        r("toWire: mistura espaco e _", NickRules.toWire("A _B").equals("A__B"), NickRules.toWire("A _B"));
        r("display: normaliza mantendo UM espaco", NickRules.normalizeDisplay("  Jose   Luis ").equals("Jose Luis"), NickRules.normalizeDisplay("  Jose   Luis "));
        r("valido: com espaco", NickRules.isValidDisplay("Jose Luis") && NickRules.isValidDisplay("Jose Luis Silva"), "");
        r("valido: sem espaco e com simbolos permitidos", NickRules.isValidDisplay("[BR]Nome_X") && NickRules.isValidDisplay("a.b=c@d$(e)"), "");
        r("invalido: curto (min 3 no formato do servidor)", !NickRules.isValidDisplay("ab") && !NickRules.isValidDisplay("a "), "");
        r("a b = a_b (3 chars) e valido", NickRules.isValidDisplay(" a b "), "");
        r("invalido: so espacos / vazio / null", !NickRules.isValidDisplay("     ") && !NickRules.isValidDisplay("") && !NickRules.isValidDisplay(null), "");
        r("invalido: caractere proibido (acento, !, /)", !NickRules.isValidDisplay("José Luis") && !NickRules.isValidDisplay("Jose!") && !NickRules.isValidDisplay("a/b c"), "");
        r("limite de 20 conta o formato do servidor", NickRules.isValidDisplay("Aaaaaaaaa Bbbbbbbbb c") == false && NickRules.isValidDisplay("Aaaaaaaaa Bbbbbbbbb") , "");
        r("21 caracteres e invalido", !NickRules.isValidDisplay("123456789012345678901"), "");
        r("differsOnWire", NickRules.differsOnWire("Jose Luis") && !NickRules.differsOnWire("Jose_Luis") && !NickRules.differsOnWire("  Jose  "), "");
        r("isValidWire rejeita espaco", !NickRules.isValidWire("Jose Luis") && NickRules.isValidWire("Jose_Luis"), "");
        r("wire de display valido e sempre valido", NickRules.isValidWire(NickRules.toWire("Jose Luis Silva")), "");
        System.out.println("\n" + pass + " ok, " + fail + " falhas");
        if (fail > 0) System.exit(1);
    }
}
