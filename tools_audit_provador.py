#!/usr/bin/env python3
"""Auditoria do mapa de slots do /provador (cliente) contra o servidor e o pack.

  python3 tools_audit_provador.py <SkinSlots.h> <maximo_look.inc> [<pasta SAMP/skins>] [--gen-variantes saida.ini]

Confere (sem alterar nenhum indice):
  1. servidor x cliente: nomes por id, grupo exclusivo, evidencia (o .inc diz GERADO a partir de SkinSlots.h)
  2. pack: variantes existentes por pasta (maximo, lacunas, JPEG que o cliente nao le)
  3. materials.csv (gerado por /skin7 mats): slots sem material no DFF, materiais sem slot, material invisivel por padrao
  4. --gen-variantes: escreve o variantes.ini do servidor (slot=maior variante existente)
Codigo de saida 0 = tabelas consistentes; 1 = divergencia servidor x cliente.
"""
import os, re, sys

def parse_client(path):
    txt = open(path, encoding='utf-8').read()
    body = txt[txt.index('kSlots[] = {'):txt.index('kCount')]
    slots = []
    # { "name", "tex" "tex"..., "folder", ev },  (textos podem ser concatenados em varias linhas)
    for m in re.finditer(r'\{\s*"([a-z_0-9]+)"\s*,\s*((?:"[^"]*"\s*)+),\s*"([^"]*)"\s*,\s*(\d)\s*\}', body):
        tex = ''.join(re.findall(r'"([^"]*)"', m.group(2)))
        slots.append(dict(name=m.group(1), tex=tex.split(','), folder=m.group(3), ev=int(m.group(4))))
    gm = re.search(r'kExclusiveGroups\[\] = \{(.*?)\};', txt, re.S)
    groups = [g.split(',') for g in re.findall(r'"([^"]*)"', gm.group(1))]
    return slots, groups

