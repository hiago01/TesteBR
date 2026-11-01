#include "../../app/src/main/cpp/samp/gui/samp_widgets/voicebutton_logic.h"
#include <cstdio>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FALHA linha %d: %s\n", __LINE__, #c); } } while (0)
int main()
{
    { VoiceHold h; h.OnPush(); CHECK(h.holding); CHECK(!h.OnMove(true)); CHECK(h.holding); CHECK(h.OnPop()); CHECK(!h.holding); }   // aperta e solta dentro
    { VoiceHold h; h.OnPush(); CHECK(h.OnMove(false)); CHECK(!h.holding); CHECK(!h.OnPop()); }                                      // arrasta para fora (ainda apertando): desliga
    { VoiceHold h; h.OnPush(); CHECK(h.OnMove(false)); CHECK(!h.OnMove(true)); CHECK(!h.holding); }                                 // voltar para dentro NAO reativa
    { VoiceHold h; h.OnPush(); CHECK(h.OnPop()); CHECK(!h.OnPop()); }                                                               // soltar fora: desliga uma vez so
    { VoiceHold h; CHECK(!h.OnMove(false)); CHECK(!h.OnPop()); CHECK(!h.holding); }                                                 // sem toque no icone: nada
    { VoiceHold h; h.OnPush(); h.Reset(); CHECK(!h.holding); h.OnPush(); CHECK(h.holding); }                                        // novo toque reativa
    std::printf("%d checks, %d falhas\n", checks, fails);
    return fails ? 1 : 0;
}
