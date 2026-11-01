#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
skin_texdb.py - converte o PACK de roupas (PNG/JPEG) num BANCO texdb do GTA SA Mobile, no MESMO formato do banco do mapa:

    texdb/<nome>/<nome>.txt                lista de texturas (uma linha por textura + linha "cat=0 ...")
    texdb/<nome>/<nome>.dxt.{dat,tmb,toc}  DXT1 (sem alfa) / DXT5 (com alfa)      -> GPUs Adreno/Tegra
    texdb/<nome>/<nome>.etc.{dat,tmb,toc}  ETC1 (sem alfa) / RGBA4444 (com alfa)  -> GPUs Mali
    texdb/<nome>/<nome>.unc.tmb            miniaturas 8x8 sem compressao
    texdb/<nome>/<nome>.idx                (so do Maximo RP) "pasta/arquivo<TAB>nome-no-banco": o cliente descobre as variantes por ele

Formato (engenharia reversa da libGTASA.so ARM64 do projeto + banco "mapa" de referencia):
  - registro = 16 bytes + dados:  u16 nameHash16 | u16 formatoGL | u16 largura | u16 altura(bit15 = tem mips) |
                                  u32 tamanho+4 | u32 rle(0 = sem RLE) | dados
  - nameHash: h = h*33 + c (u32, sem alterar maiusculas), hash = h + (h >> 5); o registro guarda os 16 bits baixos
  - .dat: registros em sequencia (mesma ordem do .txt);  .toc: u32 tamanho do .dat + u32 deslocamento de cada registro
  - .tmb: miniaturas 8x8 (um registro por textura, mesma ordem)
  - formatoGL: 0x83F0 DXT1, 0x83F3 DXT5, 0x8D64 ETC1, 0x8033 RGBA4444, 0x8363 RGB565

Uso:
  python3 skin_texdb.py build  <pasta_pack> <saida_texdb> [--name skins] [--max 512] [--no-etc] [--no-dxt]
  python3 skin_texdb.py verify <texdb/skins>            confere estrutura, hashes e decodifica todas as texturas
  python3 skin_texdb.py selftest [<pasta_mapa>]         testes internos (+ comparacao com o banco do mapa)
