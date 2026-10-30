#!/usr/bin/env python3
"""Converte os radarNN.txd (RenderWare, D3D9, DXT1 512x512) do pacote 'SA Google Maps Radar' em PNG.
Uso:  python3 tools_radar_convert.py <pasta com radar00.txd..radar143.txd> <pasta de saida>
Saida: <saida>/radar00.png .. radar143.png  (copie para files/SAMP/radar/tiles/ no aparelho)
O nome da textura dentro do .txd ('radar1' em radar00.txd) e ignorado: vale o NOME DO ARQUIVO (radarNN = linha*12+coluna, linha 0 = norte)."""
import struct, sys, os
import numpy as np
from PIL import Image

def read_txd(path):
    b = open(path, 'rb').read()
    t, s, v = struct.unpack_from('<III', b, 0)
    if t != 0x16: raise ValueError('nao e txd')
    o = 12
    t2, s2, _ = struct.unpack_from('<III', b, o); o += 12
    count, _dev = struct.unpack_from('<HH', b, o); o += s2
    t, s, _ = struct.unpack_from('<III', b, o)
    if t != 0x15: raise ValueError('textura nao encontrada')
    o += 12; o += 12                       # texture native + struct
    plat, = struct.unpack_from('<I', b, o)
    name = b[o + 8:o + 40].split(b'\0')[0].decode()
    o2 = o + 72
    rflags, = struct.unpack_from('<I', b, o2); fourcc = b[o2 + 4:o2 + 8]
    w, h = struct.unpack_from('<HH', b, o2 + 8)
    depth, mips, ttype, comp = struct.unpack_from('<BBBB', b, o2 + 12)
    data_off = o2 + 16
    size, = struct.unpack_from('<I', b, data_off); data_off += 4
    return name, w, h, comp, fourcc, b[data_off:data_off + size]

def dxt1(data, w, h):
    n = (w // 4) * (h // 4)
    blk = np.frombuffer(data, np.uint8, n * 8).reshape(n, 8)
    c0 = blk[:, 0].astype(np.uint16) | (blk[:, 1].astype(np.uint16) << 8)
    c1 = blk[:, 2].astype(np.uint16) | (blk[:, 3].astype(np.uint16) << 8)
    def rgb(c):
        r = ((c >> 11) & 31).astype(np.int32); g = ((c >> 5) & 63).astype(np.int32); bl = (c & 31).astype(np.int32)
        return np.stack([(r * 255 + 15) // 31, (g * 255 + 31) // 63, (bl * 255 + 15) // 31], 1)
    p0, p1 = rgb(c0), rgb(c1)
    pal = np.zeros((n, 4, 3), np.int32)
    pal[:, 0], pal[:, 1] = p0, p1
    four = (c0 > c1)[:, None]
    pal[:, 2] = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
    pal[:, 3] = np.where(four, (p0 + 2 * p1) // 3, 0)
    bits = blk[:, 4:8].astype(np.uint32)
    bits = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    out = np.zeros((h, w, 3), np.uint8)
    for i in range(16):
        idx = ((bits >> (2 * i)) & 3).astype(np.int64)
        px = pal[np.arange(n), idx]
        bx, by = (np.arange(n) % (w // 4)) * 4 + (i % 4), (np.arange(n) // (w // 4)) * 4 + (i // 4)
        out[by, bx] = px
    return out

def main():
    if len(sys.argv) < 3: print(__doc__); return 1
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    ok = 0
    for i in range(144):
        f = os.path.join(src, 'radar%02d.txd' % i)
        if not os.path.exists(f): print('FALTA', f); continue
        name, w, h, comp, fourcc, data = read_txd(f)
        if fourcc != b'DXT1' or w != 512 or h != 512 or len(data) < 512 * 512 // 2:
            print('IGNORADO (formato inesperado)', f, fourcc, w, h, len(data)); continue
        Image.fromarray(dxt1(data, w, h), 'RGB').save(os.path.join(dst, 'radar%02d.png' % i), optimize=True)
        ok += 1
    print('%d/144 tiles convertidos em %s' % (ok, dst))
    return 0 if ok == 144 else 2

if __name__ == '__main__':
    sys.exit(main())
