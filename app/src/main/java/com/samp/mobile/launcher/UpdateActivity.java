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
                if (valueOf == UpdateStatus.DownloadGameData) {
                    ((TextView)findViewById(R.id.installation_text)).setText("Updating game data...");
                    Log.d("x1y2z", "statusname = " + valueOf);
                    long j = msg.getData().getLong("total");
                    long j2 = msg.getData().getLong("current");
                    ((TextView) findViewById(R.id.fileName)).setText(msg.getData().getString("filename"));
                    ((TextView) findViewById(R.id.fileCount)).setText(j2/1048576 + "MB/" + j/1048576+"MB");
                    ProgressBar progressBar = findViewById(R.id.download_progress);
                    progressBar.setIndeterminate(false);
                    Log.d("UpdateActivity", (int) (j/1048576) + "/" + (int) (j2/1048576));
                    progressBar.setMax((int) (j/1048576));
                    progressBar.setProgress((int) (j2/1048576));

                    ((TextView) findViewById(R.id.fileProgressPercent)).setText(j2*100/(j+1) + "%");
                } else if (valueOf == UpdateActivity.UpdateStatus.CheckUpdate) {
                    Log.d("x1y2z", "statusname = " + valueOf);
                    long j = msg.getData().getLong("total");
                    long j2 = msg.getData().getLong("current");
                    ((TextView) findViewById(R.id.fileName)).setText(msg.getData().getString("filename"));
                    ProgressBar progressBar = (ProgressBar) UpdateActivity.this.findViewById(R.id.download_progress);
                    progressBar.setMax((int) (j)/1048576);
                    progressBar.setProgress((int) (j2)/1048576);
                } else if (valueOf == UpdateStatus.DownloadGame) {
                    ((TextView)findViewById(R.id.installation_text)).setText("Updating game...");
                    Log.d("x1y2z", "statusname = " + valueOf);
                    long j = msg.getData().getLong("total");
                    long j2 = msg.getData().getLong("current");
                    ((TextView) findViewById(R.id.fileName)).setText(msg.getData().getString("filename"));
                    ((TextView) findViewById(R.id.fileCount)).setText(msg.getData().getLong("currentfile") + "/" + msg.getData().getLong("totalfiles"));
                    ProgressBar progressBar = (ProgressBar) UpdateActivity.this.findViewById(R.id.download_progress);
                    progressBar.setIndeterminate(false);
                    progressBar.setMax((int) (j)/1048576);
                    progressBar.setProgress((int) (j2)/1048576);
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
                ((TextView)findViewById(R.id.installation_text)).setText("Installation...");
                ProgressBar progressBar = (ProgressBar) UpdateActivity.this.findViewById(R.id.download_progress);
                progressBar.setIndeterminate(true);
                startActivity(new Intent(UpdateActivity.this, MainActivity.class));
                finish();
            }
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
                .setNegativeButton("Continue", new DialogInterface.OnClickListener() {
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
                startActivity(new Intent(this, MainActivity.class));
                finish();
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
        setContentView(R.layout.activity_update);

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

    @Override
    public void onDestroy() {
        super.onDestroy();
        if(mIsBind) {
            unbindService(serviceConnection);
            mIsBind = false;
        }
    }
}
