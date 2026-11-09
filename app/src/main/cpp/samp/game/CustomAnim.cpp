// CustomAnim.cpp
#include <cassert>
#include "common.h"      // int32/uint8... (precisa vir antes dos headers de Animation/*, como no AnimManager.cpp)
#include "CustomAnim.h"
#include "IfpLoader.h"

#include "game/Animation/AnimManager.h"
#include "game/Animation/AnimBlendAssociation.h"
#include "game/Plugins/RpAnimBlendPlugin/RpAnimBlend.h"
#include "game/Entity/CPedGTA.h"
#include "../vendor/armhook/patch.h"     // CHook, g_libGTASA
#include "../main.h"                     // FLog

#include <dlfcn.h>
#include <strings.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace CustomAnim {
namespace {

// ---------------------------------------------------------------------------------------------
// SIMBOLOS / OFFSETS DO libGTASA.so (arm64)
// Resolvidos primeiro por dlsym (simbolo exportado). Os offsets abaixo so sao usados como
// fallback e foram lidos da tabela de simbolos do libGTASA.so que esta em jniLibs/arm64-v8a do
// TesteBR-free.zip (sha256 comecando em 12527bfdb63889c0). Se trocar de build do libGTASA,
// reconfira com:  nm -D --defined-only libGTASA.so | grep <simbolo>
// 0 = TODO (nao localizado).
// ---------------------------------------------------------------------------------------------
constexpr const char* SYM_BlendAnimation =
    "_ZN12CAnimManager14BlendAnimationEP7RpClumpP19CAnimBlendHierarchyif";
constexpr uintptr_t OFF_BlendAnimation = 0x466D8C;   // CAnimManager::BlendAnimation(RpClump*, CAnimBlendHierarchy*, int, float)

using BlendAnimationFn = CAnimBlendAssociation* (*)(RpClump*, CAnimBlendHierarchy*, int, float);
BlendAnimationFn s_BlendAnimation = nullptr;
std::vector<int> s_customBlocks;                      // indices em ms_aAnimBlocks

bool Resolve() {
    if (s_BlendAnimation) return true;
    uintptr_t a = CHook::lib ? (uintptr_t)dlsym(CHook::lib, SYM_BlendAnimation) : 0;   // nao usar getSym(): ele chama exit(0) se falhar
    if (!a && OFF_BlendAnimation) a = g_libGTASA + OFF_BlendAnimation;
    if (!a) { FLog("[CustomAnim] BlendAnimation nao localizado (TODO offset)"); return false; }
    s_BlendAnimation = (BlendAnimationFn)a;
    return true;
}

bool IsCustomBlockIndex(int bi) { return std::find(s_customBlocks.begin(), s_customBlocks.end(), bi) != s_customBlocks.end(); }

// true se o IFP so mexe em ossos da parte de cima (sem root/pelvis/pernas) -> overlay seguro
bool IsUpperBodyOnly(CAnimBlendHierarchy* h) {
    bool any = false;
    for (auto& s : h->GetSequences()) {
        if (!s.m_hasBoneIdSet) return false;
        int id = s.m_boneId;
        bool upper = (id >= 2 && id <= 8) || (id >= 21 && id <= 26) || (id >= 31 && id <= 36);
        if (!upper) return false;
        any = true;
    }
    return any;
}

CAnimBlendHierarchy* FindHier(const char* block, const char* anim) {
    CAnimBlock* blk = CAnimManager::GetAnimationBlock(block);
    if (!blk || !blk->bLoaded) return nullptr;
    if (!IsCustomBlockIndex(CAnimManager::GetAnimationBlockIndex(blk))) return nullptr;   // so blocos customizados
    CAnimBlendHierarchy* h = CAnimManager::GetAnimation(anim, blk);
    return (h && h->m_nSeqCount > 0) ? h : nullptr;
}

} // namespace

void RegisterCustomBlock(int bi) { if (!IsCustomBlockIndex(bi)) s_customBlocks.push_back(bi); }

bool IsCustomBlockName(const char* name) {
    if (!name || !*name) return false;
    CAnimBlock* blk = CAnimManager::GetAnimationBlock(name);
    return blk && IsCustomBlockIndex(CAnimManager::GetAnimationBlockIndex(blk));
}

