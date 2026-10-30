// Teste de host da logica pura do botao P2 (passageiro). g++ -std=c++17 -I app/src/main/cpp/samp/gui/samp_widgets
#include <cstdio>
#include <cmath>
#include "../app/src/main/cpp/samp/gui/samp_widgets/passengerbutton_logic.h"
using namespace PassengerLogic;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
int main()
{
    const float W = 1600, H = 720;
    // visibilidade
    CHECK(ShouldShow(true, false, false, true, 3.0f));
    CHECK(ShouldShow(true, false, false, true, kShowDist));
    CHECK(!ShouldShow(true, false, false, true, kShowDist + 0.1f));      // longe
    CHECK(!ShouldShow(true, false, false, true, -1.0f));                 // sem veiculo
    CHECK(!ShouldShow(true, false, true, true, 1.0f));                   // ja dentro do carro
    CHECK(!ShouldShow(true, true, false, true, 1.0f));                   // dialogo/teclado
    CHECK(!ShouldShow(true, false, false, false, 1.0f));                 // morto
    CHECK(!ShouldShow(false, false, false, true, 1.0f));                 // sem ped
    // retangulo nativo valido/invalido
    CHECK(NativeRectOk(1480, 360, 1560, 440, W, H));
    CHECK(!NativeRectOk(1560, 360, 1480, 440, W, H));                    // invertido
    CHECK(!NativeRectOk(NAN, 0, 10, 10, W, H));
    CHECK(!NativeRectOk(-500, -500, -10, -10, W, H));                    // fora
    CHECK(!NativeRectOk(0, 0, W, H, W, H));                              // tela inteira = lixo
    // posicao: com o nativo, P2 fica a ESQUERDA dele, mesma altura, dentro da tela, sem sobrepor
    Circle c = Place(true, 1480, 360, 1560, 440, W, H);
    CHECK(c.cx < 1480 - c.r && std::fabs(c.cy - 400) < 0.01f);
    CHECK(c.cx - c.r > 0 && c.cy - c.r > 0 && c.cx + c.r < W && c.cy + c.r < H);
    // sem nativo: ponto medido, tambem dentro da tela e a esquerda do botao do carro (~0.947w)
    c = Place(false, 0, 0, 0, 0, W, H);
    CHECK(c.cx < W * 0.947f - c.r && c.cx - c.r > 0 && c.cy + c.r < H);
    // nativo colado na borda esquerda: clamp
    c = Place(true, 0, 360, 60, 420, W, H);
    CHECK(c.cx - c.r >= 0);
    // area de toque
    c = Place(true, 1480, 360, 1560, 440, W, H);
    CHECK(Hit(c, c.cx, c.cy) && Hit(c, c.cx + c.r * 1.2f, c.cy));
    CHECK(!Hit(c, c.cx + c.r * 1.6f, c.cy) && !Hit(c, 1520, 400));       // nao pega o botao nativo do carro
    if (g_fail) { std::printf("%d falha(s)\n", g_fail); return 1; }
    std::printf("PASSAGEIRO: todos os testes passaram\n");
    return 0;
}
