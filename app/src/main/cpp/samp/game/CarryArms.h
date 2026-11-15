// CarryArms.h - pose dos BRACOS do carregador, parametrica (logica pura, testada no host).
// Base = quaternions do poses.ifp (bracos_cruzados: maos abracando o corpo carregado no peito) + deltas em graus por osso (editados
// no painel /carregarhud, aba CARREGADOR). O resultado e escrito DENTRO da animacao carregar.ifp/carregador (CustomAnim::SetBoneRotation).
#pragma once
#include <cmath>

namespace carryarms {

constexpr int kBones = 6;
constexpr int kBoneId[kBones] = { 22, 23, 24, 32, 33, 34 };                      // R UpperArm, R ForeArm, R Hand, L UpperArm, L ForeArm, L Hand
inline const char* BoneName(int i) {
    static const char* n[kBones] = { "Braco D (ombro)", "Braco D (cotovelo)", "Mao D", "Braco E (ombro)", "Braco E (cotovelo)", "Mao E" };
    return n[(i < 0 || i >= kBones) ? 0 : i];
}
// x,y,z,w  (poses.ifp: bracos_cruzados para os dois bracos)
constexpr float kBase[kBones][4] = {
    { 0.162381f, -0.320239f, -0.508925f, 0.782352f },     // R UpperArm
    { -0.068706f, -0.432008f, -0.593446f, 0.675626f },    // R ForeArm
    { 0.797800f, -0.003581f, 0.067776f, 0.599090f },      // R Hand
    { -0.176814f, 0.309550f, -0.506787f, 0.784909f },     // L UpperArm
    { 0.130042f, 0.514342f, -0.592379f, 0.606323f },      // L ForeArm
    { -0.808027f, -0.083431f, 0.044158f, 0.581534f },     // L Hand
};

// Pose padrao dos bracos (graus x,y,z sobre kBase), calculada por IK: maos seguram coxa/quadril do carregado.
inline const float (&DefaultDeg())[kBones][3] {
    static const float d[kBones][3] = {{4.9f,29.3f,33.1f},{0.f,0.2f,-1.6f},{0.f,0.f,0.f},{2.5f,39.8f,-17.8f},{31.9f,0.9f,89.1f},{0.f,0.f,0.f}};
    return d;
}
struct Deltas { float deg[kBones][3]; Deltas() { for (int i = 0; i < kBones; i++) for (int k = 0; k < 3; k++) deg[i][k] = DefaultDeg()[i][k]; } };                                      // x,y,z por osso
inline Deltas& Arms() { static Deltas d; return d; }

inline void QMul(const float* a, const float* b, float* o) {                      // (x,y,z,w) a*b
    o[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    o[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    o[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    o[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}
inline void QAxis(float deg, int ax, float* q) {
    const float h = deg * 3.14159265f / 360.0f, s = std::sin(h);
    q[0] = q[1] = q[2] = 0.0f; q[ax] = s; q[3] = std::cos(h);
}
// quaternion final do osso i = base * Rx * Ry * Rz (mesma ordem do tools/make_carregar_ifp.py: qeul)
inline void Compute(int i, float* out) {
    float qx[4], qy[4], qz[4], t[4], u[4];
    QAxis(Arms().deg[i][0], 0, qx); QAxis(Arms().deg[i][1], 1, qy); QAxis(Arms().deg[i][2], 2, qz);
    QMul(kBase[i], qx, t); QMul(t, qy, u); QMul(u, qz, out);
    const float n = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
    for (int k = 0; k < 4; k++) out[k] /= (n > 1e-8f ? n : 1.0f);
}

} // namespace carryarms
