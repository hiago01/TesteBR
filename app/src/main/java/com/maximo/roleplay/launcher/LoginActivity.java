package com.maximo.roleplay.launcher;

import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.drawable.RoundedBitmapDrawable;
import androidx.core.graphics.drawable.RoundedBitmapDrawableFactory;

import com.maximo.roleplay.R;
import com.maximo.roleplay.launcher.util.AccountApi;
import com.maximo.roleplay.launcher.util.AccountSession;
import com.maximo.roleplay.launcher.util.LauncherUi;
import com.maximo.roleplay.launcher.util.NickRules;

import java.util.regex.Pattern;

/** Tela de entrada obrigatoria: Entrar ou Criar conta (nick, e-mail, senha). O login fica salvo; a senha nao. */
public class LoginActivity extends AppCompatActivity {
    private static final int PASS_MIN = 6, PASS_MAX = 32;
    private static final Pattern EMAIL = Pattern.compile("^[^@\\s]+@[^@\\s]+\\.[^@\\s]{2,}$");

    private EditText mNick, mEmail, mPass, mPass2;
    private TextView mStatus, mTabIn, mTabUp;
    private Button mOk;
    private boolean mCreate = false;
    private boolean mBusy = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LauncherUi.prepareWindow(this);
        setContentView(R.layout.mx_activity_login);
        LauncherUi.applyImmersive(this);
        LauncherUi.applySafePadding(findViewById(R.id.mx_login_root));

        mNick = findViewById(R.id.mx_login_nick);
        mEmail = findViewById(R.id.mx_login_email);
        mPass = findViewById(R.id.mx_login_pass);
        mPass2 = findViewById(R.id.mx_login_pass2);
        mStatus = findViewById(R.id.mx_login_status);
        mTabIn = findViewById(R.id.mx_login_tab_in);
        mTabUp = findViewById(R.id.mx_login_tab_up);
        mOk = findViewById(R.id.mx_login_ok);

        showAvatar((ImageView) findViewById(R.id.mx_login_avatar));

        String last = AccountSession.getNick(this);
        if (last != null) mNick.setText(last);

        mTabIn.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { setMode(false); } });
        mTabUp.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { setMode(true); } });
        mOk.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { submit(); } });
        mPass.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override public boolean onEditorAction(TextView v, int actionId, android.view.KeyEvent e) {
                if (!mCreate && actionId == EditorInfo.IME_ACTION_NEXT) { submit(); return true; }
                return false;
            }
        });
        mPass2.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override public boolean onEditorAction(TextView v, int actionId, android.view.KeyEvent e) {
                if (actionId == EditorInfo.IME_ACTION_DONE) { submit(); return true; }
                return false;
            }
        });
        setMode(false);
    }

    /** Foto de perfil oficial (mesma para todos os jogadores), recortada em circulo. */
    public static void showAvatar(ImageView iv) {
        Bitmap bmp = BitmapFactory.decodeResource(iv.getResources(), R.drawable.mx_avatar_default);
        if (bmp == null) return;
        RoundedBitmapDrawable d = RoundedBitmapDrawableFactory.create(iv.getResources(), bmp);
        d.setCircular(true);
        d.setAntiAlias(true);
        iv.setImageDrawable(d);
    }

    private void setMode(boolean create) {
        if (mBusy) return;
        mCreate = create;
        mEmail.setVisibility(create ? View.VISIBLE : View.GONE);
        mPass2.setVisibility(create ? View.VISIBLE : View.GONE);
        mPass.setImeOptions(create ? EditorInfo.IME_ACTION_NEXT : EditorInfo.IME_ACTION_NEXT);
        mTabIn.setTextColor(getColor(create ? R.color.mx_text_dim : R.color.mx_text));
        mTabUp.setTextColor(getColor(create ? R.color.mx_text : R.color.mx_text_dim));
        mOk.setText(create ? R.string.mx_login_btn_up : R.string.mx_login_btn_in);
        mStatus.setText("");
    }

    private void error(int res) { error(getString(res)); }
    private void error(String s) { mStatus.setTextColor(getColor(R.color.mx_error)); mStatus.setText(s); }

    private void setBusy(boolean b) {
        mBusy = b;
        mOk.setEnabled(!b);
        mOk.setAlpha(b ? 0.55f : 1f);
        if (b) { mStatus.setTextColor(getColor(R.color.mx_text_dim)); mStatus.setText(mCreate ? R.string.mx_acc_creating : R.string.mx_login_entering); }
    }

    private static boolean passChars(String s) {
        for (int i = 0; i < s.length(); i++) { char c = s.charAt(i); if (c < 0x21 || c > 0x7e) return false; }
        return true;
    }

    private void submit() {
        if (mBusy) return;
        final String wire = NickRules.toWire(NickRules.normalizeDisplay(mNick.getText().toString()));
        final String pass = mPass.getText().toString();
        if (!NickRules.isValidWire(wire)) { error(R.string.mx_nick_err_chars); return; }
        if (pass.isEmpty()) { error(R.string.mx_login_err_fields); return; }

        AccountApi.Callback cb = new AccountApi.Callback() {
            @Override public void onResult(AccountApi.Result r) {
                setBusy(false);
                if (isFinishing() || isDestroyed()) return;
                if (!r.ok) { error(r.message); return; }
                String nick = r.nick.isEmpty() ? wire : r.nick;
                AccountSession.save(LoginActivity.this, nick, r.email, r.sessao);
                mPass.setText(""); mPass2.setText("");
                startActivity(new Intent(LoginActivity.this, MainActivity.class));
                finish();
            }
        };

        if (!mCreate) {
            setBusy(true);
            AccountApi.login(this, wire, pass, cb);
            return;
        }
        String email = mEmail.getText().toString().trim();
        if (pass.length() < PASS_MIN || pass.length() > PASS_MAX) { error(R.string.mx_acc_err_pass_len); return; }
        if (!passChars(pass)) { error(R.string.mx_acc_err_pass_chars); return; }
        if (!pass.equals(mPass2.getText().toString())) { error(R.string.mx_acc_err_pass_match); return; }
        if (email.length() > 100 || !EMAIL.matcher(email).matches()) { error(R.string.mx_login_err_email); return; }
        setBusy(true);
        AccountApi.create(this, wire, pass, email, cb);
    }

    @Override
    public void onBackPressed() {
        // Login e obrigatorio: voltar fecha o app em vez de abrir o launcher sem conta.
        finishAffinity();
    }
}
