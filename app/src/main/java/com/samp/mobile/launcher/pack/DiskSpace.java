package com.samp.mobile.launcher.pack;

import java.util.List;

public final class DiskSpace {
    private DiskSpace() {}

    public static final long MARGIN_BYTES = 200L * 1024 * 1024;

    /** zip + descompactado de todos os pacotes + margem de 200 MB (pior caso: nada e liberado durante a instalacao). */
    public static long required(List<PackSpec> packs) {
        long t = MARGIN_BYTES;
        for (PackSpec p : packs) t += p.size + p.estimatedUnpacked();
        return t;
    }

    public static boolean enough(long freeBytes, List<PackSpec> packs) {
        return freeBytes >= required(packs);
    }
}
