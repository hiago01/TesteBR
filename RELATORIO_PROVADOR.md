# Relatório – auditoria e correção do /provador (TesteBR-free)

## O que foi e o que NÃO foi verificado
- EXECUTADO (host x86, stubs de RenderWare/rede): `sh testes/run_host_provador.sh` → "todos os testes de host passaram" + "MODELO: todos os testes puros passaram". Compila o CustomSkin.cpp real com RW/rede falsos.
- EXECUTADO: `tools_audit_provador.py` (tabelas servidor x cliente x pack); verificação de sintaxe de provador.cpp contra stubs.
- NÃO EXECUTADO: APK (sem NDK no ambiente), teste em aparelho, teste com 2+ jogadores reais, reconexão real, o código do branch real que depende de PlayerPedGta/CorpTex nos jogos (não compilado). Nenhum desses está declarado como aprovado.

## Achados e correções (pontos 1–8)
1. Preparo automático: ao abrir o /provador, EnsurePrepared instala o molde só se `!InstalledOurs()`; sem reinstalar repetido; retry 3 s, desiste após 3 falhas; marcador `.applying` contra crash; botão PREPARAR manual. `kAutoApply` continua false.
2. PublishUiState: estados separados (indisponível/padrão/equipado/desligado/coberto) + marca de prévia; "material visível" ≠ "equipado" (ProvadorModel.h).
3. prev/commit/cancel: prévia só local com backup (refs de textura); commit só vira definitivo após eco do servidor (RPC 232) igual ao pedido; divergência/sem eco/queda → RestoreBackup exato + /lookx sync; cancel recusado enquanto aguarda servidor.
4. RPC: nenhum protocolo novo; usa /lookx e RPC 231/232 existentes. Servidor é a autoridade (exclusividade, snapshot ao dono e ao stream). Servidor/gamemode NÃO alterado.
5. SkinSlots.h x servidor: tabelas consistentes (auditor). Índices não alterados. Pendências sem prova: slots com ev=0 (pants/short, brow/beard compartilham pastas) deixados como estão.
6. Categorias mantidas; estados e variantes reais exibidos.
7. Threads: UI só enfileira (fila 64, retorno de fila cheia); RenderWare só em Process na game thread; refs de textura balanceadas (graveyard 5 s); fechar durante operação trata via Await/Offline.
8. Testes: ver seção "EXECUTADO".

## Limites conhecidos
- Molde é compartilhado entre peds do modelo 7: a prévia local aparece também em outros peds modelo 7 neste aparelho.
- Reinstalação do molde só afeta peds criados depois; depende de WearLocal(7) (suposição baseada no /skin7 wear, não validada em aparelho).
- Aceitação parcial do servidor → rollback ao estado base e molde marcado como defasado (próximo preparo reinstala).
- Sem variantes.ini o servidor aceita variantes 0..1023 inexistentes (ex.: tshirts/1 lacunas 27–33). Fornecido `variantes.ini` (apenas o máximo por pasta, não detecta lacunas) em server_extra/.
- `.applying`: fechar o jogo <15 s após preparar pode causar pulo falso-positivo.
- Pré-existente, não tocado: GUI lê GetLocalPlayerName da thread de desenho.
- ClothesSync.cpp e netrpc.cpp auditados, sem alteração.

## Arquivos
Novos: ProvadorModel.h, tools_audit_provador.py, testes/host_test_provador.cpp, testes/host_test_provador_model.cpp, testes/run_host_provador.sh, testes/stub/**, RELATORIO_PROVADOR.md, server_extra/.../variantes.ini
Modificados: CustomSkin.h/.cpp, CorpTex.h/.cpp, provador.cpp (app/src/main/cpp/samp/game e gui/samp_widgets)
