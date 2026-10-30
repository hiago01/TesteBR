#pragma once
// =====================================================================================
//  RadarSkin - troca os 144 tiles do radar/mapa NATIVO (radar00..radar143, 12x12, 500 un cada) por PNG
//  (ex.: pacote "SA Google Maps Radar"), SEM mexer no desenho do jogo.
//
//  COMO O JOGO FAZ (libGTASA 2.10 arm64, conferido no desassembly):
//    CRadar::StreamRadarSections -> CRadar::RequestMapSection(x, y)
//        idx = y*12 + x  (y = 0 e a linha NORTE);  se gRadarTextures[idx] == 0:
//        TextureDatabaseRuntime::GetDatabase("gta3") -> Register -> snprintf("radar%02d", idx) -> GetTexture
//        -> gRadarTextures[idx] (refCount+1).   CRadar::RemoveMapSection faz RwTextureDestroy e zera.
//    CRadar::DrawRadarSection(x,y,..) desenha gRadarTextures[idx]; fora do mundo pinta RGB(111,137,170).
//    O MESMO array serve o minimapa do HUD e o mapa do menu de pausa.
//  O QUE FAZEMOS:
//    * hook em RequestMapSection: se existir SAMP/radar/tiles/radarNN.png, cria um RwTexture (refCount 1) e o poe no
//      array; senao chama o ORIGINAL (tile nativo). Nada mais e alterado: blips, rotacao, zoom, mascara = jogo.
//    * cor do "fora do mundo" (oceano) 111,137,170 -> 143,189,240 (igual ao oceano dos tiles), 3 instrucoes MOVZ.
//    * /radarskin on|off|reload|info : liga/desliga ao vivo (RemoveRadarSections faz o jogo recarregar).
//  Arquivos:  files/SAMP/radar/tiles/radar00.png .. radar143.png (512x512, tools_radar_convert.py)
//             files/SAMP/radar/radarskin.ini   [radarskin] enabled=1  ocean=143,189,240
// =====================================================================================
#include <string>

namespace RadarSkin
{
	void InstallHooks();                       // chamar depois do CHook estar pronto (junto dos hooks do CRadar)
	bool Enabled();
	bool HasTiles();                           // existe pelo menos radar00.png
	std::string Command(const std::string& args);   // /radarskin ...
	// textura reduzida (256x256) para a interface do mapa aberto; nullptr = usar o tile nativo. So a thread de desenho.
	void* UiTileRaster(int idx);
}
