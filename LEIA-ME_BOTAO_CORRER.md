# Botão de correr = alterna andar/correr
Causa: no modo WALK (padrão) o `CMoveMode::ApplySprint` devolvia sempre 0, então o botão de correr não fazia NADA (o modo só mudava por /movimento).
Agora cada TOQUE no botão alterna: WALK/JOG -> SPRINT (corre) e SPRINT -> WALK (anda). Segurar não fica trocando (borda de subida + debounce 200 ms).
- Só troca com o personagem a pé em locomoção normal (mesma regra `Eligible()` de antes: sem veículo, nado, escalada, queda, mira).
- A troca vale na sessão; não grava o INI a cada toque. /movimento walk|jog|sprint continua funcionando e salvando.
- Teste PC: g++ -std=c++17 testes/botao_correr_check.cpp (lógica de alternância/borda/debounce).
NÃO verificado: compilação Android e comportamento no aparelho.
