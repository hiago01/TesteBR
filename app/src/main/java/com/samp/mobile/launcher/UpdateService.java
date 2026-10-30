package com.samp.mobile.launcher;

import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.os.StatFs;
import android.util.Log;

import com.android.volley.DefaultRetryPolicy;
import com.android.volley.RequestQueue;
import com.android.volley.Response;
import com.android.volley.VolleyError;
import com.android.volley.toolbox.StringRequest;
import com.android.volley.toolbox.Volley;
import com.downloader.Error;
import com.downloader.OnDownloadListener;
import com.downloader.OnProgressListener;
import com.downloader.PRDownloader;
import com.downloader.PRDownloaderConfig;
import com.downloader.Progress;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.data.FilesData;
import com.samp.mobile.launcher.pack.AtomicInstaller;
import com.samp.mobile.launcher.pack.CancelToken;
import com.samp.mobile.launcher.pack.GameFilePolicy;
import com.samp.mobile.launcher.pack.InstalledState;
import com.samp.mobile.launcher.pack.PackConfig;
import com.samp.mobile.launcher.pack.PackException;
import com.samp.mobile.launcher.pack.PackInstaller;
import com.samp.mobile.launcher.pack.PackSpec;
import com.samp.mobile.launcher.pack.SampList;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

/**
 * Servico de atualizacao. Contrato de mensagens com Splash/UpdateActivity preservado:
 *  0/7 iniciar verificacao (gputype) | 1 verificar/baixar dados do jogo | 2 atualizar APK |
 *  4 status do update | 5 status do jogo | 6 (NOVO) erro com texto, para a Activity oferecer "tentar de novo".
 *
 * Threading: TODO o estado e mutado na HandlerThread propria do servico (mInHandler). Callbacks do
 * Volley / PRDownloader (thread principal) apenas repassam para ela com post(). Nada aqui bloqueia
 * a thread principal nem faz espera ativa; cada etapa de rede tem timeout e numero maximo de tentativas.
 * mSession invalida callbacks atrasados quando uma nova verificacao/cancelamento comeca.
 */
@Obfuscate
public class UpdateService extends Service {

    private static final String TAG = "UpdateService";
    private static final String REQUEST_TAG = "update_config";
    private static final int MAX_FILE_ATTEMPTS = 3;
    private static final int MAX_GAME_ATTEMPTS = 3;
    /** Falhas seguidas (arquivos que esgotaram as tentativas) apos as quais o download e abortado. */
    private static final int MAX_CONSECUTIVE_FILE_FAILURES = 2;
    private static final long RETRY_DELAY_MS = 1500;

    Messenger mMessenger;
    Messenger mActivityMessenger;
    IncomingHandler mInHandler;
    private HandlerThread mHandlerThread;
    private RequestQueue mQueue;

    public UpdateActivity.GameStatus mGameStatus = UpdateActivity.GameStatus.Unknown;
    public UpdateActivity.UpdateStatus mUpdateStatus = UpdateActivity.UpdateStatus.Undefined;

    public volatile boolean mDownloadingStatus = false;

    public long mUpdateGameDataSize = 0;
    public long mUpdateGameDataSizeUpdated = 0;
    public String mUpdateGameURL = "";
    public int mUpdateVersion;
    /** Verificacao do APK novo (client_config.launcher_sha256 / launcher_size). Vazio/0 = sem verificacao (servidor antigo). */
    public String mUpdateGameSha = "";
    public long mUpdateGameSize = 0;

    public ArrayList<String> mUpdateFiles = new ArrayList<>();
    public ArrayList<String> mUpdateFilesName = new ArrayList<>();
    public ArrayList<Long> mUpdateFilesSize = new ArrayList<>();
    /** SHA-256 esperado de cada arquivo (do files.json; "" = lista sem hash, so confere o tamanho). */
    public ArrayList<String> mUpdateFilesSha = new ArrayList<>();

    public int mGpuType = 0;

    private int mSession = 0;
    private int mDownloadId = -1;

    // estado do download de dados do jogo (somente na HandlerThread)
    private ArrayList<String> mDlFiles = new ArrayList<>();
    private ArrayList<Long> mDlSizes = new ArrayList<>();
    private ArrayList<String> mDlShas = new ArrayList<>();
    private ArrayList<String> mDlFailed = new ArrayList<>();
    private int mDlIndex = 0;
    private int mDlConsecutiveFailures = 0;
    private long mDlDoneBytes = 0;
    /** Arquivos baixados e conferidos nesta rodada (path/size/sha): gravados no installed.json ao final (atualizacoes). */
    private ArrayList<InstalledState.Entry> mDlOk = new ArrayList<>();

    // ---- primeira instalacao por pacotes ZIP (somente na HandlerThread; o trabalho pesado roda em mPackWorker)
    /** Pacotes (core + GPU do aparelho) vindos do client_config; null = servidor sem "packs" (fluxo antigo). */
    private PackConfig mPackConfig = null;
    private List<PackSpec> mPackSelection = null;
    /** true = esta rodada de dados do jogo e a PRIMEIRA instalacao e sera feita por pacotes ZIP. */
    private boolean mUsePacks = false;
    private boolean mPackHasSampStep = false; // pacotes seguidos da etapa SAMP (so para a mensagem da tela)
    private boolean mSampStep = false;        // baixando a pasta SAMP depois dos pacotes
    private int mListTotalFiles = 0;
    /** Lista da pasta SAMP/ (samp.json). null = servidor sem samp.json -> a SAMP/ segue pelo files.json (fluxo antigo). */
    private SampList mSampList = null;
    private static final String PACKS_MARKER = "(packs)";
    private ExecutorService mPackWorker;
    private CancelToken mPackCancel;
    /** Arquivos que ficam pendentes se a instalacao por pacotes falhar (a proxima verificacao refaz). */
    private ArrayList<String> mPackPendingFiles = new ArrayList<>();