bool Init(const char* ifpPath, const char* blockOverride) {
    Resolve();
    auto r = CustomIfp::LoadFile(ifpPath, blockOverride);
    if (!r.ok) { FLog("[CustomAnim] falha ao carregar %s: %s", ifpPath, r.error.c_str()); return false; }
    FLog("[CustomAnim] bloco '%s' carregado (%d animacoes)", r.block, r.numAnims);
    return true;
}

CAnimBlendAssociation* Play(CPedGTA* ped, const char* block, const char* anim, uint16_t flags, float blendDelta) {
    if (!ped || !ped->m_pRwClump || !Resolve()) return nullptr;
    CAnimBlendHierarchy* h = FindHier(block, anim);
    if (!h) { FLog("[CustomAnim] anim nao encontrada: %s:%s", block, anim); return nullptr; }

    int gf = 0;
    if (flags & CA_LOOP) gf |= ANIMATION_LOOPED;
    if (!(flags & CA_HOLD_LAST)) gf |= ANIMATION_UNLOCK_LAST_FRAME;      // 0x8: faz fade-out ao terminar. Sem ele: congela no ultimo frame.
    if ((flags & CA_PARTIAL) || ((flags & CA_AUTO_PARTIAL) && IsUpperBodyOnly(h))) gf |= ANIMATION_PARTIAL;

    CAnimBlendAssociation* a = s_BlendAnimation(ped->m_pRwClump, h, gf, blendDelta);
    if (!a) return nullptr;
    // BlendAnimation so aplica 'flags' quando CRIA a association; se ja existia, reaplica os de comportamento
    a->SetFlag(ANIMATION_LOOPED, (flags & CA_LOOP) != 0);
    a->SetFlag(ANIMATION_UNLOCK_LAST_FRAME, !(flags & CA_HOLD_LAST));
    return a;
}

// ATENCAO (crash 'Fault address 0x2' ao soltar a pose / fechar o celular): RpAnimBlendClumpGetFirstAssociation(clump) e
// RpAnimBlendGetNextAssociation sao portes de 32 bits (usam ClumpOffset, que nunca e inicializado) e derrubam o jogo em arm64.
// Aqui so se usam as funcoes NATIVAS do libGTASA que o projeto ja usa em outros pontos (busca da association pelo NOME).
static CAnimBlendAssociation* FindAssoc(CPedGTA* ped, const char* anim) {
    CAnimBlendAssociation* a = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, anim);    // (clump, const char* name): 0x46AAF4
    if (!a || !a->m_pHierarchy) return nullptr;
    if (!IsCustomBlockIndex(a->m_pHierarchy->m_nAnimBlockId)) return nullptr;            // nome igual de uma anim do jogo: nao mexer
    return a;
}

static void FadeOut(CAnimBlendAssociation* a, float blendOut) {
    a->m_fBlendDelta = -std::fabs(blendOut);
    a->SetFlag(ANIMATION_FREEZE_LAST_FRAME, true);   // 0x4 (nome do plugin-sdk): remove a association quando o blend chega a 0
}

bool Stop(CPedGTA* ped, const char* block, const char* anim, float blendOut) {
    if (!ped || !ped->m_pRwClump || !anim || !FindHier(block, anim)) return false;
    CAnimBlendAssociation* a = FindAssoc(ped, anim);
    if (!a) return false;
    FadeOut(a, blendOut);
    return true;
}

void StopAll(CPedGTA* ped, float blendOut) {
    if (!ped || !ped->m_pRwClump) return;
    for (int bi : s_customBlocks) {
        if (bi < 0 || bi >= (int)CAnimManager::ms_aAnimBlocks.size()) continue;
        const std::vector<std::string>* names = CustomIfp::GetAnimNames(CAnimManager::ms_aAnimBlocks[bi].szName);
        if (!names) continue;
        for (const std::string& n : *names)
            if (CAnimBlendAssociation* a = FindAssoc(ped, n.c_str())) FadeOut(a, blendOut);
    }
}

bool TryApplyAnimation(CPedGTA* ped, const char* lib, const char* name, float fDelta, int bLoop, int bFreeze) {
    if (!IsCustomBlockName(lib)) return false;
    uint16_t f = CA_AUTO_PARTIAL;
    if (bLoop)   f |= CA_LOOP;
    if (bFreeze) f |= CA_HOLD_LAST;
    Play(ped, lib, name, f, fDelta > 0.f ? fDelta : 4.0f);
    return true;                                    // tratado (mesmo se falhar): nunca cair no script command com bloco customizado
}

} // namespace CustomAnim
