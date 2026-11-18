#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
//  Bandeira 3D com fisica de tecido (Maximo RP)  -  ver BANDEIRA_MAXIMO.md
//
//  Modelo : SAMP/models/flag_maximo.dff  (mastro + base + tecido de pena, frente/verso). Lido por FlagModel.cpp.
//  Textura: banco texdb/mapasmaximo (flag_maximo = frente, flag_maximo_verso = verso), ja carregado na abertura do
//           jogo pelo extra_imgs.txt (mapasmaximo.img). O .txd tambem e entregue (SAMP/models/flag_maximo.txd) para
//           ferramentas, mas o cliente nao o le: segue o padrao dos modelos novos do projeto (textura por nome do banco).
//  Fisica : FlagPhysics.cpp (Verlet, passo fixo, SO NO CLIENTE: cada jogador simula a sua copia; nada trafega por quadro).
//  Desenho: RwIm3D em lotes pequenos (mesma API da mangueira/cartao 3D), na cena 3D, dentro do RenderEffects().
//           Nenhuma escrita em RpGeometry/ RwRaster e nenhuma thread paralela mexe em dados do RenderWare.
//
//  Fontes de bandeiras:
//    1) SAMP/flags.txt (arquivo local, lido uma vez; /bandeira recarregar)
//    2) servidor via RPC 207 (ver servidor/maximo_flag.inc)
//    3) comando de teste /bandeira ...
//
//  Chamadas:
//    Flag_Update()  1x por quadro, ANTES do Render  -> game/hooks.cpp, RenderEffects()
//    Flag_Render()  1x por quadro, dentro da cena 3D -> game/hooks.cpp, RenderEffects()
//    Flag_OnServerPacket()  -> net/scriptrpc.cpp (RPC 207)
//    Flag_Reset()           -> nova conexao / desconexao (RegisterScriptRPCs / UnregisterScriptRPCs)
// =====================================================================================

namespace FlagOp
{
	enum : uint8_t { Create = 0, Remove = 1, Clear = 2, Wind = 3, Enable = 4, Move = 5 };
}

struct FlagPacket
{
	uint8_t  op = 0;
	int16_t  id = 0;
	float    x = 0, y = 0, z = 0;
	float    yawDeg = 0;          // giro inicial do tecido em torno do mastro (graus, 0 = +X/leste, 90 = +Y/norte)
	float    windDirDeg = 0;      // PARA ONDE o vento sopra (graus, mesma convencao)
	float    windInt = 1.0f;      // 0 = sem vento ... 1 = normal (~6 m/s) ... 4 = tempestade
	uint8_t  flags = 1;           // bit0 = ativa
	uint8_t  interior = 0;
};

void Flag_OnServerPacket(const FlagPacket& p);
void Flag_Reset();                       // remove as bandeiras criadas pelo servidor (as do flags.txt e as de teste ficam)
void Flag_Update();
void Flag_Render();
std::string Flag_Command(const std::string& args);   // "/bandeira ..." -> texto de resposta
