#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
icon_texdb.py - converte uma PASTA de icones (PNG/JPEG) no banco texdb "icones" do GTA SA Mobile (pasta SEPARADA do banco de skins),
reaproveitando o gerador do skin_texdb.py (mesmo formato do banco do mapa):

    <saida>/texdb/icones/icones.txt | icones.idx | icones.{dxt,etc}.{dat,tmb,toc} | icones.unc.tmb     <- vai para o JOGO (storage/texdb/icones/)
    <saida>/painel/icones.lst + <nome>.png (previa 64x64)                                              <- vai para o SERVIDOR (celular-service/icones/)

O nome do icone = nome do arquivo em minusculo (sem extensao): "agua_mineral.png" -> "agua_mineral". So a-z, 0-9 e _ (ate 31 letras).
Subpastas sao so organizacao (ex.: icones/armas/pistola.png -> "pistola"); nomes repetidos em pastas diferentes dao erro.
Icones: quadrados de preferencia; o gerador ajusta para potencia de 2 (padrao teto 128 px) e usa DXT/ETC (compressao leve p/ mobile).

Uso:
  python3 icon_texdb.py build  <pasta_icones> <saida> [--max 128] [--no-etc] [--no-dxt]
  python3 icon_texdb.py verify <saida>/texdb/icones
  python3 icon_texdb.py selftest
