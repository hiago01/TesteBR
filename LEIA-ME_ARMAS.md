# Armas exclusivas (cliente) — barra de durabilidade e slot rápido

Instale junto com o pacote do servidor (`Servidor-armas-exclusivas.zip`). O cliente novo funciona com servidor antigo (ignora o op 10), mas a barra só aparece com o servidor novo.

Arquivos: `inventory.cpp/.h`, `moderncontrols.cpp`, `weapondura_logic.h` (novo), `tools/dura_test.cpp` (teste da cor/preenchimento da barra: `g++ -std=c++17 tools/dura_test.cpp && ./a.out`).

O que mudou
- RPC 203 op 10 (durabilidade 0..100 por arma): barra verde → amarela → vermelha no rodapé da casa, no inventário, nos slots rápidos do inventário e nos slots rápidos do HUD. 0% = enferrujada (barra vermelha mínima).
- Arma EQUIPADA (no corpo ou na mão) agora pode ser arrastada para fora do slot rápido (antes o cliente travava o arrasto). Soltar na mochila = `/invx hotstow` (guarda a arma; a munição fica dentro dela). Soltar fora = `/invx barclear` (o servidor guarda e limpa o slot). Soltar em outro slot = `/invx hotmove` (troca o atalho).

NÃO foi compilado aqui (não há NDK neste ambiente): só a lógica da barra foi testada no host. Compile e me envie qualquer erro.
