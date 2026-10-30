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

---
# Rosto = formato × tom, e área PELE no /provador

## O que mudou
- `head1..head5` agora escolhem só o **formato** do rosto (mesma malha/UV, geometrias diferentes do skin.dff).
- A textura do rosto é `heads/<tom>/<n>.png`. A variante de cabeça é codificada como `tom*100 + rosto` (ex.: 304 = tom 3, rosto 4; máx. 612). Valores antigos (<100) são convertidos para `N*100+v` ao carregar (cliente e servidor).
- Nova categoria **PELE** no /provador (skin_torso, skin_leg, skin_feet): 6 tons. Ao escolher o tom, tronco, pernas, pés **e o rosto visível** mudam juntos (o rosto mantém o formato atual).
- Servidor: `maximo_look.inc` canoniza/valida variantes de cabeça (tom 1..6, rosto 1..12); head1 padrão = 101.
- `tools_audit_provador.py --gen-variantes`: pele = 6, cabeça = 612.

## Testado (host, sem aparelho)
- `sh testes/run_host_provador.sh`: provador, modelo puro, lookdefaults, rosto_tom — todos passaram.
- Os dois `maximorp.pwn` compilam com pawncc sem avisos.
- Auditoria servidor × cliente: CONSISTENTE.

## NÃO testado
- Build do APK / CorpTex.cpp com compilador real do NDK.
- Aparelho, vários jogadores, servidor open.mp rodando; lógica Pawn só compilada, não executada.
- Modal e UI de tom na tela.

## Limites conhecidos
- O servidor não obriga tom do rosto == tom do corpo; quem garante é o cliente ao escolher PELE.
- Molde model-7 compartilhado: limite anterior continua.
- É preciso recompilar `maximorp.amx`.

---
# Provador com conta nova (corpo/rosto/pernas invisíveis)

**Causa (reproduzida no teste de host, não no aparelho):** o `contanova.ini` está correto (7 slots padrão). Ao abrir o /provador o cliente reinstala o molde (DFF puro, onde TODAS as peças estão "visíveis" com textura invisível) e só aplica os slots que o servidor lista. Conta nova lista 7 slots; os outros (camisas, calças, tênis...) ficavam "visíveis" e a regra de cobertura (`CoveredByGarments`) escondia pele, pés e roupa íntima. Sobravam cabelo e mãos. Contas antigas têm "off" gravado nas peças, por isso não sofriam.

**Correção:** `SyncMoldFromServer` oculta primeiro todo slot que o servidor não lista (mesma regra `PlanWithDefaults` usada para os outros jogadores) e só depois aplica os slots do servidor. Esses slots continuam aparecendo como "padrão, nada equipado" (não "desligado") na lista do /provador.

**Testado:** `sh testes/run_host_provador.sh` (novo bloco "CONTA NOVA" falhava antes e passa agora). **Não testado:** aparelho, APK, servidor real. Snapshot que chega DEPOIS do molde já preparado não reaplica o molde (comportamento anterior, não alterado).

---
# Calçados: buraco no pé com Bota 1 e Bota 3

**Causa (pela foto + geometria do skin.dff):** boot1 e boot3 são tênis de cano baixo: a boca do calçado fica aberta e mostra o peito do pé/tornozelo. A regra de cobertura escondia a pele `body.feet*` e `body.leg5` por baixo deles, então no lugar aparecia o interior do tênis (cinza/vermelho) e a perna terminava cortada. boot2 e boot4 são fechados, por isso funcionam.
**Correção:** `SkinSlots::PieceNeverCovers` - boot1/boot3 não escondem `body.feetleft/right` nem `body.legleft5/right5` (continuam escondendo as pernas acima). Vale no /provador (CustomSkin), nos outros jogadores (LookSlots) e no caminho antigo do CorpTex.
**Testado (host):** novo teste em host_test_rosto_tom (boot1/3 não cobrem, boot2/4 cobrem, plano completo); dois testes antigos que esperavam o comportamento anterior foram atualizados. **Não testado:** aparelho. Se ainda sobrar um vão, o próximo passo é medir a malha do tênis contra a pele no aparelho (a análise de geometria só mostrou que o pé fica dentro do tênis).

---
# Botão P2 (entrar como passageiro) e botões nativos

