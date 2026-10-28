package com.samp.mobile.launcher.adapters;

import android.app.Activity;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.game.GTASA;
import com.samp.mobile.game.SAMP;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.util.ButtonAnimator;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.SharedPreferenceCore;

import org.ini4j.Wini;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
@Obfuscate
public class ServerInformationFragment extends Dialog {

    ServerAdapter mServerAdapter;
    SAMPServerInfo sampServerInfo;
    int position;
    Activity activity;

    public ServerInformationFragment(Activity a, ServerAdapter adapter, int position, SAMPServerInfo sampServerInfo1)
    {
        super(a);
        activity = a;
        mServerAdapter = adapter;
        sampServerInfo = sampServerInfo1;
        this.position = position;
    }

    @Nullable
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.alertdialog_server);

        TextView mHostName = findViewById(R.id.server_hostname);
        TextView mIP = findViewById(R.id.server_ip);
        TextView mPort = findViewById(R.id.server_port);
        TextView mOnlineServer = findViewById(R.id.server_online);
        TextView mMode = findViewById(R.id.server_mode);
        TextView mLanguage = findViewById(R.id.server_language);
        ImageView mClose = findViewById(R.id.server_close);
        EditText mServerPassword = findViewById(R.id.server_password);
        Button mSave = findViewById(R.id.save_favorites);

        mSave.setVisibility(View.GONE);

        if(!sampServerInfo.getHasPassword())
            mServerPassword.setVisibility(View.GONE);
        else
            mServerPassword.setVisibility(View.VISIBLE);

        mHostName.setText(sampServerInfo.getServerName());

        // Servidor OFICIAL: IP e porta nao sao exibidos (rotulos e valores ocultos, sem reservar espaco).
        // Os valores reais continuam em sampServerInfo e sao usados por launchGame().
        mIP.setText("");
        mPort.setText("");
        mIP.setVisibility(View.GONE);
        mPort.setVisibility(View.GONE);
        View ipLabel = findViewById(R.id.textView);
        View portLabel = findViewById(R.id.textView2);
        if (ipLabel != null) ipLabel.setVisibility(View.GONE);
        if (portLabel != null) portLabel.setVisibility(View.GONE);
        // As linhas do layout sao posicionadas por "bias" vertical; sobe as restantes para ocupar o
        // lugar de IP/Porta e nao deixar um buraco no dialogo.
        moveRow(R.id.textView3, R.id.server_online, 0.30f);
        moveRow(R.id.textView4, R.id.server_mode, 0.42f);
        moveRow(R.id.textView5, R.id.server_language, 0.53f);
        mOnlineServer.setText(sampServerInfo.getStatusText());
        mMode.setText(sampServerInfo.getServerMode());

        mLanguage.setText(sampServerInfo.getLanguage());

        Button mConnect = findViewById(R.id.server_connect);
        mConnect.setOnTouchListener(new ButtonAnimator(getContext(), mConnect));
        mConnect.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                launchGame(activity, sampServerInfo);
                dismiss();
            }
        });


        mClose.setOnTouchListener(new ButtonAnimator(getContext(), mClose));
        mClose.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                dismiss();
            }
        });
    }

    private void moveRow(int labelId, int valueId, float bias) {
        int[] ids = { labelId, valueId };
        for (int id : ids) {
            View v = findViewById(id);
            if (v != null && v.getLayoutParams() instanceof androidx.constraintlayout.widget.ConstraintLayout.LayoutParams) {
                androidx.constraintlayout.widget.ConstraintLayout.LayoutParams lp =
                        (androidx.constraintlayout.widget.ConstraintLayout.LayoutParams) v.getLayoutParams();
                lp.verticalBias = bias;
                v.setLayoutParams(lp);
            }
        }
    }

    /**
     * Fluxo ORIGINAL de conexao: grava host/porta em SAMP/settings.ini, confere os arquivos do jogo
     * e abre a SAMP activity. Usado pelo dialogo de detalhes e pelo botao CONECTAR do painel.
     */
    public static void launchGame(Activity activity, SAMPServerInfo info) {
        File file = new File(activity.getExternalFilesDir(null) + "/SAMP/settings.ini");
        if (file.exists()) {
            try {
                Wini wini = new Wini(file);
                wini.put("client", "host", info.getAddress());
                wini.put("client", "port", info.getPort());
                wini.store();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        File dxt = new File(activity.getExternalFilesDir(null) + "/Text/american.dxt");
        File font = new File(activity.getExternalFilesDir(null) + "/Textures/fonts/RussianFont.png");
        if (!dxt.exists() && !font.exists()) {
            Toast.makeText(activity, "Some important files in your modified data are missing, such as \"Text\" and \"Textures\". " +
                    "Please, fix it and after try again.", Toast.LENGTH_LONG).show();
            return;
        }
        com.samp.mobile.launcher.util.MaximoMusic.get(activity).release();
        activity.startActivity(new Intent(activity, SAMP.class));
        activity.finish();
    }

}
