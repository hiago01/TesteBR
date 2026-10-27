package com.samp.mobile.launcher.fragments;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.fragment.app.Fragment;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;

/** Aba Inicio. Os links sociais agora ficam no rodape do launcher (res/values/social_links.xml). */
@Obfuscate
public class HomeFragment extends Fragment {

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_home, container, false);

        MainActivity.hideKeyboard(getActivity());

        view.findViewById(R.id.btn_home_play).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (getActivity() instanceof MainActivity) {
                    ((MainActivity) getActivity()).showPage(MainActivity.PAGE_SERVERS);
                }
            }
        });
        return view;
    }
}
