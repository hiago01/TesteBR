// CustomAnim.h - PlayCustomAnim sobre CAnimManager::BlendAnimation (libGTASA arm64)
#pragma once
#include <cstdint>

class CPedGTA;
class CAnimBlendAssociation;

namespace CustomAnim {

// Flags proprias (tambem sao as do fio na sincronizacao). Nao confundir com eAnimationFlags do jogo.
enum Flags : uint16_t {
    CA_LOOP         = 1 << 0,   // repete
    CA_HOLD_LAST    = 1 << 1,   // nao-loop: congela no ultimo frame (pose fixa) em vez de dar fade-out
    CA_PARTIAL      = 1 << 2,   // overlay: so os ossos presentes no IFP; o resto segue idle/andar/correr
    CA_AUTO_PARTIAL = 1 << 3,   // liga PARTIAL sozinho se o IFP so tem ossos da parte de cima (spine/pescoco/cabeca/bracos)
};

// Carrega o .ifp (ANP3/ANP2/ANPK) como bloco customizado. Chamar UMA vez, depois que o jogo carregou ped.ifp.
bool Init(const char* ifpPath, const char* blockNameOverride = nullptr);

void RegisterCustomBlock(int animBlockIndex);        // usado pelo loader
bool IsCustomBlockName(const char* name);

CAnimBlendAssociation* Play(CPedGTA* ped, const char* block, const char* anim, uint16_t flags, float blendDelta);
bool Stop(CPedGTA* ped, const char* block, const char* anim, float blendOut = 4.0f);
void StopAll(CPedGTA* ped, float blendOut = 4.0f);   // para todas as anims de blocos customizados deste ped

// Para integrar no ApplyAnimation existente. Retorna true se 'lib' e um bloco customizado (tratado aqui).
bool TryApplyAnimation(CPedGTA* ped, const char* lib, const char* name, float fDelta, int bLoop, int bFreeze);

} // namespace CustomAnim
