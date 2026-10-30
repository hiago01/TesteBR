"""Formato texdb (GTA SA Android 2.10) - leitor e escritor. Conferido contra txd/mobile reais e o desassembly do libGTASA.
  <nome>.txt         indice (texto): 1a linha  cat=0 name=Default onfoot=5 slow=5 fast=5 defaultformat=2 defaultstream=0
                     depois uma linha por textura:  "nome" width=W height=H png=<hash8> img=<hash8> [format= mipmode= alphamode= streammode=]
  <nome>.<fmt>.toc   u32 LE: [tamanho do .dat] + offset de cada textura (ordem do .txt); 0xFFFFFFFF = sem dados
  <nome>.<fmt>.dat   registros: listing(8) | A u32 | B u32 | payload   (B=0: payload cru de GetTotalSize bytes, A=total+4;
                     B!=0: RLE, A-4 bytes comprimidos)
       listing: nameHash16 | glFormat16 | width16 | height15 + hasMip(bit15)
  <nome>.<fmt>.tmb   miniaturas: registros iguais, sem cabecalho de arquivo
  fmt: etc | dxt | pvr | 360 | unc(so .tmb)
  nameHash: h=0; para cada byte c: h=h*33+c; no fim h+=h>>5 (32 bits, diferencia maiuscula); o arquivo guarda os 16 bits baixos."""
import re, struct, zlib

GL_ETC1 = 0x8D64
GL_DXT1 = 0x83F0       # RGB_S3TC_DXT1
GL_RGBA4444 = 0x8033
GL_RGB565 = 0x8363
GL_RGBA8 = 0x1401
GL_RGB5A1 = 0x8034
GL_DXT3 = 0x83F2
GL_DXT5 = 0x83F3

def name_hash32(name):
    h = 0
    for c in name.encode('latin1'):
        h = (h + (h << 5) + c) & 0xFFFFFFFF
    return (h + (h >> 5)) & 0xFFFFFFFF

def name_hash16(name):
    return name_hash32(name) & 0xFFFF

def crc8(data):
    return '%08x' % (zlib.crc32(data) & 0xFFFFFFFF)

def total_size(gl, w, h, mips):
    """tamanho dos dados (todas as mips juntas) para os formatos que geramos."""
    def one(w, h):
        if gl in (GL_ETC1, GL_DXT1):
            return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
        if gl in (GL_DXT3, GL_DXT5):
            return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
        if gl in (GL_RGBA4444, GL_RGB565, GL_RGB5A1):
            return w * h * 2
        if gl == GL_RGBA8:
            return w * h * 4
        raise ValueError('formato %#x' % gl)
    if not mips:
        return one(w, h)
    t = 0
    while True:
        t += one(w, h)
        if w <= 1 and h <= 1: break
        w, h = max(1, w // 2), max(1, h // 2)
    return t

def pack_record(name, gl, w, h, payload, mips=False):
    """registro CRU (B=0): listing | A=len+4 | B=0 | payload"""
    assert len(payload) == total_size(gl, w, h, mips), 'payload != GetTotalSize'
    hh = (h & 0x7FFF) | (0x8000 if mips else 0)
    return struct.pack('<HHHHII', name_hash16(name), gl, w, hh, len(payload) + 4, 0) + payload

class Entry:
    def __init__(self, name, w, h, line=''):
        self.name, self.w, self.h, self.line = name, w, h, line

def parse_txt(path):
    cat, entries = None, []
    for raw in open(path, encoding='latin1'):
        l = raw.rstrip('\r\n')
        if not l.strip(): continue
        if l.startswith('cat='):
            cat = l; continue
        m = re.match(r'"([^"]+)"\s*(.*)', l) or re.match(r'(\S+)\s*(.*)', l)
        kv = dict(p.split('=', 1) for p in m.group(2).split() if '=' in p)
        entries.append(Entry(m.group(1), int(kv.get('width', 0)), int(kv.get('height', 0)), l))
    return cat, entries

def read_db(folder, name, fmt):
    """-> (entries, [(listing, A, B, payload) | None]) na ordem do .txt"""
    cat, ents = parse_txt('%s/%s.txt' % (folder, name))
    toc = open('%s/%s.%s.toc' % (folder, name, fmt), 'rb').read()
    offs = struct.unpack('<%dI' % (len(toc) // 4), toc)
    dat = open('%s/%s.%s.dat' % (folder, name, fmt), 'rb').read()
    assert offs[0] == len(dat), 'toc[0] != tamanho do dat'
    assert len(offs) - 1 == len(ents), 'toc/txt com contagens diferentes (%d x %d)' % (len(offs) - 1, len(ents))
    recs = []
    for i, e in enumerate(ents):
        o = offs[1 + i]
        if o == 0xFFFFFFFF: recs.append(None); continue
        h16, gl, w, hh, A, B = struct.unpack_from('<HHHHII', dat, o)
        recs.append(({'hash': h16, 'gl': gl, 'w': w, 'h': hh & 0x7FFF, 'mips': bool(hh >> 15)}, A, B, dat[o + 16:o + 12 + A]))
    return ents, recs

def write_db(folder, name, fmt, items, cat_line=None, txt_lines=None, with_txt=True):
    """items: lista de (entry_name, w, h, gl, payload, mips). Escreve .txt (se with_txt), .toc, .dat."""
    import os
    os.makedirs(folder, exist_ok=True)
    dat = bytearray(); offs = []
    for (n, w, h, gl, payload, mips) in items:
        offs.append(len(dat)); dat += pack_record(n, gl, w, h, payload, mips)
    open('%s/%s.%s.dat' % (folder, name, fmt), 'wb').write(bytes(dat))
    open('%s/%s.%s.toc' % (folder, name, fmt), 'wb').write(struct.pack('<%dI' % (len(offs) + 1), len(dat), *offs))
    if with_txt:
        with open('%s/%s.txt' % (folder, name), 'w', encoding='latin1', newline='\n') as f:
            f.write((cat_line or 'cat=0 name=Default onfoot=5 slow=5 fast=5 defaultformat=2 defaultstream=0') + '\n')
            for l in txt_lines: f.write(l + '\n')

def rle_decode(src, dst_size, seg, marker):
    """igual a RLEDecompress do jogo: segmentos de 'seg' bytes; segmento que COMECA com 'marker' = [marker][n][segmento] x n."""
    out = bytearray(); i = 0
    while len(out) < dst_size:
        if src[i] == marker:
            n = src[i + 1]
            if n: out += src[i + 2:i + 2 + seg] * n
            i += seg + 2
        else:
            out += src[i:i + seg]; i += seg
    return bytes(out[:dst_size])
