package com.maximo.roleplay.launcher;

import android.app.Service;
import android.content.Intent;
import android.app.Notification;
import android.content.pm.PackageInfo;
import android.content.pm.ServiceInfo;
import android.os.Build;
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

import androidx.core.content.ContextCompat;

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
import com.downloader.Progress;
import com.joom.paranoid.Obfuscate;
import com.maximo.roleplay.launcher.config.AppEndpoints;
import com.maximo.roleplay.launcher.pack.FileList;
import com.maximo.roleplay.launcher.pack.GameFilePolicy;
import com.maximo.roleplay.launcher.pack.PackException;
import com.maximo.roleplay.launcher.util.NotificationHelper;

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
 *  4 status do update | 5 status do jogo | 6 (NOVO) erro com texto, para a Activity oferecer "tentar de novo" |
 *  8 progresso da verificacao SHA-256 dos arquivos (currentfile/totalfiles).
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
    /** Servico -> Splash: andamento da conferencia de SHA-256 (currentfile / totalfiles). */
    public static final int MSG_VERIFY_PROGRESS = 8;
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
    /** Verificacao do APK novo (maximo_config.json: launcher_sha256 / launcher_size). Vazio/0 = sem verificacao (servidor antigo). */
    public String mUpdateGameSha = "";
    public long mUpdateGameSize = 0;

    public ArrayList<String> mUpdateFiles = new ArrayList<>();
    public ArrayList<String> mUpdateFilesName = new ArrayList<>();
    public ArrayList<Long> mUpdateFilesSize = new ArrayList<>();
    /** SHA-256 esperado de cada arquivo (da lista de arquivos; "" = sem hash, so confere o tamanho). */
    public ArrayList<String> mUpdateFilesSha = new ArrayList<>();
    /** Link de cada arquivo ("" = AppEndpoints.fileUrl(path)). */
    public ArrayList<String> mUpdateFilesUrl = new ArrayList<>();

    public int mGpuType = 0;

    private volatile int mSession = 0;   // lido tambem pela thread do SHA-256
    private int mDownloadId = -1;

    // estado do download de dados do jogo (somente na HandlerThread)
    private ArrayList<String> mDlFiles = new ArrayList<>();
    private ArrayList<Long> mDlSizes = new ArrayList<>();
    private ArrayList<String> mDlShas = new ArrayList<>();
    private ArrayList<String> mDlUrls = new ArrayList<>();
    private ArrayList<String> mDlFailed = new ArrayList<>();
    private int mDlIndex = 0;
    private int mDlConsecutiveFailures = 0;
    private long mDlDoneBytes = 0;

    /** Conferencia de SHA-256 a cada abertura: roda fora da HandlerThread para o servico continuar respondendo. */
    private ExecutorService mVerifyWorker;

    @Override
    public void onCreate() {
        super.onCreate();
        mHandlerThread = new HandlerThread("ServiceStartArguments", 10);
        mHandlerThread.start();
        try {
            DownloaderInit.ensure(this);
        } catch (RuntimeException e) {
            Log.e(TAG, "PRDownloader init failed: " + e);
        }
        mQueue = Volley.newRequestQueue(getApplicationContext());
        mInHandler = new IncomingHandler(mHandlerThread.getLooper());
        mMessenger = new Messenger(mInHandler);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mMessenger.getBinder();
    }

    // ---- servico em primeiro plano (dataSync) enquanto baixa: sem isso o Android 14/15 congela/mata o processo
    // quando o app vai para segundo plano ou a tela apaga, e o download para no meio.
    private static final String ACTION_FG = "com.maximo.roleplay.action.DOWNLOAD_FG";
    private static final long FG_STOP_DELAY_MS = 3000;   // debounce: as rodadas automaticas de retentativa alternam false->true
    private final Handler mMainH = new Handler(Looper.getMainLooper());
    private boolean mFgOn = false;                       // somente na thread principal
    private final Runnable mStopFg = new Runnable() {
        @Override public void run() {
            if (!mFgOn) return;
            mFgOn = false;
            try { stopForeground(false); } catch (RuntimeException ignored) { }
            NotificationHelper.cancelDownloadIfOngoing(UpdateService.this);
            stopSelf();   // ainda ligado a uma Activity: continua vivo ate o unbind, como antes
        }
    };

    /** Unico ponto que muda mDownloadingStatus (qualquer thread); liga/desliga o primeiro plano na thread principal. */
    private void setDownloading(final boolean on) {
        mDownloadingStatus = on;
        mMainH.post(new Runnable() {
            @Override public void run() { applyForeground(on); }
        });
    }

    private void applyForeground(boolean on) {
        if (on) {
            mMainH.removeCallbacks(mStopFg);
            if (mFgOn) return;
            try {
                ContextCompat.startForegroundService(this, new Intent(this, UpdateService.class).setAction(ACTION_FG));
                mFgOn = true;
            } catch (RuntimeException e) {     // ex.: ForegroundServiceStartNotAllowedException com o app em segundo plano
                Log.w(TAG, "startForegroundService falhou: " + e);
            }
        } else if (mFgOn) {
            mMainH.removeCallbacks(mStopFg);
            mMainH.postDelayed(mStopFg, FG_STOP_DELAY_MS);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_FG.equals(intent.getAction())) {
            try {
                final Notification n = NotificationHelper.buildDownloadForeground(this);
                if (Build.VERSION.SDK_INT >= 29) startForeground(NotificationHelper.downloadNotificationId(), n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
                else startForeground(NotificationHelper.downloadNotificationId(), n);
            } catch (RuntimeException e) {
                Log.w(TAG, "startForeground falhou: " + e);
                mFgOn = false;
            }
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        // Ninguem esta mais ligado: cancela rede/downloads e encerra a thread (sem threads vazadas).
        mMainH.removeCallbacksAndMessages(null);
        if (mFgOn) { mFgOn = false; try { stopForeground(true); } catch (RuntimeException ignored) { } }
        mSession++;
        stopVerifyWorker();
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
        setDownloading(false);
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
        mUpdateFilesUrl = new ArrayList<>();
        mUpdateGameDataSize = 0;          // original acumulava entre verificacoes
        mUpdateGameDataSizeUpdated = 0;
        mGameStatus = UpdateActivity.GameStatus.Unknown;

        setUpdateStatus(UpdateActivity.UpdateStatus.CheckUpdate);
        loadMaximoConfig(true, session, new Runnable() {
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

    /** Baixa maximo_config.json (e, se withFiles, a lista de arquivos) de forma assincrona. */
    private void loadMaximoConfig(final boolean withFiles, final int session, final Runnable onSuccess) {
        mQueue.add(newRequest(AppEndpoints.maximoConfigUrl(this), session, new Response.Listener<String>() {
            @Override
            public void onResponse(String response) {
                try {
                    JSONObject jSONObject = new JSONObject(response);
                    // aceita os campos na raiz ou dentro de "client_config" (formato do maximo_config.json do site)
                    if (jSONObject.optJSONObject("client_config") != null) jSONObject = jSONObject.getJSONObject("client_config");
                    mUpdateVersion = jSONObject.getInt("version_code");
                    mUpdateGameURL = jSONObject.getString("url_launcher");
                    mUpdateGameSha = jSONObject.optString("launcher_sha256", "").trim().toLowerCase(java.util.Locale.ROOT);
                    if (mUpdateGameSha.length() != 64) mUpdateGameSha = "";
                    mUpdateGameSize = jSONObject.optLong("launcher_size", 0);
                    if (withFiles) {
                        // url_cache_files ausente => <base>/files_testes.json; presente e que nao seja http(s) => erro.
                        final String filesUrl;
                        if (jSONObject.isNull("url_cache_files")) {
                            filesUrl = AppEndpoints.filesListUrl(UpdateService.this);
                        } else {
                            filesUrl = jSONObject.getString("url_cache_files").trim();
                            if (!isHttpUrl(filesUrl)) {
                                failCheck("maximo_config.json: url_cache_files invalido");
                                return;
                            }
                        }
                        getFilesInfo(filesUrl, session, onSuccess);
                    } else {
                        onSuccess.run();
                    }
                } catch (JSONException e) {
                    Log.e(TAG, "maximo_config parse error", e);
                    failCheck("maximo_config.json is invalid (" + e.getMessage() + ")");
                }
            }
        }, "maximo_config"));
    }

    /** Baixa e interpreta a lista de arquivos; sem espera ativa, sem variaveis globais compartilhadas. */
    public void getFilesInfo(String url, final int session, final Runnable onSuccess) {
        mQueue.add(newRequest(url, session, new Response.Listener<String>() {
            @Override
            public void onResponse(String body) {
                ArrayList<HashJob> toHash;
                try {
                    toHash = parseFilesInfo(body);
                } catch (PackException e) {
                    Log.e(TAG, "files list parse error", e);
                    failCheck("File list is invalid (" + e.getMessage() + ")");
                    return;
                } catch (IllegalStateException e) {
                    failCheck(e.getMessage());
                    return;
                }
                verifyHashes(toHash, session, onSuccess);
            }
        }, "file list"));
    }

    /** Arquivo que tem o tamanho certo: so o SHA-256 diz se esta integro. */
    private static final class HashJob {
        final FileList.Entry entry;
        HashJob(FileList.Entry entry) { this.entry = entry; }
    }

    private void addPending(FileList.Entry e) {
        mUpdateFiles.add(e.path);
        mUpdateFilesName.add(e.name);
        mUpdateFilesSize.add(e.size);
        mUpdateFilesSha.add(e.hasSha() ? e.sha256 : "");
        mUpdateFilesUrl.add(e.url);
        mUpdateGameDataSize = mUpdateGameDataSize + e.size;
    }

    /**
     * Passo 1 (rapido): ausente ou (arquivo conferido) de tamanho diferente do servidor = pendente. Arquivo com
     * "verify": false so entra se faltar. Os de tamanho certo que tem sha256 voltam para a conferencia de SHA-256
     * (verifyHashes). Nada e gravado em disco, exceto as pastas vazias da lista.
     */
    private ArrayList<HashJob> parseFilesInfo(String body) throws PackException {
        File base = getExternalFilesDir(null);
        if (base == null) throw new IllegalStateException("Storage is not available");
        FileList.Parsed list = FileList.parse(body);
        Log.d("x1y2z", "Length: " + list.files.size());
        for (String w : list.warnings) Log.w(TAG, w);
        FileList.ensureDirs(base, list.dirs);
        ArrayList<HashJob> toHash = new ArrayList<>();
        for (FileList.Entry e : list.files) {
            // Texturas de outra GPU nao sao usadas neste aparelho.
            if (!GameFilePolicy.wantedForDevice(e.path, mGpuType)) continue;
            String why = FileList.whyNeeded(base, e);
            if (why != null) {
                Log.d("x1y2z", "pendente: " + e.path + " (" + why + ")");
                addPending(e);
            } else if (e.hasSha() && e.size > 0) {
                toHash.add(new HashJob(e));
            }
        }
        Log.d("x1y2z", "faltando/tamanho errado: " + mUpdateFiles.size() + " arquivos; SHA-256 a conferir: " + toHash.size());
        return toHash;
    }

    /**
     * Passo 2 (a cada abertura): SHA-256 de todo arquivo com tamanho certo, numa thread propria. Divergente = pendente,
     * e o PRDownloader baixa de novo (o arquivo velho e apagado antes). Informa o andamento a Splash (MSG_VERIFY_PROGRESS).
     */
    private void verifyHashes(final ArrayList<HashJob> jobs, final int session, final Runnable onSuccess) {
        if (jobs.isEmpty()) {
            onSuccess.run();
            return;
        }
        final File base = getExternalFilesDir(null);
        stopVerifyWorker();
        ExecutorService worker = Executors.newSingleThreadExecutor(new ThreadFactory() {
            @Override
            public Thread newThread(Runnable r) {
                Thread t = new Thread(r, "sha-verify");
                t.setDaemon(true);
                return t;
            }
        });
        mVerifyWorker = worker;
        worker.execute(new Runnable() {
            @Override
            public void run() {
                final ArrayList<HashJob> bad = new ArrayList<>();
                final int total = jobs.size();
                long lastReport = 0;
                for (int i = 0; i < total; i++) {
                    if (session != mSession) return;   // verificacao cancelada/substituida
                    HashJob j = jobs.get(i);
                    String why = verifyDownloaded(new File(base, j.entry.path), j.entry.size, j.entry.sha256);
                    if (why != null) {
                        Log.w(TAG, "verificacao: " + j.entry.path + " -> " + why);
                        bad.add(j);
                    }
                    long now = System.currentTimeMillis();
                    if (now - lastReport >= 200) {
                        lastReport = now;
                        final int done = i + 1;
                        postToService(new Runnable() {
                            @Override
                            public void run() {
                                if (session != mSession) return;
                                Message obtain = Message.obtain(mInHandler, MSG_VERIFY_PROGRESS);
                                obtain.getData().putLong("currentfile", done);
                                obtain.getData().putLong("totalfiles", total);
                                sendToActivity(obtain);
                            }
                        });
                    }
                }
                postToService(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        mVerifyWorker = null;
                        for (HashJob j : bad) addPending(j.entry);
                        Log.d("x1y2z", "SHA-256 conferido: " + total + " arquivos, " + bad.size() + " divergentes");
                        if (!bad.isEmpty()) clLog("verificacao SHA-256: " + bad.size() + " arquivo(s) divergente(s)");
                        onSuccess.run();
                    }
                });
            }
        });
    }

    private void stopVerifyWorker() {
        if (mVerifyWorker != null) {
            mVerifyWorker.shutdownNow();
            mVerifyWorker = null;
        }
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
     * Ha APK novo quando o versionCode do maximo_config.json e DIFERENTE do instalado OU (com launcher_sha256 no maximo_config.json)
     * quando o APK instalado nao e byte a byte o publicado no servidor - assim trocar o launcher.apk sem mexer no
     * version_code tambem forca a atualizacao. Se o hash do instalado nao puder ser lido, vale so o versionCode.
     */
    public boolean isGameUpdateExists() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo("com.maximo.roleplay", 0);
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
            // Servico recriado (a Activity anterior desligou antes da nova ligar): recarrega so o maximo_config.json.
            loadMaximoConfig(false, session, new Runnable() {
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
            reportError("Invalid launcher update URL in maximo_config.json", false);
            return;
        }
        final File file = new File(dir, "launcher.apk");
        if (file.exists()) {
            file.delete();
        }

        setDownloading(true);
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
                        setDownloading(false);
                        mDownloadId = -1;
                        // APK novo: confere tamanho/SHA-256 do maximo_config.json ANTES de oferecer a instalacao.
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
                        setDownloading(false); // sempre volta a false
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
        mDlUrls = new ArrayList<>(mUpdateFilesUrl);
        mDlFailed = new ArrayList<>();
        mDlAutoRounds = 0;
        mUpdateFiles.clear();
        mUpdateFilesName.clear();
        mUpdateFilesSize.clear();
        mUpdateFilesSha.clear();
        mUpdateFilesUrl.clear();
        mDlIndex = 0;
        mDlDoneBytes = 0;
        mDlConsecutiveFailures = 0;
        String noSpace = checkFreeSpace();
        if (noSpace != null) {
            restorePending(mDlFiles);
            clLog(noSpace);
            reportError(noSpace, false);
            return;
        }
        downloadNextFile(session, 1);
    }

    /** Margem alem do tamanho dos arquivos a baixar (gravacao temporaria do PRDownloader + o jogo precisa de folga). */
    private static final long FREE_SPACE_MARGIN = 100L * 1024 * 1024;

    /** null = ha espaco (ou nao foi possivel medir); senao a mensagem de erro. Roda na HandlerThread do servico. */
    private String checkFreeSpace() {
        File base = getExternalFilesDir(null);
        if (base == null) return null;
        long need = FREE_SPACE_MARGIN;
        for (int i = 0; i < mDlFiles.size(); i++) {
            need += i < mDlSizes.size() ? mDlSizes.get(i) : 0L;
        }
        try {
            long free = new StatFs(base.getAbsolutePath()).getAvailableBytes();
            if (free < need) {
                return "Espaco insuficiente: livre " + (free >> 20) + " MB, necessario " + (need >> 20) + " MB. Libere espaco e tente de novo.";
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "nao foi possivel medir o espaco livre: " + e);
        }
        return null;
    }

    /** Registra no Crashlytics (nao fatal) o motivo de falhas de download/instalacao; nunca derruba o servico. */
    private static void clLog(String msg) {
        try {
            com.google.firebase.crashlytics.FirebaseCrashlytics cl = com.google.firebase.crashlytics.FirebaseCrashlytics.getInstance();
            cl.log("[download] " + msg);
            cl.recordException(new RuntimeException("download: " + msg));
        } catch (Throwable ignored) { }
    }

    /** Devolve a lista de pendentes (com tamanho e sha, mantendo as listas paralelas coerentes) para uma nova tentativa. */
    private void restorePending(List<String> paths) {
        for (String p : paths) {
            int i = mDlFiles.indexOf(p);
            mUpdateFiles.add(p);
            mUpdateFilesName.add(p.substring(p.lastIndexOf('/') + 1));
            mUpdateFilesSize.add(i >= 0 && i < mDlSizes.size() ? mDlSizes.get(i) : 0L);
            mUpdateFilesSha.add(i >= 0 && i < mDlShas.size() ? mDlShas.get(i) : "");
            mUpdateFilesUrl.add(i >= 0 && i < mDlUrls.size() ? mDlUrls.get(i) : "");
        }
    }

    private void downloadNextFile(final int session, final int attempt) {
        if (session != mSession) return;
        if (mDlIndex >= mDlFiles.size()) {
            finishDataUpdating(session);
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

        setDownloading(true);
        final String fileName = target.getName();
        final long[] last = { System.currentTimeMillis() };
        mDownloadId = PRDownloader.download(urlFor(rel, mDlUrls.get(mDlIndex)), parent.getAbsolutePath(), fileName).build().setOnStartOrResumeListener(null).setOnPauseListener(null).setOnCancelListener(null).setOnProgressListener(new OnProgressListener() {
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
                        setDownloading(false);
                        mDownloadId = -1;
                        // PRDownloader diz "completo" mesmo com arquivo cortado/errado: confere tamanho (e SHA-256, se a lista tiver).
                        final File done = new File(parentOf(rel), fileName);
                        String why = verifyDownloaded(done, mDlSizes.get(mDlIndex), mDlShas.get(mDlIndex));
                        if (why != null) {
                            Log.w("x1y2z", "arquivo invalido " + rel + ": " + why + " (attempt " + attempt + ")");
                            if (attempt >= MAX_FILE_ATTEMPTS) clLog("arquivo invalido " + rel + ": " + why);
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
                        setDownloading(false); // sempre volta a false
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

    /** Link da propria lista ("url"); sem ele, <base>/<prefixo de arquivos>/<path>. */
    private String urlFor(String rel, String listed) {
        if (isHttpUrl(listed)) return listed;
        return AppEndpoints.fileUrl(this, Uri.encode(rel, "/"));
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
                    byte[] buf = new byte[1024 * 1024];
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
        clLog("desistiu de " + rel + " apos " + MAX_FILE_ATTEMPTS + " tentativas");
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

    /** Rodadas automaticas de nova tentativa dos arquivos que falharam (rede instavel), antes de mostrar qualquer erro. */
    private static final int MAX_AUTO_ROUNDS = 6;
    private static final long AUTO_ROUND_DELAY_MS = 5000;
    private int mDlAutoRounds = 0;

    private void finishDataUpdating(final int session) {
        setDownloading(false);
        if (!mDlFailed.isEmpty() && mDlAutoRounds < MAX_AUTO_ROUNDS) {
            // Tenta de novo SOZINHO os arquivos que falharam (5 s, 10 s, 15 s...), sem mostrar mensagem ao jogador.
            mDlAutoRounds++;
            ArrayList<String> files = new ArrayList<>();
            ArrayList<Long> sizes = new ArrayList<>();
            ArrayList<String> shas = new ArrayList<>();
            ArrayList<String> urls = new ArrayList<>();
            for (String p : mDlFailed) {
                int i = mDlFiles.indexOf(p);
                if (i < 0) continue;
                files.add(p);
                sizes.add(i < mDlSizes.size() ? mDlSizes.get(i) : 0L);
                shas.add(i < mDlShas.size() ? mDlShas.get(i) : "");
                urls.add(i < mDlUrls.size() ? mDlUrls.get(i) : "");
            }
            if (!files.isEmpty()) {
                Log.w(TAG, "nova tentativa automatica " + mDlAutoRounds + "/" + MAX_AUTO_ROUNDS + ": " + files.size() + " arquivo(s)");
                mDlFiles = files;
                mDlSizes = sizes;
                mDlShas = shas;
                mDlUrls = urls;
                mDlFailed = new ArrayList<>();
                mDlIndex = 0;
                mDlConsecutiveFailures = 0;
                setDownloading(true); // continua "baixando" durante a espera
                mInHandler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        if (session != mSession) return;
                        downloadNextFile(session, 1);
                    }
                }, AUTO_ROUND_DELAY_MS * mDlAutoRounds);
                return;
            }
        }
        if (!mDlFailed.isEmpty()) {
            restorePending(mDlFailed); // continuam pendentes para a proxima verificacao
            int n = mDlFailed.size();
            reportError(n + " game file(s) could not be downloaded.", false);
            return;
        }
        Log.d("UpdateService", "updateGameData()");
        notifyGameDataDone();
    }

    /** Cancela qualquer download em andamento e invalida callbacks pendentes. */
    private void cancelDownloads() {
        mSession++; // callbacks e retries atrasados conferem a sessao e se descartam
        stopVerifyWorker();
        if (mQueue != null) mQueue.cancelAll(REQUEST_TAG);
        try {
            if (mDownloadId != -1) PRDownloader.cancel(mDownloadId);
        } catch (RuntimeException ignored) {
        }
        mDownloadId = -1;
        setDownloading(false);
    }
}
