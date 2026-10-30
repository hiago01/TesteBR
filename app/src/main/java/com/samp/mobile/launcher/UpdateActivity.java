package com.samp.mobile.launcher;

import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.ServiceConnection;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.FileUtils;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.provider.Settings;
import android.util.Log;
import android.view.WindowManager;
import android.view.View;
import android.widget.ImageButton;
import com.samp.mobile.launcher.util.LauncherUi;
import com.samp.mobile.launcher.util.MaximoMusic;
import com.samp.mobile.launcher.util.NotificationHelper;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.content.FileProvider;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.config.Config;
import com.samp.mobile.launcher.util.SignatureChecker;

import java.io.File;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
@Obfuscate
public class UpdateActivity extends AppCompatActivity{
    public Messenger mMessenger = new Messenger(new IncomingHandler());
    public Messenger mService;
    boolean isBind = false;
    private UpdateMode mUpdateMode = UpdateMode.Undefined;
    public int mGpuType;

    private File mGameApk;

    boolean mIsStartingUpdate = false;

    private boolean mIsBind;

    private static final int REQ_UNKNOWN_SOURCES = 2;
    private boolean mErrorDialogShowing = false;

    public enum UpdateMode {
        Undefined,
        GameDataUpdate,
        GameUpdate
    }

    public enum GameStatus {
        Unknown,
        GameUpdateRequired,
        UpdateRequired,
        Updated
    }

    public enum UpdateStatus {
        Undefined,
        CheckUpdate,
        CheckFiles,
        DownloadGame,
        DownloadGameData
    }

    public enum eGPUType {
        DXT,
        PVR,
        ETC
    }

    class IncomingHandler extends Handler {
        @Override
        public void handleMessage(Message msg) {
            if (msg.what == 4) {
                UpdateActivity.UpdateStatus valueOf = UpdateActivity.UpdateStatus.valueOf(msg.getData().getString("status", ""));
                long j = msg.getData().getLong("total");
                long j2 = msg.getData().getLong("current");
                String fname = msg.getData().getString("filename");
                if (valueOf == UpdateStatus.DownloadGameData) {
                    int phase = msg.getData().getInt("phase", 0);
                    if (phase == 2) {
                        // etapa final (pasta SAMP): a barra recomeca de 0% DE PROPOSITO; avisa o jogador uma vez
                        if (!mSampStepShown) {
                            mSampStepShown = true;
                            Toast.makeText(UpdateActivity.this, R.string.mx_update_samp_toast, Toast.LENGTH_LONG).show();
                        }
                        showProgress(R.string.mx_update_title_samp, j, j2, fname,
                                msg.getData().getLong("currentfile") + "/" + msg.getData().getLong("totalfiles")
                                        + " \u2022 " + (j2 / 1048576) + "MB / " + (j / 1048576) + "MB");
                    } else {
                        showProgress(phase == 1 ? R.string.mx_update_title_data_step1 : R.string.mx_update_title_data,
                                j, j2, fname, (j2 / 1048576) + "MB / " + (j / 1048576) + "MB");
                    }
                } else if (valueOf == UpdateActivity.UpdateStatus.CheckUpdate) {
                    showProgress(R.string.mx_update_title_check, j, j2, fname, null);
                } else if (valueOf == UpdateStatus.DownloadGame) {
                    showProgress(R.string.mx_update_title_game, j, j2, fname,
                            msg.getData().getLong("currentfile") + "/" + msg.getData().getLong("totalfiles"));
                }else if (!mIsStartingUpdate) {
                    Message obtain2 = Message.obtain((Handler) null, 1);
                    obtain2.replyTo = UpdateActivity.this.mMessenger;
                    try {
                        UpdateActivity.this.mService.send(obtain2);
                    } catch (RemoteException e6) {
                        e6.printStackTrace();
                    }
                    mIsStartingUpdate = true;
                }
            }
            else if(msg.what == 2)
            {
                Intent intent;
                Log.d("x1y2z", "UpdateService.UPDATE_GAME_DATA");
                //resetProgress(true, 100, 100);
                if (msg.getData().getBoolean("status", false)) {
                    String string3 = msg.getData().getString("apkPath", "");
                    if (string3.length() > 0) {
                        mGameApk = new File(string3);
                    }
                    if (mGameApk == null || !mGameApk.exists()) {
                        Log.d("x1y2z", "Error update game data");
                        if (mUpdateMode == UpdateMode.GameUpdate) { showUpdateError("The launcher update file is missing."); return; }
                        intent = new Intent(UpdateActivity.this, MainActivity.class);
                        startActivity(intent);
                        finish();
                    } else {
                        requestInstallGame();
                    }
                }
            }
            else if (msg.what == 6) {
                showUpdateError(msg.getData().getString("error", "Update failed"));
            }
            else if (msg.what == 1) {
                Log.i("UpdateActivity", "UpdateService.UPDATE_GAME");
                ((TextView)findViewById(R.id.installation_text)).setText(R.string.mx_update_title_install);
                ProgressBar progressBar = (ProgressBar) UpdateActivity.this.findViewById(R.id.download_progress);
                progressBar.setIndeterminate(true);
                Config.clientUpToDate = true;
                mFinishedOk = true;
                NotificationHelper.showDownloadDone(UpdateActivity.this);
                startActivity(new Intent(UpdateActivity.this, MainActivity.class));
                finish();
            }
        }
    }

