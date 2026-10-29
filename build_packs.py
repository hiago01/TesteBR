#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_packs.py - gera os pacotes ZIP do cliente (core + gpu_*) e o trecho "packs" do client_config.json.

Uso (Windows, Python 3.8+), lendo direto do .zip do cliente completo:
    python build_packs.py --src C:\\cache-server\\files.zip --out C:\\cache-server\\packs ^
        --base-url http://201.33.84.96:8080/packs --version 1
Ou a partir da pasta:
    python build_packs.py --src C:\\cache-server\\files --out C:\\cache-server\\packs ^
        --base-url http://201.33.84.96:8080/packs --version 1

Regras (devem bater com GameFilePolicy.java do app):
  * SO LE a pasta de origem; nunca altera nada nela. Tudo e escrito em --out.
  * Nomes em UTF-8, separador "/", caixa preservada. ERRO se dois caminhos diferirem so pela caixa.
  * --src aceita a PASTA do cliente OU o .zip do cliente completo (ex.: C:\\cache-server\\files.zip): nesse caso le direto de
    dentro do .zip (nada e extraido em disco) e remove a pasta raiz unica, se houver (--no-strip desliga).
  * PADRAO: copia TUDO, sem nenhuma excecao (so os internos do instalador - installed.json, .staging/.backup/.downloads - ficam
    de fora) e emite "include_all": true no client_config (o app entao extrai tudo que esta no zip).
    --seguro deixa de fora lixo de SO, lista.txt e os caches gerados no aparelho (CINFO.BIN, MINFO.BIN, cinfo.sig, col_index.txt).
    --com-excecoes = modo antigo (ignora tambem as configuracoes do jogador; lista abaixo).
  * core = tudo que nao e de GPU; gpu_dxt/gpu_etc/gpu_pvr = ".dxt." / ".etc." / ".pvr." no caminho,
    EXCETO caminhos com "samp", "player" ou "menu" (sempre core, sem diferenciar caixa) - mesma regra do app.
  * --overlay PASTA_OU_ZIP (repetivel): sobrepoe pastas inteiras. Ex.: --overlay C:\\cache-server\\SAMP.zip faz a pasta SAMP/
    do SAMP.zip substituir TODA a SAMP/ do --src (some o que ficou obsoleto, entra tudo o que e novo).
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


INTERNAL_DIRS = (".staging/", ".backup/", ".downloads/")
# Modos: "seguro" (padrao) = tudo, menos lixo de SO e CACHES gerados no aparelho; "tudo" = sem nenhuma excecao;
#        "antigo" = lista completa de ignorados (inclui settings.ini, favorites.json etc.).
MODE = "tudo"
# Pastas de topo do cliente: se a unica pasta raiz de um .zip tiver um destes nomes, ela e PARTE do jogo (ex.: SAMP.zip
# contendo SAMP/...) e NAO e removida; qualquer outro nome unico (ex.: files/) e tratado como pasta embrulho e removido.
GAME_TOP_DIRS = {"samp", "data", "texdb", "models", "audio", "anim", "fonts", "text", "textures"}
INCLUDE_ALL = True   # "seguro" e "tudo" emitem include_all:true
# Caches que o JOGO gera no celular e valida por mtime/tamanho/assinatura (CustomCol/CustomImg): trazer a copia do PC
# pode deixar cache que nao confere com os arquivos instalados.
DEVICE_CACHES = {"cinfo.bin", "models/minfo.bin", "samp/cinfo.sig", "samp/col_index.txt",
                 "samp/svlog.txt", "samp_log.txt", "lista.txt", "files.json"}


def is_ignored(rel):
    low = rel.lower()
    # internos do instalador / do proprio script: nunca entram, em nenhum modo
    if low == "installed.json" or low == MANIFEST_NAME or low.startswith(INTERNAL_DIRS):
        return True
    if MODE == "tudo":
        return False
    if MODE == "seguro":
        return low in DEVICE_CACHES or low.rsplit("/", 1)[-1] in IGNORE_BASENAMES
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


def open_ref(ref):
    """Abre a origem de um arquivo: caminho em disco (str) ou entrada de um .zip de origem ((ZipFile, ZipInfo))."""
    if isinstance(ref, tuple):
        return ref[0].open(ref[1], "r")
    return open(ref, "rb")


def sha256_ref(ref, bufsize=1024 * 1024):
    h = hashlib.sha256()
    with open_ref(ref) as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def sha256_file(path, bufsize=1024 * 1024):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def _zip_name(info):
    """Nome da entrada em UTF-8 (WinRAR marca UTF-8; senao tenta cp437->utf-8 e por fim cp1252)."""
    n = info.filename
    if not (info.flag_bits & 0x800):
        try:
            n = n.encode("cp437").decode("utf-8")
        except (UnicodeEncodeError, UnicodeDecodeError):
            try:
                n = info.filename.encode("cp437").decode("cp1252")
            except (UnicodeEncodeError, UnicodeDecodeError):
                n = info.filename
    return n.replace("\\", "/")


