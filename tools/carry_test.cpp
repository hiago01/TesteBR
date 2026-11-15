// g++ -std=c++17 -I app/src/main/cpp/samp/game tools/carry_test.cpp -o /tmp/carry_test && /tmp/carry_test
#include "PlayerCarryCore.h"
#include <cstdio>
#include <cmath>
using namespace carrycore;
static int ok = 0, bad = 0;
#define CHECK(c) do { if (c) ok++; else { bad++; printf("FALHOU linha %d: %s\n", __LINE__, #c); } } while (0)
static Msg M(uint8_t op, uint16_t a, uint16_t b, uint16_t g, float x = 0, float y = 0, float z = 0, float an = 0) {
    Msg m; m.ver = 1; m.op = op; m.carrier = a; m.carried = b; m.gen = g; m.x = x; m.y = y; m.z = z; m.a = an; return m;
}
int main() {
    // validacao
    CHECK(Validate(M(1, 1, 2, 5)));
    { Msg m = M(1, 1, 2, 5); m.ver = 2; CHECK(!Validate(m)); }
    CHECK(!Validate(M(0, 1, 2, 5)) && !Validate(M(5, 1, 2, 5)));
    CHECK(!Validate(M(1, 1004, 2, 5)) && !Validate(M(1, 1, 2004, 5)) && !Validate(M(1, 3, 3, 5)));
    CHECK(Validate(M(1, 1, 1004, 5)) && Validate(M(1, 1, 2003, 5)));   // actor 0 e actor 999 como carregado
    CHECK(!Validate(M(1, 1004, 2, 5)));                               // actor nao carrega
    CHECK(!Validate(M(3, 1, 2, 5, NAN, 0, 0, 0)) && !Validate(M(3, 1, 2, 5, 99999.f, 0, 0, 0)) && !Validate(M(3, 1, 2, 5, 0, 0, -5000.f, 0)));
    CHECK(Validate(M(3, 1, 2, 5, 100.f, -200.f, 13.5f, 90.f)));
    CHECK(Validate(M(1, 1, 2, 5, NAN, 0, 0, 0)));      // x,y,z so contam no STOP

    State s; Link st;
    CHECK(s.Apply(M(1, 10, 20, 1), &st) == R_STARTED && s.count == 1);
    CHECK(s.Find(10) && s.Find(20) && !s.Find(30));
    CHECK(s.link[10].phase == PH_PREP && s.carriedBy[20] == 10);
    CHECK(s.Apply(M(2, 10, 20, 1), &st) == R_CONFIRMED && s.link[10].phase == PH_ACTIVE);
    CHECK(s.Apply(M(4, 10, 20, 1), &st) == R_REFRESHED && s.link[10].phase == PH_ACTIVE);   // replay nao volta a fase
    // pacote antigo / fora de ordem
    CHECK(s.Apply(M(1, 10, 20, 0), &st) == R_IGNORED);
    // STOP de outra geracao e ignorado; o da geracao certa encerra uma unica vez
    CHECK(s.Apply(M(3, 10, 20, 7, 1, 2, 3, 4), &st) == R_IGNORED && s.count == 1);
    CHECK(s.Apply(M(3, 10, 20, 1, 1, 2, 3, 4), &st) == R_STOPPED && st.carrier == 10 && st.carried == 20 && s.count == 0);
    CHECK(s.Apply(M(3, 10, 20, 1, 1, 2, 3, 4), &st) == R_IGNORED);                           // idempotente
    CHECK(!s.Find(10) && !s.Find(20) && s.carriedBy[20] == kNone);
    // STOP antigo nao encerra transporte novo (mesma dupla, geracao nova)
    CHECK(s.Apply(M(1, 10, 20, 2), &st) == R_STARTED);
    CHECK(s.Apply(M(3, 10, 20, 1, 0, 0, 0, 0), &st) == R_IGNORED && s.Find(10));
    // CONFIRM sem START (perdido): cria ja ativo
    State t; CHECK(t.Apply(M(2, 5, 6, 3), &st) == R_STARTED && t.link[5].phase == PH_ACTIVE);
    // REPLAY (stream-in) cria ativo e nao despeja outro carregador
    State u; CHECK(u.Apply(M(4, 5, 6, 3), &st) == R_STARTED && u.link[5].phase == PH_ACTIVE);
    CHECK(u.Apply(M(4, 7, 6, 4), &st) == R_IGNORED && u.carriedBy[6] == 5);
    // START novo do outro carregador despeja o antigo (servidor garante exclusividade)
    CHECK(u.Apply(M(1, 7, 6, 4), &st) == R_STARTED && u.carriedBy[6] == 7 && !u.link[5].used && u.count == 1);
    // quem carrega nao vira carregado e vice-versa
    State v; v.Apply(M(1, 1, 2, 1), &st);
    CHECK(v.Apply(M(1, 3, 1, 1), &st) == R_STARTED && !v.link[1].used && v.carriedBy[1] == 3 && v.count == 1);
    // wrap de geracao
    State w; w.Apply(M(1, 1, 2, 65535), &st);
    CHECK(w.Apply(M(1, 1, 2, 0), &st) == R_STARTED && w.link[1].gen == 0);                   // 0 e mais novo que 65535
    CHECK(w.Apply(M(1, 1, 2, 65535), &st) == R_IGNORED);
    // Remove (desconexao) e Clear
    State x; x.Apply(M(1, 1, 2, 1), &st); x.Remove(1); CHECK(x.count == 0 && !x.Find(2)); x.Remove(1); CHECK(x.count == 0);
    x.Apply(M(1, 1, 2, 1), &st); x.Clear(); CHECK(x.count == 0 && !x.Find(1));

    // actor como carregado
    { State a; CHECK(a.Apply(M(1, 3, 1004, 1), &st) == R_STARTED && a.carriedBy[1004] == 3 && a.Find(1004) && a.Find(3));
      CHECK(a.Apply(M(2, 3, 1004, 1), &st) == R_CONFIRMED);
      CHECK(a.Apply(M(1, 4, 1004, 2), &st) == R_STARTED && !a.link[3].used && a.carriedBy[1004] == 4);   // outro jogador pega o boneco
      CHECK(a.Apply(M(3, 4, 1004, 2, 1, 2, 3, 4), &st) == R_STOPPED && a.carriedBy[1004] == kNone && a.count == 0);
      CHECK(a.Apply(M(3, 4, 1004, 2, 1, 2, 3, 4), &st) == R_IGNORED); }
    // posicao: carregador olhando para +Y (R=+X, F=+Y, U=+Z)
    float R[3] = {1,0,0}, F[3] = {0,1,0}, U[3] = {0,0,1}, P[3] = {100,200,10};
    Placement p = Place(R, F, U, P);
    CHECK(fabsf(p.pos[0] - (100 + kOffX)) < 1e-5f && fabsf(p.pos[1] - (200 + kOffY)) < 1e-5f && fabsf(p.pos[2] - (10 + kOffZ)) < 1e-5f);
    CHECK(p.up[2] < -0.85f && p.right[0] == 1.f);                                             // de cabeca para baixo
    CHECK(p.up[1] < 0.f && p.fwd[1] < 0.f && p.fwd[2] > 0.f);                                // cabeca para tras, pes para a frente (tilt)
    CHECK(fabsf(p.up[0]*p.up[0]+p.up[1]*p.up[1]+p.up[2]*p.up[2]-1.f) < 1e-5f && fabsf(p.fwd[0]*p.fwd[0]+p.fwd[1]*p.fwd[1]+p.fwd[2]*p.fwd[2]-1.f) < 1e-5f);
    // a rotacao e propria (det = +1): right x fwd = up
    auto cross = [](const float* a, const float* b, float* o) { o[0] = a[1]*b[2]-a[2]*b[1]; o[1] = a[2]*b[0]-a[0]*b[2]; o[2] = a[0]*b[1]-a[1]*b[0]; };
    float c[3]; cross(p.right, p.fwd, c); CHECK(fabsf(c[0]-p.up[0]) < 1e-5f && fabsf(c[1]-p.up[1]) < 1e-5f && fabsf(c[2]-p.up[2]) < 1e-5f);
    // carregador girado 90 graus (frente = -X): o carregado acompanha a rotacao
    float R2[3] = {0,1,0}, F2[3] = {-1,0,0};
    Placement q = Place(R2, F2, U, P);
    CHECK(fabsf(q.pos[0] - (100 - kOffY)) < 1e-5f && fabsf(q.pos[1] - (200 + kOffX)) < 1e-5f);
    printf("%d ok, %d falha(s)\n", ok, bad);
    return bad ? 1 : 0;
}
