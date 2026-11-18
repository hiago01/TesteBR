# Gerenciador de Grupo (/grupo) - Máximo RP  (NÃO testado no jogo / no aparelho)

Menu em tela cheia com 4 abas (Início, Membros, Coleta, Cofre), ligado às organizações (`org.*`) e ao PostgreSQL.
Textos e marca: tudo é "Máximo RP" e o nome da própria organização (vem do banco). Nada do servidor do layout original.

## Como funciona
- `/grupo` (ou `/gerenciador`) no jogo -> o servidor confere login e organização, gera um token assinado (6 h) e manda ao cliente (RPC 245 op 16).
- O cliente lê os dados direto do `celular-service` pela porta pública (a mesma do Gram, 8788), rotas `/o/*`, sempre com o token.
- Ações que mexem em dinheiro/inventário/convite/promoção/expulsão vão por RPC 246 (ops 40..45); o **servidor do jogo valida tudo** e grava no PostgreSQL. A tela só recarrega quando o servidor confirma (RPC 245 op 17). Avisos do servidor aparecem dentro do painel (RPC 245 op 18).
- Permissões por cargo (bits): 1 convidar, 2 expulsar, 4 promover/rebaixar, 8 gerenciar cargos, 16 sacar do cofre, 32 editar aviso. Padrão: cargos 1 e 2 = tudo; cargo 3 = convidar + expulsar + aviso (35); demais = nada. O líder edita nomes e permissões na tela de cargos.

## Instalar (servidor)
1. Descompacte na raiz do servidor (celular-service/, filterscripts/, gamemodes/, LEIA-ME_GRUPO.md).
2. Reinicie o celular-service: a migração `sql/017_org_manager.sql` é aplicada sozinha.
3. `config.json` -> `side_scripts` já deve ter `filterscripts/org` (já tinha). Os `.amx` de `org` e `maximorp` vão compilados; se preferir compilar de novo: `pawncc org.pwn -i../qawno/include` e `pawncc maximorp.pwn -i../qawno/include`.
4. A porta pública 8788 precisa estar aberta no firewall (é a mesma do Gram).
5. Reinicie o servidor. Atualize o cliente (build novo do APK/libs - veja o zip do cliente).

## Configurar cada organização (admin nível 5, no jogo)
`/orgcfg <tag> <chave> <valor>` - chaves: `discord` (link), `radio` (frequência, só texto), `farmname` (nome da coleta), `farmitem` (id do item no inventário do gamemode), `farmprice` (R$ por unidade), `farmgoal` (meta semanal), `max` (máx. de membros), `bank` (define o saldo), `gps` (`x;y;z` do ponto).

## Coleta (antes "Farm"; como está implementado - confirme se é o que você quer)
O jogador entrega itens do inventário (Grp_ItemCount/Take/Give no gamemode); o grupo é pago `preço x quantidade` e o histórico/meta semanal ficam em `org.farm_log`. O item e o preço são os que você cadastrar em `/orgcfg`.

## Pendências / o que NÃO existia e precisa ser refeito ou decidido
- **"Sair do grupo"**: não há sair-do-grupo no seu sistema. O botão virou **"Fechar"**.
- **"Criar cargo"**: o sistema tem 7 cargos fixos. Implementei renomear e permissões por cargo; criar cargo novo não existe.
- **Rádio**: só mostra a frequência cadastrada; sem integração com voz.
- **Discord**: botão copia o link para a área de transferência (precisa do método novo `setClipboardText` no Java/JNI; só funciona com o build novo do cliente).
- **GPS**: usa o ponto cadastrado em `/orgcfg ... gps` (checkpoint).
- **Banner do Cofre**: sem arte (desenhado por código); fontes e "blur" diferem do original (fontes SF do projeto, sem desfoque real).
- **Teclado numérico**: é o teclado próprio do celular (padrão de telefone).
- **Tempo jogado/online**: conta só a partir da instalação deste sistema.
- Rota HTTP `/o/member` existe, mas o cliente não usa (as ações de membro vão por RPC).
- Painel web de administração **não** foi estendido.
- Correções colaterais: `src/store.js` (bought_at agora independe do fuso do PostgreSQL) e `test/orgs.js` (lider sem permissão -> mensagem `noperm`).

## Testado (PC) x NÃO testado
- Testado: SQL + Node (`node test/orgpanel.js` = 78 verificações OK, `node test/orgs.js` = 85 OK) contra PostgreSQL 16 real; tela desenhada em simulação no PC com dados reais do servidor.
- Pawn: **compilou**, mas **não foi executado** num servidor open.mp.
- Cliente: lógica da tela testada no PC; **não compilado para Android e não testado no aparelho**.
