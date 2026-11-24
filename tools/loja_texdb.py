#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
loja_texdb.py - gera o banco texdb/loja/ (fotos dos produtos das LOJAS: mercado, lanchonete, roupas), pasta SEPARADA do banco de icones
(nao mexe no texdb/icones). Mesmo formato dos outros bancos (skin_texdb.py).
  <saida>/texdb/loja/loja.txt | loja.idx | loja.{dxt,etc}.{dat,tmb,toc} | loja.unc.tmb    -> vai para <jogo>/texdb/loja/
Nome da foto = nome do arquivo (minusculo, a-z 0-9 _ ate 31 letras): loja_pizza, loja_cola ... (a tabela glifo -> nome fica em shoppanel.cpp)
Uso:  python3 loja_texdb.py build <pasta_png> <saida> [--max 128]
      python3 loja_texdb.py verify <saida>/texdb/loja
Requer numpy + Pillow.
"""
import os, re, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_texdb as T

DB = "loja"
NAME = re.compile(r"^[a-z0-9_]{1,31}$")

def build(pack, outdir, cap=128, log=print):
    items = []
    for f in sorted(os.listdir(pack)):
        stem, ext = os.path.splitext(f)
        if ext.lower() not in (".png", ".jpg", ".jpeg"): continue
        nm = stem.lower()
        if not NAME.match(nm): raise SystemExit("nome invalido: " + f)
        full = os.path.join(pack, f)
        items.append((nm, T.load_image(full, cap), zlib.crc32(open(full, "rb").read()) & 0xFFFFFFFF))
    if not items: raise SystemExit("nenhuma imagem em " + pack)
    d = T.write_db(items, os.path.join(outdir, "texdb"), DB, ("dxt", "etc"), log)
    idx = dict(l.split("\t") for l in open(os.path.join(d, DB + ".idx"), encoding="utf-8").read().splitlines() if l)
    diff = [k for k, v in idx.items() if k != v]
    if diff: raise SystemExit("colisao de hash renomeou: " + ", ".join(diff))
    log("banco em " + d)
    return d

if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "build":
        build(sys.argv[2], sys.argv[3], int(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[4] == "--max" else 128)
    elif len(sys.argv) == 3 and sys.argv[1] == "verify": sys.exit(0 if T.verify(sys.argv[2]) else 1)
    else: print(__doc__)
