package com.samp.mobile.launcher.pack;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Extracao SEGURA de um pacote zip para um diretorio de staging:
 *  - le o diretorio central por conta propria: rejeita Zip64, multi-disco, criptografia, metodos != STORED/DEFLATED,
 *    entradas duplicadas / que diferem so pela caixa e LINKS SIMBOLICOS;
 *  - zip-slip: nome validado (PathSafety) e caminho canonico precisa ficar dentro do staging;
 *  - cada arquivo: tamanho lido == declarado, CRC32 confere (inclusive STORED, que o ZipFile do Java nao confere),
 *    e tamanho/SHA-256 conferem com o manifest embutido (obrigatorio);
 *  - arquivos protegidos (GameFilePolicy.isProtected) sao PULADOS (nunca extraidos);
 *  - nada e escrito fora do staging; em caso de erro o chamador apaga o staging.
 */
public final class ZipPackExtractor {
    private ZipPackExtractor() {}

    public interface Progress { void onBytes(long delta); }

    public static final class ExtractedFile {
        public final String path;
        public final long size;
        public final String sha256;
        public final File staged;
        public ExtractedFile(String path, long size, String sha256, File staged) {
            this.path = path; this.size = size; this.sha256 = sha256; this.staged = staged;
        }
    }

    public static final class Result {
        public final List<ExtractedFile> files = new ArrayList<>();
        public final List<String> skippedProtected = new ArrayList<>();
        public long totalBytes() { long t = 0; for (ExtractedFile f : files) t += f.size; return t; }
    }

    private static final class Cd {
        String name;
        long compressedSize, size;
        int method, flags;
        boolean dir;
    }

    private static final long MAX_ENTRY_BYTES = 0xFFFFFFFEL;

