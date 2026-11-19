#!/usr/bin/env python3
"""Aplica no SEU projeto (rode na RAIZ do repo) as 3 ligacoes pequenas do cabelo de tecido (hair6) que NAO vao como arquivo inteiro,
para nao sobrescrever outras alteracoes suas em hooks.cpp / chat.cpp / scriptrpc.cpp. Idempotente: pode rodar de novo.
Se uma ancora nao for encontrada, NAO altera aquele arquivo e imprime a linha para colocar na mao."""
import sys, os
B = "app/src/main/cpp/samp/"
EDITS = [
 (B+"game/hooks.cpp", [
  ('#include "Flag.h"\n', '#include "Flag.h"\n#include "HairCloth.h"\n', '#include "HairCloth.h"'),
  ("    Flag_Render();        // bandeira 3D: mastro + base + tecido (RwIm3D, com profundidade)\n",
   "    Flag_Render();        // bandeira 3D: mastro + base + tecido (RwIm3D, com profundidade)\n"
   "    HairCloth_Update();   // cabelo de tecido caido (hair6): raiz na cabeca + fisica (so quem usa o slot, ate 14 por quadro)\n"
   "    HairCloth_Render();   // hair6: calota + folha com pontas em dente (RwIm3D, cor por vertice)\n", "HairCloth_Update();"),
 ]),
 (B+"gui/samp_widgets/chat.cpp", [
  ('#include "../../game/Flag.h"\n', '#include "../../game/Flag.h"\n#include "../../game/HairCloth.h"\n', '#include "../../game/HairCloth.h"'),
  ('if (input == "/mangueira" || input.rfind("/mangueira ", 0) == 0)',
   'if (input == "/cabelo6" || input.rfind("/cabelo6 ", 0) == 0)\n        {\n'
   '            // cabelo de tecido caido (hair6): /cabelo6 teste 1-3 | cor RRGGBB RRGGBB | vento graus int | info | tune ... (ver HairCloth.h)\n'
   '            std::string args = input.size() > 7 ? input.substr(8) : std::string();\n'
   '            std::string reply = HairCloth_Command(args);\n            addDebugMessage("%s", reply.c_str());\n\n            return;\n        }\n'
   'if (input == "/mangueira" || input.rfind("/mangueira ", 0) == 0)', 'HairCloth_Command('),
 ]),
 (B+"net/scriptrpc.cpp", [
  ('#include "../game/Flag.h"\n', '#include "../game/Flag.h"\n#include "../game/HairCloth.h"\n', '#include "../game/HairCloth.h"'),
  ("\tFlag_Reset();          // nova conexao: descarta bandeiras do servidor anterior\n",
   "\tFlag_Reset();          // nova conexao: descarta bandeiras do servidor anterior\n\tHairCloth_Reset();     // idem: descarta as simulacoes do cabelo de tecido\n", "HairCloth_Reset();     // idem"),
  ("\tpRakClient->UnregisterAsRemoteProcedureCall(&RPC_ScrFlag);\n\tFlag_Reset();\n",
   "\tpRakClient->UnregisterAsRemoteProcedureCall(&RPC_ScrFlag);\n\tFlag_Reset();\n\tHairCloth_Reset();\n", "\tFlag_Reset();\n\tHairCloth_Reset();\n"),
 ]),
]
ok_all = True
for path, eds in EDITS:
    if not os.path.exists(path):
        print("AUSENTE:", path, "(rode na raiz do repo)"); ok_all = False; continue
    s = open(path, encoding="utf-8", errors="surrogateescape").read(); new = s; msgs = []
    for old, rep, marker in eds:
        if marker in new: msgs.append("ja aplicado: " + marker.strip().splitlines()[0]); continue
        if new.count(old) != 1: msgs.append("ANCORA NAO ENCONTRADA (contagem %d): %r" % (new.count(old), old[:70])); ok_all = False; continue
        new = new.replace(old, rep); msgs.append("ok: " + marker.strip().splitlines()[0])
    if new != s: open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write(new)
    print(path); [print("   ", m) for m in msgs]
print("TUDO OK" if ok_all else "ATENCAO: algo ficou sem aplicar (veja acima e coloque na mao)")
sys.exit(0 if ok_all else 1)
