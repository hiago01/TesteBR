#!/bin/sh
# Teste de PC do codificador JPEG: compila, gera JPEGs e confere com o Pillow (decodificador independente).
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -O1 -x c++ -o /tmp/test_jpeg test_jpeg.test && /tmp/test_jpeg && python3 - <<'PY'
import glob, sys
from PIL import Image
bad = 0
for jp in sorted(glob.glob('/tmp/jpegtest/p_*.jpg')):
    im = Image.open(jp); im.load()
    w, h = im.size
    raw = open(jp[:-4] + '.rgba', 'rb').read()
    src = Image.frombytes('RGBA', (w, h), raw).convert('RGB')
    a = list(im.convert('RGB').getdata()); b = list(src.getdata())
    err = sum(abs(x - y) for p, q in zip(a, b) for x, y in zip(p, q)) / (3 * len(a))
    lim = 60 if '160x96' in jp else 12   # ruido puro perde cor nos canais subamostrados (4:2:0)
    ok = im.format == 'JPEG' and im.mode == 'RGB' and err < lim
    print('%s: %dx%d erro medio %.2f %s' % (jp.split('/')[-1], w, h, err, 'OK' if ok else 'FALHOU'))
    bad += 0 if ok else 1
for jp in ['/tmp/jpegtest/q90_480x270.jpg', '/tmp/jpegtest/fit.jpg']:
    Image.open(jp).load()
print('Pillow decodifica tudo' if not bad else 'FALHAS: %d' % bad)
sys.exit(1 if bad else 0)
PY
