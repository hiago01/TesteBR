package com.samp.mobile.launcher.util;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.util.Log;
import android.view.View;
import android.widget.RemoteViews;

import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;

import com.bumptech.glide.Glide;
import com.samp.mobile.R;
import com.samp.mobile.launcher.SplashActivity;

import java.util.Locale;
import java.util.concurrent.TimeUnit;

/**
 * Notificacoes do Maximo RP: canais proprios, visual customizado (RemoteViews escuros com
 * icone/banner), acoes, PendingIntent imutavel (obrigatorio no Android 12+) e historico local.
 */
public final class NotificationHelper {
    private static final String TAG = "MxNotif";

    public static final String CH_NEWS = "mx_news";
    public static final String CH_UPDATES = "mx_updates";
    public static final String CH_EVENTS = "mx_events";
    public static final String CH_DOWNLOAD = "mx_download";
    private static final String OLD_CHANNEL = "web_app_channel";

    public static final String EXTRA_TYPE = "notif_type";
    public static final String EXTRA_MESSAGE = "notif_message";
    public static final String EXTRA_URL = "notif_url";

    private static final int ID_DOWNLOAD = 7001;
    private static final int COLOR_ACCENT = 0xFF1E90FF;

    private NotificationHelper() {}

    public static void ensureChannels(Context ctx) {
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        nm.deleteNotificationChannel(OLD_CHANNEL); // canal antigo ("web_app")
        nm.createNotificationChannel(channel(ctx, CH_NEWS, R.string.mx_channel_news, R.string.mx_channel_news_desc, NotificationManager.IMPORTANCE_HIGH));
        nm.createNotificationChannel(channel(ctx, CH_UPDATES, R.string.mx_channel_updates, R.string.mx_channel_updates_desc, NotificationManager.IMPORTANCE_HIGH));
        nm.createNotificationChannel(channel(ctx, CH_EVENTS, R.string.mx_channel_events, R.string.mx_channel_events_desc, NotificationManager.IMPORTANCE_DEFAULT));
        NotificationChannel dl = channel(ctx, CH_DOWNLOAD, R.string.mx_channel_download, R.string.mx_channel_download_desc, NotificationManager.IMPORTANCE_LOW);
        dl.enableVibration(false);
        dl.setSound(null, null);
        nm.createNotificationChannel(dl);
    }

    private static NotificationChannel channel(Context c, String id, int name, int desc, int importance) {
        NotificationChannel ch = new NotificationChannel(id, c.getString(name), importance);
        ch.setDescription(c.getString(desc));
        ch.enableLights(true);
        ch.setLightColor(COLOR_ACCENT);
        ch.enableVibration(true);
        ch.setVibrationPattern(new long[]{0, 220, 120, 220});
        return ch;
    }

    private static boolean canNotify(Context ctx) {
        return NotificationManagerCompat.from(ctx).areNotificationsEnabled();
    }

    private static String channelFor(String type) {
        if (type == null) return CH_NEWS;
        switch (type.toLowerCase(Locale.ROOT)) {
            case "update": return CH_UPDATES;
            case "event": return CH_EVENTS;
            default: return CH_NEWS;
        }
    }

