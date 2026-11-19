# Remover os cabelos 5, 6, 7 e 8 — volta ao estado dos arquivos de 18/11 21h (Source-21hrs / servidor-21hrs / texdb-skins)
Mande os 3 zips (caminhos a partir da raiz) e recompile cliente e servidor (.amx).
- `Remover-Cabelos-CLIENTE.zip`: 13 arquivos restaurados IGUAIS ao Source-21hrs (CorpTex, CustomSkin, LookSlots, ProvadorModel, SkinSlots, SkinTexDb, SkinTexDbCore, hooks, chat, provador, scriptrpc)
  + 5 arquivos do cabelo de tecido neutralizados (vazios; o CMake compila por GLOB, entao nao podem ficar com o codigo antigo). Ver APAGAR_CABELOS.txt.
- `Remover-Cabelos-SERVIDOR.zip`: maximo_look.inc e maximo_look_slots.inc do servidor-21hrs (61 slots). Recompile o .amx. Entradas salvas dos slots 62-65 nos arquivos dos jogadores deixam de ser lidas.
- `Remover-Cabelos-ASSETS.zip`: skin.dff ORIGINAL (89 materiais, 5.236.553 bytes) + banco texdb/skins original (1238 texturas) — troque os 6 arquivos do banco juntos.
ATENCAO: esses 13 arquivos voltam inteiros ao Source-21hrs; se voce alterou algum deles DEPOIS do 21hrs (fora dos cabelos), essa alteracao some. A carga protegida do banco (CheckBankFiles) tambem sai (estava no Backup).
O backup do que foi feito esta em Backup-Cabelos-5a8-CODIGO.zip e Backup-Cabelos-5a8-ASSETS.zip.
