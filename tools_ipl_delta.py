#!/usr/bin/env python3
"""
Gera o arquivo de ajuste  texdb/ipl/<nome_do_ipl>.ipl  a partir de DOIS IPLs em texto:
  1) o IPL decodificado ORIGINAL (do gta3.img, ex.: os do topico "SA all IPL files in readable format")
  2) o mesmo IPL depois de editado por voce

Uso:   python3 tools_ipl_delta.py original.ipl editado.ipl  [-o countrye_stream0.ipl]
O nome do arquivo de saida deve ser o nome do IPL dentro do IMG (ex.: countrye_stream0.ipl, int_la.ipl...).

Regras (iguais as de CustomIplParse.h / LEIA-ME_IPL.md):
 - O indice de cada instancia e' a ordem das linhas da secao 'inst' do IPL ORIGINAL (a mesma ordem do binario).
 - Linha alterada                  -> 'replace' (confere o ID original)
 - Linha apagada                   -> 'remove'  (confere o ID original)
 - Linha nova                      -> 'inst'    (entra no fim da tabela)
 - O campo lod das linhas do arquivo editado e' lido na numeracao do IPL ORIGINAL (nao renumere os lods).
   (lod que aponte para linha nova nao e' possivel -> vira -1 com aviso)
"""
import sys, re, difflib, argparse

def parse_inst(path):
    rows, sec = [], None
    for ln, raw in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        line = raw.split('#')[0].strip()
        if not line: continue
        toks = [t for t in re.split(r'[,\s]+', line) if t]
        k = toks[0].lower()
        if len(toks) == 1 and k in ('inst','cars','zone','cull','pick','path','grge','enex','occl','mult','tcyc','auzo','jump'):
            sec = k; continue
        if len(toks) == 1 and k == 'end': sec = None; continue
        if sec == 'inst':
            if len(toks) < 10:
                sys.exit(f"{path}:{ln}: linha inst com {len(toks)} campos (esperado 11)")
            if len(toks) == 10: toks.append('-1')
            rows.append(toks[:11])
    return rows

def fmt(t): return ', '.join(t)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('original'); ap.add_argument('editado'); ap.add_argument('-o', '--out')
    a = ap.parse_args()
    o, e = parse_inst(a.original), parse_inst(a.editado)
    sm = difflib.SequenceMatcher(None, [fmt(x) for x in o], [fmt(x) for x in e], autojunk=False)
    rep, rem, add, warn = [], [], [], []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal': continue
        n = min(i2 - i1, j2 - j1) if tag == 'replace' else 0
        for k in range(n): rep.append((i1 + k, o[i1 + k][0], e[j1 + k]))
        for i in range(i1 + n, i2): rem.append((i, o[i][0]))
        for j in range(j1 + n, j2): add.append(e[j])
    out = [f"# gerado por tools_ipl_delta.py | original={len(o)} instancias | trocadas={len(rep)} removidas={len(rem)} novas={len(add)}"]
    if rep:
        out.append("replace");  out += [f"{i}, {oid}, {fmt(t)}" for i, oid, t in rep]; out.append("end")
    if rem:
        out.append("remove");   out += [f"{i}, {oid}" for i, oid in rem]; out.append("end")
    if add:
        out.append("inst");     out += [fmt(t) for t in add]; out.append("end")
    txt = '\n'.join(out) + '\n'
    if a.out: open(a.out, 'w', encoding='utf-8').write(txt)
    else: sys.stdout.write(txt)
    for w in warn: print("AVISO:", w, file=sys.stderr)
    print(f"original={len(o)} editado={len(e)} -> replace={len(rep)} remove={len(rem)} inst={len(add)}", file=sys.stderr)

if __name__ == '__main__': main()
