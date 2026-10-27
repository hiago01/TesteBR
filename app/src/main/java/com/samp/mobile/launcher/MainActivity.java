package com.samp.mobile.launcher;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.PorterDuff;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.DisplayCutoutCompat;
import androidx.core.view.OnApplyWindowInsetsListener;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentPagerAdapter;
import androidx.recyclerview.widget.RecyclerView;

import com.android.volley.DefaultRetryPolicy;
import com.android.volley.NetworkResponse;
import com.android.volley.Request;
import com.android.volley.RequestQueue;
import com.android.volley.Response;
import com.android.volley.VolleyError;
import com.android.volley.toolbox.HttpHeaderParser;
import com.android.volley.toolbox.StringRequest;
import com.android.volley.toolbox.Volley;
import com.google.android.material.tabs.TabLayout;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.data.FavoritesInfo;
import com.samp.mobile.launcher.fragments.HomeFragment;
import com.samp.mobile.launcher.fragments.NewsFragment;
import com.samp.mobile.launcher.fragments.ServerPagesItemFragment;
import com.samp.mobile.launcher.fragments.ServersFragment;
import com.samp.mobile.launcher.fragments.SettingsFragment;
import com.samp.mobile.launcher.util.ConfigValidator;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.MusicController;
import com.samp.mobile.launcher.util.SampQueryAPI;
import com.samp.mobile.launcher.util.ServerConfigParser;
import com.samp.mobile.launcher.util.SignatureChecker;
import com.samp.mobile.launcher.util.SocialLinks;
import com.samp.mobile.launcher.util.ViewPagerWithoutSwipe;

import java.io.File;
import java.net.DatagramSocket;
import java.util.ArrayList;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

