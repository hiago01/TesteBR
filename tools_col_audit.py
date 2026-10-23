#!/usr/bin/env python3
"""Auditoria OFFLINE (somente leitura) do countrye_10.col / CE_brewery e do CINFO.BIN.
Mesma ordem de busca e mesmas regras do CustomCol.h do cliente.

  python3 tools_col_audit.py <raiz>          # raiz = pasta 'files' (contem SAMP/ e texdb/)
  python3 tools_col_audit.py <raiz> --list   # lista todas as entradas do .col
"""
import os, struct, sys

COL_FILE, TARGET = "countrye_10.col", "CE_brewery"
IDENTS = {b"COLL": 1, b"COL2": 2, b"COL3": 3, b"COL4": 4}

def parse(d):
    ents, off = [], 0
    while off < len(d):
        left = len(d) - off
        if left < 8:
            return ents, ("OK" if not any(d[off:]) else "CABECALHO TRUNCADO"), off
        ident = d[off:off + 4]
        if ident not in IDENTS:
            return ents, ("OK" if ents and not any(d[off:]) else "IDENT INVALIDO"), off
        size = struct.unpack_from("<I", d, off + 4)[0]
        if size < 24: return ents, "SIZE INVALIDO (<24)", off
        if off + 8 + size > len(d): return ents, "ENTRADA PASSA DO FIM (arquivo cortado)", off
        name = d[off + 8:off + 30].split(b"\0")[0].decode("latin1")
        ents.append((off, ident.decode(), size, struct.unpack_from("<H", d, off + 30)[0], name))
        off += 8 + size
    return ents, ("OK" if ents else "VAZIO"), off

def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    root, show = sys.argv[1], "--list" in sys.argv
    cands = [os.path.join(root, "SAMP", "col", COL_FILE), os.path.join(root, "SAMP", COL_FILE), os.path.join(root, "texdb", COL_FILE)]
    found = None
    for c in cands:
        ok = os.path.isfile(c); print(f"A. {c}: {'ENCONTRADO' if ok else 'nao existe'}")
        found = found or (c if ok else None)
    if found:
        d = open(found, "rb").read()
        ents, st, stop = parse(d)
        print(f"B. {found}: {len(d)} bytes, {len(ents)} entradas, status={st} (parou em +{stop})")
        for i, e in enumerate(ents):
            if show or e[4].lower() == TARGET.lower(): print(f"   #{i:03d} +{e[0]:<8} {e[1]} size={e[2]:<8} modelId={e[3]:<5} nome='{e[4]}'")
        hit = [i for i, e in enumerate(ents) if e[4].lower() == TARGET.lower()]
        print(f"C. '{TARGET}' {'ESTA dentro de ' + COL_FILE + ' (entrada #' + str(hit[0]) + ')' if hit else 'NAO esta em ' + COL_FILE}")
        if st != "OK": print("   ATENCAO: arquivo nao integro -> o cliente RECUSA a nova leitura (col_reload.on) com este arquivo")
    else:
        print("countrye_10.col nao encontrado nas 3 pastas")
    print("--- CINFO.BIN")
    any_ = False
    for rel in ("CINFO.BIN", "cinfo.bin", "models/CINFO.BIN", "MODELS/CINFO.BIN", "texdb/CINFO.BIN"):
        p = os.path.join(root, rel)
        if os.path.isfile(p):
            any_ = True; print(f"{p}: {os.path.getsize(p)} bytes{'  <- 0 bytes = invalido' if os.path.getsize(p) == 0 else ''}")
    if not any_: print("nenhum CINFO.BIN (o jogo gera um novo)")
    sig = os.path.join(root, "SAMP", "cinfo.sig")
    print("assinatura gravada:", open(sig).read().strip() if os.path.isfile(sig) else "(nenhuma: o 1o start do cliente novo renomeia o cache atual para .bak-<tempo>)")

main()
