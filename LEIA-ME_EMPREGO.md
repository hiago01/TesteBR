# Máximo RP — Agência de Empregos + Cozinheiro

**NADA aqui foi testado no jogo.** O que foi verificado de verdade: o SQL e a API/painel (38 testes automáticos contra PostgreSQL), o Pawn (compila sem erro nem aviso com o compilador 3.10.10), a sintaxe do widget novo do cliente (compilação de verificação no PC, sem o NDK) e os modelos (renderizados no PC com o mesmo sentido de faces do jogo). **O cliente Android não foi compilado** (não há NDK aqui): compile o APK e olhe o `samp_log` na primeira vez.

## O que é cada parte (3 zips)
| Zip | Conteúdo |
|---|---|
| `empregos-servidor.zip` | PostgreSQL (migrações 009, 010 e 011), operações + painel web, filterscript `emprego`, public `Emp_Level` no gamemode |
| `empregos-cliente.zip` | código do cliente (widget da Agência, RPC 245/246, toque no NPC) + arquivos do jogo (modelos novos) |
| `empregos-modelos.zip` | os modelos soltos pedidos: `MARKER/ NPC/ KITCHEN/ TEXTURES/` (DFF + TXD + COL + PNG) |

## Instalar — servidor
1. Extraia `empregos-servidor.zip` na raiz do servidor (aceite substituir).
2. `celular-service` reinicia → aplica sozinho as migrações `009_jobs.sql`, `010_agency_world.sql` e `011_agency_marker.sql` (cria o esquema `jobs`; não mexe no que já existe).
3. `config.json` do servidor (open.mp): acrescente `"filterscripts/emprego"` em `side_scripts` (ex.: `["filterscripts/poses","filterscripts/celoja","filterscripts/emprego"]`). Não enviei o seu `config.json` (tem senhas).
4. O segredo `EMP_SEGREDO` no topo de `filterscripts/emprego.pwn` já é o mesmo do `celoja.pwn`/`http.secret`. Se mudar um, mude todos.
5. `gamemodes/maximorp.amx` e `filterscripts/emprego.amx` já vão compilados (compilador 3.10.10 no Linux). Se preferir, recompile os `.pwn` no seu qawno.
6. Reinicie o servidor. No console deve aparecer `[EMPREGO] Agencia de Empregos carregada`.

## Marcador e interação (tudo por toque — o Android não tem tecla E)
O marcador **não é um objeto 3D** (no teste ele apareceu como cilindro branco opaco). O servidor manda posição/cor/raio/texto (RPC 245 op 4) e o cliente desenha **no estilo do marcador do MTA que você enviou**: **anel branco pulsante no chão** (tamanho e transparência oscilam; ao entrar nele muda para o branco-azulado), **ícone branco flutuando e balançando acima** (maleta `mk_jobs` na agência; `mk_servico` / `mk_cozinha` no balcão do cozinheiro), `(NPC) nome` sobre a cabeça e o **botão tocável** ao lado do NPC (bolinha rosa + texto). Agência: **Abrir agência**. Cozinheiro: **Iniciar serviço** → **Preparar pedido**. O **anel e o ícone são desenhados dentro da cena 3D do jogo** (RwIm3D, o mesmo método do cartão de identidade e da mangueira, chamado em `RenderEffects()` no `hooks.cpp`) com teste de profundidade: **paredes, carros e objetos escondem o anel de verdade**, e cada ponto do anel acompanha o relevo do chão (se a leitura do chão falhar, usa a altura gravada). Aparecem a menos de 70 m (ícone a menos de 60 m). O nome `(NPC)` e o botão (2D, por toque) só aparecem com linha de visão livre até o NPC, a menos de 12 m (6–7 m no balcão).

