#include "../main.h"
#include "game.h"
#include "../vendor/armhook/patch.h"
#include "Camera.h"
#include "Cam.h"
#include "World.h"
#include "Collision/ColPoint.h"
#include "camguard.h"
#include "camguard_logic.h"

#include <atomic>
#include <cstddef>
#include <cstdio>

// o offset de Source e o que o libGTASA usa (ver camguard.h): falha de compilacao se o Cam.h mudar
static_assert(offsetof(CCam, Front) == 0x168, "CCam::Front deve estar em +0x168 (libGTASA arm64)");
static_assert(offsetof(CCam, Source) == 0x174, "CCam::Source deve estar em +0x174 (libGTASA arm64)");

namespace
{
    using namespace CamGuardLogic;

    std::atomic<bool> g_on{true};
    unsigned g_calls = 0, g_wall = 0, g_ground = 0, g_logs = 0;

    void (*FollowCar_orig)(CCam*, const CVector*, float, float, float, bool) = nullptr;
    void (*FollowPed_orig)(CCam*, const CVector*, float, float, float, bool) = nullptr;

    void Guard(CCam* cam, const CVector* tgt, bool car)
    {
        if (!g_on.load(std::memory_order_relaxed) || !cam || !tgt) return;
        if (cam != &CCamera::GetActiveCamera()) return;            // so a camera ativa (nao mexe em transicoes/outras)
        ++g_calls;

        const V3 target{ tgt->x, tgt->y, tgt->z };
        V3 src{ cam->Source.x, cam->Source.y, cam->Source.z };

        // predios, objetos e dummies; sem veiculos/peds; ignora superficies "ver-atraves" e objetos marcados "camera ignora" (como o jogo)
        auto los = [](const V3& a, const V3& b) -> Hit {
            const CVector o(a.x, a.y, a.z), e(b.x, b.y, b.z);
            CColPoint cp; CEntityGTA* ent = nullptr;
            if (CWorld::ProcessLineOfSight(&o, &e, &cp, &ent, true, false, false, true, true, true, true, false))
                return Hit{ true, V3{ cp.m_vecPoint.x, cp.m_vecPoint.y, cp.m_vecPoint.z } };
            return Hit{ false, V3{ 0, 0, 0 } };
        };

        float full = 0.0f, nw = 0.0f;
        const int m = Apply(target, src, car, los, &full, &nw);
        if (!m || !Finite(src)) return;

        cam->Source.x = src.x; cam->Source.y = src.y; cam->Source.z = src.z;
        if (m & 1) ++g_wall;
        if (m & 2) ++g_ground;
        if (g_logs < 16)
        {
            ++g_logs;
            FLog("[CAMGUARD] %s: %s%s (dist %.2f -> %.2f, z=%.2f)", car ? "veiculo" : "a pe",
                 (m & 1) ? "camera recuada por obstaculo" : "", (m & 2) ? " | subiu do chao" : "", full, nw, src.z);
        }
    }

    void FollowCar_hook(CCam* self, const CVector* target, float a, float b, float c, bool d)
    {
        FollowCar_orig(self, target, a, b, c, d);
        Guard(self, target, true);
    }
    void FollowPed_hook(CCam* self, const CVector* target, float a, float b, float c, bool d)
    {
        FollowPed_orig(self, target, a, b, c, d);
        Guard(self, target, false);
    }
}

void CamGuard::Init()
{
    CHook::InlineHook("_ZN4CCam20Process_FollowCar_SAERK7CVectorfffb", &FollowCar_hook, &FollowCar_orig);
    CHook::InlineHook("_ZN4CCam20Process_FollowPed_SAERK7CVectorfffb", &FollowPed_hook, &FollowPed_orig);
    FLog("[CAMGUARD] hooks instalados (colisao da camera de seguir: parede + chao)");
}

void CamGuard::SetEnabled(bool on) { g_on.store(on); }
bool CamGuard::Enabled() { return g_on.load(); }

std::string CamGuard::Status()
{
    char b[200];
    snprintf(b, sizeof(b), "[CAMERA] guarda de colisao %s | quadros=%u parede=%u chao=%u", g_on.load() ? "LIGADO" : "desligado", g_calls, g_wall, g_ground);
    return b;
}
