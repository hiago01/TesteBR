package com.samp.mobile.game.ui;

import android.app.Activity;
import android.widget.Button;
import android.widget.EditText;

import androidx.constraintlayout.widget.ConstraintLayout;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;

import android.app.Activity;
import android.content.Context;
import android.text.Editable;
import android.os.SystemClock;
import android.view.KeyEvent;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.samp.mobile.R;

import java.util.ArrayList;
@Obfuscate
public class CustomKeyboard {

    public interface InputListener
    {
        public void OnInputEnd(String str);
    }

    private LinearLayout mInputLayout = null;
    private EditText mInputEt = null;

    private Button mButtonSlash = null;
    private Button mButtonHistoryNext = null;
    private Button mButtonHistoryPrev = null;

    private Activity mContext = null;
    private boolean mIsShowing = false;

    private String mSavedInput = null;

    private final int mMaxHistory = 20;
    private ArrayList<String> mInputHistory = null;
    private int mCurrentHistoryMessage = 0;

    private Runnable mAnimTask = null;

    // Vigia o teclado do sistema: posiciona a barra ACIMA dele e some com ela quando o teclado fecha por qualquer meio
    // (seta de baixo do sistema, Voltar, gesto) - antes a barra ficava presa no alto da tela.
    private Runnable mWatchTask = null;
    private long mShownAt = 0;
    private boolean mImeSeen = false;

    public boolean IsShowing()
    {
        return mIsShowing;
    }

