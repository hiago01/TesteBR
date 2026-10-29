#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_packs.py - gera os pacotes ZIP do cliente (core + gpu_*) e o trecho "packs" do client_config.json.

Uso (Windows, Python 3.8+):
    python build_packs.py --src C:\\cache-server\\files --out C:\\cache-server\\packs ^
        --base-url http://201.33.84.96:8080/packs --version 1

Regras (devem bater com GameFilePolicy.java do app):
  * SO LE a pasta de origem; nunca altera nada nela. Tudo e escrito em --out.
  * Nomes em UTF-8, separador "/", caixa preservada. ERRO se dois caminhos diferirem so pela caixa.
  * Ignora lixo de SO/servidor e arquivos gerados pelo jogo no celular (lista abaixo).
  * core = tudo que nao e de GPU; gpu_dxt/gpu_etc/gpu_pvr = ".dxt." / ".etc." / ".pvr." no caminho,
    EXCETO caminhos com "samp", "player" ou "menu" (sempre core, sem diferenciar caixa) - mesma regra do app.
  * Cada zip tem no maximo --max-mb (padrao 900 MiB) de dados: pacotes maiores sao divididos em
    core, core_2, core_3... (sem Zip64; o app aceita varios pacotes com gpu 0).
  * ZIP_STORED para .img .dat .png .jpeg .jpg .tmb .toc (ja comprimidos/grandes); DEFLATED para o resto.
  * Cada zip leva a entrada ".pack_manifest.json" (path, size, sha256 de cada arquivo) que o app confere.
  * Imprime o trecho para colar no client_config.json (dentro de "client_config").
