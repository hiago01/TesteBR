# Nota Fiscal azul + Agência azul (lado do CLIENTE / APK)

Descompacte na raiz do repositório (TesteBR), junto com o `app/` já existente, substituindo `jobs.cpp`, e gere o APK como de costume.
Os fontes novos são pegos sozinhos pelo CMake (GLOB_RECURSE); não é preciso editar o CMakeLists.

## O que muda
- **`notafiscal.cpp/.h` (novos)**: tela da Nota Fiscal no formato do cupom do seu print: ícone da empresa + "Nota Fiscal", itens com `(Nx)`,
  Sub-total / **Imposto (X%)** (no lugar do Desconto) / Valor total, código de barras e **Cancelar** / **Finalizar pedido**.
  Paleta azul (papel azulado, brilho azul atrás do cupom que aparece pelos furos, destaque azul). Cinco ícones desenhados por código
  (carrinho, chave inglesa, camiseta, tesoura, hambúrguer) + sacola: **nenhum arquivo de imagem novo e nenhum IMG novo**.
- **`jobs.cpp`**: a nota roda dentro da mesma tela da agência/garagem (mesmos toques e mesmo RPC 245/246), esconde o HUD como a garagem,
  e o botão voltar do celular cancela a nota. A **Agência de Empregos** trocou a paleta rosa/cinza por azul (fundo azul-marinho, cartões e
  destaques em azul; verde/vermelho continuam só para "aprovado/erro").
- Protocolo: servidor -> cliente RPC 245 op 16 (abre a nota) e op 17 (fecha); cliente -> servidor RPC 246 op 14 (a tela abriu), 15
  (Finalizar pedido), 16 (Cancelar), cada um com o número da nota. O cliente só desenha e pede; o servidor confere tudo.

## O que foi testado (e o que NÃO foi)
- `notafiscal.cpp` compila no PC (g++ -Wall -Wextra, sem avisos) e roda com um "renderer" falso: leitura do pacote (pacote cortado,
  número 0 e mais de 8 itens são recusados), toques em Finalizar/Cancelar (arrastar para fora, toque cancelado e toque durante a animação
  não contam), e gera PNG de pré-visualização (`testes/notafiscal/run.sh`; 20 verificações OK). Nas imagens a fonte é outra, então a largura
  dos textos muda um pouco no aparelho.
- **NÃO compilado no NDK/Android e NÃO testado no aparelho.** O `jobs.cpp` só recebeu pequenas alterações (modo nota fiscal e cores), mas
  não foi compilado aqui. Se o build do GitHub der erro ou algo ficar torto na tela, me mande o log/print que eu corrijo.
