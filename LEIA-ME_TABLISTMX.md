# Barra de abas /tablistmx (2 estilos)

v2: a v1 enviada em 21/11 trazia um chat.cpp com código de cabelo e quebrava a compilação; apague-a e use esta.

Descompacte na raiz do projeto (sobrescreve). Em app/src/main/cpp/samp/gui/:
  novos:      samp_widgets/tablistmx.h, tablistmx.cpp, tablistmx_logic.h, tablistmx_atlas.h (PNG dos seus 2 prints embutido, 1024x512)
  alterados:  gui.h        (+ #include "samp_widgets/tablistmx.h" e + membro m_tabListMX)
              gui.cpp      (+ bloco que cria o TabListMX, logo depois do JumpButton)
              samp_widgets/chat.cpp       (+ comando /tablistmx, logo antes de /roupas)
              samp_widgets/maximomenu.cpp (+ #include "tablistmx.h" e + 3 linhas no início de MaximoMenu::HandleRawTouch)
Base dos 4 alterados: Source-21hrs ORIGINAL (sem cabelo 5+; só as minhas linhas foram acrescentadas). Se os seus forem outros, aplique só essas linhas. hooks.cpp NÃO é alterado.

## Uso (chat)
  /tablistmx 1      estilo 1: pílula escura, aba escolhida levantada (print 1)
  /tablistmx 2      estilo 2: ícones com contorno preto + 4 separadores (print 2)
  /tablistmx 0      oculta  |  /tablistmx on  volta ao último estilo
  /tablistmx pos N  altura do centro da barra em % da tela (6-96; padrão 93 = base, centralizada)
  /tablistmx        mostra o estado
A escolha fica em SAMP/settings.ini, seção [tablistmx] (estilo=, pos=). Padrão sem ini: estilo 1.

## O que cada aba faz
  Inventário -> InventoryUI::RequestOpen() (manda /inv; o servidor tem CMD:inv).
  Diálogo, Perfil, Tarefas, Ajuda -> hoje só marcam a aba: o servidor (maximorp.pwn) NÃO tem /perfil /tarefas /ajuda /dialogo
  e o chat do jogador comum não abre teclado (g_chatAllowed), então não inventei ação.
  Para ligar uma aba a um comando existente, no settings.ini:
      [tablistmx]
      cmd_ajuda=/comando
      cmd_perfil=/comando      (também cmd_dialogo, cmd_tarefas, cmd_inventario)
  Só vale texto começando com "/" (até 63 caracteres). A ação dispara ao SOLTAR o dedo na mesma aba.

## Comportamento
  - Some com: pausa, /menu, inventário, garagem, painel de organização, celular, mapa, provador, empregos, ajuste do celular,
    edição dos controles modernos, teclado, diálogo, lista de jogadores, spawn, splash, HUD escondida (congelado/foto).
  - O dedo que cai numa aba não vai para o jogo (câmera/joystick); o resto da tela segue normal.
  - Toque mínimo: barra com pelo menos 56 px de altura (8,5% da altura da tela, máx. 120 px).

## Verificado
  - Lógica pura (layout, toque, comando, ini) testada no host com ASan/UBSan, incluindo 20.000 casos aleatórios.
  - Sintaxe de tablistmx.cpp e chat.cpp conferida com clang contra os headers reais do projeto (com stubs de Android).
## NÃO verificado
  - Não compilei com o NDK nem testei no aparelho. Posição/tamanho no seu HUD e a nitidez dos ícones só se vê no jogo.
  - Se a barra atrapalhar algum botão nativo, use /tablistmx pos <n> (ou 0) e me diga onde ela deve ficar.
