#pragma once

#include <string>

// =====================================================================================
//  Cabelo de TECIDO caido (hair6) - Maximo RP
//
//  Slot do /skin7: "hair6" (SkinSlots.h). Variante = comprimento (1 ombro / 2 meio das costas / 3 cintura).
//  Cor 1 = RAIZ, cor 2 = PONTAS (gradiente por vertice; nao depende da cor do material do ped).
//
//  Fisica : HairPhysics.cpp (Verlet, passo fixo; modulo puro, testado no PC). SO NO CLIENTE: cada jogador simula a sua copia.
//  Desenho: RwIm3D (mesmo padrao da mangueira / bandeira / cartao 3D): calota do couro cabeludo + folha de tecido com pontas em dente.
//           Nada e escrito em RpGeometry/RwRaster; tudo na thread do jogo, dentro do RenderEffects().
//  Quem esta usando: slot hair6 com estado "variante" no snapshot do servidor (RPC 232) e, para o ator local com o /provador aberto,
//           a previa do provador (CCustomSkin::UiGetState). `/cabelo6 teste` forca no ator local sem servidor.
//
//  Chamadas (game/hooks.cpp, RenderEffects):  HairCloth_Update()  e  HairCloth_Render()
//  Comando (gui/samp_widgets/chat.cpp):       /cabelo6 ...      -> HairCloth_Command
//  Nova conexao / desconexao:                 HairCloth_Reset()
// =====================================================================================

void        HairCloth_Update();
void        HairCloth_Render();
void        HairCloth_Reset();
std::string HairCloth_Command(const std::string& args);
