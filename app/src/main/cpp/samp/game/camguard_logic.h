#pragma once
// ---------------------------------------------------------------------------------------------
// Guarda de colisao da camera (logica PURA, sem engine: compila e e testada no host).
// A camera de seguir (a pe / veiculo) do jogo as vezes atravessa parede e CHAO (visto em video: a camera cai
// abaixo do piso e a tela fica so com a textura do chao). O guarda roda DEPOIS do calculo da camera do jogo e:
//   1) lanca um raio do alvo (jogador/veiculo) ate a camera; se bater em algo, recua a camera ate o ponto de impacto;
//   2) se a camera ainda ficou abaixo do chao, sobe-a para chao + folga.
// ---------------------------------------------------------------------------------------------
#include <cmath>
#include <cstdint>

namespace CamGuardLogic
{
    struct V3 { float x, y, z; };

    inline bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    inline float Dist(const V3& a, const V3& b) { const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }

    constexpr float kMinCamDist = 0.60f;    // nunca recua a camera para mais perto que isso do alvo
    constexpr float kMaxCamDist = 40.0f;    // acima disso e camera de cinematica/teleporte: nao mexe
    constexpr float kMargin     = 0.40f;    // folga entre o ponto de impacto e a camera (plano de corte do render)
    constexpr float kGroundClear = 0.35f;   // altura minima da camera sobre o chao

    // Segmento do raio: comeca a `startOffset` metros do alvo (sai de dentro do veiculo e dos objetos presos a ele,
    // como giroflex/tuning) e termina na camera. false = nada a fazer (camera muito perto/longe ou valores invalidos).
    inline bool PlanRay(const V3& target, const V3& cam, float startOffset, V3& outStart, V3& outEnd, float& outFullDist)
    {
        if (!Finite(target) || !Finite(cam)) return false;
        const float d = Dist(target, cam);
        outFullDist = d;
        if (!(d > kMinCamDist + startOffset + 0.05f) || d > kMaxCamDist) return false;
        const float k = startOffset / d;
        outStart = { target.x + (cam.x - target.x) * k, target.y + (cam.y - target.y) * k, target.z + (cam.z - target.z) * k };
        outEnd = cam;
        return true;
    }

    // Distancia final (alvo -> camera) depois de um impacto a `hitDist` do alvo.
    inline float DistAfterHit(float hitDist, float fullDist)
    {
        float nd = hitDist - kMargin;
        if (nd < kMinCamDist) nd = kMinCamDist;
        if (nd > fullDist) nd = fullDist;
        return nd;
    }

    inline V3 PointAlong(const V3& target, const V3& cam, float newDist)
    {
        const float d = Dist(target, cam);
        if (!(d > 1e-4f)) return cam;
        const float k = newDist / d;
        return { target.x + (cam.x - target.x) * k, target.y + (cam.y - target.y) * k, target.z + (cam.z - target.z) * k };
    }

    // Camera abaixo (ou rente) do chao encontrado debaixo dela? Retorna o novo Z.
    inline bool PushAboveGround(float camZ, float groundZ, float& newZ)
    {
        if (!std::isfinite(camZ) || !std::isfinite(groundZ)) return false;
        if (camZ >= groundZ + kGroundClear) return false;
        newZ = groundZ + kGroundClear;
        return true;
    }

    struct Hit { bool hit; V3 p; };

    // Algoritmo completo. `los(a, b)` -> Hit: primeiro impacto no segmento a->b (predios/objetos; sem veiculos/peds).
    // Retorna bitmask: 1 = recuou por obstaculo, 2 = subiu por estar abaixo do chao. `src` e ajustado no lugar
    // (so desliza ao longo da linha alvo->camera, exceto a subida vertical do chao: Front/Up continuam validos).
    // outFull recebe a distancia original alvo->camera (para log).
    template <class Los>
    inline int Apply(const V3& target, V3& src, bool car, Los&& los, float* outFull = nullptr, float* outNew = nullptr)
    {
        if (!Finite(target) || !Finite(src)) return 0;
        int mask = 0;
        {
            V3 a, b; float full = 0.0f;
            if (PlanRay(target, src, car ? 1.8f : 0.35f, a, b, full))
            {
                const Hit h = los(a, b);
                if (h.hit && Finite(h.p))
                {
                    const float nd = DistAfterHit(Dist(target, h.p), full);
                    if (nd < full - 0.02f)
                    {
                        src = PointAlong(target, src, nd);
                        mask |= 1;
                        if (outFull) *outFull = full;
                        if (outNew) *outNew = nd;
                    }
                }
            }
        }
        if (src.z < target.z + 1.0f)
        {
            const Hit h = los(V3{ src.x, src.y, target.z + 1.0f }, V3{ src.x, src.y, src.z - 1.5f });
            float nz;
            if (h.hit && PushAboveGround(src.z, h.p.z, nz)) { src.z = nz; mask |= 2; if (outNew) *outNew = nz; }
        }
        return mask;
    }
}
