package com.samp.mobile.launcher.fragments;

import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.transition.Fade;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.recyclerview.widget.SimpleItemAnimator;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.SplashActivity;
import com.samp.mobile.launcher.UpdateActivity;
import com.samp.mobile.launcher.adapters.FavouriteServerAdapter;
import com.samp.mobile.launcher.adapters.ServerAdapter;
import com.samp.mobile.launcher.util.ButtonAnimator;

import java.util.Objects;
@Obfuscate
public class ServerPagesItemFragment extends Fragment {

    /** Identificadores de pagina (independentes da posicao da aba). */
    public static final int PAGE_LEGACY_FAVORITES = 0;   // favoritos ANTIGOS (IP/porta manual) - desativado na UI
    public static final int PAGE_OFFICIAL = 1;           // lista oficial
    public static final int PAGE_OFFICIAL_FAVORITES = 2; // NOVO: favoritos dos servidores oficiais

    private int pagePosition;
    AlertDialog.Builder builder;

    public static ServerPagesItemFragment newInstance(int page)
    {
        ServerPagesItemFragment instance = new ServerPagesItemFragment();
        Bundle bundle = new Bundle();
        bundle.putInt("page", page);
        instance.setArguments(bundle);
        Log.d("InfoJSON", "ServerPagesItemFragment " + page);
        return instance;
    }

    public int getPage() {
        return pagePosition;
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if(getArguments() != null)
        {
            pagePosition = getArguments().getInt("page");
            Log.d("InfoJSON", "ServerPagesItemFragment " + pagePosition);
        }

        builder = new AlertDialog.Builder(getContext());
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view;
        RecyclerView recyclerView;

        ((MainActivity)getActivity()).hideKeyboard(getActivity());

        if (pagePosition == PAGE_OFFICIAL_FAVORITES) {
            view = inflater.inflate(R.layout.fragment_official_favorites, container, false);
            recyclerView = view.findViewById(R.id.server_recycler);
            ServerAdapter favAdapter = new ServerAdapter(view.getContext(), this.pagePosition);
            if (recyclerView.getItemAnimator() != null) {
                ((SimpleItemAnimator) recyclerView.getItemAnimator()).setSupportsChangeAnimations(false);
            }
            recyclerView.setLayoutManager(new LinearLayoutManager(view.getContext()));
            recyclerView.setAdapter(favAdapter);
            favAdapter.attachEmptyView(view.findViewById(R.id.mx_fav_empty));
            return view;
        }

        view = inflater.inflate(R.layout.fragment_favorite, container, false);

        if(pagePosition == PAGE_OFFICIAL) {
            view = inflater.inflate(R.layout.fragment_internet, container, false);
            ServerAdapter adapter = new ServerAdapter(view.getContext(), this.pagePosition);

            Log.d("AXL", "pagepos == 1");

            RecyclerView recyclerView2 = view.findViewById(R.id.server_recycler);
            if (recyclerView2.getItemAnimator() != null) {
                ((SimpleItemAnimator) Objects.requireNonNull(recyclerView2.getItemAnimator())).setSupportsChangeAnimations(false);
                recyclerView2.setLayoutManager(new LinearLayoutManager(view.getContext()));
                if (view.getContext() != null) {
                    recyclerView2.setAdapter(adapter);
                }
            }

            // O botao "ADD SERVER" da lista oficial (anuncio de venda de vaga do desenvolvedor original) foi removido.
        } else {
            view = inflater.inflate(R.layout.fragment_favorite, container, false);

            FavouriteServerAdapter adapter = new FavouriteServerAdapter(getActivity());

            recyclerView = view.findViewById(R.id.server_recycler);
            ((SimpleItemAnimator) recyclerView.getItemAnimator()).setSupportsChangeAnimations(false);
            recyclerView.setLayoutManager(new LinearLayoutManager(recyclerView.getContext()));
            if (recyclerView.getContext() != null) {
                recyclerView.setAdapter(adapter);
            }

            view.findViewById(R.id.buttonServer).setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    Log.d("gor", "clicked add server button");
                    ServerAddFragment newInstance = new ServerAddFragment(getActivity(), adapter);
                    newInstance.show();
                }
            });
        }

        return view;
    }
}
