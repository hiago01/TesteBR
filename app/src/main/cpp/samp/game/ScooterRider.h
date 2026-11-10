// ScooterRider.h - animacoes do piloto do patinete eletrico (veiculo 662), vindas do proprio recurso MTA do patinete.
#pragma once

namespace ScooterRider {

// Chamar 1x por frame (hooks.cpp / RenderEffects). Trabalha a cada ~100 ms: para cada jogador que dirige o modelo 662 mantem
// a animacao do patinete (parado / andando) no lugar da de moto; ao descer devolve as animacoes normais.
// Usa SAMP/anims/scooter.ifp (bloco "scooter", anims SCOOT_*). Sem o arquivo, nao faz nada (so uma linha no log).
void Update();

} // namespace ScooterRider
