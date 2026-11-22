# /corrida — variações de andar e correr (por skin, salvas)

Instalação: descompacte direto na raiz do projeto (já vem sem pasta). CMake usa GLOB_RECURSE: os arquivos novos entram sozinhos.

## Comandos (no chat do jogo)
- `/corrida lista` mostra os estilos: civil, cj, musculoso, gangue, gangue2, corredor, gordo, idoso, arrastado, bebado, swat, mulher, sexy, apressada, corredora (e `nativo` = o do jogo).
- `/corrida <estilo>` escolhe para a skin atual (salvo).
- `/corrida todas <estilo>` escolhe para todas as skins.
- `/corrida padrao` apaga a sua escolha só desta skin. `/corrida reset` apaga todas.
- `/corrida defskin <skin> <estilo|nativo>` define o PADRÃO da skin (para quem não escolheu). É assim que VOCÊ escolhe o padrão por skin.
- Tudo fica em `SAMP/corrida.ini` ([padrao] = padrão por skin, [jogador] = escolhas do jogador). Pode editar à mão.

Prioridade: escolha do jogador (skin) > escolha do jogador (todas) > padrão da skin > nativo.

## Como funciona
Não troca o ped.ifp. Só escolhe QUAL grupo de animação já existente o ped usa (CPed, offset 0x630, o mesmo que `/reanim ativas` mostra como "grupoAnd"). O arms-abertos do vídeo de 22/11 é o grupo "musculoso"; para sair dele use `/corrida civil`, `/corrida gangue` etc. e veja qual combina com o vídeo de 21/11.

## Honestidade
- NÃO compilei com NDK e NÃO testei no jogo.
- Só mexe quando o grupo atual do ped é um grupo de caminhada conhecido; senão não faz nada. Log: `[CORRIDA]` no samp_log.
- Se a troca não aparecer na hora, pare e volte a andar (o jogo reaplica a locomoção ao iniciar o passo). Se nada mudar, me mande o log `[CORRIDA]`.
- Os estilos usam os ids de grupo do enum do projeto (vanilla). Se o seu animgrp.dat reordenou grupos, os nomes podem não bater.
- Padrão por skin: ainda não defini nenhum (você disse que escolhe). Teste com `/corrida lista` e depois `/corrida defskin <skin> <estilo>`.
- Salva só neste aparelho (SAMP/corrida.ini), não na conta do servidor.
- Arquivos: runstyle.h/.cpp (novos), game/pad.cpp (1 linha + include), gui/samp_widgets/chat.cpp (comando /corrida) — a partir das versões do seu repositório de 22/11 12:34.