**Créditos:** a base visual (anel, ícone flutuante, animação, ícones brancos) vem do recurso `PlayNow_markers` do MTA, autoria de **theking** — créditos mantidos aqui e no cabeçalho do código (`jobs.cpp`). Os 3 ícones (`jobs`, `get_service`, `food`) são dessa base e entram no banco `texdb/icones` como `mk_jobs`, `mk_servico`, `mk_cozinha`.

**Banco de ícones:** o pacote do cliente traz `texdb/icones/` **regerado** com as 6 texturas que já existiam (pixels idênticos) + as 3 novas (~0,4 MB). Sem essa pasta o jogo continua normal, só que o ícone flutuante não aparece (o anel aparece). No servidor, `celular-service/icones/` ganhou as prévias e o `icones.lst` atualizado.

## Instalar — cliente
1. Código: extraia `empregos-cliente.zip` na raiz do projeto (cria/atualiza `app/src/main/cpp/samp/...`) e compile o APK. Arquivos novos: `gui/samp_widgets/jobs.h/.cpp`. Alterados (pequenos): `gui/gui.h`, `gui/gui.cpp`, `game/hooks.cpp`, `main.cpp`, `net/scriptrpc.cpp`, `vendor/raknet/SAMP/SAMPRPC.h/.cpp`.
2. Modelos (pasta `files/` do jogo): `texdb/favela.img`, `texdb/favela/*` (banco de texturas), `SAMP/SAMP.ide`, `SAMP/hospital.col`. **Eles foram montados em cima do pacote do hospital v9** (favela.img v9, banco da favela com as texturas do hospital, `SAMP.ide` do hospital v3, `hospital.col` v9). Se você mexeu nesses arquivos depois, em vez de substituir: acrescente ao seu `SAMP.ide` as linhas de `emprego.ide.txt` (dentro de `objs`, depois das do hospital).
3. Os modelos da cozinha usam os IDs **12601–12608** (faixa livre; o hospital usa 12402–12405). O marcador 12600 foi retirado do pacote.

## Montar a agência na prefeitura (admin, sem código)
1. Vá ao local da prefeitura onde a recepcionista deve ficar (pode ser **dentro** do interior: o servidor guarda interior e mundo virtual) e fique de pé olhando para onde ela deve olhar.
2. Digite `/agenciaset 9` (9 = skin; troque pelo ID que quiser, 0–311). A recepcionista aparece ali e o **anel só no chão**, 2 m à frente; ao lado dela o cliente mostra `(NPC) Atendente` e o botão **Abrir agência**. Só aparece/funciona para quem está no mesmo interior e mundo.
3. Ajustes finos (skin, **nome do NPC** (aparece como `(NPC) nome`), X/Y/Z, rotação, distância, interior, mundo e **personalização do marcador: cor do anel, raio e texto do botão**): painel `http://127.0.0.1:8789/empregos` (usuário `admin` + `admin.password` do config do celular-service). **Atenção:** o painel grava o que está nos campos; se mudar a agência por ele, confira interior/mundo.
4. **Cozinheiro**: no painel → "Novo emprego": nome `Cozinheiro`, tipo `cozinheiro`, descrição, imagem (opcional: nome de textura do `texdb/icones`; a imagem é recortada para preencher o topo do cartão), salário mín/máx, nível exigido, vagas (0 = ilimitadas), skin do funcionário, local de início = **o balcão da cozinha** (`/empregopos` no jogo mostra as coordenadas), veículo (opcional) → "Vaga ativa" → Salvar. Em até 30 s a vaga aparece na Agência.
5. Comandos de admin (nível 5 **do jogo**, nunca RCON): `/agenciaset [skin]`, `/empregoadm [listar|ativar <id>|desativar <id>|recarregar]`, `/empregopos`.
6. Mobiliar a cozinha: `KITCHEN/LEIA-ME_COZINHA.txt`.
7. Observação: actors do SA-MP não têm interior próprio, só mundo virtual; como o interior da prefeitura fica longe do mapa externo, isso não aparece na rua.

