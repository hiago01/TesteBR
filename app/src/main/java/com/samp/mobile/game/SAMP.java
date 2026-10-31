package com.samp.mobile.game;

import android.Manifest;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.google.firebase.crashlytics.FirebaseCrashlytics;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.game.ui.AttachEdit;
import com.samp.mobile.game.ui.ClothesUI;
import com.samp.mobile.game.ui.CustomKeyboard;
import com.samp.mobile.game.ui.GameMusicCard;
import com.samp.mobile.game.ui.LoadingScreen;
import com.samp.mobile.launcher.util.MaximoMusic;
import com.samp.mobile.game.ui.dialog.DialogManager;
import com.samp.mobile.launcher.util.SharedPreferenceCore;
import com.samp.mobile.launcher.util.SignatureChecker;

import java.io.UnsupportedEncodingException;
import java.nio.charset.StandardCharsets;
@Obfuscate
public class SAMP extends GTASA implements CustomKeyboard.InputListener, HeightProvider.HeightListener {
    private static final String TAG = "SAMP";
    private static SAMP instance;

    public static final boolean NATIVE_KEYBOARD = false;
    private CustomKeyboard mKeyboard;
    private DialogManager mDialog;
    private HeightProvider mHeightProvider;

    private AttachEdit mAttachEdit;
    private ClothesUI mClothes;
    private LoadingScreen mLoadingScreen;
    /** Player "Tocando agora": continua durante o loading e o jogo ate a musica acabar ou o X. */
    private GameMusicCard mMusicCard;

    public native void sendDialogResponse(int i, int i2, int i3, byte[] str);

    public static SAMP getInstance() {
        return instance;
    }

    private void showTab()
    {

    }

    private void hideTab()
    {

    }

    private void setTab(int id, String name, int score, int ping)
    {

    }

    private void clearTab()
    {

    }

