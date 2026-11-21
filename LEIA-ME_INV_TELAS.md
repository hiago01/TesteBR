# Inventário: tela PRINCIPAL nova + tela LOCAIS (a que já existia)

Descompacte na raiz do projeto (sobrescreve app/src/main/cpp/samp/gui/samp_widgets/inventory.cpp). Base: Source-21hrs original (só este arquivo muda; sem nenhuma referência a cabelo).
Use junto com Servidor-Inventario-Telas.zip (o servidor é quem escolhe a tela).

## Telas
- LOCAIS = a tela de sempre (Mochila + Chão, negociação). Continua igual. Futuramente: porta-malas e baú entram nela.
- PRINCIPAL = nova, no layout do seu print: abas no topo (Inventário | Craft | Roupas | Adesivos), personagem (avatar, nome, ID), slots rápidos 1-5, mochila, coluna Quantidade/Usar/Negociar/Dropar, cartão do item selecionado e faixa "arraste aqui para jogar o item fora".
- Abas Craft, Roupas e Adesivos já aparecem, mas ainda não existem: tocar mostra "Em breve". Só Inventário funciona.
- Negociar na tela principal troca sozinho para LOCAIS (é lá que fica o painel de troca).
- Servidor antigo (sem o 2º byte no RPC 203 op 2) = sempre LOCAIS, como antes.
- Cores: continuam as do tema vindo do PostgreSQL (rosa dos números/seleção, células, linhas), sem mudança.
- Não incluí: logo "COMPLEXO" (é de outro servidor), cadeado de slots premium, LOJA e contador de moedas.

## Verificado / NÃO verificado
- Sintaxe conferida com clang contra os headers reais do projeto (com stubs de Android). Não compilei com o NDK nem vi no aparelho.
- As posições seguem o print (1280x720 de referência), mas posição/tamanho de abas, avatar e cartão só se confirmam no jogo: me mande um print e eu ajusto.