Requer: Python 3.8+, numpy, Pillow  (pip install numpy pillow)
"""
import os, sys, struct, zlib, argparse
import numpy as np
from PIL import Image

GL_DXT1, GL_DXT5, GL_ETC1, GL_4444, GL_565 = 0x83F0, 0x83F3, 0x8D64, 0x8033, 0x8363
CAT_LINE = "cat=0 name=Default onfoot=5 slow=5 fast=5 defaultformat=2 defaultstream=1"   # igual ao do mapa
MAX_NAME = 31   # RwTexture.name tem 32 bytes

# ------------------------------------------------------------------------------ nomes / hash
def name_hash(s):
    h = 0
    for ch in s.encode("latin-1", "replace"):
        h = (h * 33 + ch) & 0xFFFFFFFF
    return (h + (h >> 5)) & 0xFFFFFFFF

def db_name(rel):
    """Nome da textura no banco para o caminho do pack (ex.: 'tshirts/1/34' -> 'tshirts_1_34'). MESMA regra do cliente
    (SkinTexDbCore.h::DbName): minusculo; '/', '\\', '.', '-', ' ' viram '_'; acima de 31 caracteres: 22 + '_' + crc32 (8 hex)."""
    s = rel.lower()
    for c in "/\\.- ":
        s = s.replace(c, "_")
    if len(s) > MAX_NAME:
        s = s[:22] + "_" + "%08x" % (zlib.crc32(rel.lower().encode("utf-8")) & 0xFFFFFFFF)
    return s

# ------------------------------------------------------------------------------ blocos 4x4
def blocks(img, c):
    """img (H,W,c) -> (N,16,c) com N = (H/4)*(W/4), ordem de linhas; pixel k = y*4+x."""
    h, w = img.shape[:2]
    b = img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)
    return b

def unblocks(b, h, w, c):
    return b.reshape(h // 4, w // 4, 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(h, w, c)

# ------------------------------------------------------------------------------ DXT
def _to565(rgb):
    r = (rgb[..., 0].astype(np.int32) * 31 + 127) // 255
    g = (rgb[..., 1].astype(np.int32) * 63 + 127) // 255
    b = (rgb[..., 2].astype(np.int32) * 31 + 127) // 255
    return (r << 11) | (g << 5) | b

def _from565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return np.stack([(r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31], -1).astype(np.int32)

def _color_blocks(px):
    """px (N,16,3) uint8 -> (c0,c1 uint16 (c0>c1), indices (N,16)) : ajuste por eixo principal (PCA)."""
    p = px.astype(np.float64)
    mean = p.mean(1, keepdims=True)
    d = p - mean
    cov = np.einsum("nki,nkj->nij", d, d)
    axis = np.tile(np.array([0.57735, 0.57735, 0.57735]), (len(p), 1))
    for _ in range(8):
        axis = np.einsum("nij,nj->ni", cov, axis)
        nrm = np.linalg.norm(axis, axis=1, keepdims=True)
        axis = np.where(nrm > 1e-9, axis / np.maximum(nrm, 1e-9), np.array([0.57735, 0.57735, 0.57735]))
    t = np.einsum("nki,ni->nk", d, axis)
    lo = mean[:, 0, :] + axis * t.min(1, keepdims=True)
    hi = mean[:, 0, :] + axis * t.max(1, keepdims=True)
    e0 = _to565(np.clip(np.rint(hi), 0, 255)); e1 = _to565(np.clip(np.rint(lo), 0, 255))
    swap = e0 < e1
    e0, e1 = np.where(swap, e1, e0), np.where(swap, e0, e1)
    same = e0 == e1                                  # bloco de cor lisa: forca c0 > c1 (modo 4 cores)
    e0 = np.where(same & (e0 < 65535), e0 + 1, e0)
    e1 = np.where(same & (e0 == 65535), e1 - 1, e1)
    e1 = np.where(same & (e1 == e0), np.maximum(e0 - 1, 0), e1)
    swap2 = e0 < e1
    e0, e1 = np.where(swap2, e1, e0), np.where(swap2, e0, e1)
    c0 = _from565(e0); c1 = _from565(e1)
    pal = np.stack([c0, c1, (2 * c0 + c1) // 3, (c0 + 2 * c1) // 3], 1)          # (N,4,3)
    dist = ((px.astype(np.int32)[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)   # (N,16,4)
    idx = dist.argmin(-1)
    return e0.astype(np.uint16), e1.astype(np.uint16), idx

def _pack_color(e0, e1, idx):
    bits = np.zeros(len(e0), np.uint32)
    lut = np.array([0, 1, 2, 3])                      # indice na ordem do GL: 0=c0, 1=c1, 2=2/3c0+1/3c1, 3=1/3c0+2/3c1
    for k in range(16):
        bits |= (lut[idx[:, k]].astype(np.uint32) << (2 * k))
    out = np.zeros((len(e0), 8), np.uint8)
    out[:, 0] = e0 & 255; out[:, 1] = e0 >> 8; out[:, 2] = e1 & 255; out[:, 3] = e1 >> 8
    for i in range(4): out[:, 4 + i] = (bits >> (8 * i)) & 255
    return out

def encode_dxt1(rgb):
    h, w = rgb.shape[:2]
    e0, e1, idx = _color_blocks(blocks(rgb, 3))
    return _pack_color(e0, e1, idx).tobytes()

def encode_dxt5(rgba):
    h, w = rgba.shape[:2]
    e0, e1, idx = _color_blocks(blocks(rgba[..., :3], 3))
    col = _pack_color(e0, e1, idx)
    a = blocks(rgba[..., 3:4], 1)[..., 0].astype(np.int32)                          # (N,16)
    a0 = a.max(1); a1 = a.min(1)
    flat = a0 == a1
    a0 = np.where(flat & (a0 < 255), a0 + 1, a0)                                    # a0 > a1 => modo de 8 valores
    a1 = np.where(flat & (a0 == 255), a1 - 1, a1)
    pal = np.zeros((len(a), 8), np.int32)
    pal[:, 0] = a0; pal[:, 1] = a1
    for i in range(1, 7): pal[:, 1 + i] = ((7 - i) * a0 + i * a1) // 7
    di = np.abs(a[:, :, None] - pal[:, None, :]).argmin(-1)                         # (N,16)
    bits = np.zeros(len(a), np.uint64)
    for k in range(16): bits |= (di[:, k].astype(np.uint64) << np.uint64(3 * k))
    out = np.zeros((len(a), 16), np.uint8)
    out[:, 0] = a0; out[:, 1] = a1
    for i in range(6): out[:, 2 + i] = ((bits >> np.uint64(8 * i)) & np.uint64(255)).astype(np.uint8)
    out[:, 8:] = col
    return out.tobytes()

def decode_dxt1(data, w, h):
    n = (w // 4) * (h // 4); b = np.frombuffer(data, np.uint8)[:n * 8].reshape(n, 8)
    c0 = b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8); c1 = b[:, 2].astype(np.int32) | (b[:, 3].astype(np.int32) << 8)
    bits = b[:, 4].astype(np.uint32) | (b[:, 5].astype(np.uint32) << 8) | (b[:, 6].astype(np.uint32) << 16) | (b[:, 7].astype(np.uint32) << 24)
    p0, p1 = _from565(c0), _from565(c1)
    four = (c0 > c1)[:, None]
    p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2); p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)
    out = np.zeros((n, 16, 3), np.int32)
    for k in range(16): out[:, k] = pal[np.arange(n), ((bits >> (2 * k)) & 3).astype(np.int64)]
    return unblocks(out.astype(np.uint8), h, w, 3)

def decode_dxt5_alpha(data, w, h):
    n = (w // 4) * (h // 4); b = np.frombuffer(data, np.uint8)[:n * 16].reshape(n, 16)
    a0 = b[:, 0].astype(np.int32); a1 = b[:, 1].astype(np.int32)
    bits = np.zeros(n, np.uint64)
    for i in range(6): bits |= b[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    pal = np.zeros((n, 8), np.int32); pal[:, 0] = a0; pal[:, 1] = a1
    for i in range(1, 7): pal[:, 1 + i] = ((7 - i) * a0 + i * a1) // 7
    out = np.zeros((n, 16), np.int32)
    for k in range(16): out[:, k] = pal[np.arange(n), ((bits >> np.uint64(3 * k)) & np.uint64(7)).astype(np.int64)]
    return unblocks(out[..., None].astype(np.uint8), h, w, 1)[..., 0], b[:, 8:].tobytes()

# ------------------------------------------------------------------------------ ETC1
ETC_TABLE = np.array([[2, 8], [5, 17], [9, 29], [13, 42], [18, 60], [24, 80], [33, 106], [47, 183]], np.int32)
ETC_MOD = np.array([[a, b, -a, -b] for a, b in ETC_TABLE], np.int32)               # (8,4): indice (msb<<1|lsb)

def _sub_masks():
    """pixel k = y*4+x (ordem do nosso bloco). flip=0: metades esquerda/direita; flip=1: cima/baixo."""
    m = np.zeros((2, 2, 16), bool)
    for k in range(16):
        y, x = divmod(k, 4)
        m[0, 0 if x < 2 else 1, k] = True
        m[1, 0 if y < 2 else 1, k] = True
    return m
_MASK = _sub_masks()

def _etc_best(px, mask, base):
    """px (N,16,3) int; mask (16,) bool; base (N,3) int (cor base ja expandida 8 bits). -> (erro (N,), tabela (N,), idx (N,16))"""
    sel = np.where(mask)[0]
    p = px[:, sel, :]                                                               # (N,8,3)
    best_err = np.full(len(px), 1 << 60, np.int64); best_t = np.zeros(len(px), np.int64); best_i = np.zeros((len(px), 8), np.int64)
    for t in range(8):
        mods = ETC_MOD[t]                                                           # (4,)
        c = np.clip(base[:, None, None, :] + mods[None, None, :, None], 0, 255)      # (N,1,4,3)
        e = ((p[:, :, None, :] - c) ** 2).sum(-1)                                   # (N,8,4)
        ix = e.argmin(-1); er = np.take_along_axis(e, ix[..., None], -1)[..., 0].sum(-1)
        better = er < best_err
        best_err = np.where(better, er, best_err); best_t = np.where(better, t, best_t)
        best_i = np.where(better[:, None], ix, best_i)
    return best_err, best_t, best_i

def encode_etc1(rgb):
    h, w = rgb.shape[:2]
    px = blocks(rgb, 3).astype(np.int64)                                            # (N,16,3)
    n = len(px)
    cand = []
    for flip in (0, 1):
        means = []
        for s in (0, 1):
            m = _MASK[flip, s]
            means.append((px[:, m, :].sum(1) + 4) // 8)                              # (N,3)
        q5 = [np.clip((mm * 31 + 127) // 255, 0, 31) for mm in means]
        d = q5[1] - q5[0]
        diff_ok = ((d >= -4) & (d <= 3)).all(1)
        q4 = [np.clip((mm * 15 + 127) // 255, 0, 15) for mm in means]
        e5 = [(q << 3) | (q >> 2) for q in q5]; e4 = [q * 17 for q in q4]
        base = [np.where(diff_ok[:, None], e5[s], e4[s]) for s in (0, 1)]
        errs, tabs, idxs = [], [], []
        for s in (0, 1):
            er, tb, ix = _etc_best(px, _MASK[flip, s], base[s]); errs.append(er); tabs.append(tb); idxs.append(ix)
        cand.append((errs[0] + errs[1], flip, diff_ok, q5, q4, tabs, idxs))
    out = np.zeros((n, 8), np.uint8)
    use1 = cand[1][0] < cand[0][0]
    for flip in (0, 1):
        sel = (use1 == bool(flip))
        if not sel.any(): continue
        _, _, diff_ok, q5, q4, tabs, idxs = cand[flip]
        i = np.where(sel)[0]
        dk = diff_ok[i]
        b0 = np.where(dk, q5[0][i, 0] << 3 | ((q5[1][i, 0] - q5[0][i, 0]) & 7), q4[0][i, 0] << 4 | q4[1][i, 0])
        b1 = np.where(dk, q5[0][i, 1] << 3 | ((q5[1][i, 1] - q5[0][i, 1]) & 7), q4[0][i, 1] << 4 | q4[1][i, 1])
        b2 = np.where(dk, q5[0][i, 2] << 3 | ((q5[1][i, 2] - q5[0][i, 2]) & 7), q4[0][i, 2] << 4 | q4[1][i, 2])
        b3 = (tabs[0][i] << 5) | (tabs[1][i] << 2) | (dk.astype(np.int64) << 1) | flip
        msb = np.zeros(len(i), np.int64); lsb = np.zeros(len(i), np.int64)
        for s in (0, 1):
            sub = np.where(_MASK[flip, s])[0]
            for j, k in enumerate(sub):
                y, x = divmod(int(k), 4); bit = x * 4 + y                            # ETC1: pixel k' = x*4 + y (coluna a coluna)
                v = idxs[s][i, j]
                msb |= ((v >> 1) & 1) << bit; lsb |= (v & 1) << bit
        out[i, 0] = b0; out[i, 1] = b1; out[i, 2] = b2; out[i, 3] = b3
        out[i, 4] = (msb >> 8) & 255; out[i, 5] = msb & 255; out[i, 6] = (lsb >> 8) & 255; out[i, 7] = lsb & 255
    return out.tobytes()

def decode_etc1(data, w, h):
    n = (w // 4) * (h // 4); b = np.frombuffer(data, np.uint8)[:n * 8].reshape(n, 8).astype(np.int64)
    diff = (b[:, 3] >> 1) & 1; flip = b[:, 3] & 1
    cw = [(b[:, 3] >> 5) & 7, (b[:, 3] >> 2) & 7]
    base = [np.zeros((n, 3), np.int64), np.zeros((n, 3), np.int64)]
    for c in range(3):
        v = b[:, c]
        a5 = v >> 3; dd = v & 7; dd = np.where(dd >= 4, dd - 8, dd); s5 = np.clip(a5 + dd, 0, 31)
        a4, b4 = v >> 4, v & 15
        base[0][:, c] = np.where(diff == 1, (a5 << 3) | (a5 >> 2), a4 * 17)
        base[1][:, c] = np.where(diff == 1, (s5 << 3) | (s5 >> 2), b4 * 17)
    msb = (b[:, 4] << 8) | b[:, 5]; lsb = (b[:, 6] << 8) | b[:, 7]
    out = np.zeros((n, 16, 3), np.int64)
    for k in range(16):
        y, x = divmod(k, 4); bit = x * 4 + y
        sub = np.where(flip == 0, 0 if x < 2 else 1, 0 if y < 2 else 1)
        idx = (((msb >> bit) & 1) << 1) | ((lsb >> bit) & 1)
        cwk = np.where(sub == 0, cw[0], cw[1]); bs = np.where(sub[:, None] == 0, base[0], base[1])
        mod = ETC_MOD[cwk, idx]
        out[:, k] = np.clip(bs + mod[:, None], 0, 255)
    return unblocks(out.astype(np.uint8), h, w, 3)

# ------------------------------------------------------------------------------ sem compressao
def to_565(rgb):
    v = _to565(rgb).astype("<u2"); return v.tobytes()
def to_4444(rgba):
    r, g, b, a = [rgba[..., i].astype(np.int32) for i in range(4)]
    v = (((r * 15 + 127) // 255) << 12) | (((g * 15 + 127) // 255) << 8) | (((b * 15 + 127) // 255) << 4) | ((a * 15 + 127) // 255)
    return v.astype("<u2").tobytes()
def from_4444(data, w, h):
    v = np.frombuffer(data, "<u2")[:w * h].astype(np.int32).reshape(h, w)
    return np.stack([((v >> s) & 15) * 17 for s in (12, 8, 4, 0)], -1).astype(np.uint8)

# ------------------------------------------------------------------------------ imagens
def pot(n, cap):
    p = 4
    while p < n and p < cap: p *= 2
    return min(p, cap)

def load_image(path, cap):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    tw, th = pot(w, cap), pot(h, cap)
    if (tw, th) != (w, h): im = im.resize((tw, th), Image.LANCZOS)
    a = np.array(im, np.uint8)
    return a

def thumb(rgba):
    im = Image.fromarray(rgba, "RGBA").resize((8, 8), Image.BOX)
    return np.array(im, np.uint8)

def has_alpha(rgba): return bool((rgba[..., 3] < 250).any())

# ------------------------------------------------------------------------------ registros
def record(name, fmt, w, h, data):
    return struct.pack("<HHHHII", name_hash(name) & 0xFFFF, fmt, w, h, len(data) + 4, 0) + data

def encode_tex(rgba, alpha, kind):
    """kind: 'dxt' | 'etc' | 'unc' ; devolve (formato GL, bytes)"""
    rgb = rgba[..., :3]
    if kind == "dxt": return (GL_DXT5, encode_dxt5(rgba)) if alpha else (GL_DXT1, encode_dxt1(rgb))
    if kind == "etc": return (GL_4444, to_4444(rgba)) if alpha else (GL_ETC1, encode_etc1(rgb))
    return (GL_4444, to_4444(rgba)) if alpha else (GL_565, to_565(rgb))

def write_db(items, outdir, name, kinds=("dxt", "etc"), log=print):
    """items: lista de (rel, rgba ndarray, crc_png). Escreve o banco em outdir/name/."""
    d = os.path.join(outdir, name); os.makedirs(d, exist_ok=True)
    items = sorted(items, key=lambda t: t[0])
    # nomes unicos (sem colisao nos 16 bits do hash: o registro so guarda 16 bits)
    used16, usedname, names = {}, set(), []
    for rel, _, _ in items:
        base = db_name(rel); nm = base; k = 0
        while nm in usedname or (name_hash(nm) & 0xFFFF) in used16:
            k += 1; suf = "~%d" % k; nm = base[:MAX_NAME - len(suf)] + suf
            if k > 5000: raise SystemExit("nao consegui nome unico para " + rel)
        if nm != base: log("  aviso: hash de 16 bits colidiu; '%s' virou '%s'" % (rel, nm))
        usedname.add(nm); used16[name_hash(nm) & 0xFFFF] = nm; names.append(nm)
    lines = [CAT_LINE]; idx = []
    for (rel, rgba, crc), nm in zip(items, names):
        h, w = rgba.shape[:2]; al = has_alpha(rgba)
        img = zlib.crc32(rgba.tobytes()) & 0xFFFFFFFF
        lines.append('"%s" width=%d height=%d%s png=%08x img=%08x' % (nm, w, h, " alphamode=1" if al else "", crc, img))
        idx.append("%s\t%s" % (rel, nm))
    with open(os.path.join(d, name + ".txt"), "w", newline="\n") as f: f.write("\n".join(lines) + "\n")
    with open(os.path.join(d, name + ".idx"), "w", newline="\n", encoding="utf-8") as f: f.write("\n".join(idx) + "\n")
    total = 0
    for kind in list(kinds) + ["unc"]:
        dat, tmb, toc = bytearray(), bytearray(), []
        for (rel, rgba, crc), nm in zip(items, names):
            h, w = rgba.shape[:2]; al = has_alpha(rgba)
            if kind != "unc":
                fmt, data = encode_tex(rgba, al, kind)
                toc.append(len(dat)); dat += record(nm, fmt, w, h, data)
            fmt, data = encode_tex(thumb(rgba), al, kind)
            tmb += record(nm, fmt, 8, 8, data)
        if kind != "unc":
            open(os.path.join(d, "%s.%s.dat" % (name, kind)), "wb").write(bytes(dat))
            open(os.path.join(d, "%s.%s.toc" % (name, kind)), "wb").write(struct.pack("<I", len(dat)) + struct.pack("<%dI" % len(toc), *toc))
            total += len(dat)
        open(os.path.join(d, "%s.%s.tmb" % (name, kind)), "wb").write(bytes(tmb))
    log("banco '%s': %d texturas, %.1f MB de dados" % (name, len(items), total / 1048576.0))
    return d

# ------------------------------------------------------------------------------ leitura / verificacao
def parse_txt(path):
    ents = []
    for ln in open(path, encoding="utf-8"):
        ln = ln.rstrip("\n")
        if ln.startswith('"'):
            nm = ln[1:ln.index('"', 1)]; rest = ln[ln.index('"', 1) + 1:].split()
            kv = dict(x.split("=", 1) for x in rest if "=" in x)
            ents.append((nm, kv))
    return ents

def read_records(dat, toc):
    t = open(toc, "rb").read(); d = open(dat, "rb").read()
    total = struct.unpack_from("<I", t, 0)[0]; n = (len(t) - 4) // 4
    offs = struct.unpack_from("<%dI" % n, t, 4)
    if total != len(d): raise ValueError("toc: tamanho %d != .dat %d" % (total, len(d)))
    recs = []
    for o in offs:
        nh, fmt, w, h, sz, rle = struct.unpack_from("<HHHHII", d, o)
        recs.append((nh, fmt, w, h & 0x7FFF, bool(h & 0x8000), sz - 4, rle, d[o + 16:o + 16 + sz - 4]))
    return recs

def decode_record(fmt, w, h, data):
    if fmt == GL_DXT1: return decode_dxt1(data, w, h)
    if fmt == GL_ETC1: return decode_etc1(data, w, h)
    if fmt == GL_DXT5:
        a, col = decode_dxt5_alpha(data, w, h); rgb = decode_dxt1(col, w, h); return np.dstack([rgb, a])
    if fmt == GL_4444: return from_4444(data, w, h)
    raise ValueError("formato 0x%x" % fmt)

def psnr(a, b):
    mse = ((a.astype(np.float64) - b.astype(np.float64)) ** 2).mean()
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)

def verify(d, log=print):
    name = os.path.basename(os.path.normpath(d)); ok = True
    ents = parse_txt(os.path.join(d, name + ".txt")); log("%s.txt: %d texturas" % (name, len(ents)))
    names = [e[0] for e in ents]
    if len(set(names)) != len(names): log("ERRO: nomes repetidos"); ok = False
    h16 = [name_hash(n) & 0xFFFF for n in names]
    if len(set(h16)) != len(h16): log("AVISO: colisao de hash de 16 bits (veja o log do build)")
    for kind in ("dxt", "etc"):
        dat = os.path.join(d, "%s.%s.dat" % (name, kind))
        if not os.path.exists(dat): log("%s: ausente (ok se --no-%s)" % (kind, kind)); continue
        recs = read_records(dat, os.path.join(d, "%s.%s.toc" % (name, kind)))
        if len(recs) != len(ents): log("ERRO %s: %d registros x %d texturas" % (kind, len(recs), len(ents))); ok = False; continue
        tm = open(os.path.join(d, "%s.%s.tmb" % (name, kind)), "rb").read(); pos = 0
        for (nm, kv), r in zip(ents, recs):
            nh, fmt, w, h, mip, ln, rle, data = r
            if nh != (name_hash(nm) & 0xFFFF) or w != int(kv["width"]) or h != int(kv["height"]) or rle: log("ERRO %s %s: cabecalho" % (kind, nm)); ok = False
            try: img = decode_record(fmt, w, h, data)
            except Exception as e: log("ERRO %s %s: %s" % (kind, nm, e)); ok = False
            _, _, _, _, tsz, _ = struct.unpack_from("<HHHHII", tm, pos)[0:6]; pos += 16 + tsz - 4
        if pos != len(tm): log("ERRO %s.tmb: %d bytes sobrando/faltando" % (kind, len(tm) - pos)); ok = False
        log("%s: %d registros ok" % (kind, len(recs)))
    log("RESULTADO: " + ("OK" if ok else "COM ERROS")); return ok

# ------------------------------------------------------------------------------ build do pack
def collect(pack):
    out = []
    for root, _, files in os.walk(pack):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in (".png", ".jpg", ".jpeg"):
                full = os.path.join(root, f); rel = os.path.relpath(full, pack).replace(os.sep, "/")
                out.append((os.path.splitext(rel)[0], full))
    return sorted(out)

def build(pack, outdir, name="skins", cap=512, kinds=("dxt", "etc"), log=print):
    files = collect(pack)
    if not files: raise SystemExit("nenhuma imagem em " + pack)
    seen, items = {}, []
    for rel, full in files:
        if rel in seen: log("  aviso: '%s' existe em mais de um formato; usando %s" % (rel, seen[rel])); continue
        seen[rel] = full
        crc = zlib.crc32(open(full, "rb").read()) & 0xFFFFFFFF
        items.append((rel, load_image(full, cap), crc))
    log("%d imagens lidas de %s (teto %dpx)" % (len(items), pack, cap))
    return write_db(items, outdir, name, kinds, log)

# ------------------------------------------------------------------------------ selftest
def selftest(mapa=None, log=print):
    fails = 0
    def chk(c, msg):
        nonlocal fails
        print(("ok    " if c else "FALHOU ") + msg); fails += (0 if c else 1)
    # nomes: vetores de teste COMPARTILHADOS com o cliente (testes/skintexdb/test_skintexdb.cpp)
    for rel, exp in [("tshirts/1/34", "tshirts_1_34"), ("Versace/TShirts/2_1", "versace_tshirts_2_1"), ("hair/2/10", "hair_2_10")]:
        chk(db_name(rel) == exp, "db_name(%s) = %s" % (rel, db_name(rel)))
    long = "corp/policia/fantasy_m_cop_torso4"; ln = db_name(long)
    chk(len(ln) <= 31 and ln.startswith("corp_policia_fantasy_m_") and ln[22] == "_", "nome longo encurtado: " + ln)
    chk(name_hash("gmap000") == 0x05fa9635, "hash gmap000 = 0x%08x (esperado 0x05fa9635 do banco do mapa)" % name_hash("gmap000"))
    # codecs: imagem sintetica
    yy, xx = np.mgrid[0:64, 0:64]
    img = np.dstack([xx * 4, yy * 4, ((xx + yy) * 2) % 256, np.where((xx // 8 + yy // 8) % 2 == 0, 255, 60)]).astype(np.uint8)
    for nm, enc, dec, ref in [("DXT1", encode_dxt1, decode_dxt1, img[..., :3]), ("ETC1", encode_etc1, decode_etc1, img[..., :3])]:
        r = dec(enc(img[..., :3]), 64, 64); p = psnr(r, ref); chk(p > 28, "%s ida-e-volta PSNR %.1f dB" % (nm, p))
    d5 = encode_dxt5(img); a, col = decode_dxt5_alpha(d5, 64, 64); r = np.dstack([decode_dxt1(col, 64, 64), a])
    chk(psnr(r, img) > 28, "DXT5 ida-e-volta PSNR %.1f dB" % psnr(r, img))
    chk(psnr(from_4444(to_4444(img), 64, 64), img) > 24, "RGBA4444 ida-e-volta")
    flat = np.full((16, 16, 3), 77, np.uint8)
    chk(psnr(decode_dxt1(encode_dxt1(flat), 16, 16), flat) > 40, "DXT1 cor lisa"); chk(psnr(decode_etc1(encode_etc1(flat), 16, 16), flat) > 40, "ETC1 cor lisa")
    if mapa and os.path.exists(os.path.join(mapa, "mapa.dxt.dat")):
        log("-- comparando com o banco do mapa (%s)" % mapa)
        ents = parse_txt(os.path.join(mapa, "mapa.txt"))
        dx = read_records(os.path.join(mapa, "mapa.dxt.dat"), os.path.join(mapa, "mapa.dxt.toc"))
        et = read_records(os.path.join(mapa, "mapa.etc.dat"), os.path.join(mapa, "mapa.etc.toc"))
        chk(len(dx) == len(ents) == len(et), "%d registros dxt/etc/txt" % len(dx))
        chk(all(r[0] == (name_hash(e[0]) & 0xFFFF) for r, e in zip(dx, ents)), "hash16 de todos os registros do mapa confere")
        chk(all(r[1] == GL_DXT1 for r in dx) and all(r[1] == GL_ETC1 for r in et), "formatos GL do mapa: DXT1 0x83F0 / ETC1 0x8D64")
        a = decode_dxt1(dx[5][7], 256, 256); b = decode_etc1(et[5][7], 256, 256)
        chk(psnr(a, b) > 30, "decodificador DXT1 e ETC1 independentes dao a MESMA imagem no tile gmap005 (PSNR %.1f dB)" % psnr(a, b))
        # reconstroi o banco do mapa com o nosso escritor e compara a ESTRUTURA byte a byte (cabecalhos/toc/tmb), nao os dados
        import tempfile
        tiles = [np.dstack([decode_dxt1(r[7], 256, 256), np.full((256, 256), 255, np.uint8)]) for r in dx[:12]]
        with tempfile.TemporaryDirectory() as td:
            items = [(e[0], t, 0) for e, t in zip(ents[:12], tiles)]
            write_db(items, td, "mapa", ("dxt", "etc"), log=lambda *_: None)
            mine = read_records(os.path.join(td, "mapa", "mapa.dxt.dat"), os.path.join(td, "mapa", "mapa.dxt.toc"))
            chk(all((m[0], m[1], m[2], m[3], m[4], m[5], m[6]) == (r[0], r[1], r[2], r[3], r[4], r[5], r[6]) for m, r in zip(mine, dx[:12])), "cabecalhos .dat identicos aos do mapa")
            t0 = open(os.path.join(td, "mapa", "mapa.dxt.toc"), "rb").read(); t1 = open(os.path.join(mapa, "mapa.dxt.toc"), "rb").read()
            chk(t0[4:4 + 48] == t1[4:4 + 48], "deslocamentos do .toc identicos")
            tm0 = open(os.path.join(td, "mapa", "mapa.dxt.tmb"), "rb").read(); tm1 = open(os.path.join(mapa, "mapa.dxt.tmb"), "rb").read()
            chk(all(tm0[i * 48:i * 48 + 16] == tm1[i * 48:i * 48 + 16] for i in range(12)), "cabecalhos .tmb dxt identicos")
            u0 = open(os.path.join(td, "mapa", "mapa.unc.tmb"), "rb").read(); u1 = open(os.path.join(mapa, "mapa.unc.tmb"), "rb").read()
            chk(all(u0[i * 144:i * 144 + 16] == u1[i * 144:i * 144 + 16] for i in range(12)), "cabecalhos .unc.tmb identicos")
            txt0 = open(os.path.join(td, "mapa", "mapa.txt")).read().split("\n")[:3]; txt1 = open(os.path.join(mapa, "mapa.txt")).read().split("\n")[:3]
            chk(txt0[0] == txt1[0] and txt0[1].split(" png=")[0] == txt1[1].split(" png=")[0], "linhas do .txt no mesmo formato do mapa")
            chk(verify(os.path.join(td, "mapa"), log=lambda *_: None), "verify() aceita o banco gerado")
    print("selftest: %d falha(s)" % fails); return fails

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd")
    b = sp.add_parser("build"); b.add_argument("pack"); b.add_argument("out"); b.add_argument("--name", default="skins")
    b.add_argument("--max", type=int, default=512); b.add_argument("--no-etc", action="store_true"); b.add_argument("--no-dxt", action="store_true")
    v = sp.add_parser("verify"); v.add_argument("dir")
    s = sp.add_parser("selftest"); s.add_argument("mapa", nargs="?")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        kinds = tuple(k for k, off in (("dxt", a.no_dxt), ("etc", a.no_etc)) if not off)
        d = build(a.pack, a.out, a.name, a.max, kinds); return 0 if verify(d) else 1
    if a.cmd == "verify": return 0 if verify(a.dir) else 1
    if a.cmd == "selftest": return 1 if selftest(a.mapa) else 0
    ap.print_help(); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
