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
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.util.NickManager;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.ViewPagerWithoutSwipe;

/**
 * Painel central "Servidores": abas Oficial/Favoritos (favoritos dos servidores oficiais), botao
 * Atualizar e barra Conectar. A aba antiga de favoritos manuais (IP/porta) fica fora da navegacao
 * enquanto {@link Config#SHOW_LEGACY_FAVORITES} for false; sua implementacao continua no projeto.
 */
@Obfuscate
public class ServersFragment extends Fragment implements MainActivity.ServerStateListener {

    /** Paginas exibidas, na ordem das abas (ids de ServerPagesItemFragment.PAGE_*). */
    private static int[] pages() {
        java.util.ArrayList<Integer> l = new java.util.ArrayList<>();
        if (Config.SHOW_LEGACY_FAVORITES) l.add(ServerPagesItemFragment.PAGE_LEGACY_FAVORITES);
        l.add(ServerPagesItemFragment.PAGE_OFFICIAL);
        if (Config.SHOW_OFFICIAL_FAVORITES) l.add(ServerPagesItemFragment.PAGE_OFFICIAL_FAVORITES);
        int[] out = new int[l.size()];
        for (int i = 0; i < out.length; i++) out[i] = l.get(i);
        return out;
    }

    /** Ultima pagina aberta (id de pagina, nao posicao da aba). */
    private static int sLastPageId = ServerPagesItemFragment.PAGE_OFFICIAL;

    public final class PagerAdapter extends FragmentPagerAdapter {
        private int pageCount;

        public PagerAdapter(FragmentManager fragmentManager, int i) {
            super(fragmentManager);
            pageCount = i;
        }

        @Override
        public Fragment getItem(int position) {
            return ServerPagesItemFragment.newInstance(pages()[position]);
        }

        @Override
        public int getCount() {
            return pageCount;
        }

        @Override
        public CharSequence getPageTitle(int position) {
            switch (pages()[position]) {
                case ServerPagesItemFragment.PAGE_LEGACY_FAVORITES:   return getString(R.string.mx_tab_legacy_favorites);
                case ServerPagesItemFragment.PAGE_OFFICIAL:           return getString(R.string.mx_tab_official);
                case ServerPagesItemFragment.PAGE_OFFICIAL_FAVORITES: return getString(R.string.mx_tab_favorites);
                default: return "";
            }
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
            view.findViewById(R.id.mx_nick_chip).setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    // Tela de Nick (antes ficava em Configuracoes). Aqui pode cancelar; na 1a conexao e obrigatoria.
                    NickManager.showDialog(main, !NickManager.hasNick(main), new NickManager.Callback() {
                        @Override public void onNick(String nick) { updateNickChip(); }
                    });
                }
            });
            updateNickChip(view);
            view.findViewById(R.id.mx_btn_connect).setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    main.connectSelected();
                }
            });
        }
        return view;
    }

    private void updateNickChip() { updateNickChip(getView()); }

    private void updateNickChip(View root) {
        if (root == null || getActivity() == null) return;
        TextView t = root.findViewById(R.id.mx_nick_chip_text);
        if (t == null) return;
        String n = NickManager.getNick(getActivity());
        t.setText(n == null ? getString(R.string.mx_nick_chip_empty) : getString(R.string.mx_nick_chip, n));
    }

    @Override
    public void onStart() {
        super.onStart();
        updateNickChip();
        View view = getView();
        if (view != null) {
            TabLayout tabLayout = view.findViewById(R.id.tabLayout_servers);
            ViewPagerWithoutSwipe viewPager = view.findViewById(R.id.viewPager_servers);
            if (viewPager.getAdapter() == null) {
                final int[] pg = pages();
                viewPager.setAdapter(new PagerAdapter(getChildFragmentManager(), pg.length));
                tabLayout.setupWithViewPager(viewPager);
                int start = 0;
                for (int i = 0; i < pg.length; i++) if (pg[i] == sLastPageId) start = i;
                viewPager.setCurrentItem(start, false);
                tabLayout.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
                    @Override public void onTabSelected(TabLayout.Tab tab) {
                        int p = tab.getPosition();
                        if (p >= 0 && p < pg.length) sLastPageId = pg[p];
                    }
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
