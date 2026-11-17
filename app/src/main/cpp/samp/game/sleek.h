#pragma once
#include <cstdint>
#include <string>

// =====================================================================================
//  SleekMovement (recriacao para o cliente Android) - integracao com o jogo.
//  Logica pura: game/sleek_logic.h (testada no PC).  Config: SAMP/sleek.ini (mesmos nomes de secao/chave do
//  SleekMovement.ini original; criada com os padroes se nao existir - NUNCA sobrescrita depois).
//  Modulos: tremor de impacto reescrito, tremor ocioso, FOV de corrida/sprint/pitch, inercia (momentum),
//  agachado mantido com sprint.   NAO portados: camera de ombro, lean/pitch do corpo, head tracking, foot IK, blends de mira.
// =====================================================================================
namespace SleekMove
{
	// Envolve a CCamera::Process do jogo (chame no hook): reescreve o tremor, chama o original, aplica o tremor ocioso.
	void CameraProcess(void (*orig)(uintptr_t), uintptr_t thiz);

	// Graus a somar ao FOV (calculado 1x por quadro em CameraProcess). 0 quando desligado/inativo.
	float FovDelta();

	// Inercia: molda o stick local (+-128) ja pronto. Seguro de chamar varias vezes por quadro.
	void ApplyStick(int16_t& lr, int16_t& ud);

	// Agachado: com o sprint apertado o motor levanta o personagem; aqui o sprint e suprimido enquanto agachado.
	uint32_t FilterSprint(uint32_t sprint);

	// Hook de postura/foot IK (RpHAnimHierarchyUpdateMatrices): chamar 1x em InjectHooks.
	void InstallPoseHook();

	// "/sleek ..." -> texto de resposta
	std::string Command(const std::string& args);
}
