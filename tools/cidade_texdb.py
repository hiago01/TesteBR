#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
cidade_texdb.py - converte as imagens da CIDADE NOVA (overview.png + t_<col>_<lin>.png, gerados por citymap.py) no banco texdb "cidade",
no MESMO formato do banco do mapa (reaproveita skin_texdb.py):

    <saida>/cidade/cidade.txt | cidade.idx | cidade.{dxt,etc}.{dat,tmb,toc} | cidade.unc.tmb     -> vai para o JOGO: <storage>/texdb/cidade/

Nomes no banco:  overview.png -> cn_overview     t_3_1.png -> cn_t_3_1      (o cliente pede exatamente esses nomes)
Sem alfa: ETC1 (GPUs Mali) / DXT1 (Adreno/Tegra), 4 bits por pixel. Tiles 1024x1024 (512 KB), visao geral 4096x1024 (2 MB). Nao reduz nada.
Uso:  python3 cidade_texdb.py <pasta_com_png> <saida>      (a pasta com os PNG e a mesma SAMP/radar/cidade; os PNG deixam de ser usados pelo jogo)
Requer: Python 3.8+, numpy, Pillow
"""
import os, re, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import skin_texdb as T
from PIL import Image

def main(src, out):
    items = []
    for f in sorted(os.listdir(src)):
        m = re.fullmatch(r"(overview|t_\d+_\d+)\.png", f)
        if not m: continue
        a = np.array(Image.open(os.path.join(src, f)).convert("RGBA"), np.uint8)
        h, w = a.shape[:2]
        if w % 4 or h % 4: raise SystemExit("%s: tamanho %dx%d nao e multiplo de 4" % (f, w, h))
        a[..., 3] = 255                      # mapa opaco -> ETC1/DXT1 (sem alfa)
        crc = zlib.crc32(open(os.path.join(src, f), "rb").read()) & 0xFFFFFFFF      # campo png= do .txt (igual ao banco do mapa)
        items.append(("cn_" + m.group(1), a, crc))
    if not any(n == "cn_overview" for n, _, _ in items): raise SystemExit("falta overview.png")
    d = T.write_db(items, out, "cidade")
    ok = T.verify(d)
    print("pronto:", d, "OK" if ok else "COM ERROS")
    return 0 if ok else 1

if __name__ == "__main__":
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