    private boolean mFinishedOk = false;
    private int mLastNotifPercent = -2;
    private boolean mSampStepShown = false;

    /**
     * Atualiza titulo, arquivo, contador e o ANEL redondo. Escala interna 0..1000 (evita o
     * truncamento em MB que travava o progresso em 0 para arquivos pequenos).
     * Quando o tamanho total ainda e desconhecido (total <= 0) o anel gira (indeterminado).
     */
    private void showProgress(int titleRes, long total, long current, String fileName, String countText) {
        ((TextView) findViewById(R.id.installation_text)).setText(titleRes);
        ((TextView) findViewById(R.id.fileName)).setText(fileName == null ? "" : fileName);
        ((TextView) findViewById(R.id.fileCount)).setText(countText == null ? "" : countText);
        ProgressBar ring = findViewById(R.id.download_progress);
        TextView percentView = (TextView) findViewById(R.id.fileProgressPercent);
        if (total <= 0) {
            if (!ring.isIndeterminate()) ring.setIndeterminate(true);
            percentView.setText("");
            return;
        }
        if (ring.isIndeterminate()) ring.setIndeterminate(false);
        int permille = (int) Math.max(0, Math.min(1000, current * 1000L / total));
        ring.setMax(1000);
        ring.setProgress(permille);
        percentView.setText((permille / 10) + "%");
        int pct = permille / 10;
        if (pct != mLastNotifPercent) {
            mLastNotifPercent = pct;
            NotificationHelper.showDownloadProgress(this, pct, ((TextView) findViewById(R.id.installation_text)).getText().toString());
        }
    }

