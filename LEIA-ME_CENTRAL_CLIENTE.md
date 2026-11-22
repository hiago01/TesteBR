# Central da Cidade (scoreboard) - CLIENTE

Abre por /central (comando do SERVIDOR) ou pela aba "Perfil" da barra /tablistmx (agora o padrao de cmd_perfil e /central; mude em settings.ini [tablistmx] cmd_perfil=).
Mostra todos os cidadaos online (nome, ID da conta, ping, tag ADMIN), seu avatar com a borda escolhida, tempo online, online/admins e barra de populacao.
Engrenagem = escolher o MODELO DE FUNDO da sua linha (9 modelos, alguns com efeito: Dourado brilho, Neon roxo pulsa, Fogo onda, Arco-iris, Natal pisca-pisca).
O modelo fica guardado por conta no servidor e todos veem. Rolar: arrastar a lista. Fechar: X, tocar fora ou Voltar.
Arquivos novos: gui/samp_widgets/central.h, central.cpp, central_logic.h.
Alterados (a partir dos originais + zips anteriores): SAMPRPC.h/.cpp, net/scriptrpc.cpp, main.cpp, gui/gui.h, gui/gui.cpp,
gui/samp_widgets/maximomenu.cpp, tablistmx.cpp, avatar.h/.cpp (DrawPhoto). Este zip ja inclui TUDO do avatar (v3) e da barra /tablistmx.
Nao compilado com o NDK nem testado em aparelho (so sintaxe com clang + teste da logica no PC). Sem campo de pesquisa por enquanto.

## v2 - layout da referencia, organizacoes, pesquisa e correcao do "Carregando"
- Faixa: icones PM (escudo) | MEC (ferramentas) | SAMU (maleta) com a quantidade EM SERVICO, e a barra de jogadores com "online/slots" reais do servidor.
- Caixa "Pesquisar...": toque abre o teclado do Android (mesmo mecanismo do EditBox/Keyboard::show) e filtra por trecho do NOME ou do ID da conta; "x" limpa.
- O pedido da lista e a troca de modelo agora vao pelo comando oculto "/cenx ..." (mesmo caminho comprovado do /invx), e o servidor ja manda a lista junto com a ordem de abrir. Se o servidor nao responder em ~4,5 s, a tela avisa.

## Atualização (v3)
- Ícone de jogadores (imagem enviada) no chip de população (online/slots) e globo no cabeçalho da Central (central_icons.h).
- Os chips PM / MEC / SAMU agora filtram a lista pelos jogadores ONLINE da organização daquela TAG (toque de novo = todos). A contagem mostra membros online.
- Precisa do servidor novo: cada linha do RPC 253 leva 1 byte a mais (org 0..3). Atualize org.pwn + maximo_central.inc junto.
