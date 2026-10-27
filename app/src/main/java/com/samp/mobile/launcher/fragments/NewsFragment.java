package com.samp.mobile.launcher.fragments;

import android.os.Bundle;
import android.text.format.DateFormat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.samp.mobile.R;
import com.samp.mobile.launcher.util.NotificationStore;
import com.samp.mobile.launcher.util.SocialLinks;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/** Pagina "Noticias": historico real das notificacoes recebidas neste aparelho. */
public class NewsFragment extends Fragment {

    private final List<NotificationStore.Item> items = new ArrayList<>();
    private NewsAdapter adapter;
    private TextView empty;

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
                NotificationStore.clear(v.getContext());
                reload();
            }
        });
        return view;
    }

    @Override
    public void onStart() {
        super.onStart();
        reload();
    }

    private void reload() {
        if (getContext() == null || adapter == null) return;
        items.clear();
        items.addAll(NotificationStore.list(requireContext()));
        adapter.notifyDataSetChanged();
        empty.setVisibility(items.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private final class NewsAdapter extends RecyclerView.Adapter<NewsAdapter.Holder> {
        @NonNull
        @Override
        public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            return new Holder(LayoutInflater.from(parent.getContext()).inflate(R.layout.mx_news_item, parent, false));
        }

        @Override
        public void onBindViewHolder(@NonNull Holder h, int position) {
            final NotificationStore.Item it = items.get(position);
            h.title.setText(it.title);
            h.body.setText(it.body);
            h.body.setVisibility(it.body.isEmpty() ? View.GONE : View.VISIBLE);
            h.time.setText(DateFormat.getMediumDateFormat(h.itemView.getContext()).format(new Date(it.time))
                    + " " + DateFormat.getTimeFormat(h.itemView.getContext()).format(new Date(it.time)));
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

            Holder(@NonNull View v) {
                super(v);
                title = v.findViewById(R.id.mx_news_title_text);
                body = v.findViewById(R.id.mx_news_body);
                time = v.findViewById(R.id.mx_news_time);
            }
        }
    }
}
