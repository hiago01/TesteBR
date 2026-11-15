#!/usr/bin/env python3
"""Gera SAMP/anims/carregar.ifp (ANP3, bloco 'carregar').
Anims (so rotacao de ossos, overlay CA_PARTIAL | CA_HOLD_LAST):
  carregador  - quem carrega: bracos segurando o corpo no ombro (estilo bombeiro)
  carregado   - quem e carregado: bracos soltos pendurados, cabeca caida
Quaternions dos bracos partem das poses do poses.ifp ja testadas; os AJUSTES (graus) ficam em DELTAS
e podem ser mexidos sem recompilar o app: edite, rode, copie o .ifp para SAMP/anims/.
Uso: python3 tools/make_carregar_ifp.py [poses.ifp] [saida.ifp]"""
import struct, sys, math

def qmul(a, b):  # x,y,z,w
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx,
            aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)

def qeul(dx, dy, dz):  # graus, rotacao local X depois Y depois Z
    def ax(a, v):
        h = math.radians(a)/2; s = math.sin(h); c = math.cos(h)
        return tuple(s*i for i in v) + (c,)
    q = ax(dx, (1, 0, 0)); q = qmul(q, ax(dy, (0, 1, 0))); return qmul(q, ax(dz, (0, 0, 1)))

def norm(q):
    n = math.sqrt(sum(i*i for i in q)); return tuple(i/n for i in q)

def read_poses(path):
    d = open(path, 'rb').read(); o = 8
    assert d[:4] == b'ANP3'
    na = struct.unpack('<i', d[o+24:o+28])[0]; o += 28; out = {}
    for _ in range(na):
        nm = d[o:o+24].split(b'\0')[0].decode(); ns = struct.unpack('<i', d[o+24:o+28])[0]; o += 36
        for _ in range(ns):
            sn = d[o:o+24].split(b'\0')[0].decode(); t, nf, b = struct.unpack('<iii', d[o+24:o+36]); o += 36
            q = struct.unpack('<ffff', d[o:o+16]); o += 20*nf
            out[(nm, b)] = q
    return out

# (anim, osso) -> (nome do osso, id, (pose-base, id), delta graus)
BONES = {22: 'R UpperArm', 23: 'R ForeArm', 24: 'R Hand', 32: 'L UpperArm', 33: 'L ForeArm', 34: 'L Hand',
         3: 'Spine1', 4: 'Neck', 5: 'Head'}
PLAN = {
 'carregador': [  # bracos cruzados (mao no ombro/peito) + braco esquerdo a frente, tronco levemente inclinado
    (22, ('bracos_cruzados', 22), (0, 0, 0)), (23, ('bracos_cruzados', 23), (0, 0, 0)), (24, ('bracos_cruzados', 24), (0, 0, 0)),
    (32, ('foto_frontal', 32), (0, 0, 0)), (33, ('foto_frontal', 33), (0, 0, 0)), (34, ('foto_frontal', 34), (0, 0, 0)),
 ],
 'carregado': [   # bracos esticados soltos (apontar_*), com cabeca caida
    (22, ('apontar_dir', 22), (0, 0, 15)), (23, ('apontar_dir', 23), (0, 0, 0)), (24, ('apontar_dir', 24), (0, 0, 0)),
    (32, ('apontar_esq', 32), (0, 0, -15)), (33, ('apontar_esq', 33), (0, 0, 0)), (34, ('apontar_esq', 34), (0, 0, 0)),
 ],
}
HEAD_DELTA = {'carregado': {4: (0, 0, 25), 5: (0, 0, 20)}}   # neck/head: sem base; so se houver pose conhecida

def build(poses):
    anims = []
    for name, rows in PLAN.items():
        seqs = []
        for bone, src, dl in rows:
            q = norm(qmul(poses[src], qeul(*dl)))
            seqs.append((BONES[bone], bone, q))
        body = b''
        for sn, bone, q in seqs:
            s = sn.encode().ljust(24, b'\0') + struct.pack('<iii', 1, 2, bone)
            for t in (0.0, 1.0): s += struct.pack('<ffffF'.replace('F', 'f'), *q, t)
            body += s
        anims.append(name.encode().ljust(24, b'\0') + struct.pack('<iii', len(seqs), len(body), 0) + body)
    blk = b'carregar'.ljust(24, b'\0') + struct.pack('<i', len(anims)) + b''.join(anims)
    return b'ANP3' + struct.pack('<I', len(blk)) + blk

if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else '/tmp/an/anims/poses.ifp'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'carregar.ifp'
    data = build(read_poses(src)); open(dst, 'wb').write(data); print(dst, len(data), 'bytes')