    /** Chamado pelo nativo (CJavaWrapper::ShowLoadingScreen): reexibe a tela ao reconectar. */
    private void showLoadingScreen()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mLoadingScreen != null) mLoadingScreen.showReconnecting();
            }
        });
    }

    /** Chamado pelo nativo quando a conexao e aceita: completa a barra em 100% e encerra a tela. */
    private void hideLoadingScreen()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mLoadingScreen != null) mLoadingScreen.hide();
            }
        });
    }

    /**
     * Chamado pelo nativo (CJavaWrapper::SetLoadingStage) a cada etapa REAL concluida da
     * inicializacao (ver LoadingScreen.STAGE_*). Pode vir de qualquer thread.
     */
    private void setLoadingStage(final int stage)
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mLoadingScreen != null) mLoadingScreen.setStage(stage);
            }
        });
    }

    public void setPauseState(boolean pause) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mClothes != null) mClothes.setPaused(pause);
                if (pause) {
                    mDialog.hideWithoutReset();
                    mAttachEdit.hideWithoutReset();
                }
                else {
                    if(mDialog.isShow)
                        mDialog.showWithOldContent();
                    if(mAttachEdit.isShow)
                        mAttachEdit.showWithoutReset();
                }
            }
        });
    }

    public void exitGame(){
        markGameRunning(false);
        FirebaseCrashlytics.getInstance().setCrashlyticsCollectionEnabled(false);

        finishAndRemoveTask();
        System.exit(0);
    }

    public void showDialog(int dialogId, int dialogTypeId, byte[] bArr, byte[] bArr2, byte[] bArr3, byte[] bArr4) {
        final String caption = new String(bArr, StandardCharsets.UTF_8);
        final String content = new String(bArr2, StandardCharsets.UTF_8);
        final String leftBtnText = new String(bArr3, StandardCharsets.UTF_8);
        final String rightBtnText = new String(bArr4, StandardCharsets.UTF_8);
        runOnUiThread(() -> { this.mDialog.show(dialogId, dialogTypeId, caption, content, leftBtnText, rightBtnText); });
    }

    private native void onInputEnd(byte[] str);

    // Teclado NATIVO (C++) para os campos dos dialogos: o texto volta em onDialogKeyboardResult; o fechamento em onDialogKeyboardClosed.
    public native int openDialogKeyboard(byte[] initial, boolean mask, byte[] hint);   // Y do topo do teclado (px) ou -1
    public native void closeDialogKeyboard();

    public void onDialogKeyboardResult(final byte[] text) {
        runOnUiThread(new Runnable() { @Override public void run() { if (mDialog != null) mDialog.onNativeKeyboardResult(text); } });
    }

    public void onDialogKeyboardClosed() {
        runOnUiThread(new Runnable() { @Override public void run() { if (mDialog != null) mDialog.onNativeKeyboardClosed(); } });
    }
    @Override
    public void OnInputEnd(String str)
    {
        byte[] toReturn = null;
        try
        {
            toReturn = str.getBytes("windows-1251");
        }
        catch(UnsupportedEncodingException e)
        {

        }

        try {
            onInputEnd(toReturn);
        }
        catch (UnsatisfiedLinkError e5) {
            Log.e(TAG, e5.getMessage());
        }
    }

    private void showKeyboard()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Log.d("AXL", "showKeyboard()");
                if (mKeyboard != null) mKeyboard.ShowInputLayout();
            }
        });
    }

    private void hideKeyboard()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mKeyboard != null) mKeyboard.HideInputLayout();
            }
        });
    }

    private void showEditObject()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mAttachEdit.show();
            }
        });
    }

    private void showClothesUI()
    {
        mClothes.show();
    }

    private void hideClothesUI()
    {
        mClothes.hide();
    }

    private void hideEditObject()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mAttachEdit.hide();
            }
        });
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "**** onCreate");
        super.onCreate(savedInstanceState);

        if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        {
            Toast.makeText(this, "Use original launcher! No remake", Toast.LENGTH_LONG).show();
            return;
        }

        //mHeightProvider = new HeightProvider(this);

        // CHAVE GERAL do teclado: false = teclado do ANDROID em todo o projeto (chat + dialogos, View Java CustomKeyboard);
        // true = teclado NATIVO (C++/ImGui). O lado C++ tem a chave Keyboard::kNativeEnabled (keyboard.h): mantenha as duas iguais.
        markGameRunning(true);   // se o processo cair, o launcher avisa na proxima abertura (ver MainActivity)
        mKeyboard = NATIVE_KEYBOARD ? null : new CustomKeyboard(this);

        mDialog = new DialogManager(this);

        mAttachEdit = new AttachEdit(this);

        mClothes = new ClothesUI(this);

        mLoadingScreen = new LoadingScreen(this);
        // Card de musica criado DEPOIS do loading para ficar por cima dele (e do jogo).
        mMusicCard = new GameMusicCard(this);
        mLoadingScreen.setCallback(new LoadingScreen.Callback() {
            @Override public void onLoadingShown() {
                if (mMusicCard != null) { mMusicCard.bringToFront(); mMusicCard.expand(); }
            }
            @Override public void onLoadingHidden() {
                if (mMusicCard != null) mMusicCard.compact();
            }
        });

        instance = this;

        try {
            initializeSAMP();
            // etapa real: ponte JNI com o SA-MP criada
            mLoadingScreen.setStage(LoadingScreen.STAGE_NATIVE_READY);
        } catch (UnsatisfiedLinkError e5) {
            Log.e(TAG, String.valueOf(e5.getMessage()));
            mLoadingScreen.showError(R.string.mx_loading_step_error);
        }

        requestMicrophoneIfNeeded();
    }

    public static final String PREF_GAME_RUNNING = "game_running";

    private void markGameRunning(boolean running) {
        try { getSharedPreferences("com.samp.mobile", MODE_PRIVATE).edit().putBoolean(PREF_GAME_RUNNING, running).commit(); }
        catch (RuntimeException ignored) { }
    }

    private static final int REQ_RECORD_AUDIO = 77;

    /**
     * RECORD_AUDIO so e usada pelo chat de voz, portanto e pedida aqui (ao entrar no jogo) e nunca
     * no launcher. E opcional: se negada o jogo segue normalmente, apenas sem voz. Depois de uma
     * negacao com "nao perguntar novamente" nao insiste (evita dialogo repetido).
     */
    public void requestMicrophoneIfNeeded() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        SharedPreferences sp = getSharedPreferences("com.samp.mobile", MODE_PRIVATE);
        boolean askedBefore = sp.getBoolean("mic_asked", false);
        if (askedBefore && !ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.RECORD_AUDIO)) {
            return; // negada permanentemente: so o usuario pode mudar nas configuracoes
        }
        sp.edit().putBoolean("mic_asked", true).apply();
        ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.RECORD_AUDIO}, REQ_RECORD_AUDIO);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_RECORD_AUDIO) {
            boolean granted = grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            if (!granted) {
                Toast.makeText(this, "Microphone denied: voice chat is disabled.", Toast.LENGTH_LONG).show();
            }
        }
    }

    private native void initializeSAMP();



    @Override
    public void onStart() {
        Log.i(TAG, "**** onStart");
        super.onStart();
    }

    @Override
    public void onRestart() {
        Log.i(TAG, "**** onRestart");
        super.onRestart();
    }

    @Override
    public void onResume() {
        Log.i(TAG, "**** onResume");
        super.onResume();
        MaximoMusic.get(this).resumeFromGameBackground();
        //mHeightProvider.init(view);
    }

    public native boolean onEventBackPressed();

    @Override
    public void onBackPressed() {
        if (mClothes != null && mClothes.isShowing()) {
            mClothes.hide();
            return;
        }
        // Menu Maximo RP: Voltar abre/fecha o menu. Nunca chama super (nao finaliza a Activity,
        // nao aciona o Pause nativo e nao desconecta o jogador).
        onEventBackPressed();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(keyCode == KeyEvent.KEYCODE_BACK)
        {
            if (mClothes != null && mClothes.isShowing()) {
                mClothes.hide();
                return true;
            }
            // Tratado pelo menu Maximo RP; NAO repassa ao motor (NvEventQueueActivity encaminharia
            // o BACK ao jogo, que abriria o menu de pausa nativo).
            if (event.getRepeatCount() == 0) onEventBackPressed();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void onPause() {
        Log.i(TAG, "**** onPause");
        MaximoMusic.get(this).pauseForGameBackground();
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "**** onStop");
        super.onStop();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "**** onDestroy");
        if (isFinishing()) markGameRunning(false);   // saida normal (o Android so recria o launcher se o processo cair)
        if (mLoadingScreen != null) mLoadingScreen.release();
        if (mMusicCard != null) mMusicCard.dismiss();
        MaximoMusic.get(this).release(); // a Activity do jogo acabou: libera o MediaPlayer
        super.onDestroy();
    }

    @Override
    public void onHeightChanged(int orientation, int height) {
        //mKeyboard.onHeightChanged(height);
        //mDialog.onHeightChanged(height);
    }
}
