# /corrida (cliente) — estilo de andar/correr por skin, definido pelo SERVIDOR

Instalação: descompacte direto na raiz do projeto (sem pasta). Substitui `game/pad.cpp`, `gui/samp_widgets/jobs.cpp` e `gui/samp_widgets/chat.cpp`; arquivos novos `game/runstyle.h/.cpp` (CMake usa GLOB_RECURSE).

## O que mudou em relação ao zip anterior do /corrida
- O jogador NÃO escolhe mais nada: o comando `/corrida` não existe mais no cliente (o `chat.cpp` voltou ao da sua versão de 22/11 12:34, sem o comando local). Quem digita `/corrida` agora fala com o servidor (só administrador 5).
- O cliente recebe do servidor o estilo da skin (RPC 245, op 25, 1 byte) ao logar, nascer ou trocar de skin. Não abre tela.
- Sem arquivo local `SAMP/corrida.ini` (pode apagar se o zip anterior criou).
- `jobs.cpp`: +1 include, +1 linha no `OnServerPacket` (op 25) e +1 linha em `JobsUI::Reset` (nova conexão volta ao nativo). `pad.cpp`: +1 include e `RunStyle::Tick()` no ramo do jogador local (também roda no controle MODERNO).

## Honestidade
- NÃO compilei com NDK e NÃO testei no jogo. O servidor foi compilado e testado (ver LEIA-ME do servidor).
- Só troca o grupo de animação (offset 0x630 do ped, o mesmo que `/reanim ativas` mostra como grupoAnd) dentro do ped.ifp que já está instalado. Se o visual diferente vier do CONTEÚDO do ped.ifp, o estilo não resolve — mande o ped.ifp antigo.
- Só vale para o seu personagem (jogadores remotos não são alterados). Log: `[CORRIDA]` no samp_log.
- Arquivos baseados nas suas versões do repo de 22/11 12:34.