@Obfuscate
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "MainActivity";
    private static final String REQUEST_TAG = "servers_config";
    /** Tentativas da consulta UDP (SA-MP Query) ao servidor; UDP perde pacotes com frequencia. */
    private static final int QUERY_ATTEMPTS = 2;
    /** Intervalo minimo entre recargas automaticas (ao abrir a aba Servers). */
    private static final long AUTO_RELOAD_MIN_MS = 5000;

    /** Paginas do ViewPager (mesma ordem de antes; Noticias foi acrescentada ao final). */
    public static final int PAGE_HOME = 0;
    public static final int PAGE_SERVERS = 1;
    public static final int PAGE_SETTINGS = 2;
    public static final int PAGE_NEWS = 3;
    /** Se ficar em LOADING alem disso, o botao Atualizar destrava (evita ficar preso). */
    private static final long REFRESH_LOCK_MAX_MS = 15000;

    public String[] tabTitles = { "Servers", "Info", "Settings" };
    public int[] tabImages = { R.drawable.ic_mainmenu, R.drawable.ic_server, R.drawable.ic_settingsmenu};
    public int[] tabSelectedImages = { R.drawable.ic_mainmenu_on, R.drawable.ic_serveron, R.drawable.ic_settingsmenu_on};

    /**
     * Lista da aba "Hosted". Contem SEMPRE no maximo 1 item: o servidor do JSON, ou uma linha de
     * estado (carregando / erro com "toque para tentar de novo"). A instancia da lista nunca e
     * trocada depois do onCreate (os adapters mantem referencia a ela); so clear()/add().
     */
    public static ArrayList<SAMPServerInfo> mServersList = new ArrayList<>();
    public static ArrayList<SAMPServerInfo> mFavoriteServersList = new ArrayList<>();

    boolean bAdsInitialized = false;

    private final Handler mMain = new Handler(Looper.getMainLooper());
    private RequestQueue mQueue;
    private ExecutorService mQueryExecutor;
    private boolean mDestroyed = false;
    /** Incrementado a cada (re)carga; respostas de cargas antigas sao descartadas. */
    private int mLoadGeneration = 0;
    private long mLastAttemptMs = 0;

    private ViewPagerWithoutSwipe mPager;
    private int mCurrentPage = PAGE_SERVERS;
    private View[] mMenuItems;
    private Button mBtnRefresh;
    private TextView mTvTitle;
    private TextView mTvServerState;
    private View mDotServerState;
    private TextView mTvMusic;
    private ImageButton mBtnMusic;
    private MusicController mMusic;
    private final Runnable mUnlockRefresh = new Runnable() {
        @Override public void run() { updateServerStateUi(); }
    };

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Config.currentContext = this;

        mServersList = new ArrayList<>();
        mFavoriteServersList = new ArrayList<>();

        ConfigValidator.validateConfigFiles(this);

        if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        {
            Toast.makeText(this, "Use original launcher! No remake", Toast.LENGTH_LONG).show();
            return;
        }

        mQueue = Volley.newRequestQueue(getApplicationContext());
        mQueryExecutor = Executors.newSingleThreadExecutor();

        File file = new File(getExternalFilesDir(null) + "/launcher.apk");
        if (file.exists()) {
            file.delete();
        }

        FragmentManager fm = getSupportFragmentManager();
        ViewPagerAdapter sa = new ViewPagerAdapter(fm);
        mPager = findViewById(R.id.fragment_place);
        mPager.setAdapter(sa);

        setupImmersiveAndInsets();
        setupMenu();
        setupFooter();
        showPage(PAGE_SERVERS);

        getServersInfo();
        getFavoriteServersInfo();
        updateServerStateUi();
    }

    @Override
    protected void onDestroy() {
        mDestroyed = true;
        mMain.removeCallbacksAndMessages(null);
        if (mMusic != null) {
            mMusic.release();
        }
        if (mQueue != null) {
            mQueue.cancelAll(REQUEST_TAG);
            mQueue.stop();
        }
        if (mQueryExecutor != null) {
            mQueryExecutor.shutdownNow();
        }
        super.onDestroy();
    }

    public final ArrayList<SAMPServerInfo> getServerList() {
        return mServersList;
    }

    public final ArrayList<SAMPServerInfo> getFavoriteServerList() {
        return mFavoriteServersList;
    }

    // ------------------------------------------------------------------------------------
    // Servidor configurado por JSON (um unico servidor)
    // ------------------------------------------------------------------------------------

    /**
     * Baixa o servers.json (timeout + tentativas limitadas), valida, exibe SOMENTE o primeiro
     * servidor e consulta seu estado real por UDP. Nao bloqueia a thread principal e nunca
     * se auto-reagenda: falhas viram uma linha de erro com opcao de tentar novamente.
     *
     * @return true se a carga foi iniciada.
     */
    public boolean getServersInfo()
    {
        if (mDestroyed || mQueue == null) return false;

        final int gen = ++mLoadGeneration;
        mLastAttemptMs = SystemClock.elapsedRealtime();
        mQueue.cancelAll(REQUEST_TAG);

        setPlaceholder(SAMPServerInfo.Status.LOADING, getString(R.string.mx_loading_server), "");
        mMain.removeCallbacks(mUnlockRefresh);
        mMain.postDelayed(mUnlockRefresh, REFRESH_LOCK_MAX_MS + 500);

        StringRequest request = new StringRequest(Request.Method.GET, AppEndpoints.serversUrl(this), new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                if (mDestroyed || gen != mLoadGeneration) return;
                try {
                    ServerConfigParser.ServerConfig cfg = ServerConfigParser.parse(response);
                    SAMPServerInfo info = new SAMPServerInfo();
                    info.setId(cfg.number);
                    info.setServerName(cfg.name);
                    info.setAddress(cfg.ip);
                    info.setPort(cfg.port);
                    info.setMaxPlayerCount(cfg.maxPlayers);
                    info.setCurrentPlayerCount(-1);
                    info.setHasPassword(cfg.password);
                    info.setServerStatus(SAMPServerInfo.Status.LOADING);
                    info.setPing(-1);
                    replaceServer(info);
                    startStatusQuery(info, gen);
                } catch (ServerConfigParser.ConfigException e) {
                    Log.w(TAG, "Invalid servers.json: " + e.getMessage());
                    setPlaceholder(SAMPServerInfo.Status.ERROR, getString(R.string.mx_err_invalid_config, e.getMessage()), "");
                }
            }
        }, new Response.ErrorListener() {
            @Override
            public void onErrorResponse(VolleyError error) {
                if (mDestroyed || gen != mLoadGeneration) return;
                Log.w(TAG, "servers.json request failed: " + error);
                setPlaceholder(SAMPServerInfo.Status.ERROR, getString(R.string.mx_err_load_list, describe(error)), "");
            }
        }) {
            @Override
            protected Response<String> parseNetworkResponse(NetworkResponse r) {
                String charset = HttpHeaderParser.parseCharset(r.headers, "UTF-8");
                try {
                    return Response.success(new String(r.data, charset), HttpHeaderParser.parseCacheHeaders(r));
                } catch (java.io.UnsupportedEncodingException e) {
                    return Response.success(new String(r.data, java.nio.charset.StandardCharsets.UTF_8), HttpHeaderParser.parseCacheHeaders(r));
                }
            }
        };
        request.setTag(REQUEST_TAG);
        request.setShouldCache(false);
        request.setRetryPolicy(new DefaultRetryPolicy(AppEndpoints.HTTP_TIMEOUT_MS, AppEndpoints.HTTP_MAX_RETRIES, 1.5f));
        mQueue.add(request);
        return true;
    }

    /** Recarrega ao voltar para a aba Servers se o ultimo resultado foi erro/offline (sem spam). */
    public void reloadServersIfNeeded() {
        if (mDestroyed || mServersList.isEmpty()) return;
        SAMPServerInfo.Status st = mServersList.get(0).getServerStatus();
        if ((st == SAMPServerInfo.Status.ERROR || st == SAMPServerInfo.Status.OFFLINE)
                && SystemClock.elapsedRealtime() - mLastAttemptMs > AUTO_RELOAD_MIN_MS) {
            getServersInfo();
        }
    }

    private static String describe(VolleyError e) {
        if (e.networkResponse != null) return "HTTP " + e.networkResponse.statusCode;
        if (e instanceof com.android.volley.TimeoutError) return "timeout";
        if (e instanceof com.android.volley.NoConnectionError) return "no connection";
        return e.getClass().getSimpleName();
    }

    private void setPlaceholder(SAMPServerInfo.Status status, String title, String detail) {
        SAMPServerInfo p = new SAMPServerInfo();
        p.setServerName(title);
        p.setAddress(detail);
        p.setServerStatus(status);
        p.setPing(-1);
        replaceServer(p);
    }

    /** Substitui (nunca acrescenta) o conteudo da lista principal: evita duplicacao. */
    private void replaceServer(SAMPServerInfo info) {
        mServersList.clear();
        mServersList.add(info);
        refreshServerListUi();
    }

    private void startStatusQuery(final SAMPServerInfo info, final int gen) {
        final String ip = info.getAddress();
        final int port = info.getPort();
        try {
            mQueryExecutor.execute(new Runnable() {
                @Override
                public void run() {
                    final QueryResult result = queryServer(ip, port);
                    mMain.post(new Runnable() {
                        @Override
                        public void run() {
                            if (mDestroyed || gen != mLoadGeneration) return;
                            if (result == null) {
                                info.setServerStatus(SAMPServerInfo.Status.OFFLINE);
                                info.setCurrentPlayerCount(-1);
                                info.setPing(-1);
                            } else {
                                info.setServerStatus(SAMPServerInfo.Status.ONLINE);
                                info.setPing(result.pingMs);
                                info.setCurrentPlayerCount(result.players);
                                if (result.maxPlayers >= 0) info.setMaxPlayerCount(result.maxPlayers);
                                if (result.mode != null && !result.mode.isEmpty()) info.setServerMode(result.mode);
                                if (result.language != null) info.setLanguage(result.language);
                                if (result.hasPassword != null) info.setHasPassword(result.hasPassword);
                            }
                            refreshServerListUi();
                        }
                    });
                }
            });
        } catch (RejectedExecutionException ignored) {
            // Activity ja esta sendo destruida.
        }
    }

    private static final class QueryResult {
        int pingMs = -1;
        int players = -1;
        int maxPlayers = -1;
        String mode;
        String language;
        Boolean hasPassword;
    }

    /** Roda em thread de fundo. Retorna null se o servidor nao respondeu a consulta valida. */
    private static QueryResult queryServer(String ip, int port) {
        for (int attempt = 0; attempt < QUERY_ATTEMPTS; attempt++) {
            if (Thread.currentThread().isInterrupted()) return null;
            SampQueryAPI q = null;
            try {
                q = new SampQueryAPI(ip, port);
                if (!q.f7282f) return null; // DNS/socket invalido: nao adianta repetir
                if (!q.mo7166d()) continue;  // sem resposta valida ao ping
                QueryResult r = new QueryResult();
                r.pingMs = q.lastPingMs;
                String[] info = q.mo7164b();
                if (info != null) {
                    try {
                        r.players = Integer.parseInt(info[1]);
                        r.maxPlayers = Integer.parseInt(info[2]);
                        r.hasPassword = !"0".equals(info[0]);
                        r.mode = info[4];
                        r.language = info[5];
                    } catch (NumberFormatException e) {
                        r.players = -1;
                        r.maxPlayers = -1;
                    }
                }
                return r;
            } catch (Exception e) {
                Log.w(TAG, "SA-MP query failed: " + e);
            } finally {
                if (q != null && q.f7277a != null) q.f7277a.close();
            }
        }
        return null;
    }

    /** Atualiza o RecyclerView da aba Hosted sem esperar nada: se a tela ainda nao existe, o
     *  adapter le a lista atual quando for criado. Sempre na thread principal. */
    private void refreshServerListUi() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mMain.post(new Runnable() { @Override public void run() { refreshServerListUi(); } });
            return;
        }
        notifyServerPage(1);
        updateServerStateUi();
    }

    private void notifyServerPage(int page) {
        if (mDestroyed) return;
        for (Fragment fragment : getSupportFragmentManager().getFragments()) {
            if (!(fragment instanceof ServersFragment) || !fragment.isAdded() || fragment.getView() == null) continue;
            for (Fragment child : fragment.getChildFragmentManager().getFragments()) {
                if (child instanceof ServerPagesItemFragment
                        && ((ServerPagesItemFragment) child).getPage() == page
                        && child.isAdded() && child.getView() != null) {
                    RecyclerView rv = child.getView().findViewById(R.id.server_recycler);
                    if (rv != null && rv.getAdapter() != null) {
                        rv.getAdapter().notifyDataSetChanged();
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // Favoritos (lista separada; nunca alimenta mServersList)
    // ------------------------------------------------------------------------------------

    public void getFavoriteServersInfo()
    {
        for(int i = 0; i< FavoritesInfo.getServerList(this).size(); i++)
        {
            SAMPServerInfo serverInfo = new SAMPServerInfo(i, i, "Loading...", FavoritesInfo.getServerList(this).get(i).ip, FavoritesInfo.getServerList(this).get(i).port, 0, 0, 0, 0, 0, "English");
            serverInfo.setFavorite(true);
            serverInfo.setQueried(false);
            getFavoriteServerList().add(serverInfo);
            new ProcessInfo().execute(i);
        }

        refreshFavoriteServers();
    }

    /** Consulta em segundo plano; aplica o resultado na thread principal (onPostExecute). */
    public class ProcessInfo extends AsyncTask<Integer, Void, SAMPServerInfo> {
        private int index;

        public SAMPServerInfo doInBackground(Integer... numArr) {
            index = numArr[0].intValue();
            final ArrayList<SAMPServerInfo> list = getFavoriteServerList();
            final String address;
            final int port;
            try {
                SAMPServerInfo cur = list.get(index);
                if (cur.getQueried()) return null;
                address = cur.getAddress();
                port = cur.getPort();
            } catch (IndexOutOfBoundsException e) {
                return null;
            }
            SampQueryAPI sampQuery = null;
            try {
                sampQuery = new SampQueryAPI(address, port);
                if (!sampQuery.mo7166d()) {
                    return null;
                }
                String[] info = sampQuery.mo7164b();
                if (info == null) return null;
                return new SAMPServerInfo(index, index, info[3], address, port, Integer.parseInt(info[1]), Integer.parseInt(info[2]), Integer.parseInt(info[0]), 0, 0, info[5]);
            } catch (Exception e) {
                Log.w(TAG, "favorite query failed: " + e);
                return null;
            } finally {
                if (sampQuery != null) {
                    DatagramSocket datagramSocket = sampQuery.f7277a;
                    if (datagramSocket != null) {
                        datagramSocket.close();
                    }
                }
            }
        }

        public void onPostExecute(SAMPServerInfo result) {
            super.onPostExecute(result);
            if (result == null || mDestroyed) return;
            ArrayList<SAMPServerInfo> list = getFavoriteServerList();
            if (index < list.size()
                    && list.get(index).getAddress().equals(result.getAddress())
                    && list.get(index).getPort() == result.getPort()) {
                result.setQueried(true);
                result.setFavorite(true);
                list.set(index, result);
                refreshFavoriteServers();
            }
        }
    }

    public void refreshFavoriteServers()
    {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mMain.post(new Runnable() { @Override public void run() { refreshFavoriteServers(); } });
            return;
        }
        notifyServerPage(0);
    }

    // ------------------------------------------------------------------------------------
    // Interface do launcher (menu lateral, cabecalho, rodape)
    // ------------------------------------------------------------------------------------

    /** Modo imersivo + recuo lateral para notch/recorte da tela (landscape). */
    private void setupImmersiveAndInsets() {
        hideSystemUi();
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        final View safe = findViewById(R.id.mx_safe_area);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main_layout), new OnApplyWindowInsetsListener() {
            @Override
            public WindowInsetsCompat onApplyWindowInsets(View v, WindowInsetsCompat insets) {
                int left = insets.getSystemWindowInsetLeft();
                int right = insets.getSystemWindowInsetRight();
                DisplayCutoutCompat cutout = insets.getDisplayCutout();
                if (cutout != null) {
                    left = Math.max(left, cutout.getSafeInsetLeft());
                    right = Math.max(right, cutout.getSafeInsetRight());
                }
                safe.setPadding(left, 0, right, 0);
                return insets;
            }
        });
    }

    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    private void setupMenu() {
        mTvTitle = findViewById(R.id.tv_page_title);
        mBtnRefresh = findViewById(R.id.btn_refresh);
        // Ordem = indice da pagina: Inicio, Servidores, Configuracoes, Noticias.
        mMenuItems = new View[4];
        mMenuItems[PAGE_HOME] = findViewById(R.id.menu_home);
        mMenuItems[PAGE_SERVERS] = findViewById(R.id.menu_servers);
        mMenuItems[PAGE_SETTINGS] = findViewById(R.id.menu_settings);
        mMenuItems[PAGE_NEWS] = findViewById(R.id.menu_news);

        for (int i = 0; i < mMenuItems.length; i++) {
            final int page = i;
            mMenuItems[i].setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { showPage(page); }
            });
        }

        // Comunidade: abre o Discord configurado (ou o site, se so ele estiver configurado).
        findViewById(R.id.menu_community).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                String url = SocialLinks.discord(MainActivity.this);
                if (url.isEmpty()) url = SocialLinks.site(MainActivity.this);
                SocialLinks.open(MainActivity.this, url);
            }
        });

        findViewById(R.id.menu_exit).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { finishAffinity(); }
        });

        mBtnRefresh.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { refreshAll(); }
        });
    }

    /** Mostra uma pagina do ViewPager e atualiza menu, titulo e botao Atualizar. */
    public void showPage(int page) {
        if (mPager == null || page < 0 || page >= mMenuItems.length) return;
        mCurrentPage = page;
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        hideKeyboard(this);
        mPager.setCurrentItem(page, false);
        for (int i = 0; i < mMenuItems.length; i++) mMenuItems[i].setSelected(i == page);
        int title;
        switch (page) {
            case PAGE_HOME:     title = R.string.mx_title_home; break;
            case PAGE_SETTINGS: title = R.string.mx_title_settings; break;
            case PAGE_NEWS:     title = R.string.mx_title_news; break;
            default:            title = R.string.mx_title_servers; break;
        }
        mTvTitle.setText(title);
        mBtnRefresh.setVisibility(page == PAGE_SERVERS ? View.VISIBLE : View.GONE);
    }

    private void setupFooter() {
        mTvServerState = findViewById(R.id.tv_server_state);
        mDotServerState = findViewById(R.id.dot_server_state);
        mTvMusic = findViewById(R.id.tv_music_status);
        mBtnMusic = findViewById(R.id.btn_music_toggle);

        // Versao do cliente (versionName do APK instalado).
        String version = "?";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) { }
        ((TextView) findViewById(R.id.tv_client_version)).setText(getString(R.string.mx_footer_version, version));

        // Links sociais: so aparecem os que estao configurados em social_links.xml.
        bindSocial(R.id.btn_social_discord, SocialLinks.discord(this));
        bindSocial(R.id.btn_social_youtube, SocialLinks.youtube(this));
        bindSocial(R.id.btn_social_instagram, SocialLinks.instagram(this));
        bindSocial(R.id.btn_social_site, SocialLinks.site(this));

        // Musica local (res/raw/launcher_music.*). Sem a faixa, os controles ficam desabilitados.
        mMusic = new MusicController(this);
        SeekBar seek = findViewById(R.id.seek_music_volume);
        seek.setProgress(mMusic.getVolumePercent());
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (fromUser) mMusic.setVolumePercent(progress);
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        mBtnMusic.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { mMusic.toggle(); }
        });
        mMusic.setListener(new MusicController.Listener() {
            @Override public void onMusicStateChanged() { updateMusicUi(); }
        });
        boolean has = mMusic.hasTrack();
        seek.setEnabled(has);
        mBtnMusic.setEnabled(has);
        seek.setAlpha(has ? 1f : 0.4f);
        mBtnMusic.setAlpha(has ? 1f : 0.4f);
        updateMusicUi();
    }

    private void bindSocial(int viewId, final String url) {
        View v = findViewById(viewId);
        if (url == null || url.isEmpty()) {
            v.setVisibility(View.GONE);
            return;
        }
        v.setVisibility(View.VISIBLE);
        v.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { SocialLinks.open(MainActivity.this, url); }
        });
    }

    private void updateMusicUi() {
        if (mMusic == null) return;
        if (!mMusic.hasTrack()) {
            mTvMusic.setText(R.string.mx_music_none);
            mBtnMusic.setImageResource(R.drawable.mx_ic_play);
            return;
        }
        boolean playing = mMusic.isPlaying();
        String name = getString(R.string.mx_music_track_name);
        mTvMusic.setText(getString(playing ? R.string.mx_music_playing : R.string.mx_music_paused, name));
        mBtnMusic.setImageResource(playing ? R.drawable.mx_ic_pause : R.drawable.mx_ic_play);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mMusic != null) mMusic.onHostPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mMusic != null) mMusic.onHostResume();
        updateMusicUi();
    }

    /** True enquanto uma consulta esta em andamento (evita disparar varias ao mesmo tempo). */
    private boolean isRefreshLocked() {
        if (mServersList.isEmpty()) return false;
        return mServersList.get(0).getServerStatus() == SAMPServerInfo.Status.LOADING
                && SystemClock.elapsedRealtime() - mLastAttemptMs < REFRESH_LOCK_MAX_MS;
    }

    /**
     * Botao Atualizar: baixa o servers.json de novo e refaz a consulta UDP real. A lista principal
     * e SUBSTITUIDA (replaceServer) e a de favoritos e limpa antes de recarregar: nunca duplica.
     */
    public void refreshAll() {
        if (mDestroyed || isRefreshLocked()) return;
        getServersInfo();
        reloadFavoriteServers();
        updateServerStateUi();
    }

    /** Recarrega os favoritos limpando a lista no proprio lugar (os adapters guardam a referencia). */
    public void reloadFavoriteServers() {
        if (mDestroyed) return;
        mFavoriteServersList.clear();
        getFavoriteServersInfo();
    }

    /** Rodape (estado do servidor) e botao Atualizar, sempre refletindo o ultimo resultado REAL. */
    private void updateServerStateUi() {
        if (mDestroyed || mTvServerState == null) return;
        SAMPServerInfo.Status st = mServersList.isEmpty()
                ? SAMPServerInfo.Status.LOADING : mServersList.get(0).getServerStatus();
        int text, color;
        switch (st) {
            case ONLINE:  text = R.string.mx_status_online;  color = R.color.mx_online;  break;
            case OFFLINE: text = R.string.mx_status_offline; color = R.color.mx_offline; break;
            case ERROR:   text = R.string.mx_status_error;   color = R.color.mx_error;   break;
            default:      text = R.string.mx_status_checking; color = R.color.mx_loading; break;
        }
        mTvServerState.setText(text);
        mDotServerState.getBackground().mutate().setColorFilter(getResources().getColor(color), PorterDuff.Mode.SRC_IN);

        boolean locked = isRefreshLocked();
        mBtnRefresh.setEnabled(!locked);
        mBtnRefresh.setText(locked ? R.string.mx_refreshing : R.string.mx_refresh);
    }

    public static void hideKeyboard(Activity activity) {
        InputMethodManager inputManager = (InputMethodManager) activity
                .getSystemService(Context.INPUT_METHOD_SERVICE);

        // check if no view has focus:
        View currentFocusedView = activity.getCurrentFocus();
        if (currentFocusedView != null) {
            inputManager.hideSoftInputFromWindow(currentFocusedView.getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
        }
    }

    public class ViewPagerAdapter extends FragmentPagerAdapter {

        public ViewPagerAdapter(FragmentManager fragmentManager) {
            super(fragmentManager);
        }

        @NonNull
        @Override
        public Fragment getItem(int position) {
            if(position == 1)
                return new ServersFragment();
            else if(position == 0)
                return new HomeFragment();
            else if(position == 2)
                return new SettingsFragment();
            else if(position == 3)
                return new NewsFragment();
            return new HomeFragment();
        }

        @Override
        public int getCount() {
            return 4;
        }
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
    }
}