def parse_server(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    nm = re.search(r'gLookSlotName\[LOOK_ID_MAX \+ 1\]\[LOOK_NAME_LEN\] = \{(.*?)\};', txt, re.S)
    names = re.findall(r'"([^"]*)"', nm.group(1))
    grp = [int(x) for x in re.search(r'gLookSlotGroup\[LOOK_ID_MAX \+ 1\] = \{([^}]*)\}', txt).group(1).split(',')]
    ev = [int(x) for x in re.search(r'gLookSlotEv\[LOOK_ID_MAX \+ 1\] = \{([^}]*)\}', txt).group(1).split(',')]
    return names, grp, ev

def main():
    args = [a for a in sys.argv[1:]]
    gen = None
    if '--gen-variantes' in args:
        i = args.index('--gen-variantes'); gen = args[i + 1]; del args[i:i + 2]
    if len(args) < 2:
        print(__doc__); return 2
    slots, groups = parse_client(args[0])
    names, grp, ev = parse_server(args[1])
    bad = 0
    print(f'cliente: {len(slots)} slots, {len(groups)} grupos exclusivos | servidor: {len([n for n in names if n])} slots')
    if len(names) != len(slots) + 1 or len(grp) != len(slots) + 1 or len(ev) != len(slots) + 1:
        print(f'ERRO: tamanhos diferentes (nomes={len(names)} grupos={len(grp)} ev={len(ev)} esperado={len(slots)+1})'); bad += 1
    gid = {}
    for gi, g in enumerate(groups):
        for n in g: gid[n] = gi
    # o servidor numera os grupos na ordem em que aparecem; compara por PARTICAO (quem divide grupo com quem)
    for i, s in enumerate(slots, 1):
        if i >= len(names): break
        if names[i] != s['name']:
            print(f'ERRO id {i}: cliente={s["name"]} servidor={names[i]}'); bad += 1
        if ev[i] != s['ev']:
            print(f'ERRO id {i} ({s["name"]}): ev cliente={s["ev"]} servidor={ev[i]}'); bad += 1
    for i, a in enumerate(slots, 1):
        for j, b in enumerate(slots, 1):
            if j <= i or j >= len(grp) or i >= len(grp): continue
            cli = (a['name'] in gid and b['name'] in gid and gid[a['name']] == gid[b['name']])
            srv = (grp[i] >= 0 and grp[i] == grp[j])
            if cli != srv:
                print(f'ERRO grupo: {a["name"]}({i}) x {b["name"]}({j}) cliente={cli} servidor={srv}'); bad += 1
    for i, s in enumerate(slots, 1):
        if i < len(grp) and (s['name'] in gid) != (grp[i] >= 0):
            print(f'ERRO grupo: {s["name"]} em grupo no cliente={s["name"] in gid} servidor={grp[i] >= 0}'); bad += 1
    print('servidor x cliente:', 'CONSISTENTE' if not bad else f'{bad} divergencia(s)')

    if len(args) >= 3:
        skins = args[2]
        pack = os.path.join(skins, 'pack')
        def variants(folder):
            d = os.path.join(pack, folder)
            if not os.path.isdir(d): return None, 0
            n = sorted(int(os.path.splitext(f)[0]) for f in os.listdir(d)
                       if f.lower().endswith('.png') and os.path.splitext(f)[0].isdigit())
            j = sum(1 for f in os.listdir(d) if f.lower().endswith(('.jpg', '.jpeg')))
            return n, j
        print('\nPACK por slot (pasta, variantes PNG, maior, lacunas, JPEG):')
        used = set()
        out = []
        for s in slots:
            n, j = variants(s['folder']); used.add(s['folder'])
            if n is None:
                print(f'  {s["name"]:11s} {s["folder"]:28s} PASTA AUSENTE'); continue
            gaps = [x for x in range(1, n[-1] + 1) if x not in n] if n else []
            note = ''
            if gaps: note += f' lacunas={gaps}'
            if j: note += f' jpeg={j}'
            print(f'  {s["name"]:11s} {s["folder"]:28s} png={len(n):3d} max={n[-1] if n else 0:3d}{note}')
            mx = n[-1] if n else 0
            if s['folder'] in ('torso/1', 'leg/1', 'feet/1'): mx = min(mx, 6)     # tom 1..6 (a variante 7 e transparente, nao e tom)
            if s['folder'].startswith('heads/'): mx = 612                          # rosto = tom*100 + rosto (ate 6*100+12); o servidor valida tom/rosto sozinho
            out.append((s['name'], mx))
        # pastas do pack sem slot
        orphans = []
        for sub in sorted(os.listdir(pack)):
            p = os.path.join(pack, sub)
            if not os.path.isdir(p): continue
            for d, ds, fs in os.walk(p):
                if any(f.lower().endswith('.png') and os.path.splitext(f)[0].isdigit() for f in fs):
                    rel = os.path.relpath(d, pack).replace('\\', '/')
                    if rel not in used: orphans.append(rel)
        print('  pastas com PNG numerado SEM slot (nao aparecem no /provador):', ', '.join(orphans) or '-')
        if gen:
            with open(gen, 'w', encoding='utf-8') as f:
                f.write('; gerado por tools_audit_provador.py: slot=maior variante existente em pack/<pasta>/ (pele: 6 tons; rosto: 612 = tom 6 rosto 12, validado por tom/rosto no servidor)\n')
                for n, m in out: f.write(f'{n}={m}\n')
            print('escrito', gen)

        csv = os.path.join(skins, 'materials.csv')
        if os.path.exists(csv):
            rows = [l.rstrip('\n').split(',') for l in open(csv, encoding='utf-8')][1:]
            mats = {r[1].lower(): r for r in rows}
            print(f'\nMATERIAIS (materials.csv): {len(rows)}')
            covered = set()
            for s in slots:
                miss = [t for t in s['tex'] if t.lower() not in mats]
                for t in s['tex']: covered.add(t.lower())
                if miss: print(f'  slot {s["name"]}: material(is) AUSENTE(S) no DFF: {miss}')
            orph = [r[1] for r in rows if r[1].lower() not in covered]
            print('  materiais SEM slot (nao controlados pelo /provador):', ', '.join(orph) or '-')
            vis0 = [r[1] for r in rows if r[6] == '0']
            print('  materiais com alfa 0 no DFF:', ', '.join(vis0) or '-')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
