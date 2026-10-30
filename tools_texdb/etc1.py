"""ETC1 (GL_ETC1_RGB8_OES) decodificador e codificador (numpy). Blocos 4x4 de 8 bytes, ordem linha-a-linha de blocos.
Bloco: b0..b2 = cores (R,G,B; 4+4 bits no modo individual, 5+3 bits no diferencial), b3 = tabela1(3)|tabela2(3)|diff(1)|flip(1),
b4..b7 = indices dos 16 pixels (msb em b4b5, lsb em b6b7, pixel i = x*4+y)."""
import numpy as np

TABLE = np.array([[2, 8], [5, 17], [9, 29], [13, 42], [18, 60], [24, 80], [33, 106], [47, 183]], np.int32)
# indice (msb<<1|lsb) -> modificador:  00:+a 01:+b 10:-a 11:-b
def _mods(t): a, b = TABLE[t]; return np.array([a, b, -a, -b], np.int32)

def _expand4(v): return (v << 4) | v
def _expand5(v): return (v << 3) | (v >> 2)

def _subblock_pixels(flip):
    """para flip 0/1: lista de 16 posicoes (x,y) -> subbloco 0/1, na ordem i=x*4+y"""
    sb = np.zeros(16, np.int32)
    for x in range(4):
        for y in range(4):
            sb[x * 4 + y] = (1 if y >= 2 else 0) if flip else (1 if x >= 2 else 0)
    return sb
SB = [_subblock_pixels(0), _subblock_pixels(1)]

def decode_block(b):
    b0, b1, b2, b3 = b[0], b[1], b[2], b[3]
    diff, flip = (b3 >> 1) & 1, b3 & 1
    t1, t2 = (b3 >> 5) & 7, (b3 >> 2) & 7
    if diff:
        def sx(v): return v - 8 if v >= 4 else v    # 3 bits com sinal (complemento de 2)
        r1, g1, bl1 = b0 >> 3, b1 >> 3, b2 >> 3
        r2, g2, bl2 = r1 + (((b0 & 7) ^ 4) - 4), g1 + (((b1 & 7) ^ 4) - 4), bl1 + (((b2 & 7) ^ 4) - 4)
        c1 = [_expand5(r1), _expand5(g1), _expand5(bl1)]
        c2 = [_expand5(r2 & 31), _expand5(g2 & 31), _expand5(bl2 & 31)]
    else:
        c1 = [_expand4(b0 >> 4), _expand4(b1 >> 4), _expand4(b2 >> 4)]
        c2 = [_expand4(b0 & 15), _expand4(b1 & 15), _expand4(b2 & 15)]
    msb = (int(b[4]) << 8) | int(b[5]); lsb = (int(b[6]) << 8) | int(b[7])
    out = np.zeros((4, 4, 3), np.uint8)
    mods = (_mods(t1), _mods(t2)); cols = (c1, c2)
    for i in range(16):
        x, y = i // 4, i % 4
        s = SB[flip][i]
        idx = (((msb >> i) & 1) << 1) | ((lsb >> i) & 1)
        m = mods[s][idx]
        out[y, x] = np.clip(np.array(cols[s]) + m, 0, 255)
    return out

def decode(data, w, h):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    img = np.zeros((bh * 4, bw * 4, 3), np.uint8)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            img[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = decode_block(data[o:o + 8])
    return img[:h, :w]


# ----------------------------------------------------------------------------------------------- codificador
def _blocks(img):
    """img HxWx3 uint8 (H,W multiplos de 4) -> P[N,16,3] com pixel i = x*4+y (x = coluna)."""
    h, w, _ = img.shape
    b = img.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 3, 1, 4)     # by,bx,x,y,c
    return b.reshape(-1, 16, 3).astype(np.int32)

_EXP5 = ((np.arange(32) << 3) | (np.arange(32) >> 2)).astype(np.int32)
_EXP4 = ((np.arange(16) << 4) | np.arange(16)).astype(np.int32)

