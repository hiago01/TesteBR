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

## Correcao 3: animacao antiga alternando com a nova
Havia 4 fontes de animacao ao mesmo tempo: (1) o servidor aplicava CARRY/liftup, CARRY/crry_prtial e PED/KO_SHOT_STOM (RPC de ApplyAnimation); (2) o cliente reaplicava CRRY_PRTIAL/KO_SHOT_STOM a cada ~1,5 s; (3) `ProcessSpecialAction` com SPECIAL_ACTION_CARRY; (4) o overlay novo carregar.ifp.
Agora: o servidor nao aplica mais nenhuma anim no transporte; o cliente toca SO carregar.ifp (as nativas so entram se o arquivo nao carregar); os RPCs ApplyAnimation (jogador e actor) de quem esta em transporte sao ignorados e logados (`[CARRY] ApplyAnimation ... ignorada`); ao soltar, o overlay e parado (`CustomAnim::StopAll`).
Arquivos: PlayerCarry.cpp/.h, scriptrpc.cpp (cliente) e maximo_carregar.inc (servidor).

## Correcao 4: corpo dobrado (carregado reto demais) + actor
- O Actor funcionou no teste (ID 1004) e fica preso ao carregador. Jogador real e Actor usam o MESMO codigo: o carregado de qualquer tipo recebe o mesmo overlay `carregado` e a mesma posicao.
- `carregar.ifp` agora tem o `carregado` com Spine+Spine1 dobrados (BEND=40 graus cada, eixo Y) alem dos bracos soltos. Bases: a coluna do ped fica ~identidade em pe (confirmado no seu scooter.ifp); os eixos locais dos ossos eu NAO consegui verificar sem ver o jogo.
- 6 anims de TESTE so de coluna: `dobra_xp dobra_xn dobra_yp dobra_yn dobra_zp dobra_zn`. No jogo: `/ifp carregar carregar.ifp carregar` e depois `/ifp tocar dobra_yp hold parcial` (idem os outros). Veja qual dobra a BARRIGA para a frente e me diga (ou mude `spine(1, SIGN)` em tools/make_carregar_ifp.py para o eixo/sinal certo e regere).
- Os valores BEND e SIGN ficam no topo do gerador.

## Correcao 5 (prints 18:10): carregado funcionando, mas o corpo dobrava de LADO
Nos prints o carregado ja fica preso, de cabeca para baixo, com a coluna dobrada, mas o tronco saia para o lado do carregador e as pernas cobriam o rosto dele.
- Dobra: o eixo Y da coluna dobra de lado (confirmado pelos prints), entao o eixo de dobrar para frente/tras e o Z. O `carregado` agora usa `spine(2, SIGN)`. Se o tronco for para o lado errado (para a frente do carregador em vez de cair nas costas), troque SIGN para -1 em tools/make_carregar_ifp.py e regere (ou teste `dobra_zp` / `dobra_zn` com `/ifp tocar`).
- Posicao: tilt 25 -> 10 graus, offsets 0.22 / -0.10 / 0.50 (kOffX/Y/Z): as pernas sobem ao lado da cabeca do carregador, nao na frente do rosto.
Arquivos: PlayerCarryCore.h, make_carregar_ifp.py, SAMP/anims/carregar.ifp.

## Painel visual de ajuste: /carregarhud
Digite `/carregarhud` no chat (cliente, nao vai ao servidor). Abre um painel ImGui com botoes grandes (- / slider / +):
- Lado (X), Frente/tras (Y), Altura (Z) do carregado em relacao ao carregador; Inclinacao (pes para a frente); Giro para o lado (roll);
- Dobra do corpo (< >): escolhe a variante `carregado_m60 ... p60` do carregar.ifp (coluna -60 a +60 graus por osso).
Tudo vale em tempo real no transporte em andamento. SALVAR grava `SAMP/carry_tuning.ini` (carrega sozinho depois) e escreve `[CARRY-TUNE] offX=... tilt=... bend=...` no samp.log: mande essa linha e eu fixo como padrao. PADRAO volta aos valores de fabrica; "Mover painel" troca de lado; FECHAR (ou /carregarhud de novo).
Arquivos novos: game/CarryTuner.h/.cpp (CMake pega sozinho). Editados: PlayerCarryCore.h (Tune()), PlayerCarry.cpp/.h, gui/imguiwrapper.cpp (Draw antes do EndFrame), gui/samp_widgets/chat.cpp (comando). carregar.ifp REGERADO (tem as variantes): copie de novo para SAMP/anims.
Aviso: arrastar o slider tambem pode mexer a camera do jogo (o toque chega aos dois). Se atrapalhar, use os botoes - e +.