    public CustomKeyboard(Activity act)
    {
        mContext = act;

        ConstraintLayout layout = (ConstraintLayout) act.getLayoutInflater().inflate(R.layout.layout_keyboard, null);
        mContext.addContentView(layout, new ConstraintLayout.LayoutParams(-1, -1));

        mInputLayout = layout.findViewById(R.id.main_input);
        mInputEt = layout.findViewById(R.id.main_et_input);
        mButtonSlash = layout.findViewById(R.id.main_but_input_slash);
        mButtonHistoryPrev = layout.findViewById(R.id.main_but_input_prev);
        mButtonHistoryNext = layout.findViewById(R.id.main_but_input_next);

        mInputHistory = new ArrayList<String>();

        mButtonSlash.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                mInputEt.getText().insert(mInputEt.getSelectionStart(), "/");
            }
        });

        mButtonHistoryPrev.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                mCurrentHistoryMessage--;
                if(mCurrentHistoryMessage < 0)
                {
                    mCurrentHistoryMessage = 0;
                }
                if(mCurrentHistoryMessage <= 0)
                {
                    mInputEt.setText("");
                    return;
                }

                mInputEt.setText(mInputHistory.get(mCurrentHistoryMessage - 1));
                mInputEt.setSelection(mInputEt.getText().length());
            }
        });

        mButtonHistoryNext.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                mCurrentHistoryMessage++;
                if((mCurrentHistoryMessage - 1) >= mInputHistory.size())
                {
                    mCurrentHistoryMessage--;
                }
                if(mCurrentHistoryMessage <= 0)
                {
                    return;
                }

                mInputEt.setText(mInputHistory.get(mCurrentHistoryMessage - 1));
                mInputEt.setSelection(mInputEt.getText().length());
            }
        });

        mInputEt.setOnEditorActionListener(new EditText.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView textView, int i, KeyEvent keyEvent)
            {
                if(i== EditorInfo.IME_ACTION_DONE || i == EditorInfo.IME_ACTION_NEXT)
                {
                    Editable editableText = mInputEt.getText();
                    if(editableText != null)
                    {
                        String str = editableText.toString();

                        mInputEt.setText("");

                        OnInputEnd(str);
                    }

                }
                return false;
            }
        });

        HideInputLayout();
    }

    public void onHeightChanged(int height)
    {
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) mInputLayout.getLayoutParams();
        params.setMargins(0, 0 , 0, height);
        mInputLayout.setLayoutParams(params);
    }

    public void ShowInputLayout()
    {
        mIsShowing = true;

        mInputLayout.setVisibility(View.INVISIBLE);

        mInputEt.requestFocus();
        InputMethodManager imm = (InputMethodManager) mContext.getSystemService(Context.INPUT_METHOD_SERVICE);
        imm.showSoftInput(mInputEt, InputMethodManager.SHOW_IMPLICIT);

        if(mAnimTask != null)
        {
            mInputLayout.removeCallbacks(mAnimTask);
            mAnimTask = null;
        }

        mAnimTask = new Runnable() {
            @Override
            public void run() {
                mInputLayout.setVisibility(View.VISIBLE);
                mInputEt.requestFocus();
            }
        };

        mInputLayout.postDelayed(mAnimTask, 60);

        mShownAt = SystemClock.uptimeMillis();
        mImeSeen = false;
        startWatch();

        mCurrentHistoryMessage = 0;

        if(mSavedInput != null)
        {
            mInputEt.setText(mSavedInput);
            mInputEt.setSelection(mInputEt.getText().length());
        }
    }

    public void HideInputLayout()
    {

        mCurrentHistoryMessage = 0;

        if(mInputEt.getEditableText() != null)
        {
            mSavedInput = mInputEt.getEditableText().toString();
        }

        if(mAnimTask != null)
        {
            mInputLayout.removeCallbacks(mAnimTask);
            mAnimTask = null;
        }

        stopWatch();

        InputMethodManager inputMethodManager = (InputMethodManager) mContext.getSystemService(Context.INPUT_METHOD_SERVICE);
        if(inputMethodManager != null)
        {
            // pela janela do proprio campo (getCurrentFocus() podia ser null e o teclado ficava aberto)
            inputMethodManager.hideSoftInputFromWindow(mInputEt.getWindowToken(), 0);
        }
        mInputEt.clearFocus();
        mInputLayout.setTranslationY(0.0f);
        mInputLayout.setVisibility(View.GONE);

        mIsShowing = false;
    }

    private void OnInputEnd(String str)
    {
        if(mInputHistory.size() >= mMaxHistory)
        {
            mInputHistory.remove(mInputHistory.size() - 1);
        }
        mInputHistory.add(0, str);

        InputListener listener = (InputListener)mContext;

        listener.OnInputEnd(str);

        // enviou (ou mandou vazio): fecha a barra e o teclado - antes ela ficava aberta no alto da tela
        HideInputLayout();
    }

    private int imeHeight(boolean[] visible)
    {
        visible[0] = false;
        View decor = mContext.getWindow().getDecorView();
        WindowInsetsCompat wi = ViewCompat.getRootWindowInsets(decor);
        if(wi == null) return 0;
        visible[0] = wi.isVisible(WindowInsetsCompat.Type.ime());
        return Math.max(0, wi.getInsets(WindowInsetsCompat.Type.ime()).bottom);
    }

    private void startWatch()
    {
        stopWatch();
        mWatchTask = new Runnable() {
            @Override
            public void run()
            {
                if(!mIsShowing) return;
                boolean[] vis = new boolean[1];
                int h = imeHeight(vis);
                if(vis[0])
                {
                    mImeSeen = true;
                    mInputLayout.setTranslationY(-h);          // barra encostada em cima do teclado
                }
                else if(android.os.Build.VERSION.SDK_INT >= 30 && (mImeSeen || SystemClock.uptimeMillis() - mShownAt > 1500))   // <30: o sistema nao informa o teclado; so enviar fecha
                {
                    HideInputLayout();                          // teclado fechou por fora (seta/Voltar/gesto)
                    return;
                }
                mInputLayout.postDelayed(this, 100);
            }
        };
        mInputLayout.postDelayed(mWatchTask, 100);
    }

    private void stopWatch()
    {
        if(mWatchTask != null)
        {
            mInputLayout.removeCallbacks(mWatchTask);
            mWatchTask = null;
        }
    }
}

