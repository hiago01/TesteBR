#!/usr/bin/env python3
"""Extrai de CorpTex.cpp os trechos puros (caminhos, tabelas, comandos) para os testes de host. uso: python3 extract_regions.py <game dir>"""
import sys
s = open(sys.argv[1] + '/CorpTex.cpp', encoding='utf-8').read()
a = s.index('// opcoes validas'); b = s.index('CEntityGTA* GetEnt(PLAYERID id)')
open('corp_region.inc', 'w', encoding='utf-8').write(s[a:b])
i = s.index('void Push(Cmd::Kind k'); j = s.index('\n', s.index('void PushLook(PLAYERID id'))
open('corp_push.inc', 'w', encoding='utf-8').write(s[i:j + 1])
k = s.index('std::string Trim(std::string a)'); e = s.index('} // namespace\n\nstd::string DebugCommand')
open('corp_helpers.inc', 'w', encoding='utf-8').write(s[k:e])
c1 = s.index('    if (cmd == "cop") {'); c2 = s.index('    const std::vector<int> v = Ints(args);')
open('cop_block.inc', 'w', encoding='utf-8').write(s[c1:c2])