## Como o jogador usa (interface igual à do print)
- Chega perto da recepcionista e **toca nela ou no botão Abrir agência** (ou digita `/agencia`, `/empregos` ou `/emprego`). Enquanto a tela está aberta os toques ficam só nela (o personagem não anda); **X** ou Voltar devolvem o controle.
- Layout: título "Agência de Empregos" + X; barra lateral com **AGÊNCIA**, cartão **EMPREGO ATUAL** (imagem, LVL, salário, DEMITIR-SE), **Nível/XP** do emprego e nome/ID do jogador; área **VAGAS DISPONÍVEIS** com setas `<` `>` (ou arrastar) e 3 cartões por página (imagem, selo LVL, ícone de livro, nome, R$ mín até máx, descrição, vagas, botão).
- Tocar num cartão o destaca (brilho rosa) e mostra requisitos e remuneração; o **livro** abre a ficha completa; **ASSINAR CARTEIRA** pede confirmação e o servidor confere (vaga ativa, nível, já empregado, vagas) e grava no PostgreSQL. **DEMITIR-SE** também pede confirmação.
- O emprego é por conta (`jobs.employees`): continua depois de sair e voltar.

## Como funciona o Cozinheiro (o que eu implementei)
1. Contratado, o jogador vai ao **balcão da cozinha** (local de início do emprego) e toca em **Iniciar serviço**: liga o expediente (troca para a skin cadastrada; cria o veículo de trabalho se o cadastro tiver um) e já começa o primeiro pedido. `/servico` encerra o expediente e devolve a skin.
2. Para os próximos pedidos toca em **Preparar pedido** no mesmo botão (reserva: `/cozinhar`). O pedido sorteia um prato.
3. O pedido tem **3 etapas de 4 s** (separar ingredientes → cozinhar → montar) com animação e porcentagem na tela. Tem que ficar a pé e a até 3,5 m do balcão; sair cancela.
4. Ao terminar, o **servidor** paga entre o salário mínimo e o máximo do cadastro: o emprego tem nível 1–5 (sobe com XP: 10 por pedido, 60 por nível) e cada nível sobe a faixa de pagamento (nível 1 paga perto do mínimo, nível 5 chega ao máximo). O dinheiro entra por `GivePlayerMoney` (o gamemode já salva o dinheiro da conta); progresso/total ganho ficam no banco.
5. Intervalo mínimo de 3 s entre pagamentos + as 12 s do próprio pedido: não dá para farmar mais rápido que isso.
- **Diferença para o vídeo do Complexo:** lá o cozinheiro anda entre várias estações (pegar, fritar, entregar). Aqui é um balcão só, com etapas por tempo. Dá para evoluir para várias estações (precisa de coordenadas por estação: nova tabela + campos no painel).

## Limites e cuidados (honestos)
- **Tocar no NPC**: o cliente projeta o actor na tela e confere o toque curto sobre ele; o servidor valida a distância (9 m). A caixa de toque é estimada (ajuste `pad` em `jobs.cpp` se ficar difícil de acertar).
- A skin do expediente usa `SetPlayerSkin`: se o seu sistema de roupas (`/skin7`, Provador) aplica roupa por cima, a roupa de trabalho pode ser sobrescrita/restaurada de um jeito diferente. Teste.
- Interface: reproduzi o print (proporções, cores, brilho no cartão); fontes e imagens não são idênticas (usa a fonte do jogo). Nunca vi na tela do celular: confira tamanhos de texto e área de toque no aparelho.
- A imagem do cartão vem do banco `texdb/icones` (mesma lista do conversor do painel da Loja). Sem imagem, o cartão mostra a inicial do nome.
- Teste antigo `test/store.js`: 1 verificação (`/s/me`, valor de data = 0) falha aqui; não tem relação com este trabalho (não mexi em nada dessa parte).
- NPC: skin existente do GTA (você escolheu). Não há modelo humano novo.
