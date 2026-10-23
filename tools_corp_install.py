#!/usr/bin/env python3
"""Instala o pacote 271.zip (texturas da policia) no formato do CorpTex.

uso: python3 tools_corp_install.py <pasta 271 descompactada> <saida: SAMP/skins/pack>

Gera:
  <saida>/corp/<corp>/fantasy_m_cop_<peca>.png   uma pasta por corporacao (e variantes <corp>_<tag>); /corp <nome>
  <saida>/271/<tipo>/<pasta>/<n>.png             pecas base (tshirts hat boot pants vest belt); /cop <peca> base [n]
  <saida>/corp_extra/<corp>/*.png                arquivos cujo sufixo NAO existe no male_police.dff (nao sao carregados)
  <saida>/corp_instalado.txt                     relatorio

Regras (o nome do arquivo termina com o material):  blusa_gnr_torso4.png -> fantasy_m_cop_torso4.png
  * sufixo valido: torso1-4 hat1-3 pants1 short1 boot1-2 belt1 vest1-3 (os 15 materiais do male_police.dff)
  * arquivo com etiqueta (colete_gnr_negociador_vest3) vira variante <corp>_<etiqueta>: a corp padrao com esta peca no lugar
    (vest/hat/boot/pants trocam a categoria inteira; torso troca so o mesmo material)
  * sufixos sem peca no DFF (torso5, torso55, hat8, hat55, hat3333, pants2, pants4) ficam em corp_extra/ e no relatorio
"""
import os, re, shutil, sys
from PIL import Image

VALID = ['belt1','boot1','boot2','hat1','hat2','hat3','pants1','short1','torso1','torso2','torso3','torso4','vest1','vest2','vest3']
KINDS = {'blusa','tshirt','calcas','chapeu','boina','botas','colete'}
CAT = lambda m: re.sub(r'\d+$', '', m)
REPLACE_CATEGORY = {'vest','hat','boot','pants'}   # so uma por vez
BASE_DEFAULT = [('boot','2'),('hat','3'),('pants','1'),('tshirts','3'),('tshirts','4'),('vest','3')]   # = kDefaultVisible do CorpTex (+ torso3)

def parse(corp, stem):
    low = stem.lower()
    if low.startswith('fantasy_m_cop_'):
        m = low[len('fantasy_m_cop_'):]
        return (m if m in VALID else None), '', low
    toks = low.split('_'); mat = toks[-1]
    tag = []
    for t in toks[:-1]:
        b = re.sub(r'\d+$', '', t); digits = t[len(b):]
        if b in KINDS: tag.append(digits) if digits else None
        elif corp.startswith(b[:4]) and len(b) >= 3: tag.append(digits) if digits else None
        else: tag.append(t)
    return (mat if mat in VALID else None), '_'.join(x for x in tag if x), low

def save_png(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    im = Image.open(src)
    if im.format == 'PNG': shutil.copyfile(src, dst)
    else: im.convert('RGBA').save(dst, 'PNG')

def main():
    if len(sys.argv) != 3: print(__doc__); sys.exit(1)
    src, out = sys.argv[1], sys.argv[2]
    rel = []
    corpdir = os.path.join(src, 'corp')
    for corp in sorted(d for d in os.listdir(corpdir) if os.path.isdir(os.path.join(corpdir, d))):
        entries = []; extras = []
        for f in sorted(os.listdir(os.path.join(corpdir, corp))):
            stem, ext = os.path.splitext(f)
            if ext.lower() not in ('.png','.jpg','.jpeg'): continue
            mat, tag, low = parse(corp, stem)
            p = os.path.join(corpdir, corp, f)
            if mat is None: extras.append(f); save_png(p, os.path.join(out, 'corp_extra', corp, low + '.png')); continue
            entries.append((mat, tag, p, f))
        default = {}; variants = []
        for mat, tag, p, f in entries:
            if not tag and mat not in default: default[mat] = (p, f)
            else: variants.append((mat, tag or os.path.splitext(f)[0].lower(), p, f))
        if default:
            for mat, (p, f) in default.items(): save_png(p, os.path.join(out, 'corp', corp, f'fantasy_m_cop_{mat}.png'))
            rel.append(f'corp {corp}: ' + ', '.join(sorted(default)))
        used = set([corp])
        for mat, tag, p, f in variants:
            name = (corp + '_' + re.sub(r'[^a-z0-9]', '', tag))[:15]
            while name in used and name != corp: name = name[:14] + 'x'
            used.add(name)
            keep = {m: pf for m, pf in default.items() if (CAT(m) != CAT(mat) if CAT(mat) in REPLACE_CATEGORY else m != mat)}
            for m, (pp, ff) in keep.items(): save_png(pp, os.path.join(out, 'corp', name, f'fantasy_m_cop_{m}.png'))
            save_png(p, os.path.join(out, 'corp', name, f'fantasy_m_cop_{mat}.png'))
            rel.append(f'corp {name}: variante de {corp} ({f}) -> {mat}; resto: ' + ', '.join(sorted(keep)))
        if extras: rel.append(f'  sem peca no DFF em {corp}: ' + ', '.join(extras))
    # pecas base
    for tipo in ('tshirts','hat','boot','pants','vest','belt'):
        d = os.path.join(src, tipo)
        if not os.path.isdir(d): continue
        for pasta in sorted(os.listdir(d)):
            for f in sorted(os.listdir(os.path.join(d, pasta))):
                if f.lower().endswith('.png'): save_png(os.path.join(d, pasta, f), os.path.join(out, '271', tipo, pasta, f))
    for tipo, pasta in BASE_DEFAULT:
        mat = {'tshirts': 'torso', 'hat': 'hat', 'boot': 'boot', 'pants': 'pants', 'vest': 'vest'}[tipo] + pasta
        save_png(os.path.join(src, tipo, pasta, '1.png'), os.path.join(out, 'corp', 'base', f'fantasy_m_cop_{mat}.png'))
    rel.append('corp base: ' + ', '.join(f'{t}/{p}' for t, p in BASE_DEFAULT) + ' (pecas base; belt fica fora: o PNG e um molde de UV 2048x4096)')
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'corp_instalado.txt'), 'w', encoding='utf-8').write('\n'.join(rel) + '\n')
    print('\n'.join(rel))

main()
