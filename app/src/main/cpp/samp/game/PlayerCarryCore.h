// PlayerCarryCore.h - logica PURA do /carregar no cliente (sem GTA/RakNet): validacao do pacote, maquina de estados por
// geracao e calculo da posicao do carregado. Testada no host (tools/carry_test.cpp). Ver maximo_carregar.inc para o protocolo.
#pragma once
#include <cmath>
#include <cstdint>

namespace carrycore {

constexpr int      kMaxPlayers = 1004;     // = MAX_PLAYERS de net/netgame.h
constexpr int      kMaxActors  = 1000;     // = MAX_ACTORS
constexpr int      kActorBase  = kMaxPlayers; // id de protocolo do actor A = kActorBase + A (so pode ser CARREGADO)
constexpr int      kMaxIds     = kMaxPlayers + kMaxActors;
inline bool IsActorId(uint16_t id) { return id >= kActorBase && id < kMaxIds; }
constexpr uint16_t kNone       = 0xFFFF;
constexpr uint8_t  kVersion    = 1;

enum Op : uint8_t { OP_START = 1, OP_CONFIRM = 2, OP_STOP = 3, OP_REPLAY = 4 };
enum Phase : uint8_t { PH_NONE = 0, PH_PREP = 1, PH_ACTIVE = 2 };

struct Msg { uint8_t ver = 0, op = 0; uint16_t carrier = kNone, carried = kNone, gen = 0; float x = 0, y = 0, z = 0, a = 0; };

inline bool Finite(float v) { return std::isfinite(v); }

// Recusa versao/op/ids/posicao invalidos. x,y,z,a so contam no STOP.
inline bool Validate(const Msg& m) {
    if (m.ver != kVersion) return false;
    if (m.op < OP_START || m.op > OP_REPLAY) return false;
    if (m.carrier >= kMaxPlayers || m.carried >= kMaxIds || m.carrier == m.carried) return false;
    if (m.op == OP_STOP) {
        if (!Finite(m.x) || !Finite(m.y) || !Finite(m.z) || !Finite(m.a)) return false;
        if (std::fabs(m.x) > 20000.f || std::fabs(m.y) > 20000.f || m.z < -1000.f || m.z > 20000.f) return false;
    }
    return true;
}

struct Link { bool used = false; uint16_t carrier = kNone, carried = kNone, gen = 0; Phase phase = PH_NONE; };

enum Result { R_IGNORED, R_STARTED, R_CONFIRMED, R_STOPPED, R_REFRESHED };

// diferenca de geracao com wrap (uint16): >= 0 = igual ou mais nova
inline int GenDiff(uint16_t a, uint16_t b) { return (int)(int16_t)(uint16_t)(a - b); }

class State {
public:
    // indexado pelo carregador; carriedBy[x] = carregador de x (kNone se nao e carregado)
    Link link[kMaxPlayers];
    uint16_t carriedBy[kMaxIds];
    int count = 0;

    State() { Clear(); }
    void Clear() { for (auto& l : link) l = Link(); for (auto& c : carriedBy) c = kNone; count = 0; }

    // Aplica uma mensagem JA validada. Em R_STOPPED, *stopped recebe o vinculo encerrado.
    Result Apply(const Msg& m, Link* stopped) {
        if (!Validate(m)) return R_IGNORED;
        Link& cur = link[m.carrier];
        switch (m.op) {
        case OP_START:
        case OP_REPLAY: {
            if (cur.used && GenDiff(m.gen, cur.gen) < 0) return R_IGNORED;                 // pacote antigo / fora de ordem
            if (cur.used && cur.carried == m.carried && cur.gen == m.gen) {                 // mesmo transporte: nao reinicia a fase
                return R_REFRESHED;
            }
            if (m.op == OP_REPLAY && carriedBy[m.carried] != kNone && carriedBy[m.carried] != m.carrier) return R_IGNORED;   // replay nao despeja outro
            if (cur.used) Remove(m.carrier);                                                // transporte novo do mesmo carregador
            if (carriedBy[m.carried] != kNone) Remove(carriedBy[m.carried]);                // o carregado so tem um carregador
            if (m.carried < kMaxPlayers && link[m.carried].used) Remove(m.carried);                                 // quem carrega nao pode ser carregado
            if (carriedBy[m.carrier] != kNone) Remove(carriedBy[m.carrier]);                // quem e carregado nao pode carregar
            cur.used = true; cur.carrier = m.carrier; cur.carried = m.carried; cur.gen = m.gen;
            cur.phase = (m.op == OP_START) ? PH_PREP : PH_ACTIVE;
            carriedBy[m.carried] = m.carrier; count++;
            return R_STARTED;
        }
        case OP_CONFIRM: {
            if (cur.used && cur.carried == m.carried) {
                if (GenDiff(m.gen, cur.gen) < 0) return R_IGNORED;
                cur.gen = m.gen; cur.phase = PH_ACTIVE; return R_CONFIRMED;
            }
            if (cur.used) return R_IGNORED;
            Msg s = m; s.op = OP_REPLAY; return Apply(s, stopped);                           // START perdido: cria ja ativo
        }
        case OP_STOP: {
            if (!cur.used || cur.carried != m.carried || cur.gen != m.gen) return R_IGNORED; // idempotente / de outra geracao
            if (stopped) *stopped = cur;
            Remove(m.carrier);
            return R_STOPPED;
        }
        }
        return R_IGNORED;
    }

