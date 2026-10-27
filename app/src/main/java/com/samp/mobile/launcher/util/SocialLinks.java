package com.samp.mobile.launcher.util;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;

import com.samp.mobile.R;

/**
 * Links sociais do launcher. Os valores vem de res/values/social_links.xml (fonte unica).
 * Link vazio ou que nao seja http/https e tratado como "nao configurado": nada e inventado.
 */
public final class SocialLinks {

    private SocialLinks() {}

    public static String discord(Context c)   { return clean(c.getString(R.string.social_discord_url)); }
    public static String youtube(Context c)   { return clean(c.getString(R.string.social_youtube_url)); }
    public static String instagram(Context c) { return clean(c.getString(R.string.social_instagram_url)); }
    public static String site(Context c)      { return clean(c.getString(R.string.social_site_url)); }

    private static String clean(String raw) {
        if (raw == null) return "";
        String v = raw.trim();
        String lower = v.toLowerCase();
        return (lower.startsWith("https://") || lower.startsWith("http://")) ? v : "";
    }

    /** Abre o link no navegador/app padrao; avisa se nao estiver configurado. */
    public static void open(Context c, String url) {
        if (url == null || url.isEmpty()) {
            Toast.makeText(c, R.string.mx_link_not_configured, Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            c.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (ActivityNotFoundException e) {
            Toast.makeText(c, R.string.mx_link_not_configured, Toast.LENGTH_SHORT).show();
        }
    }
}
