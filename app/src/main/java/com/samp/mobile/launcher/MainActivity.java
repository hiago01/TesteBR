package com.samp.mobile.launcher;

import android.app.Activity;
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
import com.google.android.material.tabs.TabLayout;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.data.FavoritesInfo;
import com.samp.mobile.launcher.fragments.HomeFragment;
import com.samp.mobile.launcher.fragments.ServerPagesItemFragment;
import com.samp.mobile.launcher.fragments.ServersFragment;
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
        ViewPagerWithoutSwipe pa = findViewById(R.id.fragment_place);
        pa.setAdapter(sa);

        TabLayout tabLayout = findViewById(R.id.constraintLayout);

        tabLayout.setupWithViewPager(pa);

        for(int  i = 0; i < tabLayout.getTabCount(); i++)
        {
            View inflate = LayoutInflater.from(this).inflate(R.layout.tablayout_item, (ViewGroup) tabLayout, false);

            ImageView image = inflate.findViewById(R.id.imageView2);
            image.setBackgroundResource(tabImages[i]);

            Objects.requireNonNull(tabLayout.getTabAt(i)).setCustomView(inflate);

            tabLayout.clearOnTabSelectedListeners();
            tabLayout.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
                @Override
                public void onTabSelected(TabLayout.Tab tab) {
                    getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
                    pa.setCurrentItem(tab.getPosition(), true);
                    ((ImageView)tab.getCustomView().findViewById(R.id.imageView2)).setBackgroundResource(tabSelectedImages[tab.getPosition()]);
                }

                @Override
                public void onTabUnselected(TabLayout.Tab tab) {
                    ((ImageView)tab.getCustomView().findViewById(R.id.imageView2)).setBackgroundResource(tabImages[tab.getPosition()]);
                }

                @Override
                public void onTabReselected(TabLayout.Tab tab) {

                }
            });

        }
        ((ImageView)tabLayout.getTabAt(0).getCustomView().findViewById(R.id.imageView2)).setBackgroundResource(tabSelectedImages[0]);

        getServersInfo();
        getFavoriteServersInfo();
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
            return new HomeFragment();
        }

        @Override
        public int getCount() {
            return 3;
        }
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
    }
}