def scan_zip(zpath, strip=True):
    """Le o cliente completo de dentro de um .zip (ex.: files.zip). Mesma saida de scan()."""
    zf = zipfile.ZipFile(zpath, "r")
    bad = zf.testzip()
    if bad:
        sys.exit("ERRO: o .zip de origem esta corrompido (CRC) em: " + bad)
    entries = []
    for info in zf.infolist():
        n = _zip_name(info)
        if n.endswith("/") or info.is_dir():
            continue
        mode = (info.external_attr >> 16) & 0xFFFF
        if (mode & 0o170000) == 0o120000:
            sys.exit("ERRO: link simbolico no .zip de origem: " + n)
        if n.startswith("/") or ".." in n.split("/") or (len(n) > 1 and n[1] == ":") or "//" in n:
            sys.exit("ERRO: caminho inseguro no .zip de origem: " + n)
        entries.append((n, info))
    if not entries:
        sys.exit("ERRO: o .zip de origem nao tem arquivos")
    prefix = ""
    if strip:
        firsts = {n.split("/", 1)[0] for n, _ in entries if "/" in n}
        if len(firsts) == 1 and all("/" in n for n, _ in entries) and next(iter(firsts)).lower() not in GAME_TOP_DIRS:
            prefix = firsts.pop() + "/"
            print("pasta raiz '%s' dentro do .zip removida dos caminhos (use --no-strip para manter)" % prefix.rstrip("/"))
    items, seen_lower, zero = [], {}, []
    for n, info in entries:
        rel = n[len(prefix):] if prefix else n
        if is_ignored(rel):
            continue
        low = rel.lower()
        if low in seen_lower:
            sys.exit("ERRO: dois caminhos diferem so pela caixa (Android e case-sensitive):\n  %s\n  %s" % (seen_lower[low], rel))
        seen_lower[low] = rel
        try:
            rel.encode("utf-8")
        except UnicodeEncodeError:
            sys.exit("ERRO: nome nao representavel em UTF-8: %r" % rel)
        if info.file_size == 0:
            zero.append(rel)
        if info.file_size >= MAX_ZIP64:
            sys.exit("ERRO: arquivo >= 4 GB nao cabe em zip sem Zip64: " + rel)
        items.append((rel, (zf, info), info.file_size))
    items.sort(key=lambda t: t[0])
    return items, zero


def scan(src, strip=True):
    if os.path.isfile(src):
        if not zipfile.is_zipfile(src):
            sys.exit("ERRO: --src e um arquivo, mas nao e .zip: " + src)
        return scan_zip(src, strip)
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