def _quant(mean, exp):
    """mean[...,3] float -> indice de quantizacao cuja expansao e a mais proxima (por canal)."""
    n = len(exp)
    q0 = np.clip(np.floor(mean * (n - 1) / 255.0).astype(np.int32), 0, n - 1)
    q1 = np.minimum(q0 + 1, n - 1)
    e0 = np.abs(exp[q0] - mean); e1 = np.abs(exp[q1] - mean)
    return np.where(e1 < e0, q1, q0)

def _best_tables(px, base):
    """px[N,8,3], base[N,3] -> (erro[N], tabela[N], idx[N,8]) escolhendo a melhor tabela de modificadores."""
    best_e = None
    for t in range(8):
        a, b = TABLE[t]
        mods = np.array([a, b, -a, -b], np.int32)
        col = np.clip(base[:, None, None, :] + mods[None, None, :, None], 0, 255)         # N,1,4,3
        d = ((col - px[:, :, None, :]) ** 2).sum(-1)                                       # N,8,4
        idx = d.argmin(-1); e = d.min(-1).sum(-1)
        if best_e is None:
            best_e, best_t, best_i = e, np.zeros(len(e), np.int32), idx
        else:
            m = e < best_e
            best_e = np.where(m, e, best_e); best_t = np.where(m, t, best_t); best_i = np.where(m[:, None], idx, best_i)
    return best_e, best_t, best_i

def encode(img):
    """img HxWx3 uint8 -> bytes ETC1 (sem mipmaps). H,W qualquer (preenche por repeticao da borda ate multiplo de 4)."""
    h, w, _ = img.shape
    ph, pw = (-h) % 4, (-w) % 4
    if ph or pw: img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode='edge')
    P = _blocks(img)                  # N,16,3
    N = P.shape[0]
    best = np.full(N, np.iinfo(np.int64).max, np.int64)
    out = np.zeros((N, 8), np.uint8)
    for flip in (0, 1):
        grp = [np.where(SB[flip] == s)[0] for s in (0, 1)]       # posicoes i de cada subbloco (8 cada)
        px = [P[:, g, :] for g in grp]                           # N,8,3
        mean = [p.mean(1) for p in px]
        for mode in ('ind', 'diff'):
            if mode == 'ind':
                q = [_quant(m, _EXP4) for m in mean]
                col = [_EXP4[x] for x in q]
            else:
                q1 = _quant(mean[0], _EXP5); q2 = _quant(mean[1], _EXP5)
                q2 = np.clip(q2, q1 - 4, q1 + 3)                 # delta de 3 bits com sinal
                q = [q1, q2]
                col = [_EXP5[q1], _EXP5[q2]]
            e1, t1, i1 = _best_tables(px[0], col[0])
            e2, t2, i2 = _best_tables(px[1], col[1])
            err = e1.astype(np.int64) + e2
            m = err < best
            if not m.any(): continue
            best = np.where(m, err, best)
            blk = np.zeros((N, 8), np.uint8)
            if mode == 'ind':
                blk[:, 0] = (q[0][:, 0] << 4) | q[1][:, 0]; blk[:, 1] = (q[0][:, 1] << 4) | q[1][:, 1]; blk[:, 2] = (q[0][:, 2] << 4) | q[1][:, 2]
                diffbit = 0
            else:
                d = (q[1] - q[0]) & 7
                for c in range(3): blk[:, c] = (q[0][:, c] << 3) | d[:, c]
                diffbit = 1
            blk[:, 3] = (t1 << 5) | (t2 << 2) | (diffbit << 1) | flip
            msb = np.zeros(N, np.int64); lsb = np.zeros(N, np.int64)
            for s, (g, ii) in enumerate(((grp[0], i1), (grp[1], i2))):
                for k, pos in enumerate(g):
                    msb |= ((ii[:, k] >> 1).astype(np.int64) & 1) << int(pos)
                    lsb |= (ii[:, k].astype(np.int64) & 1) << int(pos)
            blk[:, 4] = (msb >> 8) & 255; blk[:, 5] = msb & 255; blk[:, 6] = (lsb >> 8) & 255; blk[:, 7] = lsb & 255
            out = np.where(m[:, None], blk, out)
    return out.tobytes()

def psnr(a, b):
    mse = ((a.astype(np.float64) - b.astype(np.float64)) ** 2).mean()
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)