    /** Erro recuperavel vindo do servico: tentar de novo (nova verificacao) ou seguir para o launcher. */
    void showUpdateError(String text)
    {
        if (isFinishing() || isDestroyed() || mErrorDialogShowing) return;
        mErrorDialogShowing = true;
        new AlertDialog.Builder(this)
                .setMessage(text + "\nCheck your connection and try again.")
                .setCancelable(false)
                .setPositiveButton("Retry", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        mErrorDialogShowing = false;
                        mIsStartingUpdate = false; // permite que o novo ciclo dispare a mensagem 1
                        ProgressBar progressBar = findViewById(R.id.download_progress);
                        if (progressBar != null) progressBar.setIndeterminate(true);
                        if (mService != null) {
                            Message obtain = Message.obtain((Handler) null, mUpdateMode == UpdateMode.GameUpdate ? 2 : 7);
                            obtain.getData().putInt("gputype", mGpuType);
                            obtain.replyTo = mMessenger;
                            try {
                                mService.send(obtain);
                            } catch (RemoteException e) {
                                e.printStackTrace();
                            }
                        }
                    }
                })
                .setNegativeButton(mUpdateMode == UpdateMode.GameUpdate ? null : "Continue", mUpdateMode == UpdateMode.GameUpdate ? null : new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        mErrorDialogShowing = false;
                        startActivity(new Intent(UpdateActivity.this, MainActivity.class));
                        finish();
                    }
                })
                .show();
    }

    void requestInstallGame()
    {
        Log.d("x1y2z", "request install game");

        // Android 8+: instalar APK exige "Instalar apps desconhecidos" para este app.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !getPackageManager().canRequestPackageInstalls()) {
            Toast.makeText(this, "Allow \"Install unknown apps\" for this app to finish the update.", Toast.LENGTH_LONG).show();
            Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()));
            try {
                startActivityForResult(settings, REQ_UNKNOWN_SOURCES); // ao voltar: onActivityResult
            } catch (RuntimeException e) {
                Log.e("x1y2z", "cannot open unknown-sources settings", e);
                showUpdateError("Cannot open the \"Install unknown apps\" settings.");
            }
            return;
        }

        try {
            Uri uri = FileProvider.getUriForFile(getApplicationContext(),
                    getApplicationContext().getPackageName() + ".provider", mGameApk);
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.setDataAndType(uri, "application/vnd.android.package-archive");
            startActivity(intent);
        } catch (RuntimeException e) {
            Log.e("x1y2z", "install intent failed", e);
            Toast.makeText(UpdateActivity.this, "Error, while installing apk. Reenter to the launcher.", Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        Log.i("UpdateActivity", "onActivityResult -> code quest: " + resultCode + ", resultCode: " + data);
        if (requestCode == REQ_UNKNOWN_SOURCES) {
            // Retorno das configuracoes do Android: tenta instalar de novo se agora esta permitido.
            if (getPackageManager().canRequestPackageInstalls() && mGameApk != null && mGameApk.exists()) {
                requestInstallGame();
            } else {
                Toast.makeText(this, "Update not installed: permission was not granted.", Toast.LENGTH_LONG).show();
                if (mUpdateMode == UpdateMode.GameUpdate) { requestInstallGame(); return; }
                startActivity(new Intent(this, MainActivity.class));
                finish();
            }
            return;
        }
        if (requestCode == 0) {
            if (mGameApk != null && mGameApk.exists()) {
                mGameApk.delete();
            }
            startActivity(new Intent(this, SplashActivity.class));
            finish();
        }
    }


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LauncherUi.prepareWindow(this);
        setContentView(R.layout.activity_update);
        LauncherUi.applyImmersive(this);
        LauncherUi.applySafePadding(findViewById(R.id.mx_safe_area));
        NotificationHelper.ensureChannels(this);
        setupMusicToggle();

        if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        {
            Toast.makeText(this, "Use original launcher! No remake", Toast.LENGTH_LONG).show();
            return;
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        Config.currentContext = this;

        GLSurfaceView.Renderer mGlRenderer = new GLSurfaceView.Renderer() {
            @Override
            public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
                eGPUType egputype;
                String glGetString = gl10.glGetString(GL10.GL_EXTENSIONS);
                String glGetString2 = glGetString == null ? "" : glGetString;
                if (glGetString2.contains("GL_IMG_texture_compression_pvrtc")) {
                    egputype = eGPUType.PVR;
                    mGpuType = 3;
                } else if (glGetString2.contains("GL_EXT_texture_compression_dxt1") || glGetString2.contains("GL_EXT_texture_compression_s3tc") || glGetString2.contains("GL_AMD_compressed_ATC_texture")) {
                    egputype = eGPUType.DXT;
                    mGpuType = 1;
                } else {
                    egputype = eGPUType.ETC;
                    mGpuType = 2;
                }
                Log.e("x1y2z", "GPU name: " + glGetString);
                Log.e("x1y2z", "GPU type: " + egputype.name());

                try {
                    mUpdateMode = UpdateMode.valueOf(getIntent().getStringExtra("mode"));
                } catch (IllegalArgumentException | NullPointerException e) {
                    Log.e("x1y2z", "UpdateActivity started without a valid mode");
                    startActivity(new Intent(UpdateActivity.this, MainActivity.class));
                    finish();
                    return;
                }

                if (mService == null) {
                    Intent intent = new Intent(UpdateActivity.this, UpdateService.class);
                    mIsBind = bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
                } else if (mUpdateMode == UpdateMode.GameDataUpdate) {
                    Message obtain = Message.obtain((Handler) null, 7);
                    obtain.getData().putInt("gputype", mGpuType);
                    obtain.replyTo = mMessenger;
                    try {
                        mService.send(obtain);
                    } catch (RemoteException e) {
                        e.printStackTrace();
                    }

                }
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

    private ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName componentName, IBinder iBinder) {
            mService = new Messenger(iBinder);

            if (mUpdateMode == UpdateMode.GameDataUpdate) {
                Message obtain = Message.obtain((Handler) null, 7);
                obtain.getData().putInt("gputype", mGpuType);
                obtain.replyTo = mMessenger;
                try {
                    mService.send(obtain);
                } catch (RemoteException e) {
                    e.printStackTrace();
                }
            }
            else if (mUpdateMode == UpdateMode.GameUpdate) {
                Message obtain = Message.obtain((Handler) null, 2);
                obtain.getData().putInt("gputype", mGpuType);
                obtain.replyTo = mMessenger;
                try {
                    mService.send(obtain);
                } catch (RemoteException e) {
                    e.printStackTrace();
                }
            }

            isBind = true;
        }

        @Override
        public void onServiceDisconnected(ComponentName componentName) {
            mService = null;
            isBind = false;
        }
    };

    private final MaximoMusic.Listener mMusicListener = new MaximoMusic.Listener() {
        @Override
        public void onMusicStateChanged(boolean playing, int volumePercent) { updateMusicIcon(playing); }
    };

    private void setupMusicToggle() {
        ImageButton toggle = findViewById(R.id.mx_update_music_toggle);
        toggle.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { MaximoMusic.get(UpdateActivity.this).toggle(); }
        });
        updateMusicIcon(MaximoMusic.get(this).isPlaying());
    }

    private void updateMusicIcon(boolean playing) {
        ImageButton toggle = findViewById(R.id.mx_update_music_toggle);
        if (toggle == null) return;
        toggle.setImageResource(playing ? R.drawable.mx_ic_volume : R.drawable.mx_ic_volume_off);
        toggle.setContentDescription(getString(playing ? R.string.mx_update_music_on : R.string.mx_update_music_off));
    }

    @Override
    protected void onStart() {
        super.onStart();
        MaximoMusic m = MaximoMusic.get(this);
        m.addListener(mMusicListener);
        m.attach();
    }

    @Override
    protected void onStop() {
        MaximoMusic m = MaximoMusic.get(this);
        m.removeListener(mMusicListener);
        m.detach();
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) LauncherUi.applyImmersive(this);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (!mFinishedOk) NotificationHelper.cancelDownload(this);
        if(mIsBind) {
            unbindService(serviceConnection);
            mIsBind = false;
        }
    }
}
