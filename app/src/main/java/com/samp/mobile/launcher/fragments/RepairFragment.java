package com.samp.mobile.launcher.fragments;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.core.content.FileProvider;
import androidx.fragment.app.Fragment;

import com.samp.mobile.R;
import com.samp.mobile.launcher.config.AppEndpoints;
import com.samp.mobile.launcher.pack.AtomicInstaller;
import com.samp.mobile.launcher.pack.InstalledState;
import com.samp.mobile.launcher.pack.CancelToken;
import com.samp.mobile.launcher.pack.FileDownloader;
import com.samp.mobile.launcher.pack.MiniJson;
import com.samp.mobile.launcher.pack.PackDownloader;
import com.samp.mobile.launcher.pack.PackException;
import com.samp.mobile.launcher.pack.RepairEngine;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Engrenagem de correcoes (menu lateral "CORRECOES"). Fluxo SEPARADO do UpdateService: nao altera o atualizador nem os
 * pacotes ZIP. Toda a logica de arquivo/rede fica em pack/RepairEngine (testada em JVM); aqui so ha tela e Android.
 * Escopo: files/SAMP e files/texdb. Rede e disco sempre em thread de fundo.
 */
public class RepairFragment extends Fragment {
    private static final AtomicBoolean BUSY = new AtomicBoolean(false);
    private static final int ATTEMPTS = 3;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile CancelToken cancel = new CancelToken();

    private TextView status, detail, cancelBtn;
    private ProgressBar bar;
    private View bVerify, bFull, bExport, bClean;

