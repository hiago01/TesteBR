#include "NickWire.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
static void t(const char* in, const char* want) {
    char b[64]; strncpy(b, in, 63); b[63] = 0; NickToWire(b);
    bool ok = strcmp(b, want) == 0; if (!ok) fails++;
    std::printf("%s '%s' -> '%s'%s\n", ok ? "PASS" : "FAIL", in, b, ok ? "" : " (esperado)");
}
int main() {
    t("Jose Luis", "Jose_Luis"); t("  Jose   Luis  ", "Jose_Luis"); t("Nome_Sobrenome", "Nome_Sobrenome");
    t("A B C", "A_B_C"); t("", ""); t("   ", ""); t("Jose\tLuis", "Jose_Luis"); t("A _B", "A__B");
    std::printf(fails ? "nickwire: %d falha(s)\n" : "nickwire: OK\n", fails);
    return fails ? 1 : 0;
}