## Correcao 6: painel /carregarhud nao funcionava (video 18:25)
Problemas: (1) fonte grande demais (cortava SALVAR/FECHAR); (2) o ImGui deste projeto NAO recebe toque (UI::OnTouchEvent esta comentado no hook), entao os botoes nao respondiam e o toque caia no inventario.
Agora: painel desenhado com o draw list do ImGui (texto menor, tudo cabe) e toque proprio no mesmo esquema do /celofs: `CarryTuner::HandleRawTouch` em game/hooks.cpp (antes do CelOfsPanel), so captura o dedo que COMECA dentro do painel; joystick/camera seguem para o jogo. Segurar - / + repete. Botao `<>` no topo troca o lado.
Linhas: Lado X, Frente/tras Y, Altura Z, Inclinacao, Giro (roll), Dobra do corpo (-3..+3 = -60..+60 graus por osso). SALVAR / PADRAO / FECHAR.
Arquivos: CarryTuner.h/.cpp, hooks.cpp (include + 1 chamada).

## Correcao 7 (video 18:35): tilt no maximo (90) e cabeca do lado errado
- Tilt e roll agora vao de -180 a +180 (passo 2).
- Nova linha **Virar (yaw)** -180..180 (passo 5): gira o carregado em torno do eixo vertical do carregador. 180 = troca cabeca/pes de lado (frente <-> costas); 90 = para o lado. Segurar - / + repete.
- Ordem das rotacoes: tilt (em torno do eixo direito) -> roll -> yaw. O painel salva `yaw=` no carry_tuning.ini e na linha [CARRY-TUNE].
- Painel com 7 linhas (cabe na tela). Para o corpo "de cabeca para baixo com a cabeca nas costas": tilt 90 e use yaw 180 se a cabeca estiver para o lado errado.