    /** Notificacao vinda do servidor (FCM). Pode ser chamada de thread de fundo. */
    public static void showRemote(Context ctx, String title, String body, String imageUrl, String type, String url) {
        Context app = ctx.getApplicationContext();
        if (title == null || title.trim().isEmpty()) title = app.getString(R.string.mx_notif_default_title);
        if (body == null) body = "";
        NotificationStore.add(app, new NotificationStore.Item(title, body, type, SocialLinks.isSafeUrl(url) ? url : "", System.currentTimeMillis()));
        if (!canNotify(app)) return;
        ensureChannels(app);

        Bitmap banner = loadBanner(app, imageUrl);
        int id = (int) (System.currentTimeMillis() & 0x0FFFFFFF);

        Intent open = new Intent(app, SplashActivity.class);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        open.putExtra(EXTRA_MESSAGE, body);
        open.putExtra(EXTRA_TYPE, type);
        PendingIntent content = PendingIntent.getActivity(app, id, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        NotificationCompat.Builder b = new NotificationCompat.Builder(app, channelFor(type))
                .setSmallIcon(R.drawable.mx_ic_stat)
                .setColor(COLOR_ACCENT)
                .setContentTitle(title)
                .setContentText(body)
                .setStyle(new NotificationCompat.DecoratedCustomViewStyle())
                .setCustomContentView(views(app, R.layout.notification, title, body, null))
                .setCustomBigContentView(views(app, R.layout.notification_big, title, body, banner))
                .setCategory(NotificationCompat.CATEGORY_MESSAGE)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(content);

        if (SocialLinks.isSafeUrl(url)) {
            Intent view = new Intent(Intent.ACTION_VIEW, Uri.parse(url.trim()));
            view.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            PendingIntent pi = PendingIntent.getActivity(app, id + 1, view,
                    PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            b.addAction(0, app.getString(R.string.mx_notif_open), pi);
        }
        Intent launcher = new Intent(app, SplashActivity.class);
        launcher.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        b.addAction(0, app.getString(R.string.mx_notif_open_launcher), PendingIntent.getActivity(app, id + 2, launcher,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));

        try {
            NotificationManagerCompat.from(app).notify(id, b.build());
        } catch (SecurityException e) {
            Log.w(TAG, "Sem permissao para notificar: " + e);
        }
    }

    private static RemoteViews views(Context c, int layout, String title, String body, Bitmap banner) {
        RemoteViews rv = new RemoteViews(c.getPackageName(), layout);
        rv.setTextViewText(R.id.title, title);
        rv.setTextViewText(R.id.message, body);
        if (layout == R.layout.notification_big) {
            if (banner != null) {
                rv.setImageViewBitmap(R.id.banner, banner);
                rv.setViewVisibility(R.id.banner, View.VISIBLE);
            } else {
                rv.setViewVisibility(R.id.banner, View.GONE);
            }
        }
        return rv;
    }

    private static Bitmap loadBanner(Context app, String url) {
        if (!SocialLinks.isSafeUrl(url)) return null;
        try {
            return Glide.with(app).asBitmap().load(url.trim()).submit(720, 270).get(8, TimeUnit.SECONDS);
        } catch (Exception e) {
            Log.w(TAG, "Banner da notificacao nao carregou: " + e);
            return null;
        }
    }

    // ---------------- download / atualizacao ----------------

    /** percent < 0 => indeterminado. */
    public static void showDownloadProgress(Context ctx, int percent, String text) {
        Context app = ctx.getApplicationContext();
        if (!canNotify(app)) return;
        ensureChannels(app);
        NotificationCompat.Builder b = new NotificationCompat.Builder(app, CH_DOWNLOAD)
                .setSmallIcon(R.drawable.mx_ic_stat)
                .setColor(COLOR_ACCENT)
                .setContentTitle(app.getString(R.string.mx_notif_download_title))
                .setContentText(text == null ? "" : text)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setProgress(100, Math.max(0, percent), percent < 0);
        try {
            NotificationManagerCompat.from(app).notify(ID_DOWNLOAD, b.build());
        } catch (SecurityException ignored) { }
    }

    public static void showDownloadDone(Context ctx) {
        Context app = ctx.getApplicationContext();
        if (!canNotify(app)) return;
        ensureChannels(app);
        Intent open = new Intent(app, SplashActivity.class);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        NotificationCompat.Builder b = new NotificationCompat.Builder(app, CH_UPDATES)
                .setSmallIcon(R.drawable.mx_ic_stat)
                .setColor(COLOR_ACCENT)
                .setContentTitle(app.getString(R.string.mx_notif_download_done_title))
                .setContentText(app.getString(R.string.mx_notif_download_done_text))
                .setAutoCancel(true)
                .setContentIntent(PendingIntent.getActivity(app, ID_DOWNLOAD, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        try {
            NotificationManagerCompat.from(app).notify(ID_DOWNLOAD, b.build());
        } catch (SecurityException ignored) { }
    }

    public static void cancelDownload(Context ctx) {
        NotificationManagerCompat.from(ctx.getApplicationContext()).cancel(ID_DOWNLOAD);
    }
}
