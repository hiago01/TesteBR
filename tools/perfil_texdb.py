#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
perfil_texdb.py - gera o banco texdb/perfil/ (2 banners do Menu Jogador) no mesmo formato do banco de icones/skins.
  perfil_grupos : banner "painel de grupos"  (toque = /grupos)
  perfil_vip    : banner "Eleve sua experiencia" (acessar loja = Em breve)
  perfil_mb     : moeda do "MB Pontos" (icone 128x128, recortada em circulo)
Entrada: duas imagens originais (+ a moeda, opcional). Saida: <saida>/texdb/perfil/perfil.{txt,idx} + perfil.{dxt,etc}.{dat,tmb,toc} + perfil.unc.tmb  (vai para <jogo>/texdb/perfil/)
Uso:  python3 perfil_texdb.py build <banner_grupos.png> <banner_vip.png> <moeda_mb.png|jpg> <saida>
      python3 perfil_texdb.py verify <saida>/texdb/perfil
Requer numpy + Pillow. Cada banner vira 1024x256 (esticado; o cliente desenha a textura inteira no retangulo, entao volta ao formato certo).
"""
import os, sys, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw
import skin_texdb as T

DB = "perfil"
TW, TH = 1024, 256

def rounded_alpha(size, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return m

def prep_grupos(path):
    """imagem escura com cantos pretos: recorta o retangulo arredondado, pega a faixa dos rostos (cobertura 5:1) e arredonda os cantos."""
    im = Image.open(path).convert("RGB")
    a = np.array(im).astype(int)
    ys, xs = np.where(a.max(axis=2) > 40)
    x0, y0, x1, y1 = int(xs.min()) + 14, int(ys.min()) + 14, int(xs.max()) - 14, int(ys.max()) - 14
    w, h = x1 - x0, y1 - y0
    bh = int(w / 5.4)                       # altura da faixa visivel (proporcao do vídeo ~5,4:1)
    top = y0 + int(h * 0.04)
    band = im.crop((x0, top, x1, min(top + bh, y1)))
    out = band.resize((TW, TH), Image.LANCZOS).convert("RGBA")
    out.putalpha(rounded_alpha((TW, TH), 44))
    return out

def prep_vip(path):
    """cartao branco com cantos transparentes: recorta so o cartao"""
    im = Image.open(path).convert("RGBA")
    a = np.array(im).astype(int)
    al = a[:, :, 3]
    nonw = (a[:, :, :3].min(axis=2) < 235) & (al > 20)
    ys, xs = np.where(nonw)
    card = im.crop((int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1))
    out = card.resize((TW, TH), Image.LANCZOS)
    r = np.array(out); r[:, :, 3] = np.minimum(r[:, :, 3], np.array(rounded_alpha((TW, TH), 40)))
    return Image.fromarray(r, "RGBA")

def prep_coin(path):
    """moeda quadrada com fundo branco: recorta em circulo (fundo transparente) e deixa 128x128"""
    im = Image.open(path).convert("RGBA")
    w, h = im.size; s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s)).resize((512, 512), Image.LANCZOS)
    m = Image.new("L", (512, 512), 0)
    ImageDraw.Draw(m).ellipse((6, 6, 505, 505), fill=255)
    im.putalpha(m)
    return im.resize((128, 128), Image.LANCZOS)

def build(g, v, c, outdir, log=print):
    tmp = os.path.join(outdir, "_src"); os.makedirs(tmp, exist_ok=True)
    items = []
    for nm, im in (("perfil_grupos", prep_grupos(g)), ("perfil_vip", prep_vip(v)), ("perfil_mb", prep_coin(c))):
        p = os.path.join(tmp, nm + ".png"); im.save(p)
        items.append((nm, T.load_image(p, 1024), 0))
    d = T.write_db(items, os.path.join(outdir, "texdb"), DB, ("dxt", "etc"), log)
    log("banco em " + d)
    return d

if __name__ == "__main__":
    if len(sys.argv) == 6 and sys.argv[1] == "build": build(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif len(sys.argv) == 3 and sys.argv[1] == "verify": sys.exit(0 if T.verify(sys.argv[2]) else 1)
    else: print(__doc__)
