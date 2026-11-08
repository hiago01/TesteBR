#pragma once
#include <string>

// Trena (medidor de distancia) para conferir distancias de LOD no jogo.
//   /trena          -> mostra posicao atual, e a distancia ate o ponto A (se marcado)
//   /trena a        -> marca o ponto A na sua posicao agora
//   /trena limpar   -> apaga o ponto A
//   /trena ir <x> <y> <z> -> distancia ate uma coordenada (ex.: centro da favela: 767.2 -916.6 40.3)
std::string Trena_Command(const std::string& args);

// Salva coordenadas em <storage>/SAMP/coordenadas.txt (uma linha por ponto: x, y, z, angulo // nome)
//   /coord [nome]        -> salva a posicao atual (nome opcional)
//   /coord lista         -> mostra os ultimos 8 salvos
//   /coord apagar        -> esvazia o arquivo
std::string Coord_Command(const std::string& args);

// Diagnostico de LOD/distancia da favela (modelos 12010-12119 e LODs 12200-12309):
//   /lodinfo  -> escala de distancia ativa, quantas instancias existem, quantas tem LOD ligado, e a instancia mais proxima
std::string LodInfo_Command(const std::string& args);
