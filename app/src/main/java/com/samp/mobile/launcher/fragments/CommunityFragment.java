package com.samp.mobile.launcher.fragments;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.samp.mobile.R;
import com.samp.mobile.launcher.util.SocialLinks;

import java.util.List;

/** Pagina "Comunidade": um botao por link social configurado (nenhum link e inventado). */
public class CommunityFragment extends Fragment {

    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_community, container, false);
        LinearLayout list = view.findViewById(R.id.mx_community_list);
        TextView empty = view.findViewById(R.id.mx_community_empty);

        List<SocialLinks.Link> links = SocialLinks.configured(requireContext());
        empty.setVisibility(links.isEmpty() ? View.VISIBLE : View.GONE);
        for (final SocialLinks.Link link : links) {
            View item = inflater.inflate(R.layout.mx_link_item, list, false);
            ((ImageView) item.findViewById(R.id.mx_link_icon)).setImageResource(link.iconRes);
            ((TextView) item.findViewById(R.id.mx_link_name)).setText(link.name);
            item.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    SocialLinks.open(v.getContext(), link.url);
                }
            });
            list.addView(item);
        }
        return view;
    }
}