    @Override
    public void onCreate() {
        super.onCreate();
        mHandlerThread = new HandlerThread("ServiceStartArguments", 10);
        mHandlerThread.start();
        try {
            PRDownloader.initialize(getApplicationContext(), PRDownloaderConfig.newBuilder().setDatabaseEnabled(true).setReadTimeout(30000).setConnectTimeout(30000).build());
        } catch (RuntimeException e) {
            Log.e(TAG, "PRDownloader init failed: " + e);
        }
        mQueue = Volley.newRequestQueue(getApplicationContext());
        mInHandler = new IncomingHandler(mHandlerThread.getLooper());
        mMessenger = new Messenger(mInHandler);
        // Recuperacao no inicio: apaga .staging/.downloads incompletos e restaura .backup (na HandlerThread, nao na principal).
        mInHandler.post(new Runnable() {
            @Override
            public void run() {
                File base = getExternalFilesDir(null);
                if (base != null && base.isDirectory()) {
                    try {
                        AtomicInstaller.recoverOnStartup(base);
                    } catch (RuntimeException e) {
                        Log.w(TAG, "recoverOnStartup failed: " + e);
                    }
                }
            }
        });
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mMessenger.getBinder();
    }

    @Override
    public void onDestroy() {
        // Ninguem esta mais ligado: cancela rede/downloads e encerra a thread (sem threads vazadas).
        mSession++;
        stopPackWorker();
        if (mQueue != null) {
            mQueue.cancelAll(REQUEST_TAG);
            mQueue.stop();
        }
        try {
            PRDownloader.cancelAll();
        } catch (RuntimeException ignored) {
        }
        mDownloadingStatus = false;
        mInHandler.removeCallbacksAndMessages(null);
        mHandlerThread.quitSafely();
        super.onDestroy();
    }

