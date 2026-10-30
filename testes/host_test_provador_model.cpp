// Testes PUROS de ProvadorModel.h (classificacao, variante, espelho do servidor). Sem engine.
#include "ProvadorModel.h"
#include <cstdio>
using namespace ProvadorModel;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ ++fails; std::printf("FALHOU linha %d: %s\n", __LINE__, #c);} }while(0)
static int Id(const char* n) { for (int k = 0; k < SkinSlots::kCount; ++k) if (!std::strcmp(SkinSlots::kSlots[k].name, n)) return k + 1; return 0; }
int main()
{
    // Classify
    CHECK(Classify({0,0,0,kNoPack}) == kUnavailable);
    CHECK(Classify({1,0,0,kNoPack}) == kDefault);          // visível + textura original = nada equipado
    CHECK(Classify({1,0,0,3}) == kEquipped);
    CHECK(Classify({1,1,0,3}) == kDisabled);
    CHECK(Classify({1,1,1,kNoPack}) == kCovered);
    CHECK(Classify({3,3,3,2}) == kCovered && Classify({3,3,2,2}) == kDisabled);
    CHECK(Classify({10,4,0,kNoPack}) == kDefault && Classify({10,4,0,5}) == kEquipped);   // parcial = visível
    CHECK(Classify({2,0,0,kMixed}) == kDefault);                                          // misto nunca vira "equipado"
    // VariantOf
    CHECK(VariantOf("hair/2/4", "hair/2") == 4);
    CHECK(VariantOf("hair/2/4", "hair/1") == kNoPack);
    CHECK(VariantOf("tshirts/2_1", "tshirts") == kNoPack);       // caminho avulso de marca: não é variante numérica
    CHECK(VariantOf("versace/tshirts/2_1", "tshirts/2") == kNoPack);
    CHECK(VariantOf("hair/2/", "hair/2") == kNoPack && VariantOf("hair/2/x", "hair/2") == kNoPack && VariantOf(nullptr, "a") == kNoPack);
    CHECK(VariantOf("hair/2/1234567", "hair/2") == kNoPack);
    // espelho do servidor (Look_PutSlot)
    SrvTable t = NewTable();
    Put(t, Id("hair1"), 2, 3);
    CHECK(t[Id("hair1")].known && t[Id("hair1")].est == 2 && t[Id("hair1")].var == 3);
    Put(t, Id("hair2"), 1, 99);                                   // ligar hair2 visível => hair1 (visível) volta ao padrão
    CHECK(!t[Id("hair1")].known && t[Id("hair2")].est == 1 && t[Id("hair2")].var == 0);
    Put(t, Id("hair3"), 0, 0);                                    // oculto
    Put(t, Id("hair4"), 2, 1);                                    // hair2 (visível) sai; hair3 (oculto) FICA oculto
    CHECK(!t[Id("hair2")].known && t[Id("hair3")].known && t[Id("hair3")].est == 0 && t[Id("hair4")].est == 2);
    Put(t, Id("boot1"), 2, 5); Put(t, Id("slipper2"), 2, 1);       // tênis e chinelo: UM grupo
    CHECK(!t[Id("boot1")].known && t[Id("slipper2")].known);
    Put(t, Id("backpack1"), 2, 1); CHECK(t[Id("hair4")].known && t[Id("slipper2")].known);   // outro grupo: intocado
    Put(t, Id("beard1"), 2, 1); Put(t, Id("beard2"), 2, 1);        // sem grupo: ambos ficam
    CHECK(t[Id("beard1")].known && t[Id("beard2")].known);
    Put(t, Id("hair4"), 255, 0); CHECK(!t[Id("hair4")].known);      // LOOK_UNSET_WIRE volta ao padrão
    Put(t, 0, 2, 1); Put(t, 99, 2, 1); CHECK(t.size() == (size_t)SkinSlots::kCount + 1);   // id inválido ignorado
    // Simulate: ordem importa
    SrvTable s = Simulate(NewTable(), { {Id("torso1"), 2, 4}, {Id("torso2"), 2, 5} });
    CHECK(!s[Id("torso1")].known && s[Id("torso2")].est == 2 && s[Id("torso2")].var == 5);
    s = Simulate(NewTable(), { {Id("torso2"), 2, 5}, {Id("torso1"), 2, 4} });
    CHECK(s[Id("torso1")].est == 2 && !s[Id("torso2")].known);
    // FromSnapshot / SameTable
    struct St { int id, est, var; };
    std::vector<St> snap = { {Id("hair1"), 2, 7}, {Id("boot1"), 0, 0}, {999, 1, 0} };
    SrvTable f = FromSnapshot(snap);
    CHECK(f[Id("hair1")].var == 7 && f[Id("boot1")].known && f[Id("boot1")].est == 0 && SameTable(f, f));
    SrvTable g = f; g[Id("hair1")].var = 8; CHECK(!SameTable(f, g));
    if (fails) { std::printf("MODELO: %d FALHA(S)\n", fails); return 1; }
    std::printf("MODELO: todos os testes puros passaram\n");
    return 0;
}
