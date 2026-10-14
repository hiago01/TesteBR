package com.samp.mobile.game.ui;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.graphics.Color;
import android.net.Uri;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

/**
 * Tela de roupas em HTML.
 *
 * Le o arquivo:   <Android/data/com.samp.mobile/files>/SAMP/NET/ROUPAS.HTML
 * Abre com:       /roupas            (comando local, tratado em chat.cpp)
 * Fecha com:      /roupas fechar, botao FECHAR do HTML, tecla ESC ou botao Voltar
 *
 * O HTML ja chama window.nativeBridge.postMessage(json) a cada toque. Aqui esse metodo
 * existe de verdade (addJavascriptInterface) e converte cada acao em um COMANDO que e
 * enviado ao servidor pelo mesmo caminho do chat (CNetGame::SendChatCommand).
 *
 * >>> Para adaptar os comandos, mexa SOMENTE em buildCommand() <<<
 */
public class ClothesUI {
    private static final String TAG = "ClothesUI";

    /** Caminho relativo a getExternalFilesDir(null). */
    public static final String HTML_PATH = "SAMP/NET/ROUPAS.HTML";

    /** Ordem das categorias usada no comando de "salvar look" (igual ao state.selected do HTML). */
    private static final String[] LOOK_ORDER = {
            "roupas", "cabelos", "pele", "calcas", "bermudas", "bones", "tenis"
    };

    private static final Pattern SAFE_ID = Pattern.compile("^[a-z_]{1,16}$");

    private final Activity activity;
    private FrameLayout root;
    private WebView web;

    public boolean isShow;
    private boolean pausedByGame;

    /** Envia o comando ao servidor (implementado em java/clothesui.cpp). */
    native void sendCommand(byte[] command);

    public ClothesUI(Activity activity) {
        this.activity = activity;
    }

    public boolean isShowing() {
        return isShow;
    }

    // ------------------------------------------------------------------ abrir / fechar

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    private void ensureCreated() {
        if (web != null) return;

        web = new WebView(activity);
        web.setBackgroundColor(Color.TRANSPARENT);   // o HTML usa body transparente
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setVerticalScrollBarEnabled(false);
        web.setHorizontalScrollBarEnabled(false);

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setAllowFileAccess(true);                   // precisa para ler o proprio ROUPAS.HTML
        s.setAllowContentAccess(false);
        s.setAllowFileAccessFromFileURLs(false);
        s.setAllowUniversalAccessFromFileURLs(false);
        s.setBlockNetworkLoads(true);                 // pagina 100% local, sem internet
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);