    class IncomingHandler extends Handler {
        public IncomingHandler(Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            if (msg.replyTo != null) {
                mActivityMessenger = msg.replyTo;
            }
            try {
                if (msg.what == 0 || msg.what == 7) {
                    mGpuType = msg.getData().getInt("gputype");
                    if (mGpuType == 0) {
                        Log.e("x1y2z", "GPU not found");
                        failCheck("GPU type not detected");
                        return;
                    }
                    startUpdating();
                } else if (msg.what == 1) {
                    startGameUpdateChecking();
                } else if (msg.what == 2) {
                    updateGame();
                } else if (msg.what == 4) {
                    Message obtain = Message.obtain(mInHandler, 4);
                    obtain.getData().putString("status", mUpdateStatus.name());
                    obtain.replyTo = mMessenger;
                    sendToActivity(obtain);
                } else if (msg.what == 5) {
                    Message obtain = Message.obtain(mInHandler, 5);
                    obtain.getData().putString("status", mGameStatus.name());
                    obtain.replyTo = mMessenger;
                    sendToActivity(obtain);
                }
            } catch (RuntimeException e) {
                Log.e(TAG, "handleMessage(" + msg.what + ") failed", e);
                failCheck("Unexpected error: " + e.getClass().getSimpleName());
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // Utilidades
    // ------------------------------------------------------------------------------------

    private void sendToActivity(Message m) {
        Messenger target = mActivityMessenger;
        if (target == null) {
            m.recycle();
            return;
        }
        try {
            target.send(m);
        } catch (RemoteException e) {
            Log.w(TAG, "activity messenger is gone: " + e);
            mActivityMessenger = null;
        }
    }

    /** Mensagem 6: erro recuperavel + (como no original) status Unknown (msg 5) para o Splash seguir. */
    private void reportError(String text, boolean alsoUnknownStatus) {
        mDownloadingStatus = false;
        mUpdateStatus = UpdateActivity.UpdateStatus.Undefined; // sem notificar: evita disparar msg 1 na Activity
        if (alsoUnknownStatus) {
            mGameStatus = UpdateActivity.GameStatus.Unknown;
            Message m5 = Message.obtain(mInHandler, 5);
            m5.getData().putString("status", mGameStatus.name());
            m5.replyTo = mMessenger;
            sendToActivity(m5);
        }
        Message m6 = Message.obtain(mInHandler, 6);
        m6.getData().putString("error", text);
        m6.replyTo = mMessenger;
        sendToActivity(m6);
    }

    private void failCheck(String text) {
        Log.e(TAG, "update check failed: " + text);
        reportError(text, true);
    }

    private void postToService(Runnable r) {
        mInHandler.post(r);
    }

    private static boolean isHttpUrl(String s) {
        return s != null && (s.startsWith("http://") || s.startsWith("https://"));
    }

    private StringRequest newRequest(String url, final int session, final Response.Listener<String> ok, final String what) {
        return newRequest(url, session, ok, what, null);
    }

    /** onFail != null: em erro de rede/resposta invalida chama onFail (na HandlerThread) em vez de abortar a verificacao. */
    private StringRequest newRequest(String url, final int session, final Response.Listener<String> ok, final String what, final Runnable onFail) {
        StringRequest r = new StringRequest(url, new Response.Listener<String>() {
            @Override
            public void onResponse(final String response) {
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        try {
                            ok.onResponse(response);
                        } catch (RuntimeException e) {
                            Log.e(TAG, what + ": processing failed", e);
                            if (onFail != null) onFail.run(); else failCheck(what + ": invalid response");
                        }
                    }
                });
            }
        }, new Response.ErrorListener() {
            @Override
            public void onErrorResponse(final VolleyError error) {
                Log.w("x1y2z", "error " + error.toString());
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        if (onFail != null) onFail.run(); else failCheck("Could not reach the update server (" + what + ")");
                    }
                });
            }
        });
        r.setTag(REQUEST_TAG);
        r.setShouldCache(false);
        r.setRetryPolicy(new DefaultRetryPolicy(AppEndpoints.HTTP_TIMEOUT_MS, AppEndpoints.HTTP_MAX_RETRIES, 1.5f));
        return r;
    }

    // ------------------------------------------------------------------------------------
    // Verificacao de atualizacao
    // ------------------------------------------------------------------------------------

    void startUpdating()
    {
        cancelDownloads();
        final int session = ++mSession;
        mUpdateFiles = new ArrayList<>();
        mUpdateFilesName = new ArrayList<>();
        mUpdateFilesSize = new ArrayList<>();
        mUpdateFilesSha = new ArrayList<>();
        mUsePacks = false;
        mSampStep = false;
        mPackHasSampStep = false;
        mPackConfig = null;
        mPackSelection = null;
        mSampList = null;
        mListTotalFiles = 0;
        mUpdateGameDataSize = 0;          // original acumulava entre verificacoes
        mUpdateGameDataSizeUpdated = 0;
        mGameStatus = UpdateActivity.GameStatus.Unknown;

        setUpdateStatus(UpdateActivity.UpdateStatus.CheckUpdate);
        loadClientConfig(true, session, new Runnable() {
            @Override
            public void run() {
                if (!isGameUpdateExists()) {
                    if (mUpdateFiles.isEmpty()) {
                        mGameStatus = UpdateActivity.GameStatus.Updated;
                    } else {
                        mGameStatus = UpdateActivity.GameStatus.UpdateRequired;
                    }
                } else {
                    mGameStatus = UpdateActivity.GameStatus.GameUpdateRequired;
                }
                setUpdateStatus(UpdateActivity.UpdateStatus.Undefined);
            }
        });
    }

    /** Baixa client_config.json (e, se withFiles, a lista de arquivos) de forma assincrona. */
    private void loadClientConfig(final boolean withFiles, final int session, final Runnable onSuccess) {
        mQueue.add(newRequest(AppEndpoints.clientConfigUrl(this), session, new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                try {
                    JSONObject jSONObject = new JSONObject(response).getJSONObject("client_config");
                    mUpdateVersion = jSONObject.getInt("version_code");
                    mUpdateGameURL = jSONObject.getString("url_launcher");
                    mUpdateGameSha = jSONObject.optString("launcher_sha256", "").trim().toLowerCase(java.util.Locale.ROOT);
                    if (mUpdateGameSha.length() != 64) mUpdateGameSha = "";
                    mUpdateGameSize = jSONObject.optLong("launcher_size", 0);
                    if (withFiles) {
                        parsePackConfig(response);
                        String string = jSONObject.getString("url_cache_files");
                        if (!isHttpUrl(string)) {
                            failCheck("client_config: invalid url_cache_files");
                            return;
                        }
                        final String filesUrl = string;
                        loadSamp(session, new Runnable() {
                            @Override
                            public void run() {
                                getFilesInfo(filesUrl, session, onSuccess);
                            }
                        });
                    } else {
                        onSuccess.run();
                    }
                } catch (JSONException e) {
                    Log.e(TAG, "client_config parse error", e);
                    failCheck("client_config.json is invalid (" + e.getMessage() + ")");
                }
            }
        }, "client_config"));
    }

    /**
     * Baixa samp.json (lista da pasta SAMP/) e acrescenta a mUpdateFiles o que falta/esta diferente. Erro ou arquivo
     * ausente no servidor NAO aborta: mSampList fica null e a SAMP/ segue pelo files.json como antes.
     */
    private void loadSamp(final int session, final Runnable next) {
        final Runnable fallback = new Runnable() {
            @Override
            public void run() {
                Log.w(TAG, "samp.json indisponivel -> SAMP/ pelo files.json");
                mSampList = null;
                next.run();
            }
        };
        mQueue.add(newRequest(AppEndpoints.sampJsonUrl(this), session, new Response.Listener<String>() {
            @Override
            public void onResponse(String body) {
                File base = getExternalFilesDir(null);
                if (base == null) { fallback.run(); return; }
                SampList list;
                try {
                    list = SampList.parse(body);
                } catch (PackException e) {
                    Log.e(TAG, "samp.json invalido: " + e.getMessage());
                    fallback.run();
                    return;
                }
                mSampList = list;
                int madeDirs = list.ensureDirs(base); // pastas vazias (ex.: SAMP/skins/fallback_tex)
                if (madeDirs > 0) Log.d(TAG, "SAMP: " + madeDirs + " pasta(s) vazia(s) criada(s)");
                // verificacao profunda (SHA-256) so quando a instalacao ainda nao foi verificada por pacotes
                boolean deep = InstalledState.read(base) == null;
                int pending = 0;
                for (SampList.Entry e : list.files) {
                    String why = SampList.whyNeeded(base, e, deep);
                    if (why == null) continue;
                    Log.d(TAG, "SAMP pendente: " + e.path + " (" + why + ")");
                    mUpdateFiles.add(e.path);
                    mUpdateFilesName.add(e.path.substring(e.path.lastIndexOf('/') + 1));
                    mUpdateFilesSize.add(e.size);
                    mUpdateFilesSha.add(e.sha256);
                    mUpdateGameDataSize += e.size;
                    pending++;
                }
                Log.d(TAG, "SAMP: " + list.files.size() + " arquivos na lista, " + pending + " pendentes");
                next.run();
            }
        }, "samp.json", fallback));
    }

    /** Baixa e interpreta a lista de arquivos; sem espera ativa, sem variaveis globais compartilhadas. */
    public void getFilesInfo(String url, final int session, final Runnable onSuccess) {
        mQueue.add(newRequest(url, session, new Response.Listener<String>() {
            @Override
            public void onResponse(String body) {
                try {
                    parseFilesInfo(body);
                } catch (JSONException e) {
                    Log.e(TAG, "files list parse error", e);
                    failCheck("File list is invalid (" + e.getMessage() + ")");
                    return;
                } catch (IllegalStateException e) {
                    failCheck(e.getMessage());
                    return;
                }
                onSuccess.run();
            }
        }, "file list"));
    }

    /** Le "version"/"packs" do client_config. Invalido ou ausente => fluxo antigo (arquivo a arquivo), nunca quebra a verificacao. */
    private void parsePackConfig(String response) {
        mPackConfig = null;
        mPackSelection = null;
        try {
            PackConfig pc = PackConfig.parse(response);
            if (pc.packs.isEmpty()) return;
            List<PackSpec> sel = pc.selectFor(mGpuType);
            if (sel == null) {
                Log.w(TAG, "client_config tem packs, mas nenhum serve a GPU " + mGpuType + " -> fluxo antigo");
                return;
            }
            mPackConfig = pc;
            mPackSelection = sel;
        } catch (PackException e) {
            Log.w(TAG, "client_config: packs ignorados (" + e.getMessage() + ") -> fluxo antigo");
        }
    }

    private void parseFilesInfo(String body) throws JSONException {
        File base = getExternalFilesDir(null);
        if (base == null) throw new IllegalStateException("Storage is not available");
        JSONArray jsonArray = new JSONObject(body).getJSONArray("files");
        Log.d("x1y2z", "Length: " + jsonArray.length());
        int considered = 0;
        // SHA-256 registrado no installed.json: permite detectar arquivo ALTERADO no servidor com o mesmo tamanho (atualizacao).
        java.util.HashMap<String, String> installedSha = new java.util.HashMap<>();
        InstalledState prev = InstalledState.read(base);
        if (prev != null && prev.gpu == mGpuType) {
            for (InstalledState.Entry ie : prev.files) if (!ie.sha256.isEmpty()) installedSha.put(ie.path, ie.sha256);
        }
        for(int i = 0; i<jsonArray.length(); i++) {
            JSONObject o = jsonArray.getJSONObject(i);
            FilesData fileData = new FilesData(o.getString("name"), o.getLong("size"), o.getString("path"), o.optString("url", ""));
            String name = fileData.getName();
            String path = fileData.getPath();
            // Arquivos gerados pelo jogo/launcher no celular: nunca baixar nem sobrescrever (lista do requisito + nomes do original).
            if (name.equals("samp_log.txt") || name.equals("svlog.txt") || name.equals("gtasatelem.set")
                    || name.equals("GTASAMP10.b") || name.equals(".htaccess") || name.equals("gta_sa.set")
                    || name.equals("settings.ini") || GameFilePolicy.isInternal(path) || GameFilePolicy.isGenerated(path)) {
                continue;
            }
            // A pasta SAMP/ vem do samp.json (nada dela e ignorado); o files.json so e usado para ela se nao houver samp.json.
            if (mSampList != null && SampList.isSampPath(path)) {
                continue;
            }
            // Filtro por GPU SEM diferenciar caixa (o original usava contains("samp") e falhava com "SAMP/").
            if (!GameFilePolicy.wantedForDevice(path, mGpuType)) {
                continue;
            }
            considered++;
            File file = new File(base, path);
            String serverSha = o.optString("sha256", "").trim().toLowerCase(java.util.Locale.ROOT);
            boolean changed = false;
            if (file.exists() && file.length() == fileData.getSize() && serverSha.length() == 64) {
                String have = installedSha.get(path);
                if (have != null && !have.equals(serverSha)) {
                    changed = true;
                    Log.d(TAG, "atualizacao: " + path + " mudou no servidor (mesmo tamanho, sha diferente)");
                }
            }
            if (!file.exists() || file.length() != fileData.getSize() || changed) {
                mUpdateFiles.add(path);
                mUpdateFilesName.add(name);
                mUpdateFilesSize.add(fileData.getSize());
                mUpdateFilesSha.add(o.optString("sha256", "").trim().toLowerCase(java.util.Locale.ROOT));
                mUpdateGameDataSize = mUpdateGameDataSize + fileData.getSize();
            }
        }
        mListTotalFiles = considered;
        decidePackMode(base);
        Log.d("x1y2z", "Data size: " + mUpdateGameDataSize + " files: " + mUpdateFiles.size() + " usePacks=" + mUsePacks);
    }

    /**
     * Pacotes ZIP (verificados por SHA-256) sao usados em TODA instalacao que ainda nao foi verificada:
     * existem packs validos para a GPU e NAO ha installed.json valido para esta GPU (primeira instalacao,
     * instalacao antiga/quebrada pelo fluxo arquivo-a-arquivo, ou troca de GPU). Nesse caso tudo e baixado
     * e conferido de novo, MESMO que os arquivos ja existam com o tamanho certo (copias corrompidas com o
     * mesmo tamanho nao sao detectaveis por size). Depois de uma instalacao por pacotes, atualizacoes e a
     * verificacao seguem pelo fluxo antigo (PRDownloader, arquivo a arquivo).
     */
    private void decidePackMode(File base) {
        mUsePacks = false;
        if (mPackConfig == null || mPackSelection == null) return;
        InstalledState st = InstalledState.read(base);
        boolean verified = st != null && st.gpu == mGpuType && !st.files.isEmpty();
        if (verified) return;
        mUsePacks = true;
        // Marcador unico: a Activity so pede a instalacao (msg 1) se houver algo "pendente".
        long total = 0;
        for (PackSpec p : mPackSelection) total += p.size;
        // Mantem so as entradas da pasta SAMP/ (baixadas depois, pelo PRDownloader); o resto vira o marcador dos pacotes.
        ArrayList<String> sf = new ArrayList<>(), sn = new ArrayList<>(), sh = new ArrayList<>();
        ArrayList<Long> sz = new ArrayList<>();
        long sampBytes = 0;
        if (mSampList != null) {
            for (int i = 0; i < mUpdateFiles.size(); i++) {
                if (SampList.isSampPath(mUpdateFiles.get(i))) {
                    sf.add(mUpdateFiles.get(i)); sn.add(mUpdateFilesName.get(i));
                    sz.add(mUpdateFilesSize.get(i)); sh.add(mUpdateFilesSha.get(i));
                    sampBytes += mUpdateFilesSize.get(i);
                }
            }
        }
        mUpdateFiles.clear();
        mUpdateFilesName.clear();
        mUpdateFilesSize.clear();
        mUpdateFilesSha.clear();
        mUpdateFiles.add(PACKS_MARKER);
        mUpdateFilesName.add(PACKS_MARKER);
        mUpdateFilesSize.add(total);
        mUpdateFilesSha.add("");
        mUpdateFiles.addAll(sf);
        mUpdateFilesName.addAll(sn);
        mUpdateFilesSize.addAll(sz);
        mUpdateFilesSha.addAll(sh);
        mUpdateGameDataSize = total + sampBytes;
        Log.d(TAG, "instalacao por pacotes: " + mPackSelection.size() + " pacotes, " + total + " bytes");
    }

    private String mInstalledShaPath = "";
    private long mInstalledShaStamp = -1;
    private String mInstalledSha = "";

    /** SHA-256 do APK instalado (base.apk). Em cache por caminho+tamanho+data; roda na HandlerThread do servico. "" = nao deu para ler. */
    private String installedApkSha(String path) {
        File f = new File(path);
        long stamp = f.length() ^ (f.lastModified() * 31);
        if (path.equals(mInstalledShaPath) && stamp == mInstalledShaStamp) return mInstalledSha;
        String r = "";
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            java.io.FileInputStream in = new java.io.FileInputStream(f);
            try {
                byte[] buf = new byte[256 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            } finally {
                in.close();
            }
            StringBuilder sb = new StringBuilder(64);
            for (byte b : md.digest()) sb.append(String.format(java.util.Locale.ROOT, "%02x", b));
            r = sb.toString();
        } catch (java.security.NoSuchAlgorithmException | java.io.IOException e) {
            Log.w(TAG, "nao foi possivel calcular o sha256 do APK instalado: " + e);
        }
        mInstalledShaPath = path; mInstalledShaStamp = stamp; mInstalledSha = r;
        return r;
    }

    /**
     * Ha APK novo quando o versionCode do client_config e DIFERENTE do instalado OU (com launcher_sha256 no client_config)
     * quando o APK instalado nao e byte a byte o publicado no servidor - assim trocar o launcher.apk sem mexer no
     * version_code tambem forca a atualizacao. Se o hash do instalado nao puder ser lido, vale so o versionCode.
     */
    public boolean isGameUpdateExists() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo("com.samp.mobile", 0);
            Log.d("x1y2z", "isGameUpdateExists -> currentVersion " + packageInfo.versionCode + " | mUpdateVersion " + this.mUpdateVersion);
            if (packageInfo.versionCode != this.mUpdateVersion) return true;
            if (!mUpdateGameSha.isEmpty() && packageInfo.applicationInfo != null && packageInfo.applicationInfo.sourceDir != null) {
                String have = installedApkSha(packageInfo.applicationInfo.sourceDir);
                if (!have.isEmpty() && !have.equals(mUpdateGameSha)) {
                    Log.w(TAG, "APK instalado difere do publicado (mesmo versionCode): " + have + " != " + mUpdateGameSha);
                    return true;
                }
            }
            return false;
        } catch (PackageManager.NameNotFoundException e) {
            Log.e(TAG, "own package not found", e);
            return false;
        }
    }

    // ------------------------------------------------------------------------------------
    // Atualizacao do APK do launcher
    // ------------------------------------------------------------------------------------

    public void updateGame() {
        cancelDownloads();
        final int session = ++mSession;
        if (mUpdateGameURL.isEmpty()) {
            // Servico recriado (a Activity anterior desligou antes da nova ligar): recarrega so o client_config.
            loadClientConfig(false, session, new Runnable() {
                @Override
                public void run() {
                    updateGameChecked(session);
                }
            });
        } else {
            updateGameChecked(session);
        }
    }

    private void updateGameChecked(int session) {
        if (isGameUpdateExists()) {
            Log.d("UpdateService", "updateGame exists");
            downloadGame(session, 1);
            return;
        }
        Log.d("UpdateService", "updateGame done");
        File dir = getExternalFilesDir(null);
        File file = new File(dir, "launcher.apk");
        Message obtain = Message.obtain(mInHandler, 2);
        obtain.getData().putBoolean("status", true);
        obtain.getData().putString("apkPath", file.getAbsolutePath());
        obtain.replyTo = mMessenger;
        sendToActivity(obtain);
    }

    public void downloadGame()
    {
        downloadGame(mSession, 1);
    }

    private void downloadGame(final int session, final int attempt)
    {
        if (session != mSession) return;
        Log.d("UpdateService", "downloadGame attempt " + attempt);
        final File dir = getExternalFilesDir(null);
        if (dir == null) {
            reportError("Storage is not available", false);
            return;
        }
        if (!isHttpUrl(mUpdateGameURL)) {
            reportError("Invalid launcher update URL in client_config.json", false);
            return;
        }
        final File file = new File(dir, "launcher.apk");
        if (file.exists()) {
            file.delete();
        }

        mDownloadingStatus = true;
        final long[] last = { System.currentTimeMillis() };
        mDownloadId = PRDownloader.download(mUpdateGameURL, dir.getAbsolutePath(), "launcher.apk").build().setOnStartOrResumeListener(null).setOnPauseListener(null).setOnCancelListener(null).setOnProgressListener(new OnProgressListener() {
            @Override
            public void onProgress(final Progress progress) {
                if(System.currentTimeMillis() - last[0] > 100) {
                    last[0] = System.currentTimeMillis();
                    postToService(new Runnable() {
                        @Override
                        public void run() {
                            if (session != mSession) return;
                            Message obtain = Message.obtain(mInHandler, 4);
                            obtain.getData().putString("status", UpdateActivity.UpdateStatus.DownloadGame.name());
                            obtain.getData().putBoolean("withProgress", true);
                            obtain.getData().putLong("current", progress.currentBytes);
                            obtain.getData().putLong("total", progress.totalBytes);
                            obtain.getData().putString("filename", "launcher.apk");
                            obtain.getData().putLong("totalfiles", 1);
                            obtain.getData().putLong("currentfile", 1);
                            sendToActivity(obtain);
                        }
                    });
                }
            }
        }).start(new OnDownloadListener() {
            @Override
            public void onDownloadComplete() {
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mDownloadingStatus = false;
                        mDownloadId = -1;
                        // APK novo: confere tamanho/SHA-256 do client_config ANTES de oferecer a instalacao.
                        if (!mUpdateGameSha.isEmpty() || mUpdateGameSize > 0) {
                            String why = verifyDownloaded(file, mUpdateGameSize > 0 ? mUpdateGameSize : file.length(), mUpdateGameSha);
                            if (why != null) {
                                Log.w("x1y2z", "launcher.apk rejeitado: " + why);
                                //noinspection ResultOfMethodCallIgnored
                                file.delete();
                                if (attempt < MAX_GAME_ATTEMPTS) {
                                    mInHandler.postDelayed(new Runnable() {
                                        @Override
                                        public void run() {
                                            downloadGame(session, attempt + 1);
                                        }
                                    }, RETRY_DELAY_MS * attempt);
                                } else {
                                    reportError("The downloaded launcher update failed verification (" + why + ")", false);
                                }
                                return;
                            }
                        }
                        Message obtain = Message.obtain(UpdateService.this.mInHandler, 2);
                        obtain.getData().putBoolean("status", true);
                        obtain.getData().putString("apkPath", file.getAbsolutePath());
                        obtain.replyTo = mMessenger;
                        sendToActivity(obtain);
                        setUpdateStatus(UpdateActivity.UpdateStatus.Undefined);
                        Log.d("x1y2z", "completed");
                    }
                });
            }

            @Override
            public void onError(final Error error) {
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mDownloadingStatus = false; // sempre volta a false
                        mDownloadId = -1;
                        Log.w("x1y2z", "error downloadgame (attempt " + attempt + ") connection=" + error.isConnectionError() + " server=" + error.isServerError());
                        if (attempt < MAX_GAME_ATTEMPTS) {
                            mInHandler.postDelayed(new Runnable() {
                                @Override
                                public void run() {
                                    downloadGame(session, attempt + 1);
                                }
                            }, RETRY_DELAY_MS * attempt);
                        } else {
                            reportError("Could not download the launcher update after " + MAX_GAME_ATTEMPTS + " attempts", false);
                        }
                    }
                });
            }
        });
    }

    // ------------------------------------------------------------------------------------
    // Dados do jogo
    // ------------------------------------------------------------------------------------

    public void startGameUpdateChecking()
    {
        if (!mUpdateFiles.isEmpty()) {
            setUpdateStatus(UpdateActivity.UpdateStatus.DownloadGameData);
            startDataUpdating();
            return;
        }

        Log.d("UpdateService", "updateGameData()");
        notifyGameDataDone();
    }

    private void notifyGameDataDone() {
        Message obtain = Message.obtain(this.mInHandler, 1);
        obtain.getData().putBoolean("status", true);
        obtain.replyTo = mMessenger;
        sendToActivity(obtain);
    }

    public void setUpdateStatus(UpdateActivity.UpdateStatus updateStatus) {
        if (updateStatus.name().length() != 0 && mUpdateStatus != updateStatus) {
            mUpdateStatus = updateStatus;
            Message obtain = Message.obtain(mInHandler, 4);
            obtain.getData().putString("status", mUpdateStatus.name());
            obtain.replyTo = mMessenger;
            sendToActivity(obtain);
        }
    }

    /** Baixa os arquivos em sequencia, por callbacks: 3 tentativas por arquivo, aborta apos falhas seguidas. */
    public void startDataUpdating()
    {
        cancelDownloads();
        final int session = ++mSession;
        mDlFiles = new ArrayList<>(mUpdateFiles);
        mDlSizes = new ArrayList<>(mUpdateFilesSize);
        mDlShas = new ArrayList<>(mUpdateFilesSha);
        mDlFailed = new ArrayList<>();
        mDlOk = new ArrayList<>();
        mUpdateFiles.clear();
        mUpdateFilesName.clear();
        mUpdateFilesSize.clear();
        mUpdateFilesSha.clear();
        mDlIndex = 0;
        mDlDoneBytes = 0;
        mDlConsecutiveFailures = 0;
        if (mUsePacks && mPackSelection != null && mPackConfig != null) {
            startPackInstall(session);
            return;
        }
        downloadNextFile(session, 1);
    }

    // ------------------------------------------------------------------------------------
    // Primeira instalacao: pacotes ZIP verificados + extracao atomica (ver pacote pack/)
    // ------------------------------------------------------------------------------------

    /** Devolve a lista de pendentes (com tamanho e sha, mantendo as listas paralelas coerentes) para uma nova tentativa. */
    private void restorePending(List<String> paths) {
        for (String p : paths) {
            int i = mDlFiles.indexOf(p);
            mUpdateFiles.add(p);
            mUpdateFilesName.add(p.substring(p.lastIndexOf('/') + 1));
            mUpdateFilesSize.add(i >= 0 && i < mDlSizes.size() ? mDlSizes.get(i) : 0L);
            mUpdateFilesSha.add(i >= 0 && i < mDlShas.size() ? mDlShas.get(i) : "");
        }
    }

    /** Apaga .downloads (zips/.part) e qualquer sobra do instalador; so chamado depois de extrair e confirmar. */
    private void deletePackDownloads() {
        File base = getExternalFilesDir(null);
        if (base == null) return;
        try {
            AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_DOWNLOADS));
            AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_STAGING));
            AtomicInstaller.deleteTree(new File(base, GameFilePolicy.DIR_BACKUP));
        } catch (RuntimeException e) {
            Log.w(TAG, "cleanup after packs failed: " + e);
        }
    }

    /** Depois dos pacotes: baixa a pasta SAMP/ (PRDownloader, arquivo a arquivo) ou conclui. */
    private void afterPacks(int session) {
        // tira o marcador dos pacotes, sobram so as entradas da SAMP/
        if (!mDlFiles.isEmpty() && PACKS_MARKER.equals(mDlFiles.get(0))) {
            mDlFiles.remove(0);
            mDlSizes.remove(0);
            mDlShas.remove(0);
        }
        mUsePacks = false;
        mPackHasSampStep = false;
        if (mDlFiles.isEmpty()) {
            notifyGameDataDone();
            return;
        }
        mSampStep = true;
        long total = 0;
        for (Long l : mDlSizes) total += l;
        mUpdateGameDataSize = total;
        mDlFailed = new ArrayList<>();
        mDlIndex = 0;
        mDlDoneBytes = 0;
        mDlConsecutiveFailures = 0;
        downloadNextFile(session, 1);
    }

    private void stopPackWorker() {
        if (mPackCancel != null) mPackCancel.cancel();
        if (mPackWorker != null) {
            mPackWorker.shutdownNow(); // interrompe E-S bloqueante; o instalador confere o CancelToken
            mPackWorker = null;
        }
        mPackCancel = null;
    }

    private void startPackInstall(final int session) {
        stopPackWorker();
        final File base = getExternalFilesDir(null);
        if (base == null || !base.isDirectory()) {
            restorePending(mDlFiles);
            reportError("Storage is not available", false);
            return;
        }
        mPackPendingFiles = new ArrayList<>(mDlFiles);
        mPackHasSampStep = mDlFiles.size() > 1; // alem do marcador "(packs)" ha arquivos da SAMP/
        mSampStep = false;
        GameFilePolicy.setProtectGenerated(!mPackConfig.includeAll);
        final PackInstaller.Config cfg = new PackInstaller.Config();
        cfg.filesDir = base;
        cfg.version = mPackConfig.version;
        cfg.gpu = mGpuType;
        cfg.packs = mPackSelection;
        cfg.urls = new PackInstaller.UrlResolver() {
            @Override
            public String resolve(PackSpec spec) {
                return AppEndpoints.packUrl(UpdateService.this, spec.url);
            }
        };
        cfg.transfer = new PrDownloaderTransfer(); // PRDownloader (retoma de onde parou) em vez do HttpURLConnection
        cfg.freeSpace = new PackInstaller.FreeSpace() {
            @Override
            public long freeBytes() {
                return new StatFs(base.getAbsolutePath()).getAvailableBytes();
            }
        };
        final CancelToken cancel = new CancelToken();
        cfg.cancel = cancel;
        mPackCancel = cancel;
        final long[] last = { 0 };
        // A UI mostra o tamanho REAL do download (soma dos .zip); a extracao aparece depois, com seu proprio total.
        cfg.detail = new PackInstaller.Detail() {
            @Override
            public void onDetail(final String stage, final String packName, final int packIndex, final int packCount,
                                 final long dlDone, final long dlTotal, final long exDone, final long exTotal) {
                long now = System.currentTimeMillis();
                boolean finished = dlDone >= dlTotal && exDone >= exTotal;
                if (now - last[0] < 100 && !finished) return;
                last[0] = now;
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        boolean downloading = dlDone < dlTotal;
                        Message obtain = Message.obtain(mInHandler, 4);
                        obtain.getData().putString("status", UpdateActivity.UpdateStatus.DownloadGameData.name());
                        obtain.getData().putBoolean("withProgress", true);
                        obtain.getData().putLong("current", downloading ? dlDone : exDone);
                        obtain.getData().putLong("total", downloading ? dlTotal : exTotal);
                        obtain.getData().putString("filename", downloading ? packName + ".zip" : packName + " (extracting)");
                        obtain.getData().putLong("totalfiles", packCount);
                        obtain.getData().putLong("currentfile", packIndex + 1);
                        obtain.getData().putInt("phase", mPackHasSampStep ? 1 : 0); // 1 = ainda vem a etapa SAMP
                        sendToActivity(obtain);
                    }
                });
            }
        };
        mDownloadingStatus = true;
        ExecutorService worker = Executors.newSingleThreadExecutor(new ThreadFactory() {
            @Override
            public Thread newThread(Runnable r) {
                Thread t = new Thread(r, "pack-install");
                t.setDaemon(true);
                return t;
            }
        });
        mPackWorker = worker;
        worker.execute(new Runnable() {
            @Override
            public void run() {
                PackException failure = null;
                try {
                    PackInstaller.install(cfg);
                } catch (PackException e) {
                    failure = e;
                } catch (RuntimeException e) {
                    failure = new PackException("unexpected: " + e, e);
                }
                GameFilePolicy.setProtectGenerated(true); // o fluxo antigo (atualizacao) NUNCA sobrescreve arquivos gerados pelo jogo
                final PackException fail = failure;
                if (cancel.isCancelled()) return;
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mDownloadingStatus = false;
                        if (fail == null) {
                            Log.d(TAG, "pack install done");
                            mPackPendingFiles.clear();
                            deletePackDownloads(); // pacotes ja extraidos e conferidos: nao ficam no armazenamento do aparelho
                            afterPacks(session);
                        } else {
                            Log.e(TAG, "pack install failed: " + fail.getMessage(), fail);
                            restorePending(mPackPendingFiles); // continua pendente: a proxima verificacao tenta de novo
                            mPackPendingFiles.clear();
                            reportError("Game data install failed: " + fail.getMessage(), false);
                        }
                    }
                });
            }
        });
    }

    private void downloadNextFile(final int session, final int attempt) {
        if (session != mSession) return;
        if (mDlIndex >= mDlFiles.size()) {
            finishDataUpdating();
            return;
        }
        final String rel = mDlFiles.get(mDlIndex);
        final File base = getExternalFilesDir(null);
        File target = null;
        if (base != null) {
            try {
                File candidate = new File(base, rel);
                // Seguranca/caminho: o arquivo precisa ficar dentro do diretorio do app (sem "../").
                if (candidate.getCanonicalPath().startsWith(base.getCanonicalPath() + File.separator)) {
                    target = candidate;
                }
            } catch (IOException e) {
                Log.e(TAG, "bad path " + rel, e);
            }
        }
        File parent = target == null ? null : target.getParentFile();
        if (target == null || parent == null || (!parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory())) {
            Log.e(TAG, "cannot prepare path for " + rel);
            onFileGaveUp(session, rel);
            return;
        }
        if (target.exists()) {
            target.delete();
        }

        // Arquivo de 0 bytes (ex.: data/maps/occluLA.ipl): o PRDownloader nao conclui download sem conteudo e
        // dois arquivos assim seguidos abortavam TODO o download. Cria vazio localmente e segue.
        if (mDlSizes.get(mDlIndex) == 0L) {
            try {
                if (!target.createNewFile() && !target.isFile()) throw new IOException("createNewFile falhou");
                mDlConsecutiveFailures = 0;
                mDlIndex++;
                downloadNextFile(session, 1);
            } catch (IOException e) {
                Log.e(TAG, "cannot create empty file " + rel, e);
                onFileGaveUp(session, rel);
            }
            return;
        }

        mDownloadingStatus = true;
        final String fileName = target.getName();
        final long[] last = { System.currentTimeMillis() };
        mDownloadId = PRDownloader.download(urlFor(rel), parent.getAbsolutePath(), fileName).build().setOnStartOrResumeListener(null).setOnPauseListener(null).setOnCancelListener(null).setOnProgressListener(new OnProgressListener() {
            @Override
            public void onProgress(final Progress progress) {
                if(System.currentTimeMillis() - last[0] > 100) {
                    last[0] = System.currentTimeMillis();
                    postToService(new Runnable() {
                        @Override
                        public void run() {
                            if (session != mSession) return;
                            Message obtain = Message.obtain(mInHandler, 4);
                            obtain.getData().putString("status", UpdateActivity.UpdateStatus.DownloadGameData.name());
                            obtain.getData().putBoolean("withProgress", true);
                            obtain.getData().putLong("current", mDlDoneBytes + progress.currentBytes);
                            obtain.getData().putLong("total", mUpdateGameDataSize);
                            obtain.getData().putString("filename", fileName);
                            obtain.getData().putLong("totalfiles", mDlFiles.size());
                            obtain.getData().putLong("currentfile", mDlIndex);
                            obtain.getData().putInt("phase", mSampStep ? 2 : 0); // 2 = etapa final (pasta SAMP)
                            sendToActivity(obtain);
                        }
                    });
                }
            }
        }).start(new OnDownloadListener() {
            @Override
            public void onDownloadComplete() {
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mDownloadingStatus = false;
                        mDownloadId = -1;
                        // PRDownloader diz "completo" mesmo com arquivo cortado/errado: confere tamanho (e SHA-256, se a lista tiver).
                        final File done = new File(parentOf(rel), fileName);
                        String why = verifyDownloaded(done, mDlSizes.get(mDlIndex), mDlShas.get(mDlIndex));
                        if (why != null) {
                            Log.w("x1y2z", "arquivo invalido " + rel + ": " + why + " (attempt " + attempt + ")");
                            //noinspection ResultOfMethodCallIgnored
                            done.delete();
                            if (attempt < MAX_FILE_ATTEMPTS) {
                                mInHandler.postDelayed(new Runnable() {
                                    @Override
                                    public void run() {
                                        downloadNextFile(session, attempt + 1);
                                    }
                                }, RETRY_DELAY_MS * attempt);
                            } else {
                                onFileGaveUp(session, rel);
                            }
                            return;
                        }
                        mDlOk.add(new InstalledState.Entry(rel, mDlSizes.get(mDlIndex), mDlShas.get(mDlIndex)));
                        mDlDoneBytes += mDlSizes.get(mDlIndex);
                        mDlConsecutiveFailures = 0;
                        mDlIndex++;
                        Log.d("x1y2z", "completed");
                        downloadNextFile(session, 1);
                    }
                });
            }

            @Override
            public void onError(final Error error) {
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mDownloadingStatus = false; // sempre volta a false
                        mDownloadId = -1;
                        Log.w("x1y2z", "error downloadgamedata " + rel + " (attempt " + attempt + ") connection=" + error.isConnectionError() + " server=" + error.isServerError());
                        if (attempt < MAX_FILE_ATTEMPTS) {
                            mInHandler.postDelayed(new Runnable() {
                                @Override
                                public void run() {
                                    downloadNextFile(session, attempt + 1);
                                }
                            }, RETRY_DELAY_MS * attempt);
                        } else {
                            onFileGaveUp(session, rel);
                        }
                    }
                });
            }
        });
    }

    /** SAMP/ (quando ha samp.json) vem de <base>/SAMP/...; o resto de <base>/files/... */
    private String urlFor(String rel) {
        String enc = Uri.encode(rel, "/");
        if (mSampList != null && SampList.isSampPath(rel)) return AppEndpoints.sampFileUrl(this, enc);
        return AppEndpoints.fileUrl(this, enc);
    }

    private File parentOf(String rel) {
        File base = getExternalFilesDir(null);
        File f = new File(base, rel);
        return f.getParentFile();
    }

    /** null = ok; senao o motivo. Roda na HandlerThread do servico (nao bloqueia a thread principal). */
    private static String verifyDownloaded(File f, long expectedSize, String expectedSha) {
        if (!f.isFile()) return "arquivo ausente";
        if (f.length() != expectedSize) return "tamanho " + f.length() + " != " + expectedSize;
        if (expectedSha != null && expectedSha.length() == 64) {
            try {
                java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
                java.io.FileInputStream in = new java.io.FileInputStream(f);
                try {
                    byte[] buf = new byte[256 * 1024];
                    int n;
                    while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
                } finally {
                    in.close();
                }
                StringBuilder sb = new StringBuilder(64);
                for (byte b : md.digest()) sb.append(String.format(java.util.Locale.ROOT, "%02x", b));
                if (!sb.toString().equals(expectedSha)) return "sha256 diferente";
            } catch (java.security.NoSuchAlgorithmException | java.io.IOException e) {
                return "nao foi possivel conferir o sha256: " + e;
            }
        }
        return null;
    }

    private void onFileGaveUp(int session, String rel) {
        mDlFailed.add(rel);
        mDlConsecutiveFailures++;
        mDlIndex++;
        if (mDlConsecutiveFailures >= MAX_CONSECUTIVE_FILE_FAILURES) {
            // Provavel falta de rede/servidor fora: nao insistir arquivo por arquivo.
            for (int i = mDlIndex; i < mDlFiles.size(); i++) mDlFailed.add(mDlFiles.get(i));
            mDlIndex = mDlFiles.size();
        }
        downloadNextFile(session, 1);
    }

    private void finishDataUpdating() {
        mDownloadingStatus = false;
        recordUpdated();
        if (!mDlFailed.isEmpty()) {
            restorePending(mDlFailed); // continuam pendentes para a proxima verificacao
            int n = mDlFailed.size();
            reportError(n + " game file(s) could not be downloaded.", false);
            return;
        }
        Log.d("UpdateService", "updateGameData()");
        notifyGameDataDone();
    }

    /** Grava no installed.json (se existir) o sha dos arquivos que acabaram de ser baixados e conferidos. */
    private void recordUpdated() {
        File base = getExternalFilesDir(null);
        if (base == null || mDlOk.isEmpty()) return;
        try {
            InstalledState.record(base, mDlOk);
        } catch (RuntimeException e) {
            Log.w(TAG, "installed.json nao atualizado: " + e);
        }
        mDlOk = new ArrayList<>();
    }

    /** Cancela qualquer download em andamento e invalida callbacks pendentes. */
    private void cancelDownloads() {
        mSession++; // callbacks e retries atrasados conferem a sessao e se descartam
        stopPackWorker();
        if (mQueue != null) mQueue.cancelAll(REQUEST_TAG);
        try {
            if (mDownloadId != -1) PRDownloader.cancel(mDownloadId);
        } catch (RuntimeException ignored) {
        }
        mDownloadId = -1;
        mDownloadingStatus = false;
    }
}
