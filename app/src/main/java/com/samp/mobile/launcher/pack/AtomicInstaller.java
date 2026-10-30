package com.samp.mobile.launcher.pack;

import java.io.File;
import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/**
 * Move os arquivos JA extraidos e verificados (staging) para o destino final, por rename (mesmo volume).
 * Arquivo existente vai para .backup/ antes de ser substituido; qualquer falha restaura tudo (rollback):
 * novos arquivos removidos, antigos devolvidos e pastas criadas por nos removidas.
 * Arquivos protegidos (GameFilePolicy) nunca sao tocados.
 */
public final class AtomicInstaller {
    private AtomicInstaller() {}

    /** Ponto de injecao de falha (testes). */
    public interface FailPoint { void beforeMove(int index, String path) throws IOException; }

    public static final class Committed {
        public final List<String> paths = new ArrayList<>();
        public int replaced = 0;
    }

    public static Committed commit(List<ZipPackExtractor.ExtractedFile> files, File dest, File backupDir, FailPoint fp) throws PackException {
        final List<File> placed = new ArrayList<>();
        final List<File[]> backups = new ArrayList<>();   // {original, backup}
        final List<File> createdDirs = new ArrayList<>();
        Committed ok = new Committed();
        try {
            int idx = 0;
            for (ZipPackExtractor.ExtractedFile f : files) {
                if (GameFilePolicy.isProtected(f.path)) continue;
                File target = PathSafety.resolveInside(dest, f.path);
                mkParents(target.getParentFile(), dest, createdDirs);
                if (target.isDirectory()) throw new IOException("o destino e uma pasta: " + f.path);
                if (target.exists()) {
                    File bk = PathSafety.resolveInside(backupDir, f.path);
                    mkParents(bk.getParentFile(), backupDir, new ArrayList<File>());
                    move(target, bk);
                    backups.add(new File[]{target, bk});
                    ok.replaced++;
                }
                if (fp != null) fp.beforeMove(idx, f.path);
                move(f.staged, target);
                placed.add(target);
                ok.paths.add(f.path);
                idx++;
            }
            return ok;
        } catch (IOException | PackException e) {
            rollback(placed, backups, createdDirs);
            if (e instanceof PackException) throw (PackException) e;
            throw new PackException("falha ao instalar (rollback executado): " + e.getMessage(), e);
        }
    }

    private static void mkParents(File dir, File root, List<File> created) throws IOException {
        if (dir == null || dir.isDirectory()) return;
        mkParents(dir.getParentFile(), root, created);
        if (!dir.mkdir() && !dir.isDirectory()) throw new IOException("nao foi possivel criar a pasta " + dir);
        created.add(dir);
    }

    private static void move(File from, File to) throws IOException {
        try {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException e) {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void rollback(List<File> placed, List<File[]> backups, List<File> createdDirs) {
        for (int i = placed.size() - 1; i >= 0; i--) placed.get(i).delete();
        for (int i = backups.size() - 1; i >= 0; i--) {
            File[] b = backups.get(i);
            try { move(b[1], b[0]); } catch (IOException ignored) { /* melhor esforco: o .backup fica para a recuperacao no proximo inicio */ }
        }
        for (int i = createdDirs.size() - 1; i >= 0; i--) createdDirs.get(i).delete(); // so remove se vazia
    }

    /** Apaga uma arvore (staging/backup/downloads). */
    public static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        if (f.isDirectory() && !Files.isSymbolicLink(f.toPath())) {
            File[] kids = f.listFiles();
            if (kids != null) for (File k : kids) deleteTree(k);
        }
        f.delete();
    }

    /**
     * Recuperacao no inicio: restaura do .backup o que estiver FALTANDO no destino (o app morreu entre
     * "mover o antigo" e "colocar o novo"), depois apaga .staging, .backup e as partes de download.
     * Nunca toca em arquivos do jogo que existem.
     */
    public static void recoverOnStartup(File filesDir) {
        File backup = new File(filesDir, GameFilePolicy.DIR_BACKUP);
        if (backup.isDirectory()) {
            // layout: .backup/<versao>/<caminho relativo>
            File[] vers = backup.listFiles();
            if (vers != null) for (File v : vers) if (v.isDirectory()) restoreMissing(v, v, filesDir);
        }
        deleteTree(backup);
        deleteTree(new File(filesDir, GameFilePolicy.DIR_STAGING));
        // .downloads: os parciais do PRDownloader (*.temp) e zips completos sao MANTIDOS para retomar o download;
        // so os *.part (motor sem retomada) sao lixo. Quem termina a instalacao apaga a pasta inteira.
        File dl = new File(filesDir, GameFilePolicy.DIR_DOWNLOADS);
        File[] parts = dl.listFiles();
        if (parts != null) for (File f : parts) if (f.getName().endsWith(".part")) f.delete();
    }

    private static void restoreMissing(File root, File cur, File filesDir) {
        File[] kids = cur.listFiles();
        if (kids == null) return;
        for (File k : kids) {
            if (k.isDirectory()) { restoreMissing(root, k, filesDir); continue; }
            String rel = root.toURI().relativize(k.toURI()).getPath();
            if (rel.isEmpty() || GameFilePolicy.isProtected(rel)) continue;
            File target = new File(filesDir, rel);
            if (!target.exists()) {
                File p = target.getParentFile();
                if (p != null) p.mkdirs();
                try { move(k, target); } catch (IOException ignored) { }
            }
        }
    }
}