## Correcao 8 (prints 18:54): padrao do usuario + bracos + aba CARREGADOR
- **Padrao do carregado** agora = o ajuste do print: Lado .21, Frente .21, Altura .59, Tilt 144, Roll -16, Yaw -5, Dobra -40 (`Tuning` em PlayerCarryCore.h). Se ja existe SAMP/carry_tuning.ini no aparelho, ele manda; PADRAO no painel volta a estes valores.
- **Maos do carregador**: antes o braco D vinha do bracos_cruzados e o E do foto_frontal (mao no rosto). Agora os DOIS vem do bracos_cruzados (maos abracando o corpo carregado, na altura do peito). `tools/make_carregar_ifp.py` e `game/CarryArms.h` usam a mesma base.
- **Aba CARREGADOR** no /carregarhud (abas CARREGADO | CARREGADOR no topo): escolha o osso em < > (Braco D ombro/cotovelo/mao, Braco E idem) e gire X/Y/Z em graus. Vale ao vivo: `CustomAnim::SetBoneRotation` reescreve os keyframes da anim `carregar/carregador` em memoria. SALVAR grava `arm0..arm5=x,y,z` no ini e no log ([CARRY-TUNE] armN(...)=...). PADRAO na aba zera so os bracos.
- Arquivos novos: game/CarryArms.h. Editados: CarryTuner.cpp/.h, CustomAnim.cpp/.h (SetBoneRotation), PlayerCarry.cpp/.h, PlayerCarryCore.h, tools/*, SAMP/anims/carregar.ifp (regerado).
- Eixos dos ossos do braco NAO verificados sem ver o jogo: use a aba para achar a pose e me mande as linhas [CARRY-TUNE] arm0..arm5 que eu fixo como padrao.

## Correcao 9 - bracos do carregador
Pose padrao dos bracos calculada por cinematica inversa no esqueleto da skin (maos na coxa/quadril do carregado), gravada em CarryArms.h (DefaultDeg) e no carregar.ifp. O botao PADRAO volta para ela. O carry_tuning.ini antigo (sem ver=2) tem os bracos ignorados; ajustes do corpo continuam valendo. Nada foi compilado/testado no jogo.

## Correcao 10 - braco esquerdo normal
A animacao `carregador` agora so tem o braco DIREITO (ossos 22/23/24). O braco esquerdo nao e mais tocado: segue a animacao normal (parado, andar, correr...). A aba CARREGADOR do HUD edita so o braco direito (3 ossos). Valores de bracos de ini antigo (sem ver=2) continuam ignorados; no ini novo use arm0..arm2.

## Correcao 11 - HUD enquanto carrega
Enquanto o jogador local CARREGA alguem (`PlayerCarry::LocalIsCarrying()`), o HUD moderno esconde e desativa o toque de CORRER, PULO, AGACHAR, ATAQUE, LOCK de corrida, MIRAR e P1/P2 (via disabledMask em moderncontrols.cpp). Ficam MICROFONE, INVENTARIO e SLOTS; joystick e camera normais. Ao soltar (/soltar) os botoes voltam. Nao compilado/testado no jogo.

## Correcao 12 - menu ao tocar em jogador/actor (novo: game/TargetMenu.cpp/.h)
Toque curto (<450 ms, sem arrastar) num jogador ou Actor a ate 6 m abre um menu ao lado do toque; 1a opcao "Carregar" -> envia `/carregar <id>` (jogador) ou `/carregar <1004+id do actor>` (actor). Carregando, a unica opcao e "Soltar" (/soltar). Toque fora do menu fecha (e segue pro jogo); fecha sozinho em 10 s; nao abre dentro de veiculo. Novas opcoes: BuildOptions() em TargetMenu.cpp. Servidor inalterado. IMPORTANTE: arquivo .cpp novo -> sincronize/reconfigure o CMake (GLOB_RECURSE). Logs: `[TARGETMENU]`. Nao compilado/testado no jogo.

## Correcao 13 - menu so para admin, layout novo, animacao, erro de build
- Build: TargetMenu.cpp nao compilava (faltavam includes game/game.h, net/actorpool.h, actor.h e o nome `Layout` conflitava) -> corrigido.
- Menu agora SO abre para ADMIN: o servidor manda RPC 234 (op 1 = flag admin; op 2 = id de conta de cada jogador, so para admins). Funcoes no maximo_carregar.inc: `Tgt_OnLogin(playerid, admin)` (no OnContaLogin) e `Tgt_OnDisconnect(playerid)` (inicio do OnPlayerDisconnect) - ja ligadas no maximorp.pwn. RECOMPILE o gamemode (.pwn -> .amx).
- Layout (como no video): cabecalho "<conta> [<playerid>]" + "(jogador)"; actor: "NPC [id]" + "(npc)"; linhas com icone: Carregar (1a), Fechar. Linha apertada fica azul; a acao vale ao soltar o dedo na mesma linha.
- Animacao: abre (cresce 0.82->1 + fade, 170 ms) e fecha (inverso, 130 ms). Nada compilado/testado no jogo.

14. **Slots rápidos recolhíveis** (`moderncontrols.cpp`, `controlmode_logic.h`): sem nenhum item nos 5 slots = barra invisível e sem toque; com item = só uma seta azul minimalista (embaixo, no lugar da barra); tocar na seta sobe a barra (animação ~0,22 s); em 30 s sem uso ela desce sozinha e a seta volta (usar um slot renova os 30 s). No editor de HUD a barra aparece sempre. Teste da lógica de toque: `g++ -std=c++17 tools/slots_test.cpp -o /tmp/st && /tmp/st`.

15. **Ícones nativos das armas sem moldura** (`game/WeaponIconDb.h`, `inventory.cpp` GetGtaWeaponIcon, `game.cpp`): os ícones originais do GTA (`ak47icon`, `colt45icon`, `m4icon`...) vêm com uma moldura preta arredondada. O banco novo `texdb/armas/` tem os 42 ícones só com a arma (e o contorno dela); o inventário e os slots rápidos usam ele primeiro e, se faltar o banco ou a arma, caem no ícone original (nada quebra). Gerar de novo: `python3 tools/weapon_icons_noframe.py <files/texdb/gta3> <saida>`. Log: `[ARMASDB] texdb/armas: ATIVO, 42 icones de arma no banco`.
