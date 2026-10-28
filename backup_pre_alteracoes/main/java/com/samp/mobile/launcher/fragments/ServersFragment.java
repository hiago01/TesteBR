package com.samp.mobile.launcher.fragments;

import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.AccelerateDecelerateInterpolator;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentPagerAdapter;

import com.google.android.material.tabs.TabLayout;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.ViewPagerWithoutSwipe;

/** Painel central "Servidores": abas Favoritos/Oficial, botao Atualizar e barra Conectar. */
@Obfuscate
public class ServersFragment extends Fragment implements MainActivity.ServerStateListener {

    /** Ultima aba aberta (0 = Favoritos, 1 = Oficial). */
    private static int sLastPage = 1;

    public final class PagerAdapter extends FragmentPagerAdapter {
        private int pageCount;

        public PagerAdapter(FragmentManager fragmentManager, int i) {
            super(fragmentManager);
            pageCount = i;
        }

        @Override
        public Fragment getItem(int position) {
            return ServerPagesItemFragment.newInstance(position);
        }

        @Override
        public int getCount() {
            return pageCount;
        }

        @Override
        public CharSequence getPageTitle(int position) {
            if (position == 0) return getString(R.string.mx_tab_favorites);
            if (position == 1) return getString(R.string.mx_tab_official);
            return "";
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_servers, container, false);
        if (getActivity() instanceof MainActivity) {
            MainActivity.hideKeyboard(getActivity());
            final MainActivity main = (MainActivity) getActivity();

            final View refresh = view.findViewById(R.id.mx_btn_refresh);
            final ImageView refreshIcon = view.findViewById(R.id.mx_refresh_icon);
            refresh.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    refreshIcon.animate().cancel();
                    refreshIcon.setRotation(0f);
                    refreshIcon.animate().rotation(360f).setDuration(600)
                            .setInterpolator(new AccelerateDecelerateInterpolator()).start();
                    main.refreshAll();
                }
            });
            view.findViewById(R.id.mx_btn_connect).setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    main.connectSelected();
                }
            });
        }
        return view;
    }

    @Override
    public void onStart() {
        super.onStart();
        View view = getView();
        if (view != null) {
            TabLayout tabLayout = view.findViewById(R.id.tabLayout_servers);
            ViewPagerWithoutSwipe viewPager = view.findViewById(R.id.viewPager_servers);
            if (viewPager.getAdapter() == null) {
                viewPager.setAdapter(new PagerAdapter(getChildFragmentManager(), 2));
                tabLayout.setupWithViewPager(viewPager);
                viewPager.setCurrentItem(sLastPage, false);
                tabLayout.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
                    @Override public void onTabSelected(TabLayout.Tab tab) { sLastPage = tab.getPosition(); }
                    @Override public void onTabUnselected(TabLayout.Tab tab) { }
                    @Override public void onTabReselected(TabLayout.Tab tab) { }
                });
            }
        }
        if (getActivity() instanceof MainActivity) {
            MainActivity main = (MainActivity) getActivity();
            main.addServerStateListener(this);
            main.reloadServersIfNeeded();
        }
        onServerStateChanged();
    }

    @Override
    public void onStop() {
        if (getActivity() instanceof MainActivity) {
            ((MainActivity) getActivity()).removeServerStateListener(this);
        }
        super.onStop();
    }

    /** Atualiza o texto de ajuda e o estado do botao Conectar. */
    @Override
    public void onServerStateChanged() {
        View view = getView();
        if (view == null || !(getActivity() instanceof MainActivity)) return;
        MainActivity main = (MainActivity) getActivity();
        TextView hint = view.findViewById(R.id.mx_connect_hint);
        View connect = view.findViewById(R.id.mx_btn_connect);

        SAMPServerInfo selected = main.getSelectedServer();
        boolean canConnect = selected != null && !MainActivity.isPlaceholder(selected)
                && selected.getServerStatus() != SAMPServerInfo.Status.LOADING;
        connect.setEnabled(canConnect);
        connect.setAlpha(canConnect ? 1f : 0.55f);

        if (selected != null && !MainActivity.isPlaceholder(selected)) {
            hint.setText(getString(R.string.mx_hint_selected, selected.getServerName()));
            return;
        }
        SAMPServerInfo hosted = main.getServerList().isEmpty() ? null : main.getServerList().get(0);
        if (hosted != null && hosted.getServerStatus() == SAMPServerInfo.Status.ERROR) {
            hint.setText(R.string.mx_hint_error);
        } else if (hosted != null && hosted.getServerStatus() == SAMPServerInfo.Status.LOADING) {
            hint.setText(R.string.mx_hint_loading);
        } else {
            hint.setText(R.string.mx_hint_default);
        }
    }
}
