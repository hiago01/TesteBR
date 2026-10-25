# Correção do cliente (sincronizar a troca de visual com o servidor)

Arquivo alterado: app/src/main/cpp/samp/game/CorpTex.cpp (copie por cima do seu; faça backup antes).
NÃO compilei o APK (sem NDK aqui). Só testei o trecho novo isolado no PC. Faça o build e me mande os erros, se houver.

## Como os comandos funcionam
- /rosto /cabelo /pele /bota /calca /camisa /intima /corp /visual = POR PED (por playerid). A textura é trocada só na hora de desenhar aquele ped e devolvida depois. Por isso dá para mostrar o visual de cada jogador.
- /skin7 e /provador = só o SEU ator. O CCustomSkin troca o modelo (clump) do jogador local. Não existe versão por ped.

## O que mudou
1. Todo comando local passa por ApplyLocal, que agora marca "sujo".
2. O Process (1x por frame) manda UM "/lookx base <tom> <rosto> <estilo> <cor> <bota> <botaV> <calca> <calcaV> <camisa> <camisaV> <intima> <corp|->" ao servidor, no máximo a cada 300 ms (o servidor descarta menos de 200 ms).
3. O servidor grava e retransmite pelo RPC 231, que o cliente já recebia, para quem te vê.
4. O eco do servidor não apaga mais o /cop local.

## Ainda NÃO sincroniza
/skin7 slot e /provador: os slots (torso1, hair1, tat_*...) trocam o modelo do seu ator. Para os outros verem, falta desenhar slots por ped (como o CorpTex faz) + RPC 232. É a próxima etapa.
