#!/bin/bash
# Autoteste das ferramentas: imagens sinteticas -> banco -> releitura estrita + PSNR minimo
set -e
cd "$(dirname "$0")"; T=$(mktemp -d)
python3 - "$T" <<'PY'
import sys, numpy as np
from PIL import Image
rng = np.random.default_rng(1)
for i in (0, 7, 143):
    y, x = np.mgrid[0:512, 0:512]
    im = np.stack([(x // 2) % 256, (y // 2) % 256, ((x + y + i * 9) // 4) % 256], -1).astype(np.uint8)
    im[100:140, 100:300] = (30, 144, 255); im[300:320] = (255, 255, 255)
    Image.fromarray(im).save('%s/radar%02d.png' % (sys.argv[1], i))
PY
python3 build_mapa_db.py "$T" "$T/out"
python3 verify_db.py "$T/out/mapa" mapa --src "$T" --strict
python3 - <<PY
import struct
toc = open("$T/out/mapa/mapa.etc.toc", "rb").read(); n = len(toc) // 4 - 1
assert n == 3, n
print("selftest OK (3 tiles)")
PY
rm -rf "$T"
