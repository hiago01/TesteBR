// runstyle.h - /corrida: variacoes de andar/correr (grupo de animacao do ped), salvas em SAMP/corrida.ini, com padrao por SKIN.
//
//  /corrida                    -> mostra o estilo atual e a skin
//  /corrida lista              -> lista os estilos
//  /corrida <estilo>           -> escolhe o estilo para a SKIN atual (salvo)
//  /corrida todas <estilo>     -> escolhe o estilo para todas as skins (salvo)
//  /corrida padrao             -> volta ao padrao da skin (apaga a sua escolha para esta skin)
//  /corrida reset              -> apaga todas as suas escolhas
//  /corrida defskin <skin> <estilo|nativo>  -> define o PADRAO da skin (quem administra; vale para quem nao escolheu)
//
//  Prioridade: escolha do jogador (skin) > escolha do jogador (todas) > padrao da skin > padrao geral > nativo do jogo.
//  Nao troca o ped.ifp: so seleciona QUAL grupo de animacao ja existente o ped usa (CPed::m_nAnimGroup).
#pragma once
#include <string>
namespace RunStyle {
	std::string Command(const std::string& args);
	void Tick();    // chamar todo frame (leve): aplica o estilo ao ped local
}
