#!/usr/bin/env python3
"""
Gera SAMP/ipl/<nome_do_ipl>.ipl  (secao 'remove', so por ID) para tirar instancias de um IPL de streaming do IMG.
O LOD de cada uma NAO fica nesta tabela (e' uma linha do IPL principal, ex.: countrye.ipl): o script so informa o indice.

Uso:  python3 tools_ipl_remove.py ORIGINAL_COMPLETO.ipl --id 12938 --id 12940 -o countrye_stream4.ipl
      (ou --name sw_apartments02 --name sw_apartments07)

ORIGINAL_COMPLETO.ipl = IPL decodificado COM os LODs, na ordem do binario (pasta "Converted main" do topico).
NAO use a versao "sem LODs"/filtrada: nela o numero da linha nao e' o indice do binario.

O script RECUSA gerar o arquivo se o LOD nao fizer sentido (indice fora da tabela, ou LOD a mais de --maxdist unidades
da instancia). Se o LOD for compartilhado com outra instancia, remove so a casa e avisa (use --force-lod para remover).
"""
import sys, re, math, argparse

def parse(path):
    rows, sec = [], None
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.split('#')[0].strip()
        if not line: continue
        t = [x for x in re.split(r'[,\s]+', line) if x]
        k = t[0].lower()
        if len(t) == 1 and k == 'end': sec = None; continue
        if len(t) == 1 and k.isalpha(): sec = k; continue
        if sec == 'inst':
            if len(t) == 10: t.append('-1')
            rows.append(t[:11])
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('original'); ap.add_argument('-o', '--out')
    ap.add_argument('--id', type=int, action='append', default=[]); ap.add_argument('--name', action='append', default=[])
    a = ap.parse_args()
    rows = parse(a.original); N = len(rows)
    P = lambda r: (float(r[3]), float(r[4]), float(r[5]))
    targets = [i for i, r in enumerate(rows) if int(r[0]) in a.id or r[1].lower() in [n.lower() for n in a.name]]
    if not targets: sys.exit('nenhuma instancia com esse id/nome')
    rem = {i: rows[i][0] for i in targets}
    for i in targets:   # o LOD NAO esta nesta tabela: e' o indice da linha 'inst' no IPL PRINCIPAL (ex.: countrye.ipl)
        print(f"  idx {i}: id {rows[i][0]} {rows[i][1]} pos {rows[i][3]},{rows[i][4]} | lod = linha {rows[i][10]} do IPL principal (nao removido aqui)", file=sys.stderr)
    out = [f"# gerado por tools_ipl_remove.py | original={N} instancias | removendo={len(rem)}", 'remove']
    out += [f"{rows[i][0]}    # {rows[i][1]}" for i in sorted(rem)]; out.append('end')   # forma nova: so o ID (remove todas as instancias desse id neste IPL)
    txt = '\n'.join(out) + '\n'
    open(a.out, 'w').write(txt) if a.out else sys.stdout.write(txt)
    print(f"ok: {len(rem)} remocoes de {N}", file=sys.stderr)
main()
