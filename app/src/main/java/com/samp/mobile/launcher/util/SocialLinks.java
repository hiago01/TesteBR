package com.samp.mobile.launcher.util;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;

import com.samp.mobile.R;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Links sociais configurados em res/values/maximo_links.xml. Vazio = oculto; nada e inventado. */
public final class SocialLinks {
    private SocialLinks() {}

    public static final class Link {
        public final String name;
        public final String url;
        public final int iconRes;

        Link(String name, String url, int iconRes) {
            this.name = name;
            this.url = url;
            this.iconRes = iconRes;
        }
    }

    public static List<Link> configured(Context c) {
        List<Link> out = new ArrayList<>();
        add(out, "Discord", c.getString(R.string.mx_link_discord), R.drawable.mx_ic_discord);
        add(out, "Instagram", c.getString(R.string.mx_link_instagram), R.drawable.mx_ic_instagram);
        add(out, "YouTube", c.getString(R.string.mx_link_youtube), R.drawable.mx_ic_youtube);
        add(out, "TikTok", c.getString(R.string.mx_link_tiktok), R.drawable.mx_ic_tiktok);
        add(out, "Site oficial", c.getString(R.string.mx_link_site), R.drawable.mx_ic_link);
        return out;
    }

    private static void add(List<Link> out, String name, String url, int icon) {
        if (isSafeUrl(url)) out.add(new Link(name, url.trim(), icon));
    }

    /** Apenas http/https. */
    public static boolean isSafeUrl(String url) {
        if (url == null) return false;
        String u = url.trim().toLowerCase(Locale.ROOT);
        return u.startsWith("https://") || u.startsWith("http://");
    }

    public static void open(Context c, String url) {
        if (!isSafeUrl(url)) return;
        try {
            Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(url.trim()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            c.startActivity(i);
        } catch (ActivityNotFoundException e) {
            Toast.makeText(c, "Nenhum aplicativo para abrir o link.", Toast.LENGTH_SHORT).show();
        }
    }
}
