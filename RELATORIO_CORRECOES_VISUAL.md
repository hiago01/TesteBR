# Correções – roupa repetida, arquivo Visual de jogador novo, /atualizarvisual, saída do /provador

## Causa da "mesma roupa em todos" (cliente)
O snapshot do servidor (RPC 232) só lista os slots COM registro. Os slots não citados eram "NÃO TOCAR (padrão do molde)", mas o molde
do modelo 7 é compartilhado e guarda a roupa do jogador LOCAL (e a prévia do /provador). Todo ped remoto sem entrada no CorpTex, ou
com slots faltando, era desenhado com a SUA roupa.
Correção (CorpTex.cpp + LookSlots.h):
 - `PlanWithDefaults`: no modelo 7, slot não citado = oculto (padrão do servidor), nunca o que está no molde.
 - `SynthesizeDefaults`: jogador remoto modelo 7 sem nenhum visual recebido recebe o visual PADRÃO (= Look_ApplyDefault do servidor) até chegar o RPC 231/232; o real substitui.
 - Efeito colateral bom: a prévia do /provador não aparece mais nos outros peds do modelo 7 na sua tela.
## Servidor (maximo_look.inc / maximorp.pwn)
 - Jogador novo: o arquivo scriptfiles/MaximoRP/Visual/<nome>.ini agora é criado NA CONEXÃO com o visual padrão (antes só na 1ª mudança).
 - /atualizarvisual (atalho /attvisual, e `/lookx refresh`): recarrega o arquivo do jogador, reenvia o visual dele a ele e a quem o vê, e reenvia ao jogador o visual de todos que ele vê. Cooldown 3 s. Sem arquivo: cria. Arquivo ilegível: não altera a memória.
## /provador (provador.cpp/.h, chat.cpp)
 - VOLTAR, botão Voltar do Android, /provador e /provador fechar: com prévia não salva abrem "Sair sem salvar?" com SALVAR E SAIR / SAIR SEM SALVAR / CONTINUAR. SALVAR E SAIR só fecha depois que o servidor confirma; se recusar, continua aberto. Esperando o servidor: não fecha.
## Testes
 EXECUTADO: sh testes/run_host_provador.sh -> testes de host do provador + modelo + NOVO host_test_lookdefaults (padrão/PlanWithDefaults); provador.cpp compila (sintaxe, stubs); maximorp.pwn compila com pawncc 3.10.10 (Linux) sem warnings, nos dois gamemodes.
 NÃO EXECUTADO: APK, aparelho, 2+ jogadores, servidor open.mp rodando, CorpTex.cpp real (não compila no host), criação real do .ini, /atualizarvisual, diálogo de saída na tela.
## Limites
 - SynthesizeDefaults assume que o padrão do servidor (torso/perna/pé/rosto/sobrancelha/cabelo/íntima variante 1) é o visual de quem não mandou nada.
 - Jogador que o servidor devolve com TUDO desmarcado passa a ser mostrado com o padrão (antes ficava invisível).
 - Recompile o gamemode (maximorp.amx) – o .amx antigo não tem o comando.
 - DOF2 pode manter cache do arquivo; se editar o .ini à mão com o servidor ligado, /atualizarvisual pode reler o cache.
