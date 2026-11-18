#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mapa_agua_texdb.py - gera o banco texdb "mapaui": os MESMOS 144 tiles do mapa (banco "mapa"), mas com a AGUA AZUL quase transparente
(alfa 1/15 = 7%). Vai so para a INTERFACE do mapa aberto (RadarSkin::UiTileRaster); o radar/minimapa nativo segue com o banco "mapa" (opaco).

  ETC (Mali)  -> RGBA4444 (128 KB por tile de 256x256)      DXT -> DXT5 (64 KB)      tile 100% agua -> 16x16;  tile sem agua -> opaco (ETC1/DXT1, 32 KB)
  nomes: gui000 .. gui143 (indice igual ao do gmapNNN)

Uso:  python3 mapa_agua_texdb.py <pasta_com_mapa.etc.dat+toc> <saida>      ->  <saida>/mapaui/mapaui.*   (copie para  <jogo>/texdb/mapaui/)
Formula (igual a RadarSkin::WaterAlpha em C++):  f = clamp((B - R - 12) / 92, 0, 1);  alfa = round((1 - f*(1-1/15)) * 15) / 15
Requer: Python 3.8+, numpy, Pillow
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import skin_texdb as T
from PIL import Image

ALOW = 1.0 / 15.0

def water_alpha(rgb):
    br = rgb[..., 2].astype(np.float32) - rgb[..., 0].astype(np.float32)
    f = np.clip((br - 12.0) / 92.0, 0, 1)
    return np.round((1.0 - f * (1.0 - ALOW)) * 15.0) / 15.0

def main(src, out):
    kind = "etc" if os.path.exists(os.path.join(src, "mapa.etc.dat")) else "dxt"
    recs = T.read_records(os.path.join(src, "mapa.%s.dat" % kind), os.path.join(src, "mapa.%s.toc" % kind))
    if len(recs) != 144: raise SystemExit("esperava 144 tiles, achei %d" % len(recs))
    items, st = [], dict(opaco=0, agua=0, parcial=0)
    for i, r in enumerate(recs):
        _, fmt, w, h, _, _, _, data = r
        rgb = T.decode_record(fmt, w, h, data)[..., :3]
        A = (water_alpha(rgb) * 255).round().astype(np.uint8)
        if (A < 250).mean() < 0.004:
            rgba = np.dstack([rgb, np.full(A.shape, 255, np.uint8)]); st["opaco"] += 1
        elif (A <= 20).mean() > 0.995:
            rgba = np.array(Image.fromarray(np.dstack([rgb, A]), "RGBA").resize((16, 16), Image.BOX), np.uint8); st["agua"] += 1
        else:
            rgba = np.dstack([rgb, A]); st["parcial"] += 1
        items.append(("gui%03d" % i, rgba, 0))
    print(st)
    d = T.write_db(items, out, "mapaui")
    print("pronto:", d)

if __name__ == "__main__":
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