Depois de gerar: (1) copie <saida>/texdb/icones para o jogo (o launcher precisa distribuir esta pasta junto dos arquivos do jogo);
(2) copie o CONTEUDO de <saida>/painel para celular-service/icones/ (o painel lista os nomes e mostra as previas).
Requer: Python 3.8+, numpy, Pillow  (pip install numpy pillow)
"""
import os, re, sys, shutil, argparse, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_texdb as T
from PIL import Image

NAME = re.compile(r"^[a-z0-9_]{1,31}$")
DB = "icones"

def collect_icons(pack):
    """-> lista (nome, caminho). Erro claro para nome invalido ou repetido."""
    seen, out, bad = {}, [], []
    for root, _, files in os.walk(pack):
        for f in sorted(files):
            stem, ext = os.path.splitext(f)
            if ext.lower() not in (".png", ".jpg", ".jpeg"): continue
            nm = stem.lower()
            full = os.path.join(root, f)
            if not NAME.match(nm): bad.append("%s  (nome invalido: use a-z, 0-9 e _ ; ate 31 letras)" % os.path.relpath(full, pack)); continue
            if nm in seen: bad.append("%s  (nome repetido de %s)" % (os.path.relpath(full, pack), os.path.relpath(seen[nm], pack))); continue
            seen[nm] = full; out.append((nm, full))
    if bad: raise SystemExit("ERRO nos nomes dos icones:\n  " + "\n  ".join(bad))
    if not out: raise SystemExit("nenhuma imagem (.png/.jpg) em " + pack)
    return sorted(out)

def build(pack, outdir, cap=128, kinds=("dxt", "etc"), log=print):
    icons = collect_icons(pack)
    items = []
    for nm, full in icons:
        crc = zlib.crc32(open(full, "rb").read()) & 0xFFFFFFFF
        items.append((nm, T.load_image(full, cap), crc))
    log("%d icones lidos de %s (teto %dpx)" % (len(items), pack, cap))
    tex_root = os.path.join(outdir, "texdb")
    d = T.write_db(items, tex_root, DB, kinds, log)
    # os nomes no banco TEM que ser iguais aos do painel (sem '~' de colisao de hash): confere no idx
    idx = dict(l.split("\t") for l in open(os.path.join(d, DB + ".idx"), encoding="utf-8").read().splitlines() if l)
    diff = [k for k, v in idx.items() if k != v]
    if diff: raise SystemExit("ERRO: colisao de hash de 16 bits renomeou: %s. Renomeie estes icones e gere de novo." % ", ".join(diff))
    # arquivos do painel (servidor)
    pd = os.path.join(outdir, "painel"); os.makedirs(pd, exist_ok=True)
    for old in os.listdir(pd):
        if old.endswith(".png") or old == "icones.lst": os.remove(os.path.join(pd, old))
    for nm, full in icons:
        im = Image.open(full).convert("RGBA"); im.thumbnail((64, 64), Image.LANCZOS)
        im.save(os.path.join(pd, nm + ".png"), "PNG")
    with open(os.path.join(pd, "icones.lst"), "w", newline="\n", encoding="utf-8") as f: f.write("\n".join(n for n, _ in icons) + "\n")
    log("painel: %d previas + icones.lst em %s" % (len(icons), pd))
    return d

def selftest(log=print):
    import tempfile, numpy as np
    tmp = tempfile.mkdtemp(prefix="icon_texdb_")
    try:
        pack = os.path.join(tmp, "pack"); os.makedirs(os.path.join(pack, "armas"))
        def mk(path, rgb, alpha=255, size=100):
            a = np.zeros((size, size, 4), np.uint8); a[..., :3] = rgb; a[..., 3] = alpha
            a[size // 4:size // 2, size // 4:size // 2, 3] = 255
            Image.fromarray(a, "RGBA").save(path)
        mk(os.path.join(pack, "agua_mineral.png"), (40, 120, 255), 0)
        mk(os.path.join(pack, "armas", "Pistola_Preta.PNG"), (30, 30, 30), 255, 128)
        mk(os.path.join(pack, "celular_novo.png"), (200, 60, 200), 255, 64)
        out = os.path.join(tmp, "out")
        d = build(pack, out, 128, log=lambda *_: None)
        ok = T.verify(d, log=lambda *_: None)
        files = sorted(os.listdir(d))
        for need in ("icones.txt", "icones.idx", "icones.dxt.dat", "icones.dxt.toc", "icones.dxt.tmb", "icones.etc.dat", "icones.unc.tmb"):
            if need not in files: log("FALTOU " + need); ok = False
        lst = open(os.path.join(out, "painel", "icones.lst")).read().split()
        if lst != ["agua_mineral", "celular_novo", "pistola_preta"]: log("icones.lst errado: %r" % lst); ok = False
        for n in lst:
            if not os.path.exists(os.path.join(out, "painel", n + ".png")): log("sem previa " + n); ok = False
        idx = open(os.path.join(d, "icones.idx")).read().split("\n")
        if "pistola_preta\tpistola_preta" not in idx: log("idx sem pistola_preta"); ok = False
        # erros esperados
        for bad_name in ("Agua Mineral.png", "ação.png"):
            shutil.copy(os.path.join(pack, "agua_mineral.png"), os.path.join(pack, bad_name))
            try: collect_icons(pack); log("nome invalido aceito: " + bad_name); ok = False
            except SystemExit: pass
            os.remove(os.path.join(pack, bad_name))
        shutil.copy(os.path.join(pack, "agua_mineral.png"), os.path.join(pack, "armas", "agua_mineral.png"))
        try: collect_icons(pack); log("nome repetido aceito"); ok = False
        except SystemExit: pass
        log("selftest: " + ("OK" if ok else "FALHOU")); return ok
    finally: shutil.rmtree(tmp, ignore_errors=True)

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd")
    b = sp.add_parser("build"); b.add_argument("pack"); b.add_argument("out"); b.add_argument("--max", type=int, default=128)
    b.add_argument("--no-etc", action="store_true"); b.add_argument("--no-dxt", action="store_true")
    v = sp.add_parser("verify"); v.add_argument("dir")
    sp.add_parser("selftest")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        kinds = tuple(k for k, off in (("dxt", a.no_dxt), ("etc", a.no_etc)) if not off)
        d = build(a.pack, a.out, a.max, kinds); return 0 if T.verify(d) else 1
    if a.cmd == "verify": return 0 if T.verify(a.dir) else 1
    if a.cmd == "selftest": return 0 if selftest() else 1
    ap.print_help(); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
