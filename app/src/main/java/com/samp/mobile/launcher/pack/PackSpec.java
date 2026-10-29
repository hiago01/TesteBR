package com.samp.mobile.launcher.pack;

import java.util.Locale;

/** Um pacote zip descrito no client_config.json ("packs"). */
public final class PackSpec {
    public final String name;
    /** 0 = core, 1 = dxt, 2 = etc, 3 = pvr. */
    public final int gpu;
    public final String url;
    /** Tamanho do .zip em bytes. */
    public final long size;
    /** SHA-256 do .zip (hex minusculo). */
    public final String sha256;
    /** Tamanho descompactado (opcional; -1 = desconhecido). */
    public final long unpacked;

    public PackSpec(String name, int gpu, String url, long size, String sha256, long unpacked) throws PackException {
        if (name == null || !name.matches("[A-Za-z0-9._-]{1,64}")) throw new PackException("nome de pacote invalido: " + name);
        if (gpu < 0 || gpu > 3) throw new PackException("gpu invalida no pacote " + name + ": " + gpu);
        if (url == null || !(url.startsWith("http://") || url.startsWith("https://") || url.startsWith("/")))
            throw new PackException("url invalida no pacote " + name);
        if (size <= 0 || size >= 0xFFFFFFFFL) throw new PackException("tamanho invalido no pacote " + name + " (sem Zip64: < 4 GB)");
        if (!Hex.isSha256(sha256)) throw new PackException("sha256 invalido no pacote " + name);
        this.name = name;
        this.gpu = gpu;
        this.url = url;
        this.size = size;
        this.sha256 = sha256.toLowerCase(Locale.ROOT);
        this.unpacked = unpacked;
    }

    /** Espaco aproximado que o pacote ocupa descompactado. */
    public long estimatedUnpacked() {
        if (unpacked > 0) return unpacked;
        return size + size / 5; // sem informacao: +20% (texto deflated expande)
    }
}
