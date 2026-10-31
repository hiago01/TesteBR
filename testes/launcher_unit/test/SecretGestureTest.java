package test;

import com.samp.mobile.launcher.util.SecretGesture;

public class SecretGestureTest {
    static int fail = 0, n = 0;
    static void check(boolean c, String m) { n++; if (!c) { fail++; System.out.println("FALHOU: " + m); } else System.out.println("PASS " + m); }
    static boolean tap(SecretGesture g, long t) { return g.onTap(500, 500, 2400, 1080, t); }

    public static void main(String[] a) {
        SecretGesture g = new SecretGesture();
        check(!tap(g, 0) && !tap(g, 300) && !tap(g, 600) && tap(g, 900), "4 toques rapidos completam");
        check(!tap(g, 5000), "depois de completar reinicia (1 toque nao completa)");
        g.reset();
        check(!tap(g, 0) && !tap(g, 300) && !tap(g, 600), "3 toques nao completam");
        g.reset();
        check(!tap(g, 0) && !tap(g, 300) && !tap(g, 2000) && !tap(g, 2300), "pausa maior que o limite reinicia (so 2 apos a pausa)");
        g.reset();
        check(!tap(g, 0) && !tap(g, 1200) && !tap(g, 2400) && tap(g, 3600), "limite de 1,2 s entre toques ainda completa");
        g.reset();
        check(!tap(g, 0) && !tap(g, 1201) && !tap(g, 2402) && !tap(g, 3603), "1,201 s entre toques reinicia sempre");
        g.reset();
        check(!tap(g, 0) && !tap(g, 1000) && !tap(g, 2000) && tap(g, 3000), "toques espacados 1 s dentro do total completam");
        g.reset();
        check(!g.onTap(0, 0, 0, 0, 0) && !g.onTap(-5, -5, 0, 0, 100) && !g.onTap(1, 1, 0, 0, 200) && g.onTap(1, 1, 0, 0, 300), "posicao/tamanho ignorados (qualquer lugar)");
        // 4 ativam / 3 desligam: o alvo depende do estado
        g.reset();
        check(!g.onTap(0, 0, 0, 0, 0, SecretGesture.TAPS) && !g.onTap(0, 0, 0, 0, 300, SecretGesture.TAPS) && !g.onTap(0, 0, 0, 0, 600, SecretGesture.TAPS) && g.onTap(0, 0, 0, 0, 900, SecretGesture.TAPS), "desligado: 4 toques ativam (o 3o nao dispara)");
        g.reset();
        check(!g.onTap(0, 0, 0, 0, 0, SecretGesture.TAPS_OFF) && !g.onTap(0, 0, 0, 0, 300, SecretGesture.TAPS_OFF) && g.onTap(0, 0, 0, 0, 600, SecretGesture.TAPS_OFF), "ligado: 3 toques desligam");
        g.reset();
        check(!g.onTap(0, 0, 0, 0, 0, SecretGesture.TAPS_OFF) && !g.onTap(0, 0, 0, 0, 300, SecretGesture.TAPS_OFF), "ligado: 2 toques nao desligam");
        System.out.println(n + " verificacoes, " + fail + " falhas");
        if (fail != 0) System.exit(1);
    }
}