    public static Result extract(File zip, File staging, Progress progress, CancelToken cancel) throws PackException {
        if (zip == null || !zip.isFile()) throw new PackException("zip inexistente");
        List<Cd> cd = readCentralDirectory(zip);
        if (cd.isEmpty()) throw new PackException("zip vazio");

        // duplicadas e caixa
        Set<String> exact = new HashSet<>();
        Map<String, String> lower = new HashMap<>();
        for (Cd e : cd) {
            String n = e.name;
            if (!exact.add(n)) throw new PackException("entrada duplicada no zip: " + n);
            String lc = n.toLowerCase(Locale.ROOT);
            String prev = lower.put(lc, n);
            if (prev != null) throw new PackException("entradas que diferem so pela caixa: " + prev + " / " + n);
        }

        if (!staging.isDirectory() && !staging.mkdirs() && !staging.isDirectory()) throw new PackException("nao foi possivel criar o staging");
        final String stagingCanon;
        try { stagingCanon = staging.getCanonicalPath(); } catch (IOException e) { throw new PackException("staging invalido", e); }

        Result res = new Result();
        try (ZipFile zf = new ZipFile(zip)) {
            // 1) manifest embutido (obrigatorio)
            ZipEntry me = zf.getEntry(PackManifest.ENTRY_NAME);
            if (me == null) throw new PackException("zip sem " + PackManifest.ENTRY_NAME);
            if (me.getSize() < 2 || me.getSize() > PackManifest.MAX_BYTES) throw new PackException("manifest com tamanho invalido");
            byte[] mbytes = readAll(zf.getInputStream(me), (int) me.getSize(), cancel);
            CRC32 mcrc = new CRC32();
            mcrc.update(mbytes);
            if (me.getCrc() != -1 && mcrc.getValue() != me.getCrc()) throw new PackException("CRC do manifest nao confere");
            PackManifest manifest = PackManifest.parse(mbytes);

            Set<String> seen = new HashSet<>();
            byte[] buf = new byte[256 * 1024];
            MessageDigest sha;
            try { sha = MessageDigest.getInstance("SHA-256"); } catch (NoSuchAlgorithmException e) { throw new PackException("SHA-256 indisponivel", e); }

            for (Cd c : cd) {
                cancel.check();
                if (c.name.equals(PackManifest.ENTRY_NAME)) continue;
                if (c.dir) continue;
                String path = PathSafety.validateRelative(c.name);
                PackManifest.Item item = manifest.files.get(path);
                if (GameFilePolicy.isProtected(path)) { res.skippedProtected.add(path); continue; }
                if (item == null) throw new PackException("arquivo fora do manifest: " + path);
                ZipEntry ze = zf.getEntry(c.name);
                if (ze == null) throw new PackException("entrada ilegivel: " + path);
                if (ze.getSize() != c.size) throw new PackException("tamanho inconsistente (local x central): " + path);
                if (c.size != item.size) throw new PackException("tamanho difere do manifest: " + path + " (" + c.size + " x " + item.size + ")");

                File out = new File(staging, path);
                String canon;
                try { canon = out.getCanonicalPath(); } catch (IOException e) { throw new PackException("caminho invalido: " + path, e); }
                if (!canon.startsWith(stagingCanon + File.separator)) throw new PackException("zip-slip detectado: " + path);
                File parent = out.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) throw new PackException("nao foi possivel criar a pasta de " + path);

                CRC32 crc = new CRC32();
                sha.reset();
                long total = 0;
                boolean ok = false;
                try (InputStream in = zf.getInputStream(ze); OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        cancel.check();
                        total += n;
                        if (total > c.size) throw new PackException("entrada maior que o declarado (possivel zip bomb): " + path);
                        crc.update(buf, 0, n);
                        sha.update(buf, 0, n);
                        os.write(buf, 0, n);
                        if (progress != null) progress.onBytes(n);
                    }
                    ok = true;
                } catch (java.util.zip.ZipException ze2) {
                    throw new PackException("zip corrompido em " + path + ": " + ze2.getMessage(), ze2);
                } catch (IOException ioe) {
                    throw new PackException("erro de E/S extraindo " + path + ": " + ioe.getMessage(), ioe);
                } finally {
                    if (!ok) out.delete();
                }
                if (total != c.size) { out.delete(); throw new PackException("entrada truncada: " + path + " (" + total + " de " + c.size + ")"); }
                if (ze.getCrc() != -1 && crc.getValue() != ze.getCrc()) { out.delete(); throw new PackException("CRC invalido: " + path); }
                String digest = Hex.of(sha.digest());
                if (item.sha256 != null && !item.sha256.equals(digest)) { out.delete(); throw new PackException("SHA-256 do arquivo difere do manifest: " + path); }
                seen.add(path);
                res.files.add(new ExtractedFile(path, total, digest, out));
            }
            // 2) tudo que o manifest promete precisa ter vindo (exceto protegidos)
            for (String p : manifest.files.keySet()) {
                if (!seen.contains(p) && !GameFilePolicy.isProtected(p)) throw new PackException("arquivo do manifest ausente no zip: " + p);
            }
        } catch (PackException e) {
            throw e;
        } catch (java.util.zip.ZipException e) {
            throw new PackException("zip invalido: " + e.getMessage(), e);
        } catch (IOException e) {
            throw new PackException("nao foi possivel ler o zip: " + e.getMessage(), e);
        }
        return res;
    }

    private static byte[] readAll(InputStream in, int len, CancelToken cancel) throws PackException {
        try (InputStream is = new BufferedInputStream(in)) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream(len);
            byte[] b = new byte[8192];
            int n;
            while ((n = is.read(b)) > 0) {
                cancel.check();
                bo.write(b, 0, n);
                if (bo.size() > len) throw new PackException("manifest maior que o declarado");
            }
            if (bo.size() != len) throw new PackException("manifest truncado");
            return bo.toByteArray();
        } catch (IOException e) {
            throw new PackException("manifest ilegivel: " + e.getMessage(), e);
        }
    }

    // ------------------------------------------------------------------------------------
    // Diretorio central (somente leitura, sem Zip64)
    // ------------------------------------------------------------------------------------
    private static int u16(byte[] b, int o) { return (b[o] & 0xff) | ((b[o + 1] & 0xff) << 8); }
    private static long u32(byte[] b, int o) { return (u16(b, o) | ((long) u16(b, o + 2) << 16)) & 0xFFFFFFFFL; }

    static List<Cd> readCentralDirectory(File zip) throws PackException {
        long len = zip.length();
        if (len < 22) throw new PackException("arquivo muito pequeno para ser um zip (truncado?)");
        try (RandomAccessFile raf = new RandomAccessFile(zip, "r")) {
            int tailLen = (int) Math.min(len, 22 + 65535);
            byte[] tail = new byte[tailLen];
            raf.seek(len - tailLen);
            raf.readFully(tail);
            int eocd = -1;
            for (int i = tailLen - 22; i >= 0; i--) {
                if (u32(tail, i) == 0x06054b50L) { eocd = i; break; }
            }
            if (eocd < 0) throw new PackException("fim do zip nao encontrado (arquivo truncado ou corrompido)");
            int disk = u16(tail, eocd + 4), cdDisk = u16(tail, eocd + 6);
            int nThis = u16(tail, eocd + 8), nTotal = u16(tail, eocd + 10);
            long cdSize = u32(tail, eocd + 12), cdOff = u32(tail, eocd + 16);
            if (disk != 0 || cdDisk != 0 || nThis != nTotal) throw new PackException("zip multi-disco nao suportado");
            if (nTotal == 0xFFFF || cdSize == 0xFFFFFFFFL || cdOff == 0xFFFFFFFFL) throw new PackException("Zip64 nao suportado (divida em pacotes menores)");
            if (cdOff + cdSize > len || cdSize > 64L * 1024 * 1024) throw new PackException("diretorio central invalido");
            byte[] cdb = new byte[(int) cdSize];
            raf.seek(cdOff);
            raf.readFully(cdb);
            List<Cd> out = new ArrayList<>();
            int p = 0;
            for (int k = 0; k < nTotal; k++) {
                if (p + 46 > cdb.length || u32(cdb, p) != 0x02014b50L) throw new PackException("diretorio central corrompido");
                int madeBy = u16(cdb, p + 4);
                int flags = u16(cdb, p + 8);
                int method = u16(cdb, p + 10);
                long csize = u32(cdb, p + 20), usize = u32(cdb, p + 24);
                int nameLen = u16(cdb, p + 28), extraLen = u16(cdb, p + 30), commLen = u16(cdb, p + 32);
                int diskStart = u16(cdb, p + 34);
                long extAttr = u32(cdb, p + 38);
                if (p + 46 + nameLen + extraLen + commLen > cdb.length) throw new PackException("diretorio central corrompido");
                String name = new String(cdb, p + 46, nameLen, StandardCharsets.UTF_8);
                if (diskStart != 0) throw new PackException("zip multi-disco nao suportado");
                if ((flags & 1) != 0) throw new PackException("entrada criptografada: " + name);
                if (method != 0 && method != 8) throw new PackException("metodo de compressao nao suportado (" + method + "): " + name);
                if (csize == 0xFFFFFFFFL || usize == 0xFFFFFFFFL) throw new PackException("Zip64 nao suportado: " + name);
                if (usize > MAX_ENTRY_BYTES) throw new PackException("entrada grande demais: " + name);
                int hostOs = (madeBy >> 8) & 0xff;
                if (hostOs == 3) { // Unix: modo em extAttr >> 16
                    long mode = (extAttr >> 16) & 0xFFFF;
                    long fmt = mode & 0xF000;
                    if (fmt == 0xA000) throw new PackException("link simbolico rejeitado: " + name);
                    if (fmt != 0 && fmt != 0x8000 && fmt != 0x4000) throw new PackException("tipo de arquivo especial rejeitado: " + name);
                }
                Cd c = new Cd();
                c.name = name;
                c.method = method;
                c.flags = flags;
                c.compressedSize = csize;
                c.size = usize;
                c.dir = name.endsWith("/");
                if (c.dir) {
                    PathSafety.validateRelative(name.substring(0, name.length() - 1));
                } else {
                    PathSafety.validateRelative(name);
                }
                out.add(c);
                p += 46 + nameLen + extraLen + commLen;
            }
            return out;
        } catch (IOException e) {
            throw new PackException("zip ilegivel (truncado?): " + e.getMessage(), e);
        }
    }
}
