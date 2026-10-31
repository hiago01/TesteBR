package com.samp.mobile.launcher;

import android.Manifest;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.provider.Settings;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.util.Log;
import android.widget.ProgressBar;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.downloader.Progress;
import com.google.firebase.crashlytics.FirebaseCrashlytics;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.util.SharedPreferenceCore;
import com.samp.mobile.launcher.util.SecretGesture;
import com.samp.mobile.launcher.util.SignatureChecker;
import com.samp.mobile.launcher.util.Util;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
@Obfuscate
public class SplashActivity extends AppCompatActivity {

    private static final String TAG = "SplashActivity";
    private static final int REQ_PERMISSIONS = 1;
    /** Se o UpdateService nao responder neste prazo, o splash oferece tentar de novo / continuar. */
    private static final long LOAD_TIMEOUT_MS = 30000;

    private final Handler mUiHandler = new Handler(android.os.Looper.getMainLooper());

    // ---- Gesto secreto na Splash: 4 toques rapidos em qualquer lugar LIGAM o "modo sem verificacao" (pula a checagem de
    // atualizacao de dados e de APK novo, vai direto ao menu); com ele ligado, 3 toques DESLIGAM. Vale so para ESTA abertura:
    // o modo nunca fica gravado para as proximas (ver onCreate).
    private static final String PREF_SKIP_CHECKS = "skip_update_checks";
    private static final long SKIP_OPEN_DELAY_MS = 3500;   // tempo para dar os 3 toques e DESLIGAR o modo antes de abrir o menu
    private final SecretGesture mSecretGesture = new SecretGesture();
    private final Runnable mSkipOpen = new Runnable() {
        @Override public void run() { if (!isFinishing() && !isDestroyed()) openMain(); }
    };

    private boolean isSkipChecksEnabled() {
        if (!getResources().getBoolean(R.bool.secret_gesture_enabled)) return false;   // gesto desativado no APK
        try { return getSharedPreferences("com.samp.mobile", MODE_PRIVATE).getBoolean(PREF_SKIP_CHECKS, false); }
        catch (RuntimeException e) { return false; }
    }

    @Override
    public boolean dispatchTouchEvent(android.view.MotionEvent ev) {
        if (ev.getActionMasked() == android.view.MotionEvent.ACTION_UP && mInHandler != null
                && getResources().getBoolean(R.bool.secret_gesture_enabled)) {
            final View root = getWindow().getDecorView();
            // 4 toques ATIVAM o modo sem verificacao; com ele ligado, 3 toques DESLIGAM.
            final int target = isSkipChecksEnabled() ? SecretGesture.TAPS_OFF : SecretGesture.TAPS;
            if (mSecretGesture.onTap(ev.getRawX(), ev.getRawY(), root.getWidth(), root.getHeight(), android.os.SystemClock.uptimeMillis(), target)) {
                final boolean on = !isSkipChecksEnabled();
                try { getSharedPreferences("com.samp.mobile", MODE_PRIVATE).edit().putBoolean(PREF_SKIP_CHECKS, on).apply(); }
                catch (RuntimeException ignored) { }
                Toast.makeText(this, on ? "Modo sem verificacao: ATIVADO (nao checa atualizacao/APK). 3 toques para desativar."
                                        : "Modo sem verificacao: DESATIVADO", Toast.LENGTH_LONG).show();
                mUiHandler.removeCallbacks(mSkipOpen);
                mUiHandler.removeCallbacks(mLoadTimeout);
                if (on) mUiHandler.postDelayed(mSkipOpen, SKIP_OPEN_DELAY_MS);   // sai para o menu depois de dar tempo dos 3 toques que desligam
                else restartStartup();                            // volta a checar
            }
        }
        return super.dispatchTouchEvent(ev);
    }
    private boolean mGpuReady = false;
    private boolean mPermissionRequestInFlight = false;
    private boolean mDialogShowing = false;
    private int mPermissionRequestCount = 0;
    private static final int MAX_AUTO_PERMISSION_REQUESTS = 2;
    private final Runnable mLoadTimeout = new Runnable() {
        @Override
        public void run() {
            if (isFinishing() || isDestroyed()) return;
            showRetryDialog("The launcher could not finish checking for updates.\nCheck your connection and try again.\n\nServer: " + com.samp.mobile.launcher.config.AppEndpoints.clientConfigUrl(SplashActivity.this),
                    "Retry", new Runnable() { @Override public void run() { restartStartup(); } },
                    "Continue", new Runnable() { @Override public void run() { openMain(); } });
        }
    };

