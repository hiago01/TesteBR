#pragma once

#include <string>
#include "Core/Vector.h"

// =====================================================================================
//  Mangueira (visual, SEM colisao)  -  bomba de combustivel / torneira de parede
//
//  NAO usa o CRope do GTA (ver HOSE_MAXIMO.md: o CRope cria um CObject fisico no mundo, simula
//  colisao com o chao, so prende uma ponta em CPhysical e desenha uma linha de 1px).
//  Em vez disso desenha UM tubo continuo com RwIm3D (mesma API que o CRope::Render usa),
//  entre o ponto A (fixo) e o ponto B (osso da mao do ped ou tampa do tanque do carro).
//
//  Logica por frame, para t de 0 a 1:   P(t) = lerp(A, B, t);   P.z -= sag * sin(PI * t)
//  O sag diminui conforme A e B se afastam. Cada anel do tubo e COMPARTILHADO pelos dois
//  segmentos vizinhos (orientacao = media das direcoes), entao nao ha emendas.
//
//  Chamadas:
//    Hose_Start()   quando o jogador PEGA o bico         (ou /mangueira pegar)
//    Hose_Stop()    quando SOLTA o bico                  (ou /mangueira soltar)
//    Hose_Update()  + Hose_Render()  1x por frame  ->  game/hooks.cpp, RenderEffects()
//  Hose_Update() tambem encerra sozinha: jogador longe demais, morreu, entrou em veiculo,
//  ped/veiculo invalido.
// =====================================================================================

struct HoseTarget
{
	enum Kind { Hand, VehicleTank } kind = Hand;
	int vehicleId = -1;      // VehicleTank: ID SA-MP do veiculo (resolvido pelo pool a cada frame)
	int boneTag   = 24;      // Hand: osso (24 = mao direita, 34 = mao esquerda)
};

// pedId: ID SA-MP do jogador (-1 = jogador local). Devolve o "handle" (>0) ou 0 se falhou
// (sem slot livre, ped invalido, ...). Slots sao reaproveitados quando a mangueira termina.
int  Hose_Start(int pedId, const CVector& bombaPos, const HoseTarget& alvo = HoseTarget());

// handle > 0: encerra aquela mangueira.  handle <= 0 (padrao): encerra todas.
void Hose_Stop(int handle = -1);

bool Hose_IsActive(int handle = -1);   // handle <= 0: alguma ativa?

void Hose_Update();   // logica + calculo dos pontos (1x por frame, ANTES do Render)
void Hose_Render();   // desenho (dentro da cena 3D)

// Comando do chat: "/mangueira ..." -> texto de resposta (ver HOSE_MAXIMO.md)
std::string Hose_Command(const std::string& args);
