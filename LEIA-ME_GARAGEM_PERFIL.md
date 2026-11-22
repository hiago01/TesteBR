# Garagem azul + ícone animado, avatar do Menu Jogador, /corrida (pacote completo, substitui o corrida-cliente-v2)

Instalação: descompacte direto na raiz do projeto (sem pasta). CMake usa GLOB_RECURSE: arquivos novos entram sozinhos.

## 1) Avatar e moldura de volta no Menu Jogador
Causa: o `jobs.cpp` do repo estava com `PerfilDrawAvatar` "PENDENTE" (retornava false), por isso o menu desenhava só o círculo com a inicial. Agora usa o mesmo desenho do Central: `Avatar::DrawPhoto` + `Avatar::DrawBorder` com a borda escolhida na conta.

## 2) Garagem: paleta azul + ícone do topo animado
- Cores trocadas de rosa/vermelho para azul (mesma família do Menu Jogador): fundo, botão/abas ativos, cartão selecionado, barras, bordas.
- O quadrado do topo (onde ficava o carrinho) agora mostra as 7 silhuetas da sua imagem (moto, patinete elétrico, carro, patinete, bicicleta, van, hoverboard), vetorizadas do seu JPG (contornos + triangulação; `garagicons.h`). Com a tela aberta, a cada 1,8–3,4 s sorteia outra silhueta (nunca repete a atual): a antiga encolhe girando, a nova cresce com "pulinho" e flutua de leve. Ao abrir a tela começa numa silhueta ao acaso.
- Sem textura/banco novo e sem IMG novo (é desenho vetorial). Sem alfa nas silhuetas de propósito (triângulos vizinhos com alfa deixariam emendas).
- Os carros cobertos (car_preto/car_vip) continuam vindo do banco de ícones; o VIP segue com a capa vermelha dele.

## 3) /corrida (igual ao corrida-cliente-v2, já incluído)
`pad.cpp`, `chat.cpp`, `game/runstyle.h/.cpp` e a op 25 no `jobs.cpp` (estilo de andar/correr por skin vindo do servidor; admin 5 define com /corrida no servidor).

## Honestidade
- NÃO compilei com NDK nem testei no jogo. O código do ícone animado foi compilado e executado num teste de PC com um renderizador de mentira (7 silhuetas, sorteio e transição rodaram sem erro); a aparência real no aparelho eu não vi.
- As silhuetas ficaram fiéis no teste de imagem (preview). Janelas do carro e detalhes finos podem ficar mais "cheios" no tamanho pequeno do ícone.
- Arquivos baseados no seu repo de 22/11 12:34 (jobs.cpp, garagepanel.cpp, pad.cpp, chat.cpp).
