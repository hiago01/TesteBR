package com.samp.mobile.launcher.pack;

final class Hex {
    private Hex() {}
    private static final char[] D = "0123456789abcdef".toCharArray();

    static String of(byte[] b) {
        char[] c = new char[b.length * 2];
        for (int i = 0; i < b.length; i++) {
            c[i * 2] = D[(b[i] >> 4) & 15];
            c[i * 2 + 1] = D[b[i] & 15];
        }
        return new String(c);
    }

    static boolean isSha256(String s) {
        if (s == null || s.length() != 64) return false;
        for (int i = 0; i < 64; i++) {
            char c = s.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    }
}
