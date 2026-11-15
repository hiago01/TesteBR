# /carregar e /soltar (Máximo RP)

**Visual (vídeo de referência):** o carregado fica de cabeça para baixo sobre o ombro do carregador; ao soltar, aparece ao lado.

## Como funciona
- **Servidor** (`qawno/include/maximo_carregar.inc`): é a autoridade. Valida (conectados, vivos, a pé, mesmo interior/mundo, até 2,5 m, ninguém já envolvido em outro transporte), guarda o vínculo e encerra sozinho em: desconexão, morte, respawn, veículo, troca de interior/mundo, teleporte (distância > 6 m após 4 s). O carregado perde o controle (`TogglePlayerControllable`). A rotina de encerramento (`Carry_Stop`) é idempotente e usa uma geração para ignorar timers antigos.
- **Cliente** (`game/PlayerCarry.cpp`): recebe o RPC 233 e, **em cada cliente**, posiciona o carregado relativo à matriz do carregador, depois da sincronização normal (fim de `CPlayerPool::Process`). Nada é enviado por frame. O carregado (no próprio celular) também é movido, então o servidor o vê junto do carregador. Sem a sincronização do RemotePlayer ser desligada.
- **Stream-in:** o servidor reenvia o estado (op 4) a quem passa a ver um dos dois; o cliente guarda o vínculo mesmo com o ped fora do stream e aplica quando os dois peds existem.

## RPC 233 (servidor -> cliente), canal de ordenação 3, RELIABLE_ORDERED
`uint8 versão(1) | uint8 op | uint16 carregador | uint16 carregado | uint16 geração | float x | float y | float z | float ângulo`
op 1 iniciar · 2 confirmar · 3 encerrar (x,y,z,ângulo = ponto seguro ao lado do carregador) · 4 reaplicar (stream-in).
Ids livres conferidos no cliente e no servidor (em uso: 200–206, 230–232, 240, 241, 245–247, 250).
O cliente rejeita versão/op/ids inválidos, pacote incompleto, float fora de faixa e geração antiga.

## Animações (só nomes que já existem no GTA SA)
- Carregador: preparação `CARRY/liftup`, depois `CARRY/crry_prtial` (a mesma que o projeto já usa).
- Carregado: `PED/KO_SHOT_STOM` (corpo caído, último quadro travado), com o corpo girado 180°.
- **Limitação:** o GTA SA não tem a animação de "ombro" do vídeo (era MTA com IFP próprio). Para ficar idêntico seria preciso um IFP (carregador + carregado) via `CustomAnim`; isso não foi criado.

## Ajuste fino (não testado no jogo)
`PlayerCarryCore.h`: `kOffX` (lado), `kOffY` (trás), `kOffZ` (altura). Servidor: `CARRY_*` no topo do `.inc` (distância, tempo de preparação, `CARRY_CARRIED_CAN_END`).

## Integração (já feita nos arquivos deste zip)
Servidor: `#include <maximo_carregar.inc>` (depois do izcmd) + `Carry_OnPlayerConnect/Disconnect/Death/Spawn/StateChange/StreamIn` no gamemode (inclui um `OnPlayerStateChange` novo).
Cliente: `PlayerCarry::Register/ClearAll` em `RegisterScriptRPCs`, `Unregister` em `UnregisterScriptRPCs`, `PlayerCarry::Process()` no fim de `CPlayerPool::Process`, `PlayerCarry::OnPlayerLeft` em `CPlayerPool::Delete`. O CMake usa `GLOB_RECURSE`: arquivos novos entram sozinhos (rode "Sync/Refresh Linked C++ Projects" ou limpe o `.cxx`). O RPC 230 de animação já é registrado sempre (`Diag::On("anim")` ignora o arquivo).

## Testes
`g++ -std=c++17 -I app/src/main/cpp/samp/game tools/carry_test.cpp -o /tmp/carry_test && /tmp/carry_test` (33 verificações: validação do pacote, geração/ordem/idempotência, wrap, stream-in, matemática da posição).
Roteiro em jogo (precisa de 2–4 aparelhos): carregar/soltar, andar e girar, soltar pelo carregado, 3º jogador vendo, 4º entrando depois, desconectar cada um, morrer, entrar em veículo, carregar quem já carrega/é carregado, respawn/reconexão, FPS com vários jogadores.

## Atualizacao: boneco (Actor) + IFP novo
- O boneco de teste agora e um **Actor** do servidor (o NPC do open.mp nao aparecia). `/npccarregar` (admin) cria na sua frente, `/carregar npc` ou `/carregar <id mostrado>` carrega, `/soltar` solta, `/npcremover` apaga. Id de protocolo do actor = 1004 + actorid (so pode ser carregado).
- `SAMP/anims/carregar.ifp` (copie para a pasta SAMP/anims do celular, junto do poses.ifp): bloco `carregar`, anims `carregador` e `carregado` (so ossos dos bracos, overlay CA_PARTIAL|CA_HOLD_LAST por cima das anims nativas CARRY/PED). Carrega sozinho na 1a vez que um transporte comeca; se faltar o arquivo o log mostra `[Carry] ... NAO carregou` e segue com as nativas.
- Gerador: `python3 tools/make_carregar_ifp.py poses.ifp carregar.ifp`. Os quaternions dos bracos partem das poses do seu poses.ifp; AJUSTE os graus em PLAN/HEAD_DELTA e regere. NAO foi visto em jogo: a pose pode precisar de ajuste.
- Teste rapido sem servidor: `/ifp carregar carregar.ifp carregar` e `/ifp tocar carregador hold parcial`.

## Correcao (video de teste 17:40): carregado voando/girando
Causa provavel: o posicionamento era feito em CPlayerPool::Process (tick de rede), ANTES do ProcessControl/PreRender do jogo, que reconstroi a matriz do ped a partir do heading (perde o giro de 180 graus) e deixa o carregado defasado do carregador. Agora o posicionamento que vale roda em `CWorld::ProcessPedsAfterPreRender` (`PlayerCarry::ProcessAfterPreRender`), depois do PreRender e antes dos attaches, e recalcula os ossos (`UpdateRpHAnim`).
Postura conforme o video MTA de referencia (estilo bombeiro): de cabeca para baixo no ombro, pes inclinados para a frente (`kTiltDeg` 25), offsets `kOffX/Y/Z` = 0.15/-0.05/0.50 em `PlayerCarryCore.h`. Ajuste esses 4 numeros em jogo se precisar.
Arquivos alterados: PlayerCarry.cpp/.h, PlayerCarryCore.h, World.cpp. NAO compilado nem testado no aparelho.

## Correcao 2 (video 17:51: carregado ainda orbitando o carregador)
O carregado agora e ANCORADO pelo osso: depois de aplicar a matriz, le a posicao real da pelve (`GetBonePosition`, osso 1) e desloca o ped para a pelve ficar no ponto do ombro. Isso cancela o deslocamento de raiz da animacao KO_SHOT_STOM e qualquer defasagem. Limite de correcao: 2,25 m (acima disso ignora).
DIAGNOSTICO: a cada ~120 frames o log tem `[CARRY-PLACE] carrier=... F=... U=... alvo=... pelve=... corr=X m`. Se o corpo continuar fora do lugar, mande essas linhas: elas dizem se o problema e a matriz do carregador (F/U estranhos), o osso (`[osso invalido]`) ou a correcao.
Se nao aparecer nenhuma linha `[CARRY-PLACE]`, o passo pos-PreRender nao esta rodando (build antigo ou vinculo nao ativo).
