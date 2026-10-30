#!/usr/bin/env python3
"""Verifica um banco texdb: contagens, toc, hash dos nomes, tamanho dos registros, thumbs; opcional compara com PNGs.
Uso: python3 verify_db.py <pasta_do_banco> <nome> [--src <pasta_png>]"""
import sys, os, struct, re
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texdb_format as T, etc1, dxt1
from PIL import Image

STRICT = '--strict' in sys.argv
def main():
    folder, name = sys.argv[1], sys.argv[2]
    src = sys.argv[sys.argv.index('--src') + 1] if '--src' in sys.argv else None
    bad = 0
    for fmt, dec in (('etc', etc1.decode), ('dxt', dxt1.decode)):
        if not os.path.exists('%s/%s.%s.dat' % (folder, name, fmt)): print(fmt, ': ausente'); continue
        ents, recs = T.read_db(folder, name, fmt)
        ps = []
        for e, r in zip(ents, recs):
            if r is None: print('  sem dados:', e.name); bad += 1 if STRICT else 0; continue
            L, A, B, pay = r
            if L['hash'] != T.name_hash16(e.name): print('  hash errado', e.name); bad += 1
            if (L['w'], L['h']) != (e.w, e.h): print('  tamanho != txt', e.name); bad += 1
            tot = T.total_size(L['gl'], L['w'], L['h'], L['mips'])
            if B != 0 and L['gl'] != T.GL_ETC1: continue      # RLE de outros formatos: segmento diferente, nao conferido aqui
            if B != 0:
                try: pay = T.rle_decode(pay, tot, 8, B)
                except Exception: print('  RLE invalido', e.name); bad += 1; continue
            if len(pay) != tot: print('  registro invalido', e.name); bad += 1; continue
            if src:
                m = re.fullmatch(r'gmap(\d{3})', e.name)
                p = '%s/radar%02d.png' % (src, int(m.group(1)))
                if os.path.exists(p):
                    ref = np.array(Image.open(p).convert('RGB').resize((L['w'], L['h']), Image.LANCZOS))
                    ps.append(etc1.psnr(ref, dec(pay, L['w'], L['h'])))
        tmb = open('%s/%s.%s.tmb' % (folder, name, fmt), 'rb').read(); o = n = 0
        while o < len(tmb): o += 12 + struct.unpack_from('<I', tmb, o + 8)[0]; n += 1
        if o != len(tmb) or n != sum(1 for r in recs if r is not None): print('  tmb inconsistente', n, len(ents)); bad += 1
        print('%s: %d entradas, dat=%d bytes%s' % (fmt, len(ents), os.path.getsize('%s/%s.%s.dat' % (folder, name, fmt)),
              (', PSNR min/med %.1f/%.1f dB' % (min(ps), float(np.median(ps)))) if ps else ''))
    print('FALHAS:', bad); sys.exit(1 if bad else 0)
main()