**Achado:** o botão nativo do carro (WIDGET_ENTER_CAR) só entra como motorista (P1). Entrar de passageiro dependia do botão "G" do `ButtonPanel`, mas esse painel é criado com `setVisible(false)` em `gui.cpp` e nunca é mostrado: o "P2" não existia na prática (só `bNeedEnterVehicle` + `HandlePassengerEntry` no `localplayer.cpp`, sem ninguém para acioná-los). O círculo que aparece no vídeo é o joystick nativo (aparece onde se toca), não uma segunda mira; **não consegui reproduzir "mira dupla" pelo vídeo nem achar no código** nenhum desenho de mira duplicado (nenhuma referência a crosshair em `samp/`). Os widgets nativos só são tocados em `camerazoom.cpp` (WIDGET_LOOK).
**Feito:** novo widget `PassengerButton` ("P2", `gui/samp_widgets/passengerbutton.*`): círculo à esquerda do botão nativo do carro (lê `CTouchInterface::m_pWidgets[WIDGET_ENTER_CAR]`; se o retângulo não for confiável usa o ponto medido do vídeo), visível só a pé, vivo, sem diálogo/teclado/lista/spawn/splash e com veículo SA-MP a ≤ 6 m. Toque = `bNeedEnterVehicle = true` -> `HandlePassengerEntry` (EnterVehicle passageiro + notificação ao servidor). O dedo que começa no botão não vai para o jogo (hook em `hooks.cpp`, igual ao seletor de armas). Lógica pura em `passengerbutton_logic.h`.
**Testado:** `testes/host_test_passengerbutton.cpp` (visibilidade, validação do retângulo nativo, posição à esquerda sem sobrepor o botão do carro, área de toque). **NÃO testado/compilado:** `passengerbutton.cpp`, `gui.cpp`, `hooks.cpp` com o NDK; posição real no aparelho (depende de o retângulo do widget nativo estar em px da tela do jogo); entrada de passageiro na prática. "HUD nativa por jogador": não implementado, pedido ficou impreciso.

## Calçados - 2ª rodada (boot1/boot3 ainda sem tornozelo)
Pela foto nova o pé já aparecia (pele marrom no cabedal/calcanhar), mas o tornozelo continuava cortado. Medindo o skin.dff: `body.leg4` vai até y=-0.69 (a caixa de cobertura dizia -0.81) e as texturas de pele dos 6 tons têm alfa cheio nessas regiões (não é PNG transparente). Então boot1/boot3 passaram a não esconder também `body.legleft4/legright4`. Testado só no host; falta confirmar no aparelho.

## P2: ícone "enterandexit"
`passengerbutton.cpp` agora desenha a textura `enterandexit` do banco `mobile` (mesma chamada `CUtil::LoadTextureFromDB("mobile", ...)` que o `CWidgetGta::SetTexture` usa para o ícone do carro), no lugar do círculo "P2". Carrega sob demanda, tenta de novo a cada 2 s se não achar e, sem a textura, cai no círculo "P2". Tamanho = diâmetro do botão, toque = mesma área. Não compilado/testado no aparelho (não tenho o texdb aqui, só a lógica de posição está no teste de host).

---
# Entrada em veículo: P1 x P2 e anti-flood

**Como o P1 já era seguro:** o botão nativo só inicia a tarefa `TASK_COMPLEX_ENTER_CAR_AS_DRIVER` uma vez por vez (o jogo ignora cliques durante a animação) e `MaybeSendEnterVehicle` só envia o RPC na **borda de subida** dessa tarefa, não a cada clique; o hook de `CPad::ExitVehicleJustDown` ainda trava 1 s após uma entrada de passageiro.
**Falhas do P2 (antes desta rodada):** cada toque setava `bNeedEnterVehicle` e `HandlePassengerEntry` mandava `EnterVehicle` + RPC imediatamente, sem checar se já havia entrada em andamento nem limitar a taxa; a flag podia ficar pendente. Nada impedia toques repetidos, e um cliente adulterado podia mandar o RPC 26 em loop.
**Cliente:** (1) `EnterVehicleLimiter.h` + `CLocalPlayer::SendEnterVehicleNotification` é o ponto único de saída: id válido, ≥700 ms entre pedidos, ≥1500 ms para o mesmo veículo/papel; só pedidos aceitos avançam a janela. (2) `HandlePassengerEntry` consome a flag SEMPRE (um toque = no máximo um pedido), recusa se morto/dentro do carro/entrando/saindo/longe (>8 m) e consulta o limitador ANTES de mandar o ped andar (não fica andando sem avisar o servidor). (3) O botão P2 some durante entrada/saída e engole toque se há pedido pendente.
**Servidor (cheaters que burlam o cliente):** `maximo_antiflood.inc` intercepta o RPC 26 (Pawn.RakNet `IRPC:26`): descarta pacote curto, passageiro > 1, veículo inexistente, jogador fora de ONFOOT e intervalo < 400 ms entre entradas aceitas; 15 descartes em 10 s → Kick (via timer). Incluído e ligado (connect/disconnect) nos dois maximorp.pwn; compilam sem avisos.
**Testado:** host: `host_test_entervehiclelimiter` (flood 200 toques/2 s passa no máximo 2; wrap do contador; Check sem gravar) e `host_test_passengerbutton`. Pawn: só compilação. **NÃO testado:** NDK (localplayer.cpp/passengerbutton.cpp), aparelho, servidor open.mp real (se o `IRPC:26` do Pawn.RakNet realmente descarta com `return 0` neste open.mp, e se o RPC chega com o buffer no início). Se o `IsEnteringCar()` não enxergar a tarefa de passageiro, quem segura o flood é o limitador (700/1500 ms) e o servidor. Outros RPCs/pacotes (chat, comandos, sync) não foram revisados.