"""
import argparse
import hashlib
import json
import os
import sys
import zipfile

IGNORE_BASENAMES = {".ds_store", "thumbs.db", "desktop.ini", ".htaccess"}
IGNORE_PATHS = {
    "lista.txt", "files.json",
    "cinfo.bin", "models/minfo.bin",
    "samp/cinfo.sig", "samp/col_index.txt", "samp/favorites.json", "samp/svlog.txt",
    "samp/sampvoice/svconfig.ini",
    "settings.ini", "samp/settings.ini",
    "samp_log.txt", "gta_sa.set", "gtasatelem.set",
    "installed.json",
}
STORED_EXT = {".img", ".dat", ".png", ".jpeg", ".jpg", ".tmb", ".toc"}
MANIFEST_NAME = ".pack_manifest.json"
GPU_NAMES = {0: "core", 1: "gpu_dxt", 2: "gpu_etc", 3: "gpu_pvr"}
FIXED_DATE = (2020, 1, 1, 0, 0, 0)  # zips reproduziveis (mesmo conteudo => mesmo hash)
MAX_ZIP64 = 0xFFFFFFFF


def is_ignored(rel):
    low = rel.lower()
    if low in IGNORE_PATHS:
        return True
    return low.rsplit("/", 1)[-1] in IGNORE_BASENAMES


def gpu_of(rel):
    low = rel.lower()
    if "samp" in low or "player" in low or "menu" in low:
        return 0
    if ".dxt." in low:
        return 1
    if ".etc." in low:
        return 2
    if ".pvr." in low:
        return 3
    return 0


def sha256_file(path, bufsize=1024 * 1024):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def scan(src):
    items = []          # (rel, abs, size)
    seen_lower = {}
    zero = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for name in sorted(files):
            ab = os.path.join(root, name)
            rel = os.path.relpath(ab, src).replace(os.sep, "/")
            if is_ignored(rel):
                continue
            if os.path.islink(ab):
                sys.exit("ERRO: link simbolico nao suportado: " + rel)
            low = rel.lower()
            if low in seen_lower:
                sys.exit("ERRO: dois caminhos diferem so pela caixa (Android e case-sensitive):\n  %s\n  %s"
                         % (seen_lower[low], rel))
            seen_lower[low] = rel
            try:
                rel.encode("utf-8")
            except UnicodeEncodeError:
                sys.exit("ERRO: nome nao representavel em UTF-8: %r" % rel)
            size = os.path.getsize(ab)
            if size == 0:
                zero.append(rel)
            if size >= MAX_ZIP64:
                sys.exit("ERRO: arquivo >= 4 GB nao cabe em zip sem Zip64: " + rel)
            items.append((rel, ab, size))
    return items, zero


def split_groups(items, max_bytes):
    groups, cur, cur_sz = [], [], 0
    for it in items:
        if cur and cur_sz + it[2] > max_bytes:
            groups.append(cur)
            cur, cur_sz = [], 0
        cur.append(it)
        cur_sz += it[2]
    if cur:
        groups.append(cur)
    return groups


def build_zip(zpath, pack_name, group):
    manifest = {"name": pack_name, "files": []}
    for rel, ab, size in group:
        manifest["files"].append({"path": rel, "size": size, "sha256": sha256_file(ab)})
    mbytes = json.dumps(manifest, ensure_ascii=False, indent=0).encode("utf-8")
    tmp = zpath + ".tmp"
    with zipfile.ZipFile(tmp, "w", allowZip64=False) as z:
        zi = zipfile.ZipInfo(MANIFEST_NAME, FIXED_DATE)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = (0o100644 & 0xFFFF) << 16
        z.writestr(zi, mbytes)
        for rel, ab, size in group:
            zi = zipfile.ZipInfo(rel, FIXED_DATE)
            ext = os.path.splitext(rel)[1].lower()
            zi.compress_type = zipfile.ZIP_STORED if ext in STORED_EXT else zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100644 & 0xFFFF) << 16   # arquivo regular (nunca symlink)
            with open(ab, "rb") as fi, z.open(zi, "w") as fo:
                while True:
                    b = fi.read(1024 * 1024)
                    if not b:
                        break
                    fo.write(b)
    os.replace(tmp, zpath)
    zsize = os.path.getsize(zpath)
    if zsize >= MAX_ZIP64:
        sys.exit("ERRO: %s ficou com %d bytes (>= 4 GB). Use --max-mb menor." % (zpath, zsize))
    return zsize, sum(g[2] for g in group), manifest


def main():
    ap = argparse.ArgumentParser(description="Gera pacotes ZIP do cliente + trecho do client_config.json")
    ap.add_argument("--src", default=r"C:\cache-server\files", help="pasta com os arquivos do jogo (somente leitura)")
    ap.add_argument("--out", default=r"C:\cache-server\packs", help="pasta onde os zips e o manifest serao escritos")
    ap.add_argument("--base-url", required=True, help="URL publica da pasta dos zips, ex.: http://IP:8080/packs")
    ap.add_argument("--version", required=True, help="versao do conteudo (letras/numeros . _ -), ex.: 1")
    ap.add_argument("--max-mb", type=int, default=900, help="dados maximos por zip (MiB); padrao 900")
    a = ap.parse_args()

    src = os.path.abspath(a.src)
    out = os.path.abspath(a.out)
    if not os.path.isdir(src):
        sys.exit("ERRO: pasta de origem nao existe: " + src)
    if out == src or out.startswith(src + os.sep):
        sys.exit("ERRO: --out nao pode ficar dentro de --src (a origem nao pode ser alterada)")
    if not a.version or not all(c.isalnum() or c in "._-" for c in a.version):
        sys.exit("ERRO: --version invalida")
    os.makedirs(out, exist_ok=True)

    items, zero = scan(src)
    if not items:
        sys.exit("ERRO: nenhum arquivo encontrado em " + src)
    if zero:
        print("AVISO: %d arquivo(s) de 0 bytes (verifique se sao intencionais):" % len(zero))
        for z in zero[:50]:
            print("   " + z)
        if len(zero) > 50:
            print("   ... +%d" % (len(zero) - 50))

    by_gpu = {0: [], 1: [], 2: [], 3: []}
    for it in items:
        by_gpu[gpu_of(it[0])].append(it)

    max_bytes = a.max_mb * 1024 * 1024
    packs, full_manifest = [], {"version": a.version, "packs": []}
    base_url = a.base_url.rstrip("/")
    for gpu in (0, 1, 2, 3):
        if not by_gpu[gpu]:
            continue
        groups = split_groups(by_gpu[gpu], max_bytes)
        for i, g in enumerate(groups):
            name = GPU_NAMES[gpu] if i == 0 else "%s_%d" % (GPU_NAMES[gpu], i + 1)
            zpath = os.path.join(out, name + ".zip")
            print("gerando %s (%d arquivos)..." % (zpath, len(g)))
            zsize, unpacked, manifest = build_zip(zpath, name, g)
            digest = sha256_file(zpath)
            packs.append({"name": name, "gpu": gpu, "url": "%s/%s.zip" % (base_url, name),
                          "size": zsize, "sha256": digest, "unpacked": unpacked})
            full_manifest["packs"].append({"name": name, "gpu": gpu, "size": zsize, "sha256": digest,
                                           "unpacked": unpacked, "files": manifest["files"]})

    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(full_manifest, f, ensure_ascii=False, indent=1)

    snippet = {"version": a.version, "packs": packs}
    print("\n=== cole dentro de \"client_config\" no client_config.json (mantenha url_cache_files) ===")
    print(json.dumps(snippet, indent=2))
    print("\nTotal: %d arquivos em %d pacote(s). Copie os .zip para %s no servidor." % (len(items), len(packs), a.base_url))


if __name__ == "__main__":
    main()
