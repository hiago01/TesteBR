package com.samp.mobile.launcher.pack;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Interpreta os campos "version" e "packs" do client_config.json. Sem packs => fluxo antigo (arquivo a arquivo). */
public final class PackConfig {
    public final String version;       // versao do CONTEUDO (null se ausente)
    public final List<PackSpec> packs; // vazio se ausente

    private PackConfig(String version, List<PackSpec> packs) {
        this.version = version;
        this.packs = packs;
    }

    /** @param json o texto COMPLETO do client_config.json (aceita {"client_config":{...}} ou o objeto direto). */
    public static PackConfig parse(String json) throws PackException {
        Object root = MiniJson.parse(json);
        Map<String, Object> m = MiniJson.asMap(root);
        if (m == null) throw new PackException("client_config nao e um objeto JSON");
        Map<String, Object> cfg = MiniJson.asMap(m.get("client_config"));
        if (cfg == null) cfg = m;

        String version = MiniJson.str(cfg, "version");
        if (version != null) version = version.trim();
        if (version != null && version.isEmpty()) version = null;
        if (version != null && !version.matches("[A-Za-z0-9._-]{1,64}")) throw new PackException("\"version\" invalida (use letras, numeros, . _ -)");

        List<PackSpec> list = new ArrayList<>();
        List<Object> arr = MiniJson.asList(cfg.get("packs"));
        if (arr != null) {
            for (Object o : arr) {
                Map<String, Object> p = MiniJson.asMap(o);
                if (p == null) throw new PackException("item de \"packs\" invalido");
                list.add(new PackSpec(
                        MiniJson.str(p, "name"),
                        (int) MiniJson.lng(p, "gpu", -1),
                        MiniJson.str(p, "url"),
                        MiniJson.lng(p, "size", -1),
                        MiniJson.str(p, "sha256"),
                        MiniJson.lng(p, "unpacked", -1)));
            }
            for (int a = 0; a < list.size(); a++)
                for (int b = a + 1; b < list.size(); b++)
                    if (list.get(a).name.equals(list.get(b).name)) throw new PackException("pacote repetido: " + list.get(a).name);
            if (!list.isEmpty() && version == null) throw new PackException("\"packs\" exige \"version\" no client_config");
        }
        return new PackConfig(version, list);
    }

    /** core (gpu 0) + os pacotes do gpuType do aparelho, na ordem do JSON. null se faltar pacote da GPU. */
    public List<PackSpec> selectFor(int gpuType) {
        List<PackSpec> sel = new ArrayList<>();
        boolean hasGpuPack = false;
        boolean anyGpuPackExists = false;
        for (PackSpec p : packs) {
            if (p.gpu != 0) anyGpuPackExists = true;
            if (p.gpu == 0) sel.add(p);
            else if (p.gpu == gpuType) { sel.add(p); hasGpuPack = true; }
        }
        if (sel.isEmpty()) return null;
        if (anyGpuPackExists && !hasGpuPack) return null; // o servidor tem pacotes de GPU, mas nenhum serve a este aparelho
        return sel;
    }
}
