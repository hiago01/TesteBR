package test;

import com.samp.mobile.launcher.pack.GraphicsPreset;
import com.samp.mobile.launcher.pack.IniFix;

public class GraphicsPresetTest {
    static int fail = 0;
    static void r(String n, boolean g) { System.out.println((g ? "PASS " : "FAIL ") + n); if (!g) fail++; }
    public static void main(String[] a) {
        String base = "[client]\nname = x\n\n[gui]\nandroidkeyboard = 1\n";
        String o = GraphicsPreset.request(base, 0, 1);
        r("secao criada", "0".equals(IniFix.read(o, "maximo", "preset_request")) && "1".equals(IniFix.read(o, "maximo", "preset_request_id")));
        r("preserva gui", "1".equals(IniFix.read(o, "gui", "androidkeyboard")) && "x".equals(IniFix.read(o, "client", "name")));
        String o2 = GraphicsPreset.request(o, 3, 2);
        r("atualiza sem duplicar", "3".equals(IniFix.read(o2, "maximo", "preset_request")) && o2.indexOf("preset_request =") == o2.lastIndexOf("preset_request ="));
        r("idempotente", GraphicsPreset.request(o2, 3, 2).equals(o2));
        String done = IniFix.enforce(o, "maximo", "preset_done_id", "1");
        r("preserva done_id", "1".equals(IniFix.read(GraphicsPreset.request(done, 0, 1), "maximo", "preset_done_id")));
        r("clamp", GraphicsPreset.clamp(9) == 4 && GraphicsPreset.clamp(-3) == 0 && GraphicsPreset.DEFAULT == 0);
        System.out.println(fail == 0 ? "OK" : "FALHOU");
        if (fail != 0) System.exit(1);
    }
}
