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

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;

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

    public ArrayList<String> mUpdateFiles = new ArrayList<>();
    public ArrayList<String> mUpdateFilesName = new ArrayList<>();
    public ArrayList<Long> mUpdateFilesSize = new ArrayList<>();

    public int mGpuType = 0;

    private int mSession = 0;
    private int mDownloadId = -1;

    // estado do download de dados do jogo (somente na HandlerThread)
    private ArrayList<String> mDlFiles = new ArrayList<>();
    private ArrayList<Long> mDlSizes = new ArrayList<>();
    private ArrayList<String> mDlFailed = new ArrayList<>();
    private int mDlIndex = 0;
    private int mDlConsecutiveFailures = 0;
    private long mDlDoneBytes = 0;

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
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mMessenger.getBinder();
    }

    @Override
    public void onDestroy() {
        // Ninguem esta mais ligado: cancela rede/downloads e encerra a thread (sem threads vazadas).
        mSession++;
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
                            failCheck(what + ": invalid response");
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
                        failCheck("Could not reach the update server (" + what + ")");
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
                    if (withFiles) {
                        String string = jSONObject.getString("url_cache_files");
                        if (!isHttpUrl(string)) {
                            failCheck("client_config: invalid url_cache_files");
                            return;
                        }
                        getFilesInfo(string, session, onSuccess);
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

    private void parseFilesInfo(String body) throws JSONException {
        File base = getExternalFilesDir(null);
        if (base == null) throw new IllegalStateException("Storage is not available");
        JSONArray jsonArray = new JSONObject(body).getJSONArray("files");
        Log.d("x1y2z", "Length: " + jsonArray.length());
        for(int i = 0; i<jsonArray.length(); i++) {
            JSONObject o = jsonArray.getJSONObject(i);
            FilesData fileData = new FilesData(o.getString("name"), o.getLong("size"), o.getString("path"), o.optString("url", ""));
            if (!fileData.getName().equals("samp_log.txt") && !fileData.getName().equals("svlog.txt") && !fileData.getName().equals("gtasatelem.set")) {
                if (!fileData.getName().equals("GTASAMP10.b") && !fileData.getName().equals(".htaccess")) {
                    if (!fileData.getName().equals("gta_sa.set")) {
                        if (!fileData.getName().equals("settings.ini")) {
                            File file = new File(base, fileData.getPath());
                            if (!file.exists() || file.length() != fileData.getSize()) {
                                if(!fileData.getPath().contains("player") && !fileData.getPath().contains("playerhi") && !fileData.getPath().contains("menu") && !fileData.getPath().contains("samp")) {
                                    if ((fileData.getPath().contains(".dxt.") && mGpuType != 1))
                                        continue;
                                    else if ((fileData.getPath().contains(".etc.") && mGpuType != 2))
                                        continue;
                                    else if ((fileData.getPath().contains(".pvr.") && mGpuType != 3))
                                        continue;
                                }

                                mUpdateFiles.add(fileData.getPath());
                                mUpdateFilesName.add(fileData.getName());
                                mUpdateFilesSize.add(fileData.getSize());
                                mUpdateGameDataSize = mUpdateGameDataSize + fileData.getSize();
                            }
                        }
                    }
                }
            }
        }
        Log.d("x1y2z", "Data size: " + mUpdateGameDataSize + " files: " + mUpdateFiles.size());
    }

    public boolean isGameUpdateExists() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo("com.samp.mobile", 0);
            Log.d("x1y2z", "isGameUpdateExists -> currentVersion " + packageInfo.versionCode + " | mUpdateVersion " + this.mUpdateVersion);
            return packageInfo.versionCode != this.mUpdateVersion;
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
        mDlFailed = new ArrayList<>();
        mUpdateFiles.clear();
        mUpdateFilesName.clear();
        mUpdateFilesSize.clear();
        mDlIndex = 0;
        mDlDoneBytes = 0;
        mDlConsecutiveFailures = 0;
        downloadNextFile(session, 1);
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

        mDownloadingStatus = true;
        final String fileName = target.getName();
        final long[] last = { System.currentTimeMillis() };
        mDownloadId = PRDownloader.download(AppEndpoints.fileUrl(this, Uri.encode(rel, "/")), parent.getAbsolutePath(), fileName).build().setOnStartOrResumeListener(null).setOnPauseListener(null).setOnCancelListener(null).setOnProgressListener(new OnProgressListener() {
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
                        mDownloadingStatus = false;
                        mDownloadId = -1;
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
        if (!mDlFailed.isEmpty()) {
            mUpdateFiles.addAll(mDlFailed); // continuam pendentes para a proxima verificacao
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
        if (mQueue != null) mQueue.cancelAll(REQUEST_TAG);
        try {
            if (mDownloadId != -1) PRDownloader.cancel(mDownloadId);
        } catch (RuntimeException ignored) {
        }
        mDownloadId = -1;
        mDownloadingStatus = false;
    }
}
