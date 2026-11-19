#!/usr/bin/env python3
"""Cola um pedaco de San Fierro (radar ORIGINAL, banco texdb/mapa gmap000-143) nas imagens da cidade nova (overview + tiles 1024).
So troca o 'vazio' (lilas) da imagem; tudo que a cidade desenha (rodovia/ponte etc.) fica por cima. Regiao: x de -3000 a -1000 (o mapa da cidade limita a camera em -1175)."""
import sys, os, numpy as np
from PIL import Image
from scipy import ndimage as ndi
sys.path.insert(0, '/tmp/cdb'); import skin_texdb as T
SRC, DST, BANK = sys.argv[1], sys.argv[2], sys.argv[3]
X0, Y1 = -16250.0, 3653.0; TM = 1638.4; TPX = 1024; OVM = 4.3
VOID = np.array([205, 216, 228], np.float64); SEA = np.array([138, 189, 240], np.float64)
XA, XB = -3000.0, -1000.0           # faixa de San Fierro colada
# ---- mosaico do radar original ----
ents = T.parse_txt(BANK + '/mapa.txt'); recs = T.read_records(BANK + '/mapa.etc.dat', BANK + '/mapa.etc.toc')
mos = np.zeros((3072, 3072, 3), np.uint8)
for (nm, kv), r in zip(ents, recs):
    if not nm.startswith('gmap'): continue
    i = int(nm[4:]); nh, fmt, w, h, mip, ln, rle, data = r
    img = T.decode_record(fmt, w, h, data)[..., :3]
    rr, cc = divmod(i, 12); mos[rr * 256:(rr + 1) * 256, cc * 256:(cc + 1) * 256] = img
mosf = np.stack([ndi.gaussian_filter(mos[..., c].astype(np.float64), 0.0) for c in range(3)], -1)
def vanilla(xs, ys, blur):
    """xs, ys: matrizes de coordenadas do mundo (centro do pixel). Devolve RGB float; fora de +-3000 = mar."""
    px = (xs + 3000.0) / 6000.0 * 3072.0 - 0.5; py = (3000.0 - ys) / 6000.0 * 3072.0 - 0.5
    src = mosf if blur <= 0 else np.stack([ndi.gaussian_filter(mosf[..., c], blur) for c in range(3)], -1)
    out = np.stack([ndi.map_coordinates(src[..., c], [py, px], order=1, mode='nearest') for c in range(3)], -1)
    outside = (xs < -3000) | (xs > 3000) | (ys < -3000) | (ys > 3000)
    out[outside] = SEA
    return out
def composite(img, xs, ys, blur):
    a = img.astype(np.float64)
    d = np.abs(a - VOID).max(-1)
    solid = d >= 45
    # cobertura do vazio: 1 = vazio puro, 0 = conteudo. Pixels de transicao (antialias) usam a cor solida mais proxima
    cov = np.where(d <= 3, 1.0, 0.0)
    part = (d > 3) & ~solid
    if part.any():
        dist, (iy, ix) = ndi.distance_transform_edt(~solid, return_indices=True)
        F = a[iy, ix]
        v = F - VOID; den = (v * v).sum(-1)
        al = ((a - VOID) * v).sum(-1) / np.maximum(den, 1e-6)      # fracao de CONTEUDO
        al = np.clip(al, 0, 1)
        near = dist <= 3.0
        cov = np.where(part & near & (den > 400), 1.0 - al, cov)
        cov = np.where(part & ~(near & (den > 400)), 0.0, cov)
    inreg = (xs >= XA) & (xs <= XB)
    cov = np.where(inreg, cov, 0.0)
    van = vanilla(xs, ys, blur)
    out = a + cov[..., None] * (van - VOID)           # p + (1-alpha)(B-V)
    # fora do mapa original (y>3000) dentro da regiao: o 'vazio' puro vira mar tambem (ja coberto por vanilla() = SEA)
    return np.clip(np.rint(out), 0, 255).astype(np.uint8), inreg
def grid(x0, y1, w, h, m):
    xs = x0 + (np.arange(w) + 0.5) * m; ys = y1 - (np.arange(h) + 0.5) * m
    return np.meshgrid(xs, ys)
os.makedirs(DST, exist_ok=True)
# ---- overview ----
im = np.array(Image.open(SRC + '/overview.png').convert('RGB'))
xs, ys = grid(X0, Y1, im.shape[1], im.shape[0], OVM)
out, _ = composite(im, xs, ys, 1.0)
Image.fromarray(out).save(DST + '/overview.png', optimize=True); print('overview ok')
# ---- tiles: colunas 8 e 9, linhas 0..2 (as que ainda nao existiam nascem so com o radar + vazio) ----
for c in (8, 9):
    for r in (0, 1, 2):
        f = SRC + '/t_%d_%d.png' % (c, r)
        tx0 = X0 + c * TM; ty1 = Y1 - r * TM
        if os.path.exists(f): im = np.array(Image.open(f).convert('RGB'))
        else: im = np.empty((TPX, TPX, 3), np.uint8); im[:] = VOID.astype(np.uint8)
        xs, ys = grid(tx0, ty1, TPX, TPX, TM / TPX)
        out, inreg = composite(im, xs, ys, 0.0)
        if not inreg.any(): continue
        Image.fromarray(out).save(DST + '/t_%d_%d.png' % (c, r), optimize=True); print('tile', c, r, 'novo' if not os.path.exists(f) else 'alterado')
