#include "../app/src/main/cpp/samp/gui/samp_widgets/weapondura_logic.h"
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALHOU linha %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main()
{
	int r, g, b;
	WeaponDura::Color(100, r, g, b); CHECK(r == 70 && g == 200 && b == 90);
	WeaponDura::Color(50, r, g, b);  CHECK(r == 235 && g == 200 && b == 60);
	WeaponDura::Color(0, r, g, b);   CHECK(r == 225 && g == 55 && b == 55);
	WeaponDura::Color(75, r, g, b);  CHECK(r > 70 && r < 235 && g == 200);
	WeaponDura::Color(25, r, g, b);  CHECK(r > 225 && g > 55 && g < 200);
	WeaponDura::Color(-5, r, g, b);  CHECK(r == 225);
	WeaponDura::Color(250, r, g, b); CHECK(r == 70);
	// monotonia: o vermelho nunca aumenta em direcao ao verde
	int prevG = -1; for (int p = 0; p <= 100; p++) { WeaponDura::Color(p, r, g, b); CHECK(g >= 0 && g <= 255 && r >= 0 && r <= 255); if (p <= 50) { CHECK(g >= prevG); prevG = g; } }
	CHECK(WeaponDura::Fill(100) == 1.0f); CHECK(WeaponDura::Fill(0) > 0.0f && WeaponDura::Fill(0) < 0.1f); CHECK(WeaponDura::Fill(50) == 0.5f);
	printf(fails ? "FALHAS: %d\n" : "OK: todos os testes passaram\n", fails);
	return fails ? 1 : 0;
}
