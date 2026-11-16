#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
weapon_icons_noframe.py - gera o banco texdb/armas: os icones NATIVOS das armas do GTA (<modelo>icon, do banco gta3) SEM a moldura preta
arredondada. Fica so a arma com o proprio contorno preto. O cliente (inventario / slots rapidos) usa este banco primeiro.

Uso:  python3 tools/weapon_icons_noframe.py <pasta_banco_gta3> <saida> [--png]
      <pasta_banco_gta3> = pasta com gta3.txt / gta3.etc.dat / gta3.etc.toc (a do jogo: files/texdb/gta3)
      saida/texdb/armas/...  vai para o JOGO (files/texdb/armas/);  --png tambem grava os PNG em saida/png
Como: a moldura e achada como os pixels pretos comuns a >=55% dos icones; o que esta perto dela e longe (> 7 px) do corpo da arma e apagado.
Requer: numpy, scipy, Pillow.
"""
import sys, os, struct, zlib
import numpy as np
from scipy import ndimage as ndi
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_texdb as T

def rle_decode(src, dst_size, seg, marker):      # RLEDecompress do jogo (marcador = campo B do registro)
    out = bytearray(); i = 0
    while len(out) < dst_size:
        if src[i] == marker:
            n = src[i + 1]
            if n: out += src[i + 2:i + 2 + seg] * n
            i += seg + 2
        else:
            out += src[i:i + seg]; i += seg
    return bytes(out[:dst_size])

def main(a):
    bank, outdir = a[0], a[1]; keep_png = "--png" in a
    ents = []
    for raw in open(os.path.join(bank, "gta3.txt"), encoding="latin1"):
        l = raw.rstrip("\r\n")
        if l.startswith('"'): ents.append(l[1:l.index('"', 1)].lower())
    toc = open(os.path.join(bank, "gta3.etc.toc"), "rb").read(); offs = struct.unpack("<%dI" % (len(toc) // 4), toc)
    dat = open(os.path.join(bank, "gta3.etc.dat"), "rb").read()
    names = {n: i for i, n in enumerate(ents)}
    models = ["brassknuckle","golfclub","nitestick","knifecur","bat","shovel","poolcue","katana","chnsaw","gun_dildo1","gun_dildo2","gun_vibe1","gun_vibe2","flowera","gun_cane","grenade","teargas","molotov","colt45","silenced","desert_eagle","chromegun","sawnoff","shotgspa","micro_uzi","mp5lng","ak47","m4","tec9","cuntgun","sniper","rocketla","heatseek","flame","minigun","satchel","bomb","spraycan","fire_ex","camera","nvgoggles","irgoggles","gun_para"]
    def get(n):
        i = names.get(n.lower())
        if i is None: return None
        o = offs[1 + i]
        if o == 0xFFFFFFFF: return None
        h16, gl, W, hh, A, B = struct.unpack_from("<HHHHII", dat, o); p = dat[o + 16:o + 12 + A]
        if gl != 0x8033: return None
        out = rle_decode(p, W * (hh & 0x7fff) * 2, 4, B) if B else p[:W * (hh & 0x7fff) * 2]
        b = np.frombuffer(out, "<u2").reshape(hh & 0x7fff, W)
        return np.dstack([((b >> 12) & 0xF) * 17, ((b >> 8) & 0xF) * 17, ((b >> 4) & 0xF) * 17, (b & 0xF) * 17]).astype(np.uint8)
    icons = {}
    for m in models:
        im = get(m + "icon")
        if im is not None and im.shape[:2] == (256, 256): icons[m] = im
    print(len(icons), "icones de arma encontrados")
    blk=lambda im:(im[...,3]>128)&(im[...,:3].max(2)<70)
    cnt=np.zeros((256,256))
    for im in icons.values(): cnt+=blk(im)
    frame=cnt>=0.55*len(icons)
    frame=ndi.binary_dilation(frame,iterations=2)
    print('pixels de moldura (todas as armas):',frame.sum())
    def process(im):
        a=im[...,3]; opaque=a>40; black=blk(im)
        lilac=(a>200)&(im[...,:3].min(2)>150)
        lilac=ndi.binary_opening(lilac,iterations=1)
        dist=ndi.distance_transform_edt(~lilac)
        # espessura do contorno da arma: distancia maxima dos pixels pretos FORA da moldura
        transp=a<=40; edge=black&ndi.binary_dilation(transp)&~frame_region
        W=float(np.percentile(dist[edge],60)) if edge.sum()>30 else 7.0
        W=7.0; pass
        prot=ndi.binary_fill_holes(ndi.binary_dilation(lilac,iterations=int(round(W))+1))
        remove=frame_region&~prot
        out=im.copy(); out[remove,3]=0; out[remove,:3]=0
        # tira meio-pixels soltos da borda da moldura (alfa parcial perto da regiao removida)
        return out,W
    frame_region=ndi.binary_dilation(frame,iterations=14)

    res = {}
    for m, im in icons.items():
        o, W = process(im)
        a_ = o[..., 3] > 40; lab, n = ndi.label(a_, structure=np.ones((3, 3)))
        if n > 1:
            sizes = ndi.sum(a_, lab, range(1, n + 1)); big = sizes.max()
            for k, s in enumerate(sizes, 1):
                if s < 0.02 * big: o[lab == k, 3] = 0
        res[m] = o
    pngdir = os.path.join(outdir, "png"); os.makedirs(pngdir, exist_ok=True)
    items = []
    for m, arr in sorted(res.items()):
        nm = m.lower() + "icon"; p = os.path.join(pngdir, nm + ".png"); Image.fromarray(arr).save(p)
        items.append((nm, T.load_image(p, 128), zlib.crc32(open(p, "rb").read()) & 0xFFFFFFFF))
    d = T.write_db(items, os.path.join(outdir, "texdb"), "armas", ("dxt", "etc"), print)
    if not keep_png:
        import shutil; shutil.rmtree(pngdir, ignore_errors=True)
    return 0 if T.verify(d) else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
