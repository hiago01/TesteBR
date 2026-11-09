#pragma once
// Texturas dos iPhones da mao: modelos 330 (cellphone = iPhone 17 Pro Max laranja) e 367 (camera = iPhone 16 aquamarine).
// Os DFFs vem de texdb/DFF/cellphone.dff e camera.dff (CustomDff.h). A textura vem de PNG (mesmo esquema do LedScreen, que ja funciona
// no aparelho, sem depender da busca nos bancos de textura do motor):
//     <files>/texdb/celular/iph17pro.png   (modelo 330)
//     <files>/texdb/celular/iph16aqua.png  (modelo 367)   -- ate 1024x1024; 512x512 recomendado (leve no celular)
// So age se o DFF externo do modelo foi aplicado (CustomSkinStream::LooseApplied). Sem PNG = nada muda. Log: prefixo [CELTEX].
// Chamado 1x por frame em CGame::Process (game.cpp).
namespace PhoneModelTex { void Process(); }
