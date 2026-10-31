package test;

import com.samp.mobile.launcher.util.NewsParser;
import java.util.List;

public class NewsParserTest {
    static int pass, fail;
    static void check(boolean ok, String n) { if (ok) { pass++; System.out.println("PASS " + n); } else { fail++; System.out.println("FAIL " + n); } }
    public static void main(String[] a) throws Exception {
        String j = "{\"news\":[{\"id\":\"1\",\"title\":\"Antiga\",\"message\":\"m1\",\"date\":\"2026-10-29 10:00\"},"
                + "{\"id\":\"2\",\"title\":\"Nova\",\"body\":\"m2\",\"image\":\"https://x/y.png\",\"url\":\"javascript:alert(1)\",\"type\":\"update\",\"date\":\"2026-10-30 22:00\"},"
                + "{\"title\":\"\",\"message\":\"\"},{\"title\":\"T\",\"image\":\"ftp://x\",\"time\":5}]}";
        List<NewsParser.Item> l = NewsParser.parse(j);
        check(l.size() == 3, "descarta item vazio (3 de 4)");
        check(l.get(0).title.equals("Nova"), "mais recente primeiro");
        check(l.get(0).body.equals("m2"), "\"body\" vale como mensagem");
        check(l.get(0).image.equals("https://x/y.png"), "imagem https aceita");
        check(l.get(0).url.isEmpty(), "url javascript: rejeitada");
        check(l.get(0).type.equals("update"), "tipo lido");
        check(l.get(2).image.isEmpty() && l.get(2).time == 5, "imagem ftp rejeitada; time em ms");
        check(l.get(1).time > 0 && l.get(0).time > l.get(1).time, "datas yyyy-MM-dd HH:mm");
        check(NewsParser.parse("[{\"title\":\"x\"}]").size() == 1, "array na raiz");
        boolean threw = false;
        try { NewsParser.parse("{\"x\":1}"); } catch (Exception e) { threw = true; }
        check(threw, "sem lista -> erro");
        threw = false;
        try { NewsParser.parse("nao e json"); } catch (Exception e) { threw = true; }
        check(threw, "json invalido -> erro");
        System.out.println("RESULT: pass=" + pass + " fail=" + fail);
        if (fail > 0) System.exit(1);
    }
}
