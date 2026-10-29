#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_install.py - confere se os pacotes ZIP e a pasta instalada no celular batem com o cliente completo.
So LE; nunca altera nada. Precisa estar na mesma pasta do build_packs.py.

1) Conferir os ZIPs contra o cliente completo (no PC do servidor):
   python verify_install.py zip --src C:\\cache-server\\files --packs C:\\cache-server\\packs

   Se o --src for so uma parte (ex.: SAMP.zip), use --solo SAMP. Para o cliente completo igual ao build:
   python verify_install.py zip --src C:\\cache-server\\files.zip --overlay C:\\cache-server\\SAMP.zip --packs C:\\cache-server\\packs --relatorio C:\\cache-server\\relatorio.txt
   Contar/listar o que o script enxerga num zip:  python verify_install.py lista --src C:\\cache-server\\SAMP.zip --filtro "New Folder"

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
    return bp.sha256_file(path)  # arquivo do celular (em disco)


class Tee:
    """Escreve na tela e (opcionalmente) em um arquivo de relatorio, para nao perder linhas no PowerShell."""
    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8") if path else None
        self.o = sys.stdout
    def write(self, t):
        self.o.write(t)
        if self.f: self.f.write(t)
    def flush(self):
        self.o.flush()


def source_files(src, overlays=(), solo=None, excluir=()):
    items, _ = bp.scan(src)
    for ov in overlays:
        oitems, _ = bp.scan(os.path.abspath(ov))
        items = bp.apply_overlay(items, oitems)
    d = {rel: (ab, size) for rel, ab, size in items}
    if excluir:
        ex = {x.strip("/").lower() for x in excluir}
        d = {k: v for k, v in d.items() if "/" not in k or k.split("/", 1)[0].lower() not in ex}
    if solo:
        pre = solo.strip("/").lower() + "/"
        d = {k: v for k, v in d.items() if k.lower().startswith(pre)}
    return d


def cmd_zip(a):
    src = source_files(a.src, a.overlay, a.solo, a.excluir_pasta)
    pre = (a.solo.strip("/").lower() + "/") if a.solo else None
    seen = {}
    problems = 0
    sobra = 0
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
            if pre and not n.lower().startswith(pre):
                continue
            seen[n] = zn
            if n not in src:
                print("  SOBRA no zip (nao existe no cliente): " + n); problems += 1; sobra += 1; continue
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
            if h.hexdigest() != bp.sha256_ref(ab):
                print("  CONTEUDO diferente: " + n); problems += 1
    for rel in sorted(src):
        if rel not in seen:
            print("FALTA em todos os zips: " + rel); problems += 1
    falta = [r for r in src if r not in seen]
    print("\nRESUMO: cliente=%d  nos_zips=%d  FALTAM_nos_zips=%d  SOBRAM_nos_zips=%d" % (len(src), len(seen), len(falta), sobra))
    print("RESULTADO: %s (%d problema(s))" % ("OK" if problems == 0 else "COM PROBLEMAS", problems))
    if sobra and not a.solo and not a.overlay:
        print("DICA: se o --src e so uma parte do cliente (ex.: SAMP.zip), use --solo SAMP ou compare com o files.zip + --overlay.")
    return 1 if problems else 0


def cmd_device(a):
    src = source_files(a.src, a.overlay, a.solo)
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
        elif a.hash and sha(have[rel]) != bp.sha256_ref(ab):
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
    z.add_argument("--overlay", action="append", default=[], help="igual ao build_packs.py (pasta/.zip que substitui pastas de topo)")
    z.add_argument("--solo", help="confere so esta pasta de topo, ex.: SAMP")
    z.add_argument("--excluir-pasta", action="append", default=[], help="igual ao build_packs.py (ex.: SAMP)")
    z.add_argument("--relatorio", help="grava tudo tambem neste arquivo .txt")
    d = sub.add_parser("device"); d.add_argument("--src", required=True); d.add_argument("--dest", required=True)
    d.add_argument("--gpu", type=int, choices=(1, 2, 3), required=True)
    d.add_argument("--no-hash", dest="hash", action="store_false", help="so tamanho (mais rapido)")
    d.add_argument("--overlay", action="append", default=[], help="igual ao build_packs.py")
    d.add_argument("--solo", help="confere so esta pasta de topo")
    d.add_argument("--relatorio", help="grava tudo tambem neste arquivo .txt")
    l = sub.add_parser("lista", help="conta e lista os arquivos de uma pasta/.zip (como o build_packs.py os enxerga)")
    l.add_argument("--src", required=True); l.add_argument("--filtro", help="mostra so caminhos que contem este texto")
    l.add_argument("--relatorio")
    a = ap.parse_args()
    sys.stdout = Tee(getattr(a, "relatorio", None))
    if a.cmd == "lista":
        items, zero = bp.scan(os.path.abspath(a.src))
        print("arquivos: %d  (0 bytes: %d)  bytes: %d" % (len(items), len(zero), sum(i[2] for i in items)))
        for rel, _, size in items:
            if not a.filtro or a.filtro.lower() in rel.lower():
                print("%10d  %s" % (size, rel))
        sys.exit(0)
    sys.exit(cmd_zip(a) if a.cmd == "zip" else cmd_device(a))


if __name__ == "__main__":
    main()
