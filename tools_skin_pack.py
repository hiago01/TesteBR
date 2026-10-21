#!/usr/bin/env python3
"""Prepara o pack de texturas do skin male para o /skin7 (cliente Android, stb_image so-PNG).

Uso:  python3 tools_skin_pack.py <pasta_7_descompactada> <pasta_saida> [--so-convertidos]

  * copia os .png como estao e CONVERTE os .jpeg/.jpg para .png de verdade
    (o cliente compila stb_image com STBI_ONLY_PNG: .jpeg falha com 'unknown image type');
  * mantem a estrutura  <categoria>/<pasta>/<n>.png  (ex.: tshirts/2/17.png);
  * escreve skin_pack_index.csv (categoria, pasta, n, origem, largura, altura, modo, alfa, pot).
Destino no aparelho:  <raiz>/SAMP/skins/pack/<categoria>/<pasta>/<n>.png
"""
import csv, os, shutil, sys
from PIL import Image

def is_pot(v): return v > 0 and (v & (v - 1)) == 0

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    only_conv = '--so-convertidos' in sys.argv
    rows, converted, copied = [], 0, 0
    for root, _, files in sorted(os.walk(src)):
        for f in sorted(files):
            stem, ext = os.path.splitext(f); ext = ext.lower()
            if ext not in ('.png', '.jpg', '.jpeg'): continue
            p = os.path.join(root, f)
            rel = os.path.relpath(root, src).replace('\\', '/')
            parts = rel.split('/')
            cat = parts[0]; pasta = '/'.join(parts[1:])
            im = Image.open(p)
            alpha = im.mode in ('RGBA', 'LA') or 'transparency' in im.info
            out_dir = os.path.join(dst, rel); out = os.path.join(out_dir, stem + '.png')
            if ext == '.png':
                if not only_conv:
                    os.makedirs(out_dir, exist_ok=True); shutil.copyfile(p, out); copied += 1
            else:
                os.makedirs(out_dir, exist_ok=True)
                im.convert('RGB').save(out, 'PNG', optimize=True); converted += 1
            w, h = im.size
            rows.append([cat, pasta, stem, 'png' if ext == '.png' else 'jpeg->png', w, h, im.mode,
                         int(alpha), int(is_pot(w) and is_pot(h))])
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(dst, 'skin_pack_index.csv'), 'w', newline='', encoding='utf-8') as fh:
        wr = csv.writer(fh)
        wr.writerow(['categoria', 'pasta', 'n', 'origem', 'largura', 'altura', 'modo', 'alfa', 'pot'])
        wr.writerows(rows)
    print(f'{len(rows)} imagens no indice | {converted} jpeg convertido(s) | {copied} png copiado(s)')

if __name__ == '__main__':
    main()