        web.addJavascriptInterface(new Bridge(), "nativeBridge");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return true; // nunca navega para outro lugar
            }
        });

        root = new FrameLayout(activity);
        root.addView(web, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.setVisibility(View.GONE);
        activity.addContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Procura SAMP/NET/ROUPAS.HTML; se o case nao bater (roupas.html etc.) acha mesmo assim. */
    private File findHtml() {
        File base = activity.getExternalFilesDir(null);
        if (base == null) return null;

        File exact = new File(base, HTML_PATH);
        if (exact.isFile()) return exact;

        File dir = new File(base, "SAMP/NET");
        File[] list = dir.listFiles();
        if (list != null) {
            for (File f : list) {
                if (f.isFile() && f.getName().equalsIgnoreCase("ROUPAS.HTML")) return f;
            }
        }
        return null;
    }

    /** Abre (recarrega o arquivo toda vez, assim voce edita o HTML sem reiniciar o jogo). */
    public void show() {
        activity.runOnUiThread(() -> {
            File html = findHtml();
            if (html == null) {
                File base = activity.getExternalFilesDir(null);
                Toast.makeText(activity, "ROUPAS.HTML nao encontrado em " + base + "/" + HTML_PATH,
                        Toast.LENGTH_LONG).show();
                Log.e(TAG, "arquivo nao encontrado: " + base + "/" + HTML_PATH);
                return;
            }
            ensureCreated();
            web.loadUrl(Uri.fromFile(html).toString());
            root.setVisibility(View.VISIBLE);
            root.bringToFront();
            web.requestFocus();
            isShow = true;
            pausedByGame = false;
        });
    }

    public void hide() {
        activity.runOnUiThread(() -> {
            if (root != null) root.setVisibility(View.GONE);
            isShow = false;
            pausedByGame = false;
        });
    }

    /** Chamado pelo SAMP.setPauseState: esconde enquanto o jogo esta pausado e volta depois. */
    public void setPaused(boolean pause) {
        if (root == null) return;
        if (pause && isShow) {
            root.setVisibility(View.GONE);
            pausedByGame = true;
        } else if (!pause && pausedByGame) {
            root.setVisibility(View.VISIBLE);
            pausedByGame = false;
        }
    }

    // ------------------------------------------------------------------ ponte HTML -> cliente

    private class Bridge {
        /** O HTML chama window.nativeBridge.postMessage(JSON.stringify({type:"clothing", action, ...})). */
        @JavascriptInterface
        public void postMessage(String payload) {
            try {
                JSONObject json = new JSONObject(payload);
                String action = json.optString("action", "");

                String command = buildCommand(action, json);
                if (command != null) {
                    sendCommand(command.getBytes(StandardCharsets.ISO_8859_1));
                    Log.d(TAG, "toque -> " + command);
                }

                if (action.equals("close")) hide();
            } catch (Exception e) {
                Log.e(TAG, "mensagem invalida do HTML: " + payload, e);
            }
        }
    }

    /**
     * ============================  COMANDOS (ADAPTE AQUI)  ============================
     *
     *  Toque no HTML            action               Comando enviado ao servidor
     *  ----------------------   ------------------   ------------------------------------
     *  trocar categoria         category_changed     /roupas_cat <categoria>
     *  escolher item            item_selected        /roupas_item <categoria> <indice>
     *  escolher tom de pele     skin_tone_selected   /roupas_pele <indice 0-4>
     *  VISTA PREVIA             preview              /roupas_previa <0|1>
     *  COMPRAR                  purchase_request     /roupas_comprar <categoria> <indice>
     *  SALVAR LOOK              save_look            /roupas_salvar <7 indices> (ver LOOK_ORDER)
     *  FECHAR / ESC             close                /roupas_fechar
     *  saldo insuficiente       purchase_failed      (nenhum - e so aviso local do HTML)
     *
     *  categoria = roupas | cabelos | pele | calcas | bermudas | bones | tenis
     *  indice    = posicao do item na lista da categoria no HTML (comeca em 0)
     *
     *  Retorne null para NAO enviar nada para aquela acao.
     */
    private static String buildCommand(String action, JSONObject j) {
        switch (action) {
            case "category_changed": {
                String cat = safeId(j.optString("category"));
                return cat == null ? null : "/roupas_cat " + cat;
            }
            case "item_selected": {
                String cat = safeId(j.optString("category"));
                return cat == null ? null : "/roupas_item " + cat + " " + j.optInt("index", 0);
            }
            case "skin_tone_selected":
                return "/roupas_pele " + j.optInt("index", 0);

            case "preview":
                return "/roupas_previa " + (j.optBoolean("enabled", false) ? 1 : 0);

            case "purchase_request": {
                String cat = safeId(j.optString("category"));
                return cat == null ? null : "/roupas_comprar " + cat + " " + j.optInt("index", 0);
            }
            case "save_look": {
                JSONObject sel = j.optJSONObject("selections");
                if (sel == null) return "/roupas_salvar";
                StringBuilder sb = new StringBuilder("/roupas_salvar");
                for (String key : LOOK_ORDER) sb.append(' ').append(sel.optInt(key, 0));
                return sb.toString();
            }
            case "close":
                return "/roupas_fechar";

            default:
                return null; // purchase_failed e qualquer acao desconhecida
        }
    }

    /** So aceita ids simples (a-z e _), para o HTML nunca conseguir injetar texto no comando. */
    private static String safeId(String s) {
        return (s != null && SAFE_ID.matcher(s).matches()) ? s : null;
    }
}
