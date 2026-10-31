package com.samp.mobile.launcher.fragments;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.format.DateFormat;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.util.NewsParser;
import com.samp.mobile.launcher.util.NotificationStore;
import com.samp.mobile.launcher.util.SocialLinks;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Pagina "Noticias". Fonte principal: noticias.json do site (AppEndpoints.newsUrl). Mostra primeiro a ultima copia
 * guardada no aparelho (abre na hora, funciona offline), depois atualiza pela rede. As notificacoes push recebidas
 * (NotificationStore) continuam aparecendo na mesma lista, sem repetir as que ja estao no JSON.
 */
public class NewsFragment extends Fragment {

    private static final String TAG = "MxNews";
    private static final String PREFS = "mx_news_cache";
    private static final String KEY_JSON = "json";
    private static final int MAX_BYTES = 512 * 1024;

    private final List<NewsParser.Item> items = new ArrayList<>();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private List<NewsParser.Item> remote = new ArrayList<>();
    private NewsAdapter adapter;
    private TextView empty;
    private Thread loader;
    private volatile boolean alive;

    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_news, container, false);
        empty = view.findViewById(R.id.mx_news_empty);
        RecyclerView rv = view.findViewById(R.id.mx_news_recycler);
        rv.setLayoutManager(new LinearLayoutManager(requireContext()));
        adapter = new NewsAdapter();
        rv.setAdapter(adapter);
        view.findViewById(R.id.mx_news_clear).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                NotificationStore.clear(v.getContext());   // limpa so o historico de push; as noticias do site continuam
                rebuild();
            }
        });
        return view;
    }

    @Override
    public void onStart() {
        super.onStart();
        alive = true;
        remote = readCache();
        rebuild();
        fetchRemote();
    }

    @Override
    public void onStop() {
        alive = false;
        if (loader != null) loader.interrupt();
        super.onStop();
    }

    // ---- dados ----

    private List<NewsParser.Item> readCache() {
        if (getContext() == null) return new ArrayList<>();
        try {
            String s = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_JSON, null);
            return s == null ? new ArrayList<NewsParser.Item>() : NewsParser.parse(s);
        } catch (Exception e) {
            return new ArrayList<>();
        }
    }

    private void fetchRemote() {
        final Context app = requireContext().getApplicationContext();
        final String url = AppEndpoints.newsUrl(app);
        loader = new Thread(new Runnable() {
            @Override
            public void run() {
                String body = null;
                HttpURLConnection c = null;
                try {
                    c = (HttpURLConnection) new URL(url).openConnection();
                    c.setConnectTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
                    c.setReadTimeout(AppEndpoints.HTTP_TIMEOUT_MS);
                    c.setRequestProperty("Cache-Control", "no-cache");
                    c.setRequestProperty("Accept-Encoding", "identity");
                    if (c.getResponseCode() != 200) throw new java.io.IOException("HTTP " + c.getResponseCode());
                    InputStream in = c.getInputStream();
                    ByteArrayOutputStream bo = new ByteArrayOutputStream();
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        bo.write(buf, 0, n);
                        if (bo.size() > MAX_BYTES) throw new java.io.IOException("noticias.json grande demais");
                    }
                    body = bo.toString("UTF-8");
                    final List<NewsParser.Item> parsed = NewsParser.parse(body);   // so guarda no cache se for valido
                    app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_JSON, body).apply();
                    ui.post(new Runnable() {
                        @Override public void run() {
                            if (!alive || !isAdded()) return;
                            remote = parsed;
                            rebuild();
                        }
                    });
                } catch (Exception e) {
                    Log.w(TAG, "noticias.json indisponivel (" + e + "): fica a copia guardada");
                } finally {
                    if (c != null) c.disconnect();
                }
            }
        }, "news-load");
        loader.start();
    }

    private void rebuild() {
        if (getContext() == null || adapter == null) return;
        items.clear();
        items.addAll(remote);
        Set<String> seen = new HashSet<>();
        for (NewsParser.Item r : remote) seen.add(key(r.title, r.body));
        for (NotificationStore.Item p : NotificationStore.list(requireContext())) {
            if (seen.add(key(p.title, p.body))) items.add(new NewsParser.Item("", p.title, p.body, "", p.url, p.type, p.time));
        }
        Collections.sort(items, new Comparator<NewsParser.Item>() {
            @Override public int compare(NewsParser.Item a, NewsParser.Item b) { return Long.compare(b.time, a.time); }
        });
        adapter.notifyDataSetChanged();
        empty.setVisibility(items.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private static String key(String t, String b) { return (t == null ? "" : t.trim()) + "\u0001" + (b == null ? "" : b.trim()); }

    private final class NewsAdapter extends RecyclerView.Adapter<NewsAdapter.Holder> {
        @NonNull
        @Override
        public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            return new Holder(LayoutInflater.from(parent.getContext()).inflate(R.layout.mx_news_item, parent, false));
        }

        @Override
        public void onBindViewHolder(@NonNull Holder h, int position) {
            final NewsParser.Item it = items.get(position);
            h.title.setText(it.title);
            h.body.setText(it.body);
            h.body.setVisibility(it.body.isEmpty() ? View.GONE : View.VISIBLE);
            if (it.time > 0) {
                h.time.setVisibility(View.VISIBLE);
                h.time.setText(DateFormat.getMediumDateFormat(h.itemView.getContext()).format(new Date(it.time))
                        + " " + DateFormat.getTimeFormat(h.itemView.getContext()).format(new Date(it.time)));
            } else {
                h.time.setVisibility(View.GONE);
            }
            if (!it.image.isEmpty()) {
                h.banner.setVisibility(View.VISIBLE);
                Glide.with(h.banner).load(it.image).into(h.banner);
            } else {
                Glide.with(h.banner).clear(h.banner);
                h.banner.setVisibility(View.GONE);
            }
            h.itemView.setOnClickListener(SocialLinks.isSafeUrl(it.url) ? new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    SocialLinks.open(v.getContext(), it.url);
                }
            } : null);
        }

        @Override
        public int getItemCount() {
            return items.size();
        }

        final class Holder extends RecyclerView.ViewHolder {
            final TextView title, body, time;
            final ImageView banner;

            Holder(@NonNull View v) {
                super(v);
                title = v.findViewById(R.id.mx_news_title_text);
                body = v.findViewById(R.id.mx_news_body);
                time = v.findViewById(R.id.mx_news_time);
                banner = v.findViewById(R.id.mx_news_banner);
            }
        }
    }
}
