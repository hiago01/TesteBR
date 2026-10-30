"""DXT1 (BC1) - codificador/decodificador numpy. Bloco: c0(u16 565) c1(u16 565) idx(u32, 2 bits/pixel, linha a linha). Sem alfa (c0>c1)."""
import numpy as np

def _rgb565(c):
    return ((c[..., 0].astype(np.int32) >> 3) << 11) | ((c[..., 1].astype(np.int32) >> 2) << 5) | (c[..., 2].astype(np.int32) >> 3)

def _exp565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)

def decode(data, w, h):
    n = (w // 4) * (h // 4)
    a = np.frombuffer(data, np.uint8)[:n * 8].reshape(n, 8)
    c0 = a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8)
    c1 = a[:, 2].astype(np.int32) | (a[:, 3].astype(np.int32) << 8)
    idx = a[:, 4].astype(np.uint32) | (a[:, 5].astype(np.uint32) << 8) | (a[:, 6].astype(np.uint32) << 16) | (a[:, 7].astype(np.uint32) << 24)
    e0, e1 = _exp565(c0), _exp565(c1)
    four = (c0 > c1)[:, None]
    pal = np.stack([e0, e1, np.where(four, (2 * e0 + e1) // 3, (e0 + e1) // 2), np.where(four, (e0 + 2 * e1) // 3, 0)], 1)  # n,4,3
    sh = (np.arange(16, dtype=np.uint32) * 2)[None, :]
    ii = ((idx[:, None] >> sh) & 3).astype(np.int64)
    px = np.take_along_axis(pal, ii[:, :, None], 1)           # n,16,3 (linha a linha: y*4+x)
    return px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)

def encode(img):
    h, w, _ = img.shape
    ph, pw = (-h) % 4, (-w) % 4
    if ph or pw: img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode='edge'); h, w, _ = img.shape
    P = img.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3).astype(np.float64)
    N = len(P)
    mean = P.mean(1, keepdims=True)
    X = P - mean
    # eixo principal por iteracao de potencia (3x3 covariancia)
    cov = np.einsum('nki,nkj->nij', X, X)
    v = np.ones((N, 3)) 
    for _ in range(6):
        v = np.einsum('nij,nj->ni', cov, v); v /= (np.linalg.norm(v, axis=1, keepdims=True) + 1e-9)
    t = np.einsum('nki,ni->nk', X, v)
    lo = P[np.arange(N), t.argmin(1)]; hi = P[np.arange(N), t.argmax(1)]
    c0 = _rgb565(np.clip(hi, 0, 255).astype(np.int32)); c1 = _rgb565(np.clip(lo, 0, 255).astype(np.int32))
    flat = (c0 == c1)
    # garante c0 > c1 (modo 4 cores); se iguais, mexe 1 passo em c0 quando possivel
    sw = c0 < c1
    c0, c1 = np.where(sw, c1, c0), np.where(sw, c0, c1)
    bump = (c0 == c1) & (c0 < 0xFFFF)
    c0 = np.where(bump, c0 + 1, c0)
    e0, e1 = _exp565(c0), _exp565(c1)
    pal = np.stack([e0, e1, (2 * e0 + e1) // 3, (e0 + 2 * e1) // 3], 1).astype(np.float64)    # N,4,3
    d = ((P[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)                                    # N,16,4
    ii = d.argmin(-1).astype(np.uint32)
    idx = np.zeros(N, np.uint32)
    for k in range(16): idx |= ii[:, k] << np.uint32(2 * k)
    out = np.zeros((N, 8), np.uint8)
    out[:, 0] = c0 & 255; out[:, 1] = c0 >> 8; out[:, 2] = c1 & 255; out[:, 3] = c1 >> 8
    for b in range(4): out[:, 4 + b] = (idx >> np.uint32(8 * b)) & 255
    return out.tobytes()
