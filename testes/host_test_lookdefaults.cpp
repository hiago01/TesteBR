// Testa (host, puro) o visual PADRAO e o plano com padroes explicitos: um slot que o servidor NAO cita vira "ocultar",
// para o ped remoto nunca herdar a roupa que esta no molde (= a do jogador local).
#include <cstdio>
#include <set>
#include <string>
#include "../app/src/main/cpp/samp/game/LookSlots.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main()
{
    using namespace LookSlots;
    const std::vector<St> def = DefaultSlots();
    CHECK(def.size() == 7);
    std::set<std::string> names;
    for (const St& s : def) { CHECK(s.est == kPack && s.var == SkinSlots::CanonVar(SkinSlots::kSlots[s.id - 1], 1)); names.insert(SkinSlots::kSlots[s.id - 1].name); }
    for (const St& s : def) if (std::string(SkinSlots::kSlots[s.id - 1].name) == "head1") CHECK(s.var == 101);
    for (const char* n : {"skin_torso", "skin_leg", "skin_feet", "head1", "brow1", "hair1", "underwear"}) CHECK(names.count(n) == 1);

    // 1) so o padrao: TODO material de slot nao citado fica oculto
    std::vector<Op> ops;
    PlanWithDefaults(def, ops);
    std::set<std::string> hid, shown;
    for (const Op& o : ops) (o.kind == kHide ? hid : shown).insert(o.mat);
    CHECK(shown.count("fantasy_m_hair1") == 1);               // hair1 variante 1 visivel
    CHECK(hid.count("fantasy_m_hair2") == 1);                 // hair2..4 nao citados: ocultos
    CHECK(hid.count("fantasy_m_torso2") == 1);                // camisa nao citada: oculta (nao herda a do molde)
    for (const std::string& m : hid) CHECK(shown.count(m) == 0);   // nenhum material fica nos dois lados
    // cada material de qualquer slot tem exatamente uma op
    std::multiset<std::string> all; for (const Op& o : ops) all.insert(o.mat);
    for (const std::string& m : all) CHECK(all.count(m) == 1);

    // 2) com camisa citada: ela aparece, o resto do grupo continua oculto
    std::vector<St> st = def;
    const SkinSlots::Slot* sh = SkinSlots::Find("torso2");
    int shId = 0; for (int i = 0; i < SkinSlots::kCount; ++i) if (&SkinSlots::kSlots[i] == sh) shId = i + 1;
    if (shId) {
        st.push_back(St{(uint8_t)shId, kPack, 5});
        PlanWithDefaults(st, ops);
        bool found = false; for (const Op& o : ops) if (o.slot == shId && o.kind != kHide) found = true;
        CHECK(found);
    } else std::printf("(aviso) slot torso2 nao existe nesta tabela: caso 2 pulado\n");

    // 3) Plan puro nao muda (so lista o citado)
    std::vector<Op> plain; Plan(def, plain);
    CHECK(plain.size() < ops.size());

    if (g_fail) { std::printf("%d falha(s)\n", g_fail); return 1; }
    std::printf("LOOKDEFAULTS: todos os testes passaram\n");
    return 0;
}
