#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gerar_samp_json.py - le a pasta SAMP do cache do servidor e gera SO o samp.json dela.

    py gerar_samp_json.py --src C:\\cache-server\\SAMP --out C:\\cache-server\\samp.json

* NAO ignora nenhum arquivo (nem .DS_Store, settings.ini, favorites.json, svlog.txt, cinfo.sig, col_index.txt...).
* So le a pasta; nunca altera nada nela. Grava apenas o samp.json.
* Cada item: {"name": nome, "path": "SAMP/<caminho>", "size": bytes, "sha256": "..."}; caminhos em UTF-8 com "/",
  caixa preservada. ERRO se dois caminhos diferirem so pela caixa (Android e case-sensitive).
* O launcher baixa cada arquivo em  http://IP:8080/<path>  (ex.: http://IP:8080/SAMP/dff/x.dff), entao a pasta
  C:\\cache-server\\SAMP precisa ficar na raiz do servidor HTTP (ao lado do launcher.apk) e o samp.json em C:\\cache-server.
* --src tambem aceita um .zip da pasta SAMP (com ou sem a pasta raiz "SAMP/" dentro).
"""
import argparse
import hashlib
import json
import os
import sys
import zipfile


def sha_stream(f):
    h = hashlib.sha256()
    while True:
        b = f.read(1 << 20)
        if not b:
            break
        h.update(b)
    return h.hexdigest()


def scan_dir(src, empty_dirs):
    out = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        if not dirs and not files and root != src:
            empty_dirs.append(os.path.relpath(root, src).replace(os.sep, "/"))
        for n in sorted(files):
            ab = os.path.join(root, n)
            if os.path.islink(ab):
                sys.exit("ERRO: link simbolico nao suportado: " + ab)
            rel = os.path.relpath(ab, src).replace(os.sep, "/")
            with open(ab, "rb") as f:
                out.append((rel, os.path.getsize(ab), sha_stream(f)))
    return out


def scan_zip(path, empty_dirs):
    z = zipfile.ZipFile(path)
    bad = z.testzip()
    if bad:
        sys.exit("ERRO: zip corrompido (CRC) em: " + bad)
    out = []
    zdirs = []
    for i in z.infolist():
        n = i.filename.replace("\\", "/")
        if n.endswith("/") or i.is_dir():
            dn = n.rstrip("/")
            if dn:
                zdirs.append(dn)
            continue
        if n.startswith("/") or ".." in n.split("/") or "//" in n:
            sys.exit("ERRO: caminho inseguro no zip: " + n)
        out.append((n, i, None))
    names = [n for n, _, _ in out]
    strip = bool(names) and all(n.split("/", 1)[0].lower() == "samp" and "/" in n for n in names)
    have = set()
    for n in names:
        parts = n.split("/")[:-1]
        for k in range(1, len(parts) + 1):
            have.add("/".join(parts[:k]))
    for d in zdirs:
        if d not in have:  # pasta sem nenhum arquivo dentro
            empty_dirs.append(d.split("/", 1)[1] if strip and "/" in d else d)
    res = []
    for n, i, _ in out:
        rel = n.split("/", 1)[1] if strip else n
        with z.open(i) as f:
            res.append((rel, i.file_size, sha_stream(f)))
    return sorted(res)


def main():
    ap = argparse.ArgumentParser(description="Gera o samp.json da pasta SAMP (sem ignorar nada)")
    ap.add_argument("--src", default=r"C:\cache-server\SAMP", help="pasta SAMP (ou .zip dela)")
    ap.add_argument("--out", default=r"C:\cache-server\samp.json", help="arquivo samp.json de saida")
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    out = os.path.abspath(a.out)
    if not os.path.exists(src):
        sys.exit("ERRO: nao existe: " + src)
    if os.path.isdir(src) and (out == src or out.startswith(src + os.sep)):
        sys.exit("ERRO: --out nao pode ficar dentro da pasta SAMP (ela nao pode ser alterada)")
    empty_dirs = []
    items = scan_dir(src, empty_dirs) if os.path.isdir(src) else scan_zip(src, empty_dirs)
    empty_dirs = sorted(d for d in set(empty_dirs) if d and d.lower() != "samp")
    if not items:
        sys.exit("ERRO: nenhum arquivo encontrado em " + src)
    seen, zero = {}, []
    files = []
    for rel, size, sha in sorted(items):
        low = rel.lower()
        if low in seen:
            sys.exit("ERRO: dois caminhos diferem so pela caixa:\n  %s\n  %s" % (seen[low], rel))
        seen[low] = rel
        try:
            rel.encode("utf-8")
        except UnicodeEncodeError:
            sys.exit("ERRO: nome nao representavel em UTF-8: %r" % rel)
        if size == 0:
            zero.append(rel)
        files.append({"name": rel.rsplit("/", 1)[-1], "path": "SAMP/" + rel, "size": size, "sha256": sha})
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"files": files, "dirs": ["SAMP/" + d for d in empty_dirs]}, f, ensure_ascii=False, indent=1)
    os.replace(tmp, out)
    print("samp.json gerado: %s" % out)
    print("arquivos: %d   bytes: %d   (nada ignorado)" % (len(files), sum(x["size"] for x in files)))
    if empty_dirs:
        print("pastas vazias incluidas (o jogo precisa que existam): %s" % ", ".join("SAMP/" + d for d in empty_dirs))
    if zero:
        print("AVISO: %d arquivo(s) de 0 bytes: %s" % (len(zero), ", ".join(zero[:10])))
    print("Confira no navegador: https://maximoroleplay.com.br/samp.json  e  https://maximoroleplay.com.br/%s" % files[0]["path"])


if __name__ == "__main__":
    main()
