package test;

import com.samp.mobile.launcher.util.SecretGesture;

public class SecretGestureTest {
    static int fail = 0, n = 0;
    static void check(boolean c, String m) { n++; if (!c) { fail++; System.out.println("FALHOU: " + m); } else System.out.println("PASS " + m); }
    static final float W = 2400, H = 1080;
    static final float[][] C = { {50, 50}, {W - 50, 50}, {W - 50, H - 50}, {50, H - 50} };   // TL TR BR BL
    static boolean tap(SecretGesture g, int c, long t) { return g.onTap(C[c][0], C[c][1], W, H, t); }

    public static void main(String[] a) {
        SecretGesture g = new SecretGesture();
        check(!tap(g, 0, 1000) && !tap(g, 1, 1500) && !tap(g, 2, 2000) && tap(g, 3, 2500), "TL TR BR BL rapido completa");
        check(!tap(g, 0, 10000), "depois de completar reinicia (1 toque nao completa)");
        g.reset();
        check(!tap(g, 0, 0) && !tap(g, 1, 500) && !tap(g, 3, 1000) && !tap(g, 2, 1500) && !tap(g, 3, 2000), "ordem errada nao completa");
        g.reset();
        check(!tap(g, 0, 0) && !tap(g, 1, 500) && !tap(g, 2, 4000) && !tap(g, 3, 4500), "passo lento (>3s) reinicia");
        g.reset();
        check(!tap(g, 0, 0) && !tap(g, 1, 2500) && !tap(g, 2, 5000) && !tap(g, 3, 8500), "total maior que 8 s nao completa");
        g.reset();
        check(!g.onTap(W / 2, H / 2, W, H, 0) && !tap(g, 1, 100) && !tap(g, 2, 200) && !tap(g, 3, 300), "toque no meio / sem comecar no TL nao completa");
        g.reset();
        check(!tap(g, 0, 0) && !tap(g, 0, 500) && !tap(g, 1, 900) && !tap(g, 2, 1300) && tap(g, 3, 1700), "TL repetido recomeca e completa");
        g.reset();
        check(!tap(g, 0, 0) && !g.onTap(W / 2, H / 2, W, H, 300) && !tap(g, 1, 600) && !tap(g, 2, 900) && !tap(g, 3, 1200), "toque no meio no meio da sequencia reinicia");
        check(!g.onTap(0, 0, 0, 0, 0), "tela de tamanho 0 nao quebra");
        System.out.println(n + " verificacoes, " + fail + " falhas");
        if (fail != 0) System.exit(1);
    }
}