def apply_overlay(items, overlay_items):
    """A sobreposicao (ex.: SAMP.zip) e a VERDADE para as pastas de topo que ela contem: remove tudo da base nessas pastas
    (ex.: SAMP/ obsoleto como SAMP2.ide) e coloca o conteudo da sobreposicao. Arquivos soltos na raiz substituem/adicionam."""
    tops = {rel.split("/", 1)[0].lower() for rel, _, _ in overlay_items if "/" in rel}
    exact = {rel.lower() for rel, _, _ in overlay_items if "/" not in rel}
    kept = [it for it in items if not ("/" in it[0] and it[0].split("/", 1)[0].lower() in tops) and it[0].lower() not in exact]
    removed = len(items) - len(kept)
    out = kept + list(overlay_items)
    out.sort(key=lambda t: t[0])
    print("sobreposicao: pastas %s substituidas (%d arquivos da base removidos, %d da sobreposicao adicionados)"
          % (sorted(tops), removed, len(overlay_items)))
    return out


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
        manifest["files"].append({"path": rel, "size": size, "sha256": sha256_ref(ab)})
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
            with open_ref(ab) as fi, z.open(zi, "w") as fo:
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
    ap.add_argument("--src", default=r"C:\cache-server\files",
                    help="pasta com os arquivos do jogo OU o .zip do cliente completo, ex.: C:\\cache-server\\files.zip (somente leitura)")
    ap.add_argument("--overlay", action="append", default=[],
                    help="pasta ou .zip que SUBSTITUI as pastas de topo que contem (ex.: --overlay C:\\cache-server\\SAMP.zip troca TODA a pasta SAMP/ do --src). Pode repetir.")
    ap.add_argument("--excluir-pasta", action="append", default=[],
                    help="pasta de topo que NAO vai nos pacotes nem no files.json (ex.: --excluir-pasta SAMP: ela e baixada pelo "
                         "launcher via samp.json). Pode repetir.")
    ap.add_argument("--no-strip", action="store_true", help="com --src .zip: nao remove a pasta raiz unica de dentro do .zip")
    ap.add_argument("--out", default=r"C:\cache-server\packs", help="pasta onde os zips e o manifest serao escritos")
    ap.add_argument("--base-url", required=True, help="URL publica da pasta dos zips, ex.: http://IP:8080/packs")
    ap.add_argument("--version", required=True, help="versao do conteudo (letras/numeros . _ -), ex.: 1")
    ap.add_argument("--seguro", action="store_true",
                    help="deixa de fora lixo de SO, lista.txt e os caches do aparelho (CINFO.BIN, MINFO.BIN, cinfo.sig, col_index.txt, logs)")
    ap.add_argument("--sem-excecoes", action="store_true", help="(padrao) copia tudo; so os internos do instalador ficam de fora")
    ap.add_argument("--com-excecoes", action="store_true",
                    help="modo antigo: ignora tambem settings.ini, favorites.json, gta_sa.set etc.")
    ap.add_argument("--max-mb", type=int, default=900, help="dados maximos por zip (MiB); padrao 900")
    a = ap.parse_args()
    global INCLUDE_ALL, MODE
    MODE = "antigo" if a.com_excecoes else ("seguro" if a.seguro else "tudo")
    INCLUDE_ALL = MODE != "antigo"
    print("modo: " + MODE)

    src = os.path.abspath(a.src)
    out = os.path.abspath(a.out)
    if not os.path.exists(src):
        sys.exit("ERRO: origem nao existe: " + src)
    if os.path.isfile(src):
        if out == src or src.startswith(out + os.sep) and False:
            sys.exit("ERRO: --out invalido")
    elif out == src or out.startswith(src + os.sep):
        sys.exit("ERRO: --out nao pode ficar dentro de --src (a origem nao pode ser alterada)")
    if not a.version or not all(c.isalnum() or c in "._-" for c in a.version):
        sys.exit("ERRO: --version invalida")
    os.makedirs(out, exist_ok=True)

    items, zero = scan(src, not a.no_strip)
    for ov in a.overlay:
        ovp = os.path.abspath(ov)
        if not os.path.exists(ovp):
            sys.exit("ERRO: --overlay nao existe: " + ovp)
        oitems, ozero = scan(ovp, not a.no_strip)
        if not oitems:
            sys.exit("ERRO: --overlay vazio: " + ovp)
        items = apply_overlay(items, oitems)
        zero = sorted(set(zero) | set(ozero))
    if a.excluir_pasta:
        ex = {x.strip("/").lower() for x in a.excluir_pasta}
        before = len(items)
        items = [it for it in items if it[0].split("/", 1)[0].lower() not in ex or "/" not in it[0]]
        print("pastas fora dos pacotes: %s (%d arquivos removidos da lista)" % (sorted(ex), before - len(items)))
        if not items:
            sys.exit("ERRO: nada sobrou depois de --excluir-pasta")
    low_seen = {}
    for it in items:
        if it[0].lower() in low_seen and low_seen[it[0].lower()] != it[0]:
            sys.exit("ERRO: dois caminhos diferem so pela caixa (Android e case-sensitive):\n  %s\n  %s" % (low_seen[it[0].lower()], it[0]))
        low_seen[it[0].lower()] = it[0]
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

    # files.json com sha256 por arquivo (formato do launcher: {"files":[{name,size,path,sha256}]}) para o fluxo de
    # atualizacao/verificacao arquivo-a-arquivo conferir o conteudo. Copie para a raiz do servidor (ao lado do launcher.apk).
    files_json = {"files": []}
    for pk in full_manifest["packs"]:
        for fi in pk["files"]:
            files_json["files"].append({"name": fi["path"].rsplit("/", 1)[-1], "size": fi["size"],
                                        "path": fi["path"], "sha256": fi["sha256"]})
    files_json["files"].sort(key=lambda x: x["path"])
    with open(os.path.join(out, "files.json"), "w", encoding="utf-8") as f:
        json.dump(files_json, f, ensure_ascii=False)

    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(full_manifest, f, ensure_ascii=False, indent=1)

    snippet = {"version": a.version}
    if INCLUDE_ALL:
        snippet["include_all"] = True
    snippet["packs"] = packs
    print("\n=== cole dentro de \"client_config\" no client_config.json (mantenha url_cache_files) ===")
    print(json.dumps(snippet, indent=2))
    print("\nTambem gerado: %s (files.json COM sha256; copie para a raiz do servidor, no lugar do antigo)." % os.path.join(out, "files.json"))
    print("\nTotal: %d arquivos em %d pacote(s). Copie os .zip para %s no servidor." % (len(items), len(packs), a.base_url))


if __name__ == "__main__":
    main()
