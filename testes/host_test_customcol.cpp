// g++ -std=c++17 -I../app/src/main/cpp/samp/game -o host_test_customcol host_test_customcol.cpp && ./host_test_customcol
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "CustomColParse.h"
using namespace CustomColParse;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU linha %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::vector<uint8_t> MkEntry(const char* id, const char* name, uint16_t mid, uint32_t extra = 40)
{
    std::vector<uint8_t> v(8 + 24 + extra, 0x11);
    std::memcpy(&v[0], id, 4); uint32_t sz = 24 + extra; std::memcpy(&v[4], &sz, 4);
    std::memset(&v[8], 0, 24); std::strncpy((char*)&v[8], name, 22); std::memcpy(&v[30], &mid, 2);
    return v;
}
static void Add(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }

int main()
{
    std::vector<uint8_t> f;
    Add(f, MkEntry("COL3", "CE_a", 0)); Add(f, MkEntry("COL3", "ce_BREWERY", 9)); Add(f, MkEntry("COL2", "CE_c", 0));
    auto r = Parse(f.data(), f.size());
    CHECK(r.status == Status::Ok && r.entries.size() == 3 && r.bytesOk == f.size());
    CHECK(Find(r, "CE_brewery") == 1);          // sem diferenciar maiusculas
    CHECK(Find(r, "CE_brewer") == -1);          // prefixo nao casa
    CHECK(r.entries[1].modelId == 9 && r.entries[2].version == 2);

    auto pad = f; pad.insert(pad.end(), 100, 0);   // padding de zeros no fim e' aceito
    CHECK(Parse(pad.data(), pad.size()).status == Status::Ok);

    auto cut = f; cut.resize(cut.size() - 5);      // arquivo cortado
    auto rc = Parse(cut.data(), cut.size());
    CHECK(rc.status == Status::Overrun && rc.entries.size() == 2 && Find(rc, "CE_brewery") == 1);

    auto bad = f; std::memcpy(&bad[72], "XXXX", 4);  // 2a entrada com ident invalido
    CHECK(Parse(bad.data(), bad.size()).status == Status::BadIdent);

    auto small = MkEntry("COL3", "z", 0); uint32_t s = 10; std::memcpy(&small[4], &s, 4);
    CHECK(Parse(small.data(), small.size()).status == Status::BadSize);

    auto tail = f; tail.push_back(7); CHECK(Parse(tail.data(), tail.size()).status == Status::TruncatedHeader);
    CHECK(Parse(nullptr, 0).status == Status::Empty);

    // nome de 22 bytes cheios, sem '\0'
    auto full = MkEntry("COL3", "ABCDEFGHIJKLMNOPQRSTUV", 0);
    auto rf = Parse(full.data(), full.size());
    CHECK(rf.status == Status::Ok && std::strlen(rf.entries[0].name) == 22);

    std::printf(fails ? "%d FALHA(S)\n" : "todos os testes passaram\n", fails);
    return fails ? 1 : 0;
}
