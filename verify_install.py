#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_install.py - confere se os pacotes ZIP e a pasta instalada no celular batem com o cliente completo.
So LE; nunca altera nada. Precisa estar na mesma pasta do build_packs.py.

1) Conferir os ZIPs contra o cliente completo (no PC do servidor):
   python verify_install.py zip --src C:\\cache-server\\files --packs C:\\cache-server\\packs

2) Conferir o que ficou no celular contra o cliente completo:
   copie a pasta do app para o PC (cabo USB ou:  adb pull /sdcard/Android/data/com.samp.mobile/files C:\\celular_files)
   python verify_install.py device --src C:\\cache-server\\files --dest C:\\celular_files --gpu 1
   (--gpu: 1=dxt, 2=etc, 3=pvr, a GPU do aparelho testado)
"""
import argparse
import os
import sys
import zipfile

import build_packs as bp


def sha(path):
    return bp.sha256_file(path)


def source_files(src):
    items, _ = bp.scan(src)
    return {rel: (ab, size) for rel, ab, size in items}


def cmd_zip(a):
    src = source_files(a.src)
    seen = {}
    problems = 0
    zips = sorted(f for f in os.listdir(a.packs) if f.lower().endswith(".zip"))
    if not zips:
        sys.exit("nenhum .zip em " + a.packs)
    for zn in zips:
        zp = os.path.join(a.packs, zn)
        print("== %s (%d bytes)" % (zn, os.path.getsize(zp)))
        try:
            z = zipfile.ZipFile(zp)
        except zipfile.BadZipFile as e:
            print("  ERRO: zip invalido:", e); problems += 1; continue
        bad = z.testzip()
        if bad:
            print("  ERRO: CRC invalido em", bad); problems += 1
        for info in z.infolist():
            n = info.filename
            if n == bp.MANIFEST_NAME:
                continue
            seen[n] = zn
            if n not in src:
                print("  SOBRA no zip (nao existe no cliente): " + n); problems += 1; continue
            ab, size = src[n]
            if info.file_size != size:
                print("  TAMANHO diferente: %s zip=%d cliente=%d" % (n, info.file_size, size)); problems += 1; continue
            import hashlib
            h = hashlib.sha256()
            with z.open(info) as f:
                while True:
                    b = f.read(1 << 20)
                    if not b: break
                    h.update(b)
            if h.hexdigest() != sha(ab):
                print("  CONTEUDO diferente: " + n); problems += 1
    for rel in sorted(src):
        if rel not in seen:
            print("FALTA em todos os zips: " + rel); problems += 1
    print("\nRESULTADO: %s (%d problema(s); %d arquivos do cliente, %d nos zips)" %
          ("OK" if problems == 0 else "COM PROBLEMAS", problems, len(src), len(seen)))
    return 1 if problems else 0


def cmd_device(a):
    src = source_files(a.src)
    wanted = {r: v for r, v in src.items() if bp.gpu_of(r) in (0, a.gpu)}
    dest = os.path.abspath(a.dest)
    have = {}
    for root, dirs, files in os.walk(dest):
        for n in files:
            ab = os.path.join(root, n)
            have[os.path.relpath(ab, dest).replace(os.sep, "/")] = ab
    have_lower = {k.lower(): k for k in have}
    missing = diffsize = diffhash = case = 0
    for rel in sorted(wanted):
        ab, size = wanted[rel]
        if rel not in have:
            if rel.lower() in have_lower:
                print("CAIXA diferente: esperado %s, no celular %s" % (rel, have_lower[rel.lower()])); case += 1
            else:
                print("FALTA: " + rel); missing += 1
            continue
        if os.path.getsize(have[rel]) != size:
            print("TAMANHO diferente: %s celular=%d cliente=%d" % (rel, os.path.getsize(have[rel]), size)); diffsize += 1
        elif a.hash and sha(have[rel]) != sha(ab):
            print("CONTEUDO diferente (mesmo tamanho): " + rel); diffhash += 1
    extra = [r for r in sorted(have) if r not in src and not bp.is_ignored(r)
             and not r.startswith((".staging/", ".backup/", ".downloads/"))]
    leftovers = [r for r in have if r.startswith((".staging/", ".backup/", ".downloads/"))]
    if leftovers:
        print("\nSOBRAS do instalador (nao deveriam existir apos instalar): %d arquivo(s), ex.: %s" % (len(leftovers), leftovers[0]))
    if extra:
        print("\nExtras no celular (nao existem no cliente; podem ser gerados pelo jogo ou lixo de instalacao antiga): %d" % len(extra))
        for r in extra[:40]: print("   " + r)
    print("\nRESULTADO: esperados=%d faltando=%d tamanho=%d conteudo=%d caixa=%d extras=%d" %
          (len(wanted), missing, diffsize, diffhash, case, len(extra)))
    ok = not (missing or diffsize or diffhash or case)
    print("INSTALACAO " + ("CONFERE com o cliente completo." if ok else "NAO confere - veja as linhas acima."))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    z = sub.add_parser("zip"); z.add_argument("--src", required=True); z.add_argument("--packs", required=True)
    d = sub.add_parser("device"); d.add_argument("--src", required=True); d.add_argument("--dest", required=True)
    d.add_argument("--gpu", type=int, choices=(1, 2, 3), required=True)
    d.add_argument("--no-hash", dest="hash", action="store_false", help="so tamanho (mais rapido)")
    a = ap.parse_args()
    sys.exit(cmd_zip(a) if a.cmd == "zip" else cmd_device(a))


if __name__ == "__main__":
    main()
