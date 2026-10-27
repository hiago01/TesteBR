package com.samp.mobile.launcher.fragments;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;

/** Pagina "Inicio". Os links sociais agora vivem na pagina Comunidade e no rodape (maximo_links.xml). */
@Obfuscate
public class HomeFragment extends Fragment {

    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_home, container, false);
        if (getActivity() instanceof MainActivity) {
            MainActivity.hideKeyboard(getActivity());
        }
        view.findViewById(R.id.mx_home_go_servers).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (getActivity() instanceof MainActivity) {
                    ((MainActivity) getActivity()).openPage(MainActivity.PAGE_SERVERS);
                }
            }
        });
        return view;
    }
}
