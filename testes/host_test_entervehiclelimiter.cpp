// Teste de host do limitador de entrada em veiculo (cliente). g++ -std=c++17
#include <cstdio>
#include "../app/src/main/cpp/samp/net/EnterVehicleLimiter.h"
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
int main()
{
    EnterVehicleLimiter L;
    CHECK(!L.Allow(1000, 0xFFFF, false) && !L.Allow(1000, 0, true));          // id invalido
    CHECK(L.Allow(1000, 5, true));                                             // 1o pedido passa
    CHECK(!L.Allow(1000, 5, true));                                            // mesmo instante
    CHECK(!L.Allow(1699, 6, false));                                           // outro veiculo mas < 700 ms
    CHECK(L.Allow(1700, 6, false));                                            // >= 700 ms, outro veiculo
    CHECK(!L.Allow(2300, 6, false));                                           // mesmo veiculo/papel < 1500 ms
    CHECK(L.Allow(2400, 6, true));                                             // papel diferente (>=700): passa
    CHECK(L.Allow(4000, 6, true));                                             // mesmo mas >= 1500: passa
    // flood de 200 toques a cada 10 ms durante 2 s: so ~2 passam (700 ms de gap / 1500 ms mesmo veiculo)
    L.Reset(); int ok = 0;
    for (unsigned t = 10000; t < 12000; t += 10) ok += L.Allow(t, 7, true);
    CHECK(ok <= 2 && ok >= 1);
    // rejeitados NAO empurram a janela: depois do flood, 1500 ms apos o ultimo ACEITO libera
    L.Reset(); L.Allow(0 + 5000, 3, true);
    for (unsigned t = 5001; t < 6400; t += 5) L.Allow(t, 3, true);
    CHECK(L.Allow(6500, 3, true));
    // Check nao grava
    L.Reset(); CHECK(L.Check(100, 1, true) && L.Check(100, 1, true) && L.Allow(100, 1, true) && !L.Check(100, 1, true));
    // volta do contador (wrap): tick menor que o anterior nunca trava para sempre
    L.Reset(); L.Allow(4000000000u, 9, false); CHECK(L.Allow(5, 9, false));
    if (g_fail) { std::printf("%d falha(s)\n", g_fail); return 1; }
    std::printf("LIMITADOR: todos os testes passaram\n");
    return 0;
}
