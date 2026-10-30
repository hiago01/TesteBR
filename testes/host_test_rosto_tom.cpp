// Rosto = FORMATO (head1..5) x TOM (1..6): variante do rosto = tom*100 + rosto. Regras puras (host).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../app/src/main/cpp/samp/game/LookSlots.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

struct FakeBS {                      // bytes em ordem; Read<T> le little-endian
    std::vector<unsigned char> b; size_t p = 0;
    template <class T> void W(T v) { for (size_t i = 0; i < sizeof(T); ++i) b.push_back((unsigned char)((unsigned long long)v >> (8 * i))); }
    template <class T> bool Read(T& v) { if (p + sizeof(T) > b.size()) return false; unsigned long long x = 0; for (size_t i = 0; i < sizeof(T); ++i) x |= (unsigned long long)b[p + i] << (8 * i); v = (T)x; p += sizeof(T); return true; }
};

int main()
{
    using namespace SkinSlots;
    const Slot* h1 = Find("head1"); const Slot* h3 = Find("head3"); const Slot* tor = Find("skin_torso"); const Slot* sh = Find("torso2");
    CHECK(h1 && h3 && tor && sh);
    CHECK(IsHeadSlot(*h1) && !IsHeadSlot(*tor) && IsToneSlot(*tor) && !IsToneSlot(*h1));

    // formato antigo (<100) vira N*100+v; ja novo nao muda; 0 nao muda; slot que nao e rosto nao muda
    CHECK(CanonVar(*h1, 1) == 101 && CanonVar(*h3, 5) == 305 && CanonVar(*h3, 305) == 305 && CanonVar(*h3, 0) == 0);
    CHECK(CanonVar(*sh, 34) == 34 && CanonVar(*tor, 4) == 4);

    // caminho <-> variante (ida e volta)
    CHECK(VariantRel(*h1, 305) == "heads/3/5");            // qualquer FORMATO aceita qualquer TOM
    CHECK(VariantRel(*h3, 612) == "heads/6/12");
    CHECK(VariantRel(*h3, 2)   == "heads/3/2");            // antigo: pasta do slot
    CHECK(VariantRel(*sh, 34)  == "tshirts/2/34");
    CHECK(VariantFromRel(*h1, "heads/3/5") == 305);
    CHECK(VariantFromRel(*h3, "heads/3/5") == 305);
    CHECK(VariantFromRel(*h1, "heads/1/1") == 101);
    CHECK(VariantFromRel(*tor, "torso/1/4") == 4);
    CHECK(VariantFromRel(*tor, "torso/1/x") == -1 && VariantFromRel(*tor, "leg/1/4") == -1 && VariantFromRel(*h1, "heads/7/1") == -1);
    CHECK(VariantFromRel(*h1, "versace/x/1") == -1 && VariantFromRel(*h1, nullptr) == -1);
    for (int t = 1; t <= kTones; ++t) for (int f = 1; f <= kToneFaces[t]; ++f) {
        const int v = t * 100 + f;
        CHECK(HeadVarValid(v));
        CHECK(VariantFromRel(*h1, VariantRel(*h1, v).c_str()) == v);
    }
    CHECK(!HeadVarValid(0) && !HeadVarValid(99) && !HeadVarValid(100) && !HeadVarValid(107) && !HeadVarValid(713) && !HeadVarValid(613) && !HeadVarValid(509));
    CHECK(HeadVarTone(305) == 3 && HeadVarFace(305) == 5 && HeadVarFace(7) == 7);
    CHECK(kToneFaces[6] == 12 && 6 * 100 + 12 <= LookSlots::kMaxVar);   // cabe no protocolo (ate 1023)

    // Parse (RPC 232): um snapshot ANTIGO do servidor (head2 var 3) chega canonico (203); novo passa como esta
    {
        FakeBS bs; const int id2 = (int)(Find("head2") - kSlots) + 1, idT = (int)(tor - kSlots) + 1, id1 = (int)(h1 - kSlots) + 1;
        bs.W<uint16_t>(7); bs.W<uint8_t>(1); bs.W<uint8_t>(0); bs.W<uint8_t>(3);
        bs.W<uint8_t>((uint8_t)id2); bs.W<uint8_t>(2); bs.W<uint16_t>(3);       // antigo
        bs.W<uint8_t>((uint8_t)idT); bs.W<uint8_t>(2); bs.W<uint16_t>(4);       // tom 4
        bs.W<uint8_t>((uint8_t)id1); bs.W<uint8_t>(2); bs.W<uint16_t>(405);     // novo: tom 4, rosto 5
        uint16_t pid = 0; std::vector<LookSlots::St> out; std::string err;
        CHECK(LookSlots::Parse(bs, pid, out, err));
        int got2 = -1, gotT = -1, got1 = -1;
        for (auto& s : out) { if (s.id == id2) got2 = s.var; if (s.id == idT) gotT = s.var; if (s.id == id1) got1 = s.var; }
        CHECK(got2 == 203 && gotT == 4 && got1 == 405);
        // Plan: caminho do PNG usa o tom do VALOR e nao o da pasta do slot
        std::vector<LookSlots::Op> ops; LookSlots::Plan(out, ops);
        bool ok1 = false, ok2 = false;
        for (auto& o : ops) { if (o.slot == id1 && o.rel == "heads/4/5.png") ok1 = true; if (o.slot == id2 && o.rel == "heads/2/3.png") ok2 = true; }
        CHECK(ok1 && ok2);
    }
    {   // tenis de cano baixo (boot1/boot3) NAO escondem pe/tornozelo; boot2/boot4 continuam cobrindo
        using namespace SkinSlots;
        auto piece = [](const char* n) { for (const Box& b : kCoverPieces) if (!std::strcmp(b.name, n)) return &b; return (const Box*)nullptr; };
        auto target = [](const char* n) { for (const Box& b : kCoverTargets) if (!std::strcmp(b.name, n)) return &b; return (const Box*)nullptr; };
        const char* skin[] = { "body.feetleft", "body.feetright", "body.legleft5", "body.legright5" };
        for (const char* t : skin) {
            CHECK(!PieceCovers(*piece("fantasy_m_boot1"), *target(t)));
            CHECK(!PieceCovers(*piece("fantasy_m_boot3"), *target(t)));
            CHECK(PieceCovers(*piece("fantasy_m_boot2"), *target(t)));
            CHECK(PieceCovers(*piece("fantasy_m_boot4"), *target(t)));
        }
        // Plano completo: boot1 ligado nao esconde body.feet*; boot2 ligado esconde
        const int b1 = (int)(Find("boot1") - kSlots) + 1, b2 = (int)(Find("boot2") - kSlots) + 1;
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<LookSlots::St> st; LookSlots::St x; x.id = (uint8_t)(pass ? b2 : b1); x.est = 1; x.var = 0; st.push_back(x);
            LookSlots::St f; f.id = (uint8_t)((int)(Find("skin_feet") - kSlots) + 1); f.est = 2; f.var = 1; st.push_back(f);
            std::vector<LookSlots::Op> ops; LookSlots::Plan(st, ops);
            bool feetHidden = false;
            for (auto& o : ops) if (o.mat == "body.feetright" && o.kind == LookSlots::kHide) feetHidden = true;
            CHECK(feetHidden == (pass == 1));
        }
    }
    if (g_fail) { std::printf("%d falha(s)\n", g_fail); return 1; }
    std::printf("ROSTO_TOM: todos os testes passaram\n");
    return 0;
}
