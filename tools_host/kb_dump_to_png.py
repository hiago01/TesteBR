#!/usr/bin/env python3
"""Transforma um quadro gravado por testes/host_test_keyboard_render (KB_DUMP=prefixo) em PNG.
Rasteriza os triangulos do ImGui (com a textura da fonte) sobre um fundo de cena falso. Uso:
    KB_DUMP=/tmp/k ./kbr 1280 720 ; python3 tools_host/kb_dump_to_png.py /tmp/k_oi.bin /tmp/k_oi.png"""
import struct, sys
import numpy as np
from PIL import Image

def load(path):
    b = open(path, 'rb').read(); o = 0
    W, H, tw, th = struct.unpack_from('4i', b, o); o += 16
    atlas = np.frombuffer(b, np.uint8, tw * th * 4, o).reshape(th, tw, 4); o += tw * th * 4
    nv, = struct.unpack_from('i', b, o); o += 4
    vt = np.frombuffer(b, np.dtype([('x', '<f4'), ('y', '<f4'), ('u', '<f4'), ('v', '<f4'), ('c', '<u4')]), nv, o); o += nv * 20
    ni, = struct.unpack_from('i', b, o); o += 4
    idx = np.frombuffer(b, np.uint16, ni, o); o += ni * 2
    nc, = struct.unpack_from('i', b, o); o += 4
    cmds = []
    for _ in range(nc):
        cx0, cy0, cx1, cy1 = struct.unpack_from('4f', b, o); o += 16
        e, = struct.unpack_from('I', b, o); o += 4
        cmds.append(((cx0, cy0, cx1, cy1), e))
    return W, H, atlas, vt, idx, cmds

def scene(W, H):
    y = np.linspace(0, 1, H)[:, None, None]
    sky = np.array([110, 170, 220], float); ground = np.array([150, 130, 100], float)
    img = sky * (1 - y) + ground * y
    img = np.repeat(img, W, axis=1)
    img[int(H * 0.45):int(H * 0.55), :, :] *= 0.8            # "predio" qualquer, so para ver a transparencia
    return img

def render(path, out):
    W, H, atlas, vt, idx, cmds = load(path)
    tw, th = atlas.shape[1], atlas.shape[0]
    img = scene(W, H)
    pos = 0
    for (cx0, cy0, cx1, cy1), n in cmds:
        for t in range(pos, pos + n, 3):
            v = vt[idx[t:t + 3]]
            xs, ys = v['x'], v['y']
            x0 = max(int(np.floor(xs.min())), int(cx0), 0); x1 = min(int(np.ceil(xs.max())), int(cx1), W - 1)
            y0 = max(int(np.floor(ys.min())), int(cy0), 0); y1 = min(int(np.ceil(ys.max())), int(cy1), H - 1)
            if x1 < x0 or y1 < y0: continue
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
            l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
            l2 = 1 - l0 - l1
            m = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
            if not m.any(): continue
            u = l0 * v['u'][0] + l1 * v['u'][1] + l2 * v['u'][2]
            w = l0 * v['v'][0] + l1 * v['v'][1] + l2 * v['v'][2]
            tx = np.clip((u * tw).astype(int), 0, tw - 1); ty = np.clip((w * th).astype(int), 0, th - 1)
            ta = atlas[ty, tx, 3] / 255.0
            c = [int(v['c'][k]) for k in range(3)]
            def ch(k, s): return np.array([((c[i] >> s) & 255) for i in range(3)], float)
            R = l0 * ((c[0]) & 255) + l1 * ((c[1]) & 255) + l2 * ((c[2]) & 255)
            G = l0 * ((c[0] >> 8) & 255) + l1 * ((c[1] >> 8) & 255) + l2 * ((c[2] >> 8) & 255)
            B = l0 * ((c[0] >> 16) & 255) + l1 * ((c[1] >> 16) & 255) + l2 * ((c[2] >> 16) & 255)
            A = (l0 * ((c[0] >> 24) & 255) + l1 * ((c[1] >> 24) & 255) + l2 * ((c[2] >> 24) & 255)) / 255.0 * ta
            A = np.where(m, A, 0)[..., None]
            region = img[y0:y1 + 1, x0:x1 + 1, :]
            col = np.stack([R, G, B], -1)
            img[y0:y1 + 1, x0:x1 + 1, :] = region * (1 - A) + col * A
        pos += n
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(out)

if __name__ == '__main__':
    render(sys.argv[1], sys.argv[2])
