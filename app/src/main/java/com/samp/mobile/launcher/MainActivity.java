package com.samp.mobile.launcher;

import android.Manifest;
import android.app.Activity;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.os.Build;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.ImageButton;
import androidx.appcompat.app.AlertDialog;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.widget.ImageViewCompat;
import com.samp.mobile.launcher.adapters.ServerInformationFragment;
import com.samp.mobile.launcher.fragments.CommunityFragment;
import com.samp.mobile.launcher.fragments.NewsFragment;
import com.samp.mobile.launcher.util.LauncherUi;
import com.samp.mobile.launcher.util.MaximoMusic;
import com.samp.mobile.launcher.util.NotificationHelper;
import com.samp.mobile.launcher.util.SocialLinks;
import java.util.concurrent.CopyOnWriteArrayList;
import android.content.Context;
import android.os.AsyncTask;
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
import android.widget.ImageView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
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
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.data.FavoritesInfo;
import com.samp.mobile.launcher.data.OfficialFavorites;
import com.samp.mobile.launcher.fragments.HomeFragment;
import com.samp.mobile.launcher.fragments.ServerPagesItemFragment;
import com.samp.mobile.launcher.fragments.ServersFragment;
import com.samp.mobile.launcher.fragments.RepairFragment;
import com.samp.mobile.launcher.fragments.SettingsFragment;
import com.samp.mobile.launcher.util.ConfigValidator;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.SampQueryAPI;
import com.samp.mobile.launcher.util.ServerConfigParser;
import com.samp.mobile.launcher.util.SignatureChecker;
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

    public static final int PAGE_HOME = 0;
    public static final int PAGE_SERVERS = 1;
    public static final int PAGE_NEWS = 2;
    public static final int PAGE_SETTINGS = 3;
    public static final int PAGE_COMMUNITY = 4;
    public static final int PAGE_REPAIR = 5;
    private static final int REQ_NOTIFICATIONS = 4101;

    private static final int[] MENU_ICONS = { R.drawable.mx_ic_home, R.drawable.mx_ic_servers, R.drawable.mx_ic_news,
            R.drawable.mx_ic_settings, R.drawable.mx_ic_community, R.drawable.mx_ic_repair, R.drawable.mx_ic_exit };
    private static final int[] MENU_TEXTS = { R.string.mx_menu_home, R.string.mx_menu_servers, R.string.mx_menu_news,
            R.string.mx_menu_settings, R.string.mx_menu_community, R.string.mx_menu_repair, R.string.mx_menu_exit };
    private static final int MENU_EXIT = 6;

    /** Notificado quando a lista, a selecao ou o estado de algum servidor muda. */
    public interface ServerStateListener { void onServerStateChanged(); }

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
    private final View[] mMenuViews = new View[MENU_TEXTS.length];
    private final CopyOnWriteArrayList<ServerStateListener> mStateListeners = new CopyOnWriteArrayList<>();
    private String mSelectedKey;
    private boolean mUiStarted = false;

    private final MaximoMusic.Listener mMusicListener = new MaximoMusic.Listener() {
        @Override
        public void onMusicStateChanged(boolean playing, int volumePercent) { updatePlayerUi(playing, volumePercent); }
    };

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LauncherUi.prepareWindow(this);
        setContentView(R.layout.activity_main);
        LauncherUi.applyImmersive(this);
        LauncherUi.applySafePadding(findViewById(R.id.mx_safe_area));

        Config.currentContext = this;

        mServersList = new ArrayList<>();
        mFavoriteServersList = new ArrayList<>();

        ConfigValidator.validateConfigFiles(this);
        com.samp.mobile.launcher.util.KeyboardGuard.check(this); // garante settings.ini e androidkeyboard=1 desde a abertura do launcher

        if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        {
            Toast.makeText(this, "Use original launcher! No remake", Toast.LENGTH_LONG).show();
            return;
        }

        // O jogo terminou sem passar pela saida normal (queda do app, morte do processo): avisa e limpa o marcador.
        try {
            SharedPreferences sp = getSharedPreferences("com.samp.mobile", MODE_PRIVATE);
            if (sp.getBoolean(com.samp.mobile.game.SAMP.PREF_GAME_RUNNING, false)) {
                sp.edit().putBoolean(com.samp.mobile.game.SAMP.PREF_GAME_RUNNING, false).commit();
                Toast.makeText(this, "O jogo foi encerrado inesperadamente. Se repetir, abra Solucoes de problemas (engrenagem).", Toast.LENGTH_LONG).show();
            }
        } catch (RuntimeException ignored) { }

        mQueue = Volley.newRequestQueue(getApplicationContext());
        mQueryExecutor = Executors.newSingleThreadExecutor();

        File file = new File(getExternalFilesDir(null) + "/launcher.apk");
        if (file.exists()) {
            file.delete();
        }

        NotificationHelper.ensureChannels(this);

        mPager = findViewById(R.id.fragment_place);
        mPager.setOffscreenPageLimit(1);
        mPager.setAdapter(new ViewPagerAdapter(getSupportFragmentManager()));
        buildSidebar();
        View helpBtn = findViewById(R.id.mx_btn_help);   // engrenagem "SOLUCOES DE PROBLEMAS" no topo
        if (helpBtn != null) helpBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { openPage(PAGE_REPAIR); }
        });
        bindFooter();
        openPage(PAGE_SERVERS);

        getServersInfo();
        getFavoriteServersInfo();
        requestNotificationPermissionOnce();
    }

    @Override
    protected void onStart() {
        super.onStart();
        MaximoMusic music = MaximoMusic.get(this);
        music.addListener(mMusicListener);
        music.attach();
        mUiStarted = true;
        updateFooterStatus();
    }

    @Override
    protected void onStop() {
        MaximoMusic music = MaximoMusic.get(this);
        music.removeListener(mMusicListener);
        music.detach();
        mUiStarted = false;
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) LauncherUi.applyImmersive(this);
    }

    @Override
    protected void onDestroy() {
        mDestroyed = true;
        mMain.removeCallbacksAndMessages(null);
        if (mQueue != null) {
            mQueue.cancelAll(REQUEST_TAG);
            mQueue.stop();
        }
        if (mQueryExecutor != null) {
            mQueryExecutor.shutdownNow();
        }
        super.onDestroy();
    }

    // ------------------------------------------------------------------------------------
    // Menu lateral, navegacao e rodape
    // ------------------------------------------------------------------------------------

    private void buildSidebar() {
        LinearLayout sidebar = findViewById(R.id.mx_sidebar);
        LayoutInflater inflater = LayoutInflater.from(this);
        for (int i = 0; i < MENU_TEXTS.length; i++) {
            final int index = i;
            View item = inflater.inflate(R.layout.mx_menu_item, sidebar, false);
            ((ImageView) item.findViewById(R.id.mx_menu_icon)).setImageResource(MENU_ICONS[i]);
            ((TextView) item.findViewById(R.id.mx_menu_text)).setText(MENU_TEXTS[i]);
            item.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (index == MENU_EXIT) confirmExit(); else openPage(index);
                }
            });
            sidebar.addView(item);
            mMenuViews[i] = item;
        }
    }

    /** Abre uma das paginas (PAGE_*), reutilizando os Fragments existentes. */
    public void openPage(int page) {
        if (mPager == null) return;
        hideKeyboard(this);
        mPager.setCurrentItem(page, false);
        for (int i = 0; i < mMenuViews.length; i++) {
            if (mMenuViews[i] != null) mMenuViews[i].setSelected(i == page);
        }
    }

    private void confirmExit() {
        if (isFinishing()) return;
        new AlertDialog.Builder(this)
                .setTitle(R.string.mx_exit_title)
                .setMessage(R.string.mx_exit_msg)
                .setNegativeButton(R.string.mx_cancel, null)
                .setPositiveButton(R.string.mx_exit_yes, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int w) {
                        MaximoMusic.get(MainActivity.this).release();
                        finishAffinity();
                    }
                }).show();
    }

    private void bindFooter() {
        final MaximoMusic music = MaximoMusic.get(this);
        findViewById(R.id.mx_btn_play).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                music.toggle();
                if (music.hasFailed()) Toast.makeText(MainActivity.this, R.string.mx_music_error, Toast.LENGTH_SHORT).show();
            }
        });
        findViewById(R.id.mx_btn_prev).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { music.previous(); }
        });
        findViewById(R.id.mx_btn_next).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { music.next(); }
        });
        findViewById(R.id.mx_btn_volume).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                // mudo/volume anterior
                music.setVolumePercent(music.getVolumePercent() == 0 ? 55 : 0);
            }
        });
        SeekBar seek = findViewById(R.id.mx_volume_seek);
        seek.setProgress(music.getVolumePercent());
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (fromUser) music.setVolumePercent(progress);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { }
            @Override public void onStopTrackingTouch(SeekBar sb) { }
        });

        // versao real do cliente
        updateFooterStatus();

        // redes sociais: somente as configuradas
        LinearLayout social = findViewById(R.id.mx_social_container);
        java.util.List<SocialLinks.Link> links = SocialLinks.configured(this);
        findViewById(R.id.mx_footer_social).setVisibility(links.isEmpty() ? View.GONE : View.VISIBLE);
        findViewById(R.id.mx_footer_social_divider).setVisibility(links.isEmpty() ? View.GONE : View.VISIBLE);
        int size = (int) (22 * getResources().getDisplayMetrics().density);
        int gap = (int) (8 * getResources().getDisplayMetrics().density);
        for (final SocialLinks.Link link : links) {
            ImageButton b = new ImageButton(this);
            b.setImageResource(link.iconRes);
            b.setBackground(null);
            b.setContentDescription(link.name);
            b.setScaleType(ImageView.ScaleType.FIT_CENTER);
            ImageViewCompat.setImageTintList(b, ColorStateList.valueOf(0xFFFFFFFF));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(size, size);
            lp.setMarginEnd(gap);
            b.setLayoutParams(lp);
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { SocialLinks.open(MainActivity.this, link.url); }
            });
            social.addView(b);
        }
        updatePlayerUi(music.isPlaying(), music.getVolumePercent());
    }

    private void updatePlayerUi(boolean playing, int volumePercent) {
        ImageButton play = findViewById(R.id.mx_btn_play);
        ImageButton vol = findViewById(R.id.mx_btn_volume);
        SeekBar seek = findViewById(R.id.mx_volume_seek);
        if (play == null || vol == null || seek == null) return;
        play.setImageResource(playing ? R.drawable.mx_ic_pause : R.drawable.mx_ic_play);
        vol.setImageResource(volumePercent == 0 ? R.drawable.mx_ic_volume_off : R.drawable.mx_ic_volume);
        if (seek.getProgress() != volumePercent) seek.setProgress(volumePercent);
    }

    /** Rodape: versao do cliente e estado REAL do servidor oficial. */
    private void updateFooterStatus() {
        TextView client = findViewById(R.id.mx_footer_client_value);
        TextView server = findViewById(R.id.mx_footer_server_value);
        ImageView icon = findViewById(R.id.mx_footer_server_icon);
        if (client == null || server == null || icon == null) return;
        String version = "";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) { }
        client.setText(getString(Config.clientUpToDate ? R.string.mx_footer_client_updated : R.string.mx_footer_client_unknown, version));

        int text; int color;
        SAMPServerInfo.Status st = mServersList.isEmpty() ? SAMPServerInfo.Status.LOADING : mServersList.get(0).getServerStatus();
        switch (st) {
            case ONLINE:  text = R.string.mx_status_online;   color = R.color.mx_online;   break;
            case OFFLINE: text = R.string.mx_status_offline;  color = R.color.mx_offline;  break;
            case ERROR:   text = R.string.mx_status_error;    color = R.color.mx_warning;  break;
            default:      text = R.string.mx_status_checking; color = R.color.mx_checking; break;
        }
        int c = ContextCompat.getColor(this, color);
        server.setText(text);
        server.setTextColor(c);
        ImageViewCompat.setImageTintList(icon, ColorStateList.valueOf(c));
    }

    private void requestNotificationPermissionOnce() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return;
        android.content.SharedPreferences p = getSharedPreferences("mx_launcher", MODE_PRIVATE);
        if (p.getBoolean("notif_asked", false)) return;
        p.edit().putBoolean("notif_asked", true).apply();
        ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFICATIONS);
    }

    // ------------------------------------------------------------------------------------
    // Selecao de servidor e botao CONECTAR
    // ------------------------------------------------------------------------------------

    public void addServerStateListener(ServerStateListener l) { if (l != null && !mStateListeners.contains(l)) mStateListeners.add(l); }
    public void removeServerStateListener(ServerStateListener l) { mStateListeners.remove(l); }

    private void fireServerStateChanged() {
        updateFooterStatus();
        for (ServerStateListener l : mStateListeners) l.onServerStateChanged();
    }

    /** Linha de estado (carregando sem servidor / erro): nao e um servidor real. */
    public static boolean isPlaceholder(SAMPServerInfo i) {
        return i == null || i.getServerStatus() == SAMPServerInfo.Status.ERROR || i.getPort() == 0;
    }

    private static String keyOf(SAMPServerInfo i) { return i.getAddress() + ":" + i.getPort(); }

    public boolean isSelected(SAMPServerInfo i) {
        return i != null && mSelectedKey != null && !isPlaceholder(i) && mSelectedKey.equals(keyOf(i));
    }

    public void selectServer(SAMPServerInfo i) {
        if (isPlaceholder(i)) return;
        mSelectedKey = keyOf(i);
        notifyServerPage(ServerPagesItemFragment.PAGE_LEGACY_FAVORITES);
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL);
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL_FAVORITES);
        fireServerStateChanged();
    }

    public SAMPServerInfo getSelectedServer() {
        if (mSelectedKey == null) return null;
        for (SAMPServerInfo i : mServersList) if (!isPlaceholder(i) && mSelectedKey.equals(keyOf(i))) return i;
        for (SAMPServerInfo i : mFavoriteServersList) if (!isPlaceholder(i) && mSelectedKey.equals(keyOf(i))) return i;
        return null;
    }

    /** Botao ATUALIZAR: nova consulta real do servidor oficial e dos favoritos, sem duplicar registros. */
    public void refreshAll() {
        getServersInfo();
        reloadFavoriteServers();
    }

    /** Botao CONECTAR: mesmo fluxo do dialogo original (settings.ini + checagem de arquivos + SAMP). */
    public void connectSelected() {
        final SAMPServerInfo s = getSelectedServer();
        if (s == null) {
            Toast.makeText(this, R.string.mx_connect_select_first, Toast.LENGTH_SHORT).show();
            return;
        }
        if (s.getServerStatus() == SAMPServerInfo.Status.LOADING) {
            Toast.makeText(this, R.string.mx_connect_wait, Toast.LENGTH_SHORT).show();
            return;
        }
        if (s.getServerStatus() == SAMPServerInfo.Status.OFFLINE) {
            // Sem "conectar mesmo assim": servidor que nao respondeu nao deixa entrar.
            Toast.makeText(this, R.string.mx_connect_offline_toast, Toast.LENGTH_LONG).show();
            return;
        }
        ServerInformationFragment.launchGame(this, s);
    }


    // ------------------------------------------------------------------------------------
    // Favoritos dos servidores OFICIAIS (independentes dos favoritos antigos)
    // ------------------------------------------------------------------------------------

    /** true se este servidor oficial (host+porta normalizados) esta marcado como favorito. */
    public boolean isOfficialFavorite(SAMPServerInfo i) {
        return i != null && !isPlaceholder(i) && OfficialFavorites.isFavorite(this, i.getAddress(), i.getPort());
    }

    /** Marca/desmarca o servidor oficial e atualiza as duas listas. Placeholders nao entram. */
    public void toggleOfficialFavorite(SAMPServerInfo i) {
        if (i == null || isPlaceholder(i)) return;
        boolean now = OfficialFavorites.toggle(this, i.getAddress(), i.getPort());
        Toast.makeText(this, now ? R.string.mx_fav_added : R.string.mx_fav_removed, Toast.LENGTH_SHORT).show();
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL);
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL_FAVORITES);
        fireServerStateChanged();
    }

    /**
     * Servidores da lista oficial ATUAL que estao marcados. E um filtro da propria lista oficial
     * (mesmas instancias/endereco/porta reais): nada e inventado e nada e removido do armazenamento
     * quando um servidor some da lista ou fica offline.
     */
    public ArrayList<SAMPServerInfo> getOfficialFavoriteList() {
        ArrayList<SAMPServerInfo> out = new ArrayList<>();
        for (SAMPServerInfo i : mServersList) {
            if (isOfficialFavorite(i)) out.add(i);
        }
        return out;
    }

    /** Quantidade de favoritos salvos (inclui os que nao estao na lista oficial agora). */
    public int getSavedOfficialFavoriteCount() {
        return OfficialFavorites.list(this).size();
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

        setPlaceholder(SAMPServerInfo.Status.LOADING, "Loading server...", "");

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
                    setPlaceholder(SAMPServerInfo.Status.ERROR, e.getMessage(), "Tap to retry");
                }
            }
        }, new Response.ErrorListener() {
            @Override
            public void onErrorResponse(VolleyError error) {
                if (mDestroyed || gen != mLoadGeneration) return;
                Log.w(TAG, "servers.json request failed: " + error);
                setPlaceholder(SAMPServerInfo.Status.ERROR, "Could not load server list (" + describe(error) + ")", "Tap to retry");
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
        if (mSelectedKey == null && !isPlaceholder(info)) mSelectedKey = keyOf(info); // pre-seleciona o oficial
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
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL);
        notifyServerPage(ServerPagesItemFragment.PAGE_OFFICIAL_FAVORITES);
        fireServerStateChanged();
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
        // Categoria antiga desativada na interface: nao consulta (nem carrega) os servidores manuais.
        // Os dados em SAMP/favorites.json continuam intactos e voltam a funcionar com a flag ligada.
        if (!Config.SHOW_LEGACY_FAVORITES) return;
        for(int i = 0; i< FavoritesInfo.getServerList(this).size(); i++)
        {
            SAMPServerInfo serverInfo = new SAMPServerInfo(i, i, "Loading...", FavoritesInfo.getServerList(this).get(i).ip, FavoritesInfo.getServerList(this).get(i).port, 0, 0, 0, 0, 0, "English");
            serverInfo.setFavorite(true);
            serverInfo.setQueried(false);
            serverInfo.setServerStatus(SAMPServerInfo.Status.LOADING);
            getFavoriteServerList().add(serverInfo);
            new ProcessInfo().execute(i);
        }

        refreshFavoriteServers();
    }

    /** Recarrega os favoritos do zero (limpa a mesma lista) para nao duplicar registros. */
    public void reloadFavoriteServers() {
        mFavoriteServersList.clear();
        getFavoriteServersInfo();
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
                SAMPServerInfo r = new SAMPServerInfo(index, index, info[3], address, port, Integer.parseInt(info[1]), Integer.parseInt(info[2]), Integer.parseInt(info[0]), 1, 0, info[5]);
                r.setServerMode(info[4]);
                r.setPing(-1); // ping real desconhecido nos favoritos
                return r;
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
            if (mDestroyed) return;
            ArrayList<SAMPServerInfo> list = getFavoriteServerList();
            if (result == null) {
                // sem resposta valida: Offline (so se ainda estava "verificando")
                if (index < list.size() && !list.get(index).getQueried()
                        && list.get(index).getServerStatus() == SAMPServerInfo.Status.LOADING) {
                    list.get(index).setServerStatus(SAMPServerInfo.Status.OFFLINE);
                    refreshFavoriteServers();
                }
                return;
            }
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
        notifyServerPage(ServerPagesItemFragment.PAGE_LEGACY_FAVORITES);
        fireServerStateChanged();
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
            switch (position) {
                case PAGE_SERVERS: return new ServersFragment();
                case PAGE_NEWS: return new NewsFragment();
                case PAGE_SETTINGS: return new SettingsFragment();
                case PAGE_COMMUNITY: return new CommunityFragment();
                case PAGE_REPAIR: return new RepairFragment();
                default: return new HomeFragment();
            }
        }

        @Override
        public int getCount() {
            return 6;
        }
    }

    @Override
    public void onBackPressed() {
        if (mPager != null && mPager.getCurrentItem() != PAGE_HOME) {
            openPage(PAGE_HOME);
        } else {
            confirmExit();
        }
    }
}