    /**
     * Permissoes OBRIGATORIAS para iniciar o launcher. Lista vazia em todas as versoes (API 26+):
     * launcher e jogo gravam somente em getExternalFilesDir() (armazenamento especifico do app),
     * que nao exige permissao de armazenamento; MANAGE_EXTERNAL_STORAGE tambem nao e necessario.
     * RECORD_AUDIO e opcional (so chat de voz) e e pedida no jogo (SAMP.java), nao aqui.
     * Se um dia algo passar a exigir permissao, basta incluir aqui: todo o fluxo abaixo
     * (todas concedidas, negada, cancelada, "nao perguntar mais", retorno das configuracoes) ja trata.
     */
    private String[] getRequiredPermissions() {
        return new String[0];
    }

    public int mGpuType;

    SharedPreferences prefs = null;

    AlertDialog.Builder builder;

    private final ServiceConnection mConnection = new ServiceConnection() {
        public void onServiceConnected(ComponentName componentName, IBinder iBinder) {
            mService = new Messenger(iBinder);
            checkUpdate();
        }

        public void onServiceDisconnected(ComponentName componentName) {
            mService = null;
        }
    };


    public IncomingHandler mInHandler;
    public Messenger mMessenger;
    public Messenger mService;

    private boolean mIsBind;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_splash);

        if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        {
            ((TextView)findViewById(R.id.launcher_orig_text)).setText("Using not original launcher");
        }
        else {
            Config.currentContext = this;

            prefs = getSharedPreferences("com.samp.mobile", MODE_PRIVATE);
            // O modo "sem verificacao" NUNCA vale de uma abertura para outra: sempre verifica o cliente ao abrir.
            // (Antes ficava gravado: 4 toques sem querer na splash faziam o app pular o download em todas as aberturas seguintes.)
            try { prefs.edit().remove(PREF_SKIP_CHECKS).apply(); } catch (RuntimeException ignored) { }

            builder = new AlertDialog.Builder(this);

            if (!Util.isNetworkConnected(Config.currentContext)) {
                builder.setMessage("There is no internet connection.\n" +
                                "Please exit, connect to the internet and reenter the launcher")
                        .setCancelable(false)
                        .setPositiveButton("Ok", new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog, int id) {
                                FirebaseCrashlytics.getInstance().setCrashlyticsCollectionEnabled(false);

                                finishAndRemoveTask();
                                System.exit(0);
                                dialog.dismiss();
                            }
                        })
                        .setNegativeButton("", null);
                AlertDialog alert = builder.create();
                alert.setTitle("Info");
                alert.show();
                return;
            }

            mInHandler = new IncomingHandler();
            mMessenger = new Messenger(mInHandler);

            GLSurfaceView.Renderer mGlRenderer = new GLSurfaceView.Renderer() {
                @Override
                public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
                    UpdateActivity.eGPUType egputype;
                    String glGetString = gl10.glGetString(GL10.GL_EXTENSIONS);
                    String glGetString2 = glGetString == null ? "" : glGetString;
                    if (glGetString2.contains("GL_IMG_texture_compression_pvrtc")) {
                        egputype = UpdateActivity.eGPUType.PVR;
                        mGpuType = 3;
                    } else if (glGetString2.contains("GL_EXT_texture_compression_dxt1") || glGetString2.contains("GL_EXT_texture_compression_s3tc") || glGetString2.contains("GL_AMD_compressed_ATC_texture")) {
                        egputype = UpdateActivity.eGPUType.DXT;
                        mGpuType = 1;
                    } else {
                        egputype = UpdateActivity.eGPUType.ETC;
                        mGpuType = 2;
                    }
                    Log.e("x1y2z", "GPU name: " + glGetString);
                    Log.e("x1y2z", "GPU type: " + egputype.name());
                    try { // usado pela tela CORRECOES para nao tratar textura de outra GPU como "ausente"
                        getSharedPreferences("com.samp.mobile", MODE_PRIVATE).edit().putInt("gpu_type", mGpuType).apply();
                    } catch (RuntimeException ignored) { }

                    // Esta callback roda na thread GL: permissoes, dialogos e bindService vao para a UI thread.
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            mGpuReady = true;
                            proceedIfReady();
                        }
                    });
                }

                @Override
                public void onSurfaceChanged(GL10 gl10, int i, int i1) {

                }

                @Override
                public void onDrawFrame(GL10 gl10) {

                }
            };

            ConstraintLayout gpuLayout = findViewById(R.id.gpu);
            GLSurfaceView mGlSurfaceView = new GLSurfaceView(this);
            mGlSurfaceView.setRenderer(mGlRenderer);
            gpuLayout.addView(mGlSurfaceView);
        }
    }

    public class IncomingHandler extends Handler {
        public IncomingHandler() {}

        @Override
        public void handleMessage(@NonNull Message msg) {
            super.handleMessage(msg);
            if (isFinishing() || isDestroyed()) return;
            if (msg.what == 4) {
                UpdateActivity.UpdateStatus valueOf;
                try {
                    valueOf = UpdateActivity.UpdateStatus.valueOf(msg.getData().getString("status", ""));
                } catch (IllegalArgumentException e) {
                    return;
                }
                if (mService == null) return;
                if (valueOf == UpdateActivity.UpdateStatus.Undefined) {
                    Message obtain = Message.obtain((Handler) null, 5);
                    obtain.replyTo = mMessenger;
                    try {
                        mService.send(obtain);
                    } catch (RemoteException e5) {
                        e5.printStackTrace();
                    }
                } else if (valueOf == UpdateActivity.UpdateStatus.CheckUpdate) {
                    Message obtain2 = Message.obtain((Handler) null, 4);
                    obtain2.replyTo = mMessenger;
                    try {
                        mService.send(obtain2);
                    } catch (RemoteException e6) {
                        e6.printStackTrace();
                    }
                }
            } else if (msg.what == 5) {
                mUiHandler.removeCallbacks(mLoadTimeout);
                UpdateActivity.GameStatus valueOf2;
                try {
                    valueOf2 = UpdateActivity.GameStatus.valueOf(msg.getData().getString("status", ""));
                } catch (IllegalArgumentException e) {
                    valueOf2 = UpdateActivity.GameStatus.Unknown;
                }
                Log.i("x1y2z", "gameStatus = " + valueOf2);
                // modo sem verificacao: ignora "dados/APK desatualizado" e segue para o menu
                if (isSkipChecksEnabled() && valueOf2 != UpdateActivity.GameStatus.Updated) valueOf2 = UpdateActivity.GameStatus.Unknown;

                if (valueOf2 == UpdateActivity.GameStatus.UpdateRequired) {
                    // Atualizacao de dados SEMPRE obrigatoria: um unico botao, sem "Nao", sem cancelar.
                    if (!isFinishing() && !isDestroyed() && !mDialogShowing) {
                        mDialogShowing = true;
                        View v = getLayoutInflater().inflate(R.layout.mx_dialog_update_required, null);
                        final AlertDialog alert = new AlertDialog.Builder(SplashActivity.this)
                                .setView(v).setCancelable(false).create();
                        alert.setCanceledOnTouchOutside(false);
                        v.findViewById(R.id.mx_upd_req_btn).setOnClickListener(new View.OnClickListener() {
                            @Override
                            public void onClick(View view) {
                                mDialogShowing = false;
                                Intent intent = new Intent(SplashActivity.this, UpdateActivity.class);
                                intent.putExtra("mode", UpdateActivity.UpdateMode.GameDataUpdate.name());
                                startActivity(intent);
                                finish();
                                alert.dismiss();
                            }
                        });
                        alert.show();
                        if (alert.getWindow() != null) {
                            alert.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));
                            int w = Math.min((int) (400 * getResources().getDisplayMetrics().density),
                                    (int) (getResources().getDisplayMetrics().widthPixels * 0.9f));
                            alert.getWindow().setLayout(w, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
                        }
                    }
                }
                else if (valueOf2 == UpdateActivity.GameStatus.GameUpdateRequired) {
                    // APK desatualizado: aviso OBRIGATORIO (um botao, sem cancelar) e entao baixa/instala o APK novo.
                    if (!isFinishing() && !isDestroyed() && !mDialogShowing) {
                        mDialogShowing = true;
                        View v = getLayoutInflater().inflate(R.layout.mx_dialog_update_required, null);
                        ((android.widget.TextView) v.findViewById(R.id.mx_upd_req_title)).setText(R.string.mx_upd_app_title);
                        ((android.widget.TextView) v.findViewById(R.id.mx_upd_req_msg)).setText(R.string.mx_upd_app_msg);
                        final AlertDialog alert = new AlertDialog.Builder(SplashActivity.this)
                                .setView(v).setCancelable(false).create();
                        alert.setCanceledOnTouchOutside(false);
                        v.findViewById(R.id.mx_upd_req_btn).setOnClickListener(new View.OnClickListener() {
                            @Override
                            public void onClick(View view) {
                                mDialogShowing = false;
                                Intent intent = new Intent(SplashActivity.this, UpdateActivity.class);
                                intent.putExtra("mode", UpdateActivity.UpdateMode.GameUpdate.name());
                                startActivity(intent);
                                finish();
                                alert.dismiss();
                            }
                        });
                        alert.show();
                        if (alert.getWindow() != null) {
                            alert.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));
                            int w = Math.min((int) (400 * getResources().getDisplayMetrics().density),
                                    (int) (getResources().getDisplayMetrics().widthPixels * 0.9f));
                            alert.getWindow().setLayout(w, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
                        }
                    }
                }
                else {
                    // Updated: nenhuma atualizacao pendente (so este caminho marca "Atualizado")
                    Config.clientUpToDate = (valueOf2 == UpdateActivity.GameStatus.Updated);
                    startActivity(new Intent(SplashActivity.this, MainActivity.class));
                    finish();
                }

            }
        }
    }

    public void checkUpdate() {
        Log.d("x1y2z", "checkUpdate");
        if (mService == null) return;
        Message obtain = Message.obtain((Handler) null, 0);
        obtain.getData().putInt("gputype", mGpuType);
        obtain.replyTo = mMessenger;
        try {
            mService.send(obtain);
        } catch (RemoteException e5) {
            e5.printStackTrace();
        }
    }

    // ------------------------------------------------------------------------------------
    // Permissoes e inicializacao do UpdateService
    // ------------------------------------------------------------------------------------

    /** true somente se TODAS as permissoes obrigatorias estiverem concedidas. */
    public boolean isPermissionsGranted()
    {
        return getMissingPermissions().isEmpty();
    }

    private List<String> getMissingPermissions() {
        List<String> missing = new ArrayList<>();
        for (String permission : getRequiredPermissions()) {
            if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }
        return missing;
    }

    /** Ponto unico de avanco: so conecta ao UpdateService quando GPU detectada e permissoes OK. */
    private void proceedIfReady() {
        if (!mGpuReady || mIsBind || mDialogShowing || mPermissionRequestInFlight || isFinishing() || isDestroyed()) return;
        if (mInHandler == null) return; // assinatura invalida / sem internet: fluxo nao iniciado

        List<String> missing = getMissingPermissions();
        if (missing.isEmpty()) {
            startUpdateService();
        } else if (mPermissionRequestCount < MAX_AUTO_PERMISSION_REQUESTS) {
            mPermissionRequestCount++;
            mPermissionRequestInFlight = true;
            ActivityCompat.requestPermissions(this, missing.toArray(new String[0]), REQ_PERMISSIONS);
        } else {
            showPermissionDenied(missing);
        }
    }

    // ---- APK novo: apaga os arquivos dos clientes antigos e deixa o fluxo normal baixar tudo de novo (PRDownloader) ----
    private static final String PREF_WIPED_VC = "wiped_for_version_code";
    private boolean mWipeDone = false;

    private int currentVersionCode() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionCode; } catch (Throwable t) { return -1; }
    }

    /** true = esta e a 1a abertura deste versionCode e o recurso wipe_files_on_new_apk esta ligado (endpoints.xml). */
    private boolean needsWipe() {
        try {
            if (!getResources().getBoolean(R.bool.wipe_files_on_new_apk)) return false;
            final int vc = currentVersionCode();
            return vc > 0 && getSharedPreferences("com.samp.mobile", MODE_PRIVATE).getInt(PREF_WIPED_VC, -1) != vc;
        } catch (Throwable t) { return false; }
    }

    private void wipeThenStart() {
        Toast.makeText(this, "Preparando instalacao nova: removendo arquivos antigos...", Toast.LENGTH_LONG).show();
        final java.io.File dir = getExternalFilesDir(null);
        final int vc = currentVersionCode();
        new Thread(new Runnable() {
            @Override public void run() {
                String res = "";
                try { res = com.samp.mobile.launcher.pack.ClientWipe.wipe(dir).toString(); }
                catch (Throwable t) { Log.e(TAG, "ClientWipe falhou", t); res = "erro " + t; }
                Log.i(TAG, "ClientWipe (versionCode " + vc + "): " + res);
                try { getSharedPreferences("com.samp.mobile", MODE_PRIVATE).edit().putInt(PREF_WIPED_VC, vc).apply(); } catch (Throwable ignored) {}
                runOnUiThread(new Runnable() { @Override public void run() {
                    mWipeDone = true;
                    if (!isFinishing() && !isDestroyed()) startUpdateService();
                } });
            }
        }, "client-wipe").start();
    }

    private void startUpdateService() {
        if (!isSkipChecksEnabled() && !mWipeDone && needsWipe()) { wipeThenStart(); return; }
        if (isSkipChecksEnabled()) {
            // modo sem verificacao: nao conecta ao UpdateService (sem checar dados nem APK). Pequena espera para o gesto de desligar.
            Config.clientUpToDate = false;
            Toast.makeText(this, "Modo sem verificacao ATIVO (4 toques na tela para desativar)", Toast.LENGTH_SHORT).show();
            mUiHandler.removeCallbacks(mSkipOpen);
            mUiHandler.postDelayed(mSkipOpen, SKIP_OPEN_DELAY_MS);
            return;
        }
        mIsBind = bindService(new Intent(this, UpdateService.class), mConnection, Context.BIND_AUTO_CREATE);
        if (!mIsBind) {
            Log.e(TAG, "bindService(UpdateService) failed");
            showRetryDialog("Could not start the update service.", "Retry", new Runnable() { @Override public void run() { restartStartup(); } },
                    "Continue", new Runnable() { @Override public void run() { openMain(); } });
            return;
        }
        mUiHandler.removeCallbacks(mLoadTimeout);
        mUiHandler.postDelayed(mLoadTimeout, LOAD_TIMEOUT_MS);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_PERMISSIONS) {
            return;
        }
        mPermissionRequestInFlight = false;

        // grantResults vazio = pedido cancelado/interrompido. Valida TODOS os resultados.
        boolean allGranted = grantResults.length > 0 && grantResults.length >= permissions.length;
        for (int r : grantResults) {
            if (r != PackageManager.PERMISSION_GRANTED) allGranted = false;
        }
        if (allGranted && isPermissionsGranted()) {
            proceedIfReady();
            return;
        }
        List<String> missing = getMissingPermissions();
        if (missing.isEmpty()) {
            proceedIfReady();
        } else {
            showPermissionDenied(missing);
        }
    }

    private void showPermissionDenied(List<String> missing) {
        boolean canAskAgain = false;
        for (String p : missing) {
            if (ActivityCompat.shouldShowRequestPermissionRationale(this, p)) canAskAgain = true;
        }
        final boolean again = canAskAgain;
        String msg = "The launcher needs the following permission(s) to run:\n" + missing + "\n" +
                (again ? "Please allow them to continue." : "Permission was denied permanently. Open the app settings and allow it, then come back.");
        showRetryDialog(msg,
                again ? "Try again" : "Open settings",
                new Runnable() {
                    @Override
                    public void run() {
                        if (again) {
                            mPermissionRequestCount = 0; // nova tentativa explicita do usuario
                            proceedIfReady();
                        } else {
                            Intent i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", getPackageName(), null));
                            startActivity(i); // ao voltar, onResume() reavalia
                        }
                    }
                },
                "Exit",
                new Runnable() {
                    @Override
                    public void run() {
                        finishAndRemoveTask();
                    }
                });
    }

    /** Dialogo de 2 botoes; nunca empilha dois dialogos. */
    private void showRetryDialog(String message, String positive, final Runnable onPositive, String negative, final Runnable onNegative) {
        if (isFinishing() || isDestroyed() || mDialogShowing) return;
        mDialogShowing = true;
        AlertDialog.Builder b = new AlertDialog.Builder(this);
        b.setMessage(message).setCancelable(false)
                .setPositiveButton(positive, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        mDialogShowing = false;
                        dialog.dismiss();
                        onPositive.run();
                    }
                })
                .setNegativeButton(negative, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        mDialogShowing = false;
                        dialog.dismiss();
                        onNegative.run();
                    }
                });
        AlertDialog alert = b.create();
        alert.setTitle("Info");
        alert.show();
    }

    private void restartStartup() {
        mUiHandler.removeCallbacks(mLoadTimeout);
        if (mIsBind) {
            unbindService(mConnection);
            mIsBind = false;
        }
        mService = null;
        mPermissionRequestCount = 0;
        proceedIfReady();
    }

    private void openMain() {
        mUiHandler.removeCallbacks(mLoadTimeout);
        startActivity(new Intent(SplashActivity.this, MainActivity.class));
        finish();
    }

    @Override
    public void onDestroy() {
        mUiHandler.removeCallbacksAndMessages(null);
        super.onDestroy();
        if(mIsBind) {
            unbindService(mConnection);
            mIsBind = false;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (prefs == null) return; // assinatura invalida: onCreate nao inicializou o fluxo

        if (prefs.getBoolean("firstrun", true)) {
            new SharedPreferenceCore().setInt(getApplicationContext(), "FPS_LIMIT", 60);
            new SharedPreferenceCore().setInt(getApplicationContext(), "MESSAGE_COUNT", 6);
            new SharedPreferenceCore().setBoolean(getApplicationContext(), "AIM", false);
            new SharedPreferenceCore().setBoolean(getApplicationContext(), "MODIFIED_DATA", false);
            new SharedPreferenceCore().setBoolean(getApplicationContext(), "AML", false);
            new SharedPreferenceCore().setBoolean(getApplicationContext(), "CLEO", false);
            new SharedPreferenceCore().setBoolean(getApplicationContext(), "MLOADER", false);
            new SharedPreferenceCore().setInt(getApplicationContext(), "VERSION", 0);
            prefs.edit().putBoolean("firstrun", false).commit();
        }

        com.samp.mobile.launcher.util.FixedSettings.apply(this);

        // Retorno das configuracoes do Android (ou de um dialogo): reavalia se ja pode prosseguir.
        proceedIfReady();
    }
}
