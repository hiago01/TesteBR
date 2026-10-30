#!/usr/bin/env python3
"""Gera o banco texdb "mapa" (tiles do radar/mapa) a partir de PNGs radarNN.png (NN = 00..143, grade 12x12).
Uso:  python3 build_mapa_db.py <pasta_com_radarNN.png> <pasta_saida> [--size 256] [--name mapa] [--rle]
Saida: <saida>/mapa/mapa.txt + mapa.etc.{dat,toc,tmb} + mapa.dxt.{dat,toc,tmb} + mapa.unc.tmb
Copie a pasta 'mapa' para  Android/data/com.samp.mobile/files/texdb/  (ao lado de gta3/, txd/, ...)."""
import sys, os, re, argparse
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texdb_format as T, etc1, dxt1

def rgb565(img):
    r = img[..., 0].astype(np.uint16) >> 3; g = img[..., 1].astype(np.uint16) >> 2; b = img[..., 2].astype(np.uint16) >> 3
    return ((r << 11) | (g << 5) | b).astype('<u2').tobytes()

def resize(im, s):
    return im.resize((s, s), Image.LANCZOS) if im.size != (s, s) else im

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--size', type=int, default=256); ap.add_argument('--name', default='mapa')
    a = ap.parse_args()
    files = {}
    for f in os.listdir(a.src):
        m = re.fullmatch(r'radar(\d{2,3})\.(png|PNG)', f)
        if m: files[int(m.group(1))] = os.path.join(a.src, f)
    if not files: sys.exit('nenhum radarNN.png em ' + a.src)
    idxs = sorted(files); folder = os.path.join(a.out, a.name)
    etc_items, dxt_items, etc_tmb, dxt_tmb, unc_tmb, lines = [], [], b'', b'', b'', []
    for i in idxs:
        nm = 'gmap%03d' % i
        im = Image.open(files[i]).convert('RGB')
        big = np.array(resize(im, a.size)); th = np.array(im.resize((8, 8), Image.BOX))
        pe, pd = etc1.encode(big), dxt1.encode(big)
        etc_items.append((nm, a.size, a.size, T.GL_ETC1, pe, False)); dxt_items.append((nm, a.size, a.size, T.GL_DXT1, pd, False))
        etc_tmb += T.pack_record(nm, T.GL_ETC1, 8, 8, etc1.encode(th)); dxt_tmb += T.pack_record(nm, T.GL_DXT1, 8, 8, dxt1.encode(th))
        unc_tmb += T.pack_record(nm, T.GL_RGB565, 8, 8, rgb565(th))
        lines.append('"%s" width=%d height=%d png=%s img=%s' % (nm, a.size, a.size, T.crc8(big.tobytes()), T.crc8(pe)))
    cat = 'cat=0 name=Default onfoot=5 slow=5 fast=5 defaultformat=2 defaultstream=1'   # stream=1: residente (stream=0 faz o motor mostrar miniaturas 8x8 borradas ate carregar)
    T.write_db(folder, a.name, 'etc', etc_items, cat, lines, True)
    T.write_db(folder, a.name, 'dxt', dxt_items, cat, lines, False)
    for fmt, d in (('etc', etc_tmb), ('dxt', dxt_tmb)): open('%s/%s.%s.tmb' % (folder, a.name, fmt), 'wb').write(d)
    open('%s/%s.unc.tmb' % (folder, a.name), 'wb').write(unc_tmb)
    print('ok: %d tiles %dx%d em %s' % (len(idxs), a.size, a.size, folder))

if __name__ == '__main__': main()
