// Teste de PC de SkinTexDbCore.h (regra de nomes identica a tools/skin_texdb.py + indice de variantes).
#include "../../app/src/main/cpp/samp/game/SkinTexDbCore.h"
#include <cstdio>
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FALHA linha %d: %s\n", __LINE__, #c); } } while (0)
using namespace SkinTexDbCore;
int main()
{
    CHECK(DbName("tshirts/1/34") == "tshirts_1_34");
    CHECK(DbName("Versace/TShirts/2_1") == "versace_tshirts_2_1");
    CHECK(DbName("hair/2/10") == "hair_2_10");
    CHECK(DbName("corp/policia/fantasy_m_cop_torso4") == "corp_policia_fantasy_m_48dd3725");
    CHECK(DbName("corp/policia/fantasy_m_cop_torso4").size() == 31);
    CHECK(DbName("a-b c.d") == "a_b_c_d");
    CHECK(Crc32("123456789") == 0xCBF43926u);
    CHECK(ValidRel("heads/1/2")); CHECK(!ValidRel("../x")); CHECK(!ValidRel("/x")); CHECK(!ValidRel("a\\b")); CHECK(!ValidRel(""));

    Index ix;
    const std::string t = "tshirts/1/34\ttshirts_1_34\r\ntshirts/1/2\ttshirts_1_2\ntshirts/1/10\ttshirts_1_10~1\nTshirts/2/5\ttshirts_2_5\n"
                          "ruim sem tab\n../x\tbad\n\nhair/1/abc\thair_1_abc\ncorp/policia/Torso4\tcorp_policia_torso4\ncorp/bombeiro/a\tcorp_bombeiro_a\n"
                          "tshirts/1/2\toutro_nome\n";
    CHECK(ix.Parse(t) == 7);
    CHECK(ix.Has("tshirts/1/34")); CHECK(ix.Has("TSHIRTS/1/34")); CHECK(!ix.Has("tshirts/1/99"));
    CHECK(ix.Name("tshirts/1/10") == "tshirts_1_10~1");
    CHECK(ix.Name("tshirts/1/2") == "tshirts_1_2");             // duplicata: vale a primeira
    const std::vector<int> v = ix.Variants("tshirts/1");
    CHECK(v.size() == 3 && v[0] == 2 && v[1] == 10 && v[2] == 34);
    CHECK(ix.Variants("tshirts/2").size() == 1);
    CHECK(ix.Variants("hair/1").empty());                         // "abc" nao e numerico
    CHECK(ix.Variants("nada").empty());
    CHECK(ix.WithPrefix("corp/policia/").size() == 1);
    const std::vector<std::string> sf = ix.SubFolders("corp/");
    CHECK(sf.size() == 2 && sf[0] == "bombeiro" && sf[1] == "policia");
    CHECK(ix.Parse("") == 0);
    std::printf("%d checks, %d falhas\n", checks, fails);
    return fails ? 1 : 0;
}
