// g++ -std=c++14 -I app/src/main/cpp/samp/game testes/host_test_damagerules.cpp -o /tmp/dmg && /tmp/dmg
#include "DamageRules.h"
#include <cstdio>
int main()
{
	int fail = 0, n = 0;
	auto chk = [&](bool c, const char* t) { n++; if (!c) { fail++; std::printf("FALHOU: %s\n", t); } };
	using namespace DamageRules;
	// (agressorLocal, vitimaLocal, agressorEJogadorRemoto)
	chk(!Ignore(true,  false, false), "eu atiro em outro: segue (SendGiveDamage)");
	chk(!Ignore(true,  true,  false), "eu me machuco (queda/explosao com agressor local): segue");
	chk(!Ignore(false, true,  true),  "JOGADOR REMOTO ME ACERTA: tem que seguir (antes era ignorado = bug)");
	chk( Ignore(false, true,  false), "ped nao-jogador (civil) me acerta: ignora");
	chk( Ignore(false, false, true),  "remoto acerta outro remoto: ignora (nao mexe no ped remoto)");
	chk( Ignore(false, false, false), "ninguem local envolvido: ignora");
	// o que mudou em relacao a regra antiga: exatamente UMA combinacao
	int changed = 0;
	for (int a = 0; a < 2; a++) for (int v = 0; v < 2; v++) for (int p = 0; p < 2; p++)
		if (Ignore(a, v, p) != IgnoreOld(a, v, p)) { changed++; chk(!a && v && p, "so remoto->local pode ter mudado"); }
	chk(changed == 1, "exatamente 1 combinacao mudou");
	std::printf("%d verificacoes, %d falhas\n", n, fail);
	return fail ? 1 : 0;
}
