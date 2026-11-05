#pragma once
// Posto (modelo 12002 mx_posto) em runtime, sem tocar na TXD compartilhada barrio1_lae:
//  * Outdoor (3 lados): textura animada com os quadros PNG de <files>/texdb/led/board/*.png (ordem alfabetica, ate 96, max 256x256), em loop, sem som.
//  * Friso da cobertura: troca a textura "closedblank1_256" (so neste modelo) por <files>/texdb/led/beira.png.
//  * Pisca-pisca de Natal (modelo 12003 mx_luzes): anima a cor dos materiais 0..3.
// Opcional: <files>/texdb/led/led.txt com "fps=12" (1..30). Arquivos ausentes = nada muda (visual original).
// Chamado 1x por frame em CGame::Process (game.cpp). Usa so os helpers de raster que hud.cpp/CorpTex.cpp ja usam.
namespace LedScreen { void Process(); }
