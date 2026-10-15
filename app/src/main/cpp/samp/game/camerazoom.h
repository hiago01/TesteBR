#pragma once
#include <string>

// =====================================================================================
//  Zoom da camera (gesto de pinca com 2 dedos) -> desativado, com nivel salvo + comando
//
//  libGTASA.so (arm64), verificado no binario:
//    CCamera::CamControl() chama CTouchInterface::IsPinchZooming(0xAF = regiao "olhar")
//    e altera dois niveis inteiros dentro do TheCamera (clamp [1.0, 3.0]):
//        TheCamera + 0xCC : zoom A PE      (lido por CCam::Process_FollowPed_SA)
//        TheCamera + 0xB8 : zoom de VEICULO (lido por CCam::Process_FollowCar_SA)
//
//  O que este modulo faz:
//    - Init()    : hook em CTouchInterface::IsPinchZooming -> a pinca NAO muda mais o zoom
//    - Process() : 1x por frame; aplica o nivel salvo (a pe e veiculo)
//    - settings  : [camera] ped_zoom / car_zoom / pinch em SAMP/settings.ini
//    - comando   : /camera  (ver DebugCommand)
// =====================================================================================
namespace CCameraZoom
{
	void Init();                                          // instala o hook (1x, no InstallHooks)
	void Process();                                       // chamar 1x por frame
	std::string DebugCommand(const std::string& args);    // /camera ...
}