    // remove o vinculo do carregador (sem notificar). Usado em desconexao.
    void Remove(uint16_t carrier) {
        if (carrier >= kMaxPlayers || !link[carrier].used) return;
        uint16_t c = link[carrier].carried;
        if (c < kMaxIds) carriedBy[c] = kNone;
        link[carrier] = Link();
        if (count > 0) count--;
    }
    // vinculo em que `id` participa (como carregador ou carregado); nullptr se nenhum
    const Link* Find(uint16_t id) const {
        if (id >= kMaxIds) return nullptr;
        if (id < kMaxPlayers && link[id].used) return &link[id];
        if (carriedBy[id] != kNone && link[carriedBy[id]].used) return &link[carriedBy[id]];
        return nullptr;
    }
};

// Posicao/orientacao do carregado a partir da matriz do carregador (R,F,U,P). Estilo "bombeiro" do video de referencia:
// de cabeca para baixo sobre o ombro, com os pes inclinados para a FRENTE do carregador e a cabeca/bracos caindo para tras.
// Rotacao em torno do eixo direito: up' = -cos(t)*U - sin(t)*F ; fwd' = up' x R = -cos(t)*F + sin(t)*U (right x fwd = up, det +1).
struct Placement { float right[3], fwd[3], up[3], pos[3]; };
constexpr float kOffX = 0.22f;   // para o lado do ombro (+ = direita do carregador)
constexpr float kOffY = -0.10f;  // para tras
constexpr float kOffZ = 0.50f;   // para cima (a origem do ped fica na altura da cintura)
constexpr float kTiltDeg = 10.0f; // inclinacao: 0 = vertical perfeito; > 0 = pes para a frente

// Valores AJUSTAVEIS em jogo (painel /carregarhud, CarryTuner.cpp). Comecam nos padroes acima; sao lidos por Place().
struct Tuning { float offX = kOffX, offY = kOffY, offZ = kOffZ, tilt = kTiltDeg, roll = 0.0f; int bend = 2; /* -3..3 -> carregado_<x> */ };
inline Tuning& Tune() { static Tuning t; return t; }

inline Placement Place(const float R[3], const float F[3], const float U[3], const float P[3]) {
    Placement o;
    const Tuning& tu = Tune();
    const float t = tu.tilt * 3.14159265f / 180.0f, c = std::cos(t), sn = std::sin(t);
    // (right, up', fwd') ja rotacionados em torno do eixo direito (tilt); depois roll em torno de fwd' (inclina para o lado)
    float up0[3], fw0[3];
    for (int i = 0; i < 3; i++) { up0[i] = -c * U[i] - sn * F[i]; fw0[i] = -c * F[i] + sn * U[i]; }
    const float r = tu.roll * 3.14159265f / 180.0f, cr = std::cos(r), sr = std::sin(r);
    for (int i = 0; i < 3; i++) {
        o.fwd[i]   = fw0[i];
        o.right[i] = cr * R[i] + sr * up0[i];          // rotacao em torno de fwd': right' = cos*R + sin*up ; up'' = cos*up - sin*R
        o.up[i]    = cr * up0[i] - sr * R[i];
        o.pos[i]   = P[i] + R[i] * tu.offX + F[i] * tu.offY + U[i] * tu.offZ;
    }
    return o;
}

} // namespace carrycore