    // estado da ultima execucao (so lido/escrito na thread de fundo ou depois de postar na UI)
    private volatile List<RepairEngine.Entry> lastEntries;
    private volatile RepairEngine.VerifyResult lastVerify;
    private volatile RepairEngine.RepairResult lastRepair;
    private volatile boolean haveSampJson;
    private volatile int gpuType;                 // 1=dxt 2=etc 3=pvr, 0=desconhecida
    private volatile List<String> listWarnings = new ArrayList<>();
    private volatile long lastByteMs;             // ultimo instante em que chegaram bytes (detector de travamento)
    private volatile boolean downloading;
    private volatile long fileStartMs, lastUiMs;
    private volatile String stallNote = "";

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle state) {
        View v = inflater.inflate(R.layout.fragment_repair, container, false);
        status = v.findViewById(R.id.repair_status);
        detail = v.findViewById(R.id.repair_detail);
        bar = v.findViewById(R.id.repair_progress);
        cancelBtn = v.findViewById(R.id.repair_btn_cancel);
        bVerify = v.findViewById(R.id.repair_btn_verify);
        bFull = v.findViewById(R.id.repair_btn_full);
        bExport = v.findViewById(R.id.repair_btn_export);
        bClean = v.findViewById(R.id.repair_btn_clean);

        bVerify.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View x) { startVerify(); } });
        bFull.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View x) { confirmFull(); } });
        bExport.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View x) { startExport(); } });
        bClean.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View x) { startClean(); } });
        cancelBtn.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View x) { cancel.cancel(); setStatus("Cancelando..."); } });
        setBusyUi(BUSY.get());
        return v;
    }

    private final Runnable stallTicker = new Runnable() {
        @Override public void run() {
            if (detail == null || !downloading) return;
            long idle = System.currentTimeMillis() - lastByteMs;
            if (idle > 8000) {
                stallNote = "\n⚠ Sem receber dados há " + (idle / 1000) + " s (conexão lenta ou servidor travado). Tentando de novo automaticamente.";
                CharSequence cur = detail.getText();
                String base = cur.toString();
                int k = base.indexOf("\n⚠");
                if (k >= 0) base = base.substring(0, k);
                detail.setText(base + stallNote);
            }
            ui.postDelayed(this, 1000);
        }
    };

    @Override
    public void onDestroyView() {
        ui.removeCallbacks(stallTicker);
        cancel.cancel();
        status = detail = cancelBtn = null;
        bar = null;
        bVerify = bFull = bExport = bClean = null;
        super.onDestroyView();
    }

    @Override
    public void onDestroy() {
        cancel.cancel();
        worker.shutdownNow();
        super.onDestroy();
    }

    // ------------------------------------------------------------------ acoes
    private void startVerify() {
        run(new Job() {
            @Override public void run(File base) throws PackException {
                List<RepairEngine.Entry> entries = loadEntries(base);
                RepairEngine.VerifyResult v = RepairEngine.verify(base, entries, stepProgress("Verificando"), cancel);
                lastVerify = v;
                lastRepair = null;
                post("Verificação concluída", summarize(v) + warnText());
                if (!v.bad.isEmpty()) askFix(v.bad.size());
            }
        });
    }

    private void askFix(final int n) {
        ui.post(new Runnable() {
            @Override public void run() {
                Activity a = getActivity();
                if (a == null || a.isFinishing() || !isAdded()) return;
                new AlertDialog.Builder(a)
                        .setTitle(R.string.mx_repair_fix_title)
                        .setMessage(getString(R.string.mx_repair_fix_msg, n))
                        .setNegativeButton(R.string.mx_repair_no, null)
                        .setPositiveButton(R.string.mx_repair_yes, new android.content.DialogInterface.OnClickListener() {
                            @Override public void onClick(android.content.DialogInterface d, int w) { startFixProblems(); }
                        }).show();
            }
        });
    }

    private void startFixProblems() {
        run(new Job() {
            @Override public void run(File base) throws PackException {
                RepairEngine.VerifyResult v = lastVerify;
                if (v == null || v.bad.isEmpty()) { post("Nada a corrigir", ""); return; }
                doRepair(base, RepairEngine.targetsOf(v), lastEntries);
            }
        });
    }

    private void confirmFull() {
        if (BUSY.get()) return;
        Activity a = getActivity();
        if (a == null || a.isFinishing()) return;
        new AlertDialog.Builder(a)
                .setTitle(R.string.mx_repair_full_title)
                .setMessage(R.string.mx_repair_full_msg)
                .setNegativeButton(R.string.mx_repair_no, null)
                .setPositiveButton(R.string.mx_repair_yes, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int w) { startFull(); }
                }).show();
    }

    private void startFull() {
        run(new Job() {
            @Override public void run(File base) throws PackException {
                List<RepairEngine.Entry> entries = loadEntries(base);
                doRepair(base, RepairEngine.allTargets(base, entries), entries);
            }
        });
    }

    /** Baixa os alvos (verificados), confere de novo e mostra o resultado. */
    private void doRepair(File base, List<RepairEngine.Entry> targets, List<RepairEngine.Entry> entries) throws PackException {
        final File fb = base;
        lastByteMs = System.currentTimeMillis();
        downloading = true;
        ui.post(new Runnable() { @Override public void run() { ui.removeCallbacks(stallTicker); ui.postDelayed(stallTicker, 1000); } });
        RepairEngine.RepairResult rr;
        try {
            rr = RepairEngine.repair(base, targets, urlResolver(), ATTEMPTS, SLEEP,
                new RepairEngine.FreeSpace() { @Override public long freeBytes() { return new StatFs(fb.getAbsolutePath()).getAvailableBytes(); } },
                null, byteProgress(), cancel);
        } finally {
            downloading = false;
        }
        lastRepair = rr;
        RepairEngine.VerifyResult v = RepairEngine.verify(base, entries, stepProgress("Conferindo"), cancel);
        lastVerify = v;
        String head = rr.failed.isEmpty() && v.bad.isEmpty() ? "Tudo certo" : "Concluído com pendências";
        post(head, "Corrigidos: " + rr.fixed + "  |  Falhas: " + rr.failed.size() + "  |  Baixado: " + (rr.downloadedBytes >> 20) + " MB\n" + summarize(v) + warnText());
    }

    private void startExport() {
        run(new Job() {
            @Override public void run(File base) throws PackException {
                String text = RepairEngine.formatReport(deviceInfo(base), base, lastVerify, lastRepair, logTail(base),
                        lastVerify == null ? "(nenhuma verificação feita nesta sessão: rode VERIFICAR ARQUIVOS antes para um relatório completo)" : "");
                File out = new File(base, "relatorio_correcao.txt");
                try (FileOutputStream f = new FileOutputStream(out)) { f.write(text.getBytes(StandardCharsets.UTF_8)); }
                catch (IOException e) { throw new PackException("não foi possível gravar o relatório: " + e.getMessage(), e); }
                post("Relatório gerado", out.getAbsolutePath());
                share(out);
            }
        });
    }

    private void startClean() {
        run(new Job() {
            @Override public void run(File base) {
                AtomicInstaller.recoverOnStartup(base);   // restaura o que faltar do .backup; apaga .staging/.backup/.downloads
                int stray = RepairEngine.cleanStrayTemp(base);
                post("Limpeza concluída", "Arquivos temporários removidos em texdb/SAMP: " + stray);
            }
        });
    }

    // ------------------------------------------------------------------ infraestrutura
    private interface Job { void run(File base) throws PackException; }

    private static final PackDownloader.Sleeper SLEEP = new PackDownloader.Sleeper() {
        @Override public void sleep(long ms) throws InterruptedException { Thread.sleep(ms); }
    };

    private void run(final Job job) {
        if (!BUSY.compareAndSet(false, true)) { setStatus("Já existe uma operação em andamento."); return; }
        final Activity act = getActivity();
        final File base = act == null ? null : act.getExternalFilesDir(null);
        cancel = new CancelToken();
        setBusyUi(true);
        try {
            worker.execute(new Runnable() {
                @Override public void run() {
                    try {
                        if (base == null) throw new PackException("armazenamento do aplicativo indisponível");
                        job.run(base);
                    } catch (CancelToken.Cancelled c) {
                        post("Cancelado", "");
                    } catch (PackException e) {
                        post("Erro", e.getMessage());
                    } catch (RuntimeException e) {
                        post("Erro inesperado", e.toString());
                    } finally {
                        BUSY.set(false);
                        ui.post(new Runnable() { @Override public void run() { setBusyUi(false); } });
                    }
                }
            });
        } catch (RuntimeException e) { // executor encerrado (fragment destruido)
            BUSY.set(false);
        }
    }

    /** client_config -> url_cache_files; samp.json (opcional, como no UpdateService); files.json. */
    private List<RepairEngine.Entry> loadEntries(File base) throws PackException {
        Activity a = getActivity();
        if (a == null) throw new PackException("tela fechada");
        post("Baixando listas do servidor...", "");
        String cfgText = FileDownloader.fetchText(AppEndpoints.clientConfigUrl(a), 2 << 20, ATTEMPTS, SLEEP, cancel);
        Map<String, Object> root = MiniJson.asMap(MiniJson.parse(cfgText));
        Map<String, Object> cfg = root == null ? null : MiniJson.asMap(root.get("client_config"));
        String filesUrl = cfg == null ? null : MiniJson.str(cfg, "url_cache_files");
        if (filesUrl == null || !(filesUrl.startsWith("http://") || filesUrl.startsWith("https://")))
            throw new PackException("client_config sem url_cache_files válido");

        List<RepairEngine.Entry> samp = null;
        try {
            String sj = FileDownloader.fetchText(AppEndpoints.sampJsonUrl(a), 8 << 20, ATTEMPTS, SLEEP, cancel);
            samp = RepairEngine.fromSampJson(sj);
        } catch (CancelToken.Cancelled c) {
            throw c;
        } catch (PackException e) {
            samp = null; // sem samp.json a SAMP/ vem do files.json (igual ao UpdateService)
        }
        haveSampJson = samp != null;
        List<String> warns = new ArrayList<>();
        String fj = FileDownloader.fetchText(filesUrl, 32 << 20, ATTEMPTS, SLEEP, cancel);
        List<RepairEngine.Entry> files = RepairEngine.fromFilesJson(fj, warns);
        List<RepairEngine.Entry> merged = RepairEngine.merge(samp, files);
        gpuType = detectGpu(a, base);
        List<RepairEngine.Entry> all = RepairEngine.forGpu(base, merged, gpuType, warns);
        listWarnings = warns;
        if (all.isEmpty()) throw new PackException("as listas do servidor não têm arquivos de texdb/SAMP");
        lastEntries = all;
        return all;
    }

    /** GPU gravada pelo Splash (1=dxt 2=etc 3=pvr); senao a do installed.json; senao 0 (desconhecida). */
    private static int detectGpu(Activity a, File base) {
        try {
            int g = a.getSharedPreferences("com.samp.mobile", Activity.MODE_PRIVATE).getInt("gpu_type", 0);
            if (g >= 1 && g <= 3) return g;
        } catch (RuntimeException ignored) { }
        try {
            InstalledState st = InstalledState.read(base);
            if (st != null && st.gpu >= 1 && st.gpu <= 3) return st.gpu;
        } catch (RuntimeException ignored) { }
        return 0;
    }

    private RepairEngine.UrlResolver urlResolver() {
        final Activity a = getActivity();
        final boolean samp = haveSampJson;
        return new RepairEngine.UrlResolver() {
            @Override public String urlFor(String rel) {
                String enc = Uri.encode(rel, "/");
                if (samp && rel.regionMatches(true, 0, "SAMP/", 0, 5)) return AppEndpoints.sampFileUrl(a, enc);
                return AppEndpoints.fileUrl(a, enc);
            }
        };
    }

    private RepairEngine.Progress stepProgress(final String what) {
        return new RepairEngine.Progress() {
            private int last = -1;
            @Override public void onStep(final String path, final int done, final int total) {
                if (done == last) return;
                last = done;
                ui.post(new Runnable() { @Override public void run() {
                    if (status == null) return;
                    status.setText(what + " (" + done + "/" + total + ")");
                    detail.setText(path);
                    bar.setProgress(total == 0 ? 0 : (int) (1000L * done / total));
                } });
            }
        };
    }

    /** Progresso por BYTE: "12,3 / 140 MB - 1,8 MB/s", barra pelo total. Atualiza a tela no maximo 4x por segundo. */
    private RepairEngine.ByteProgress byteProgress() {
        return new RepairEngine.ByteProgress() {
            private int curIdx = -1;
            @Override public void onBytes(final int idx, final int total, final String path, final long fd, final long ft, final long ad, final long at) {
                long now = System.currentTimeMillis();
                if (idx != curIdx) { curIdx = idx; fileStartMs = now; }
                if (fd > 0) lastByteMs = now; else if (fd == 0) lastByteMs = now;
                boolean last = fd >= ft;
                if (!last && now - lastUiMs < 250) return;
                lastUiMs = now;
                final double secs = Math.max(0.001, (now - fileStartMs) / 1000.0);
                final double mbps = fd / 1048576.0 / secs;
                ui.post(new Runnable() { @Override public void run() {
                    if (status == null) return;
                    status.setText("Baixando (" + (idx + 1) + "/" + total + ")");
                    detail.setText(path + "\n" + String.format(java.util.Locale.US, "%.1f / %.1f MB  ·  %.2f MB/s", fd / 1048576.0, ft / 1048576.0, mbps)
                            + (now - lastByteMs > 8000 ? stallNote : ""));
                    bar.setProgress(at == 0 ? 0 : (int) (1000L * ad / at));
                } });
            }
        };
    }

    private String warnText() {
        StringBuilder b = new StringBuilder();
        for (String w : listWarnings) b.append("\n• ").append(w);
        return b.toString();
    }

    private static String summarize(RepairEngine.VerifyResult v) {
        StringBuilder b = new StringBuilder();
        b.append("Arquivos: ").append(v.total).append("  |  OK: ").append(v.ok).append("  |  Com problema: ").append(v.bad.size());
        if (v.sizeOnly > 0) b.append("\n").append(v.sizeOnly).append(" arquivo(s) só conferidos por tamanho (a lista do servidor não traz SHA-256).");
        int n = 0;
        for (RepairEngine.Problem p : v.bad) {
            if (n++ >= 12) { b.append("\n... e mais ").append(v.bad.size() - 12); break; }
            b.append("\n• ").append(p.entry.path).append(" — ").append(p.why);
        }
        for (String w : v.warnings) b.append("\n! ").append(w);
        return b.toString();
    }

    private void post(final String head, final String body) {
        ui.post(new Runnable() { @Override public void run() {
            if (status == null) return;
            status.setText(head);
            detail.setText(body);
        } });
    }

    private void setStatus(String s) { if (status != null) status.setText(s); }

    private void setBusyUi(boolean busy) {
        if (bVerify == null) return;
        for (View v : new View[]{bVerify, bFull, bExport, bClean}) { v.setEnabled(!busy); v.setAlpha(busy ? 0.5f : 1f); }
        cancelBtn.setVisibility(busy ? View.VISIBLE : View.GONE);
        bar.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (!busy) bar.setProgress(0);
    }

    private String deviceInfo(File base) {
        StringBuilder b = new StringBuilder();
        b.append("aparelho: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" | Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(')')
                .append(" | ABI ").append(Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?");
        try {
            StatFs fs = new StatFs(base.getAbsolutePath());
            b.append("\nespaço livre: ").append(fs.getAvailableBytes() >> 20).append(" MB de ").append(fs.getTotalBytes() >> 20).append(" MB");
        } catch (RuntimeException ignored) { }
        try {
            Activity a = getActivity();
            if (a != null) b.append("\napp: ").append(a.getPackageName()).append(' ')
                    .append(a.getPackageManager().getPackageInfo(a.getPackageName(), 0).versionName);
        } catch (Exception ignored) { }
        b.append("\nGPU do launcher: ").append(gpuType == 1 ? "DXT" : gpuType == 2 ? "ETC" : gpuType == 3 ? "PVR" : "desconhecida");
        b.append("\nsamp.json: ").append(haveSampJson ? "usado" : "indisponível (SAMP/ pelo files.json)");
        return b.toString();
    }

    private static String logTail(File base) {
        String t = RepairEngine.tail(new File(base, "SAMP/samp_log.txt"), 12 * 1024);
        if (t.isEmpty()) t = RepairEngine.tail(new File(base, "samp_log.txt"), 12 * 1024);
        return t;
    }

    private void share(final File f) {
        ui.post(new Runnable() { @Override public void run() {
            Activity a = getActivity();
            if (a == null || a.isFinishing() || !isAdded()) return;
            try {
                Uri uri = FileProvider.getUriForFile(a, a.getPackageName() + ".provider", f);
                Intent i = new Intent(Intent.ACTION_SEND);
                i.setType("text/plain");
                i.putExtra(Intent.EXTRA_STREAM, uri);
                i.putExtra(Intent.EXTRA_SUBJECT, "Relatório de correção - Máximo RP");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                a.startActivity(Intent.createChooser(i, "Enviar relatório"));
            } catch (RuntimeException e) {
                post("Relatório salvo (compartilhar indisponível)", f.getAbsolutePath());
            }
        } });
    }
}
