// Teste no HOST (sem Android) do mesclador de IPL:  g++ -std=c++17 -Wall -I../app/src/main/cpp/samp/game -o /tmp/tipl host_test_customipl.cpp && /tmp/tipl
// Usa IPLs binarios SINTETICOS montados com o layout confirmado no desassembly (nao ha IPL real do IMG neste ambiente).
#include "CustomIplParse.h"
#include <cassert>
#include <iostream>
using namespace CustomIplParse;

static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) g_ok++; else { g_fail++; std::cout << "FALHOU linha " << __LINE__ << ": " #c "\n"; } } while (0)

static Inst MkInst(int id, float x, float y, float z, int lod, uint32_t type = 0)
{ Inst i{}; i.pos[0]=x; i.pos[1]=y; i.pos[2]=z; i.rot[3]=1.0f; i.id=id; i.type=type; i.lod=lod; return i; }

// monta bnry: cabecalho 0x4C | instancias | car generators (0x30 cada) | rodape qualquer
static std::vector<uint8_t> MakeBin(const std::vector<Inst>& v, int cargens, bool zeroPad20 = true)
{
    std::vector<uint8_t> b(kHdrSize, 0);
    std::memcpy(b.data(), "bnry", 4);
    WrU32(b.data() + 4, (uint32_t)v.size());
    WrU32(b.data() + 0x14, (uint32_t)cargens);
    WrU32(b.data() + 0x1C, (uint32_t)kHdrSize);
    (void)zeroPad20;
    const size_t instEnd = kHdrSize + v.size() * kInstSize;
    WrU32(b.data() + 0x3C, (uint32_t)instEnd);
    WrU32(b.data() + 0x20, 0);
    b.resize(instEnd + cargens * 0x30 + 16, 0);
    if (!v.empty()) std::memcpy(b.data() + kHdrSize, v.data(), v.size() * kInstSize);
    for (int c = 0; c < cargens; c++) for (int k = 0; k < 0x30; k++) b[instEnd + c * 0x30 + k] = (uint8_t)(0x10 + c + k);
    for (int k = 0; k < 16; k++) b[b.size() - 16 + k] = 0xEE;
    return b;
}
static std::vector<Inst> ReadInsts(const std::vector<uint8_t>& b)
{
    BinView v; CHECK(ReadBin(b.data(), b.size(), v) == BinStatus::Ok);
    std::vector<Inst> r(v.count); if (v.count) std::memcpy(r.data(), b.data() + v.instOff, v.count * kInstSize); return r;
}
static OverrideFile Parse(const std::string& t) { OverrideFile f; ParseOverride(t.data(), t.size(), f); return f; }
static int Ok(const char*, int id) { return (id >= 1 && id < 20000) ? id : -1; }          // todo id 1..19999 "existe"
static int OnlyNames(const char* n, int id) { return std::strcmp(n, "naoexiste") == 0 ? -1 : id; }

int main()
{
    static_assert(sizeof(Inst) == 0x28, "");

    // --- 1. parser de texto
    {
        OverrideFile f = Parse(
            "# comentario\n"
            "replace\n 2, 100,  200, novo_obj, 0, 1.5, 2.5, 3.5, 0,0,0,1, 4  # fim\nend\n"
            "remove\n 3 100\nend\n"
            "inst\n 300 lamp 13 10 20 30 0 0 0 1 -1\n 301,lamp2,0,1,2,3,0,0,0,1,-1\nend\n");
        CHECK(f.rep.size() == 1 && f.rem.size() == 1 && f.add.size() == 2 && f.linesBad == 0);
        CHECK(f.rep[0].index == 2 && f.rep[0].origId == 100 && f.rep[0].t.id == 200 && f.rep[0].t.lod == 4 && f.rep[0].t.pos[2] == 3.5f);
        CHECK(f.add[0].t.type == 13 && std::string(f.add[0].t.name) == "lamp");
        OverrideFile g = Parse("inst\n 1 a 0 0 0 0 0 0 0 0 -1\nend\n");            // quaternion zero
        CHECK(g.add.empty() && g.linesBad == 1);
        OverrideFile h = Parse("inst\n 1 a 0 0 0 0 0 0 0 1\nend\n");                // 10 campos
        CHECK(h.add.empty() && h.linesBad == 1);
        OverrideFile i = Parse("inst\n 1 a 0 99999999 0 0 0 0 0 1 -1\nend\n");     // coordenada absurda
        CHECK(i.add.empty() && i.linesBad == 1);
        OverrideFile j = Parse("1 a 0 0 0 0 0 0 0 1 -1\n");                          // fora de secao
        CHECK(j.Empty() && j.linesBad == 1);
        OverrideFile k = Parse("inst\n 1 a 0 0 0 0 0 0 0 1 -1\n");                   // sem end
        CHECK(k.add.size() == 1 && k.linesBad == 1);
        OverrideFile l = Parse("inst\n 1 a 0 nan 0 0 0 0 0 1 -1\nend\n");
        CHECK(l.add.empty());
    }

    // --- 2. replace in-place + conferencia de ID + bytes intocados
    {
        std::vector<Inst> v = { MkInst(10,0,0,0,-1), MkInst(11,5,5,0,0), MkInst(12,9,9,0,-1), MkInst(13,1,1,0,2) };
        auto bin = MakeBin(v, 3);
        OverrideFile f = Parse("replace\n 1, 11, 777, trocado, 256, 50, 60, 7, 0,0,0,1, 0\nend\nreplace\n 2, 9999, 5, errado,0,0,0,0,0,0,0,1,-1\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why));
        CHECK(rep.replaced == 1 && rep.skipped == 1 && rep.removed == 0 && rep.added == 0 && rep.finalCount == 4);
        auto r = ReadInsts(out);
        CHECK(r.size() == 4 && r[1].id == 777 && r[1].type == 256 && r[1].pos[0] == 50.f && r[1].lod == 0);
        CHECK(r[0].id == 10 && r[2].id == 12 && r[3].id == 13 && r[3].lod == 2);
        // bytes originais (incluindo car generators e rodape) intactos; so +4 e +0x1C mudaram
        std::vector<uint8_t> a(out.begin(), out.begin() + bin.size()), b = bin;
        WrU32(a.data() + 4, 0); WrU32(b.data() + 4, 0); WrU32(a.data() + 0x1C, 0); WrU32(b.data() + 0x1C, 0);
        CHECK(a == b);
        CHECK(RdU32(out.data() + 0x14) == 3 && RdU32(out.data() + 0x3C) == RdU32(bin.data() + 0x3C));      // cargens: contagem e offset
        CHECK(std::memcmp(out.data() + RdU32(out.data() + 0x3C), bin.data() + RdU32(bin.data() + 0x3C), 3 * 0x30) == 0);
        CHECK(RdU32(out.data() + 0x1C) >= bin.size() && (RdU32(out.data() + 0x1C) & 3) == 0);
        CHECK(Validate(out.data(), out.size(), nullptr).empty());
    }

    // --- 3. remove com compactacao; lod NAO e' remapeado (indexa o IPL principal)
    {
        // 0,1,2 apontam para LODs 4 e 5 (a frente); 3 sem lod; 4,5 sao os LODs
        std::vector<Inst> v = { MkInst(10,0,0,0,4), MkInst(11,1,0,0,5), MkInst(12,2,0,0,4), MkInst(13,3,0,0,-1), MkInst(14,4,0,0,-1), MkInst(15,5,0,0,-1) };
        auto bin = MakeBin(v, 0);
        OverrideFile f = Parse("remove\n 1, 11\n 4, 14\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why));
        auto r = ReadInsts(out);
        CHECK(r.size() == 4 && rep.removed == 2);
        // novo: [10,12,13,15]; os lod (4 e 4) continuam IGUAIS: sao indices do IPL principal, nao desta tabela
        CHECK(r[0].id == 10 && r[0].lod == 4 && r[1].id == 12 && r[1].lod == 4 && r[2].id == 13 && r[3].id == 15);
        CHECK(rep.lodFixed == 0);
        CHECK(Validate(out.data(), out.size(), nullptr).empty());
        // remover so o 1: o LOD 5 (id 15) passa a indice 4 e quem apontava p/ ele... (ninguem apos remocao do 1)
        OverrideFile g = Parse("remove\n 1, 11\nend\n");
        CHECK(Merge(bin.data(), bin.size(), { &g }, Ok, out, rep, &why));
        r = ReadInsts(out);
        CHECK(r.size() == 5 && r[0].lod == 4 && r[1].lod == 4 && r[4].id == 15);
        // remocao com ID errado nao remove
        OverrideFile h = Parse("remove\n 1, 999\nend\n");
        CHECK(!Merge(bin.data(), bin.size(), { &h }, Ok, out, rep, &why) && rep.skipped == 1);
    }

    // --- 3b. REGRESSAO: tabela de 3 instancias com lod 350/522 (apontam para o IPL principal): nada pode virar -1
    {
        std::vector<Inst> v = { MkInst(10,0,0,0,350), MkInst(11,1,0,0,522), MkInst(12,2,0,0,-1) };
        auto bin = MakeBin(v, 0);
        CHECK(Validate(bin.data(), bin.size(), nullptr).empty());
        OverrideFile f = Parse("remove\n 2, 12\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why));
        auto r = ReadInsts(out);
        CHECK(r.size() == 2 && r[0].lod == 350 && r[1].lod == 522 && rep.lodFixed == 0);
    }

    // --- 4. inst (acrescentar) + lod no indice original + modelo inexistente
    {
        std::vector<Inst> v = { MkInst(10,0,0,0,-1), MkInst(11,1,0,0,-1) };
        auto bin = MakeBin(v, 1);
        OverrideFile f = Parse("inst\n 500 a 0 3 3 3 0 0 0 1 0\n 501 naoexiste 0 4 4 4 0 0 0 1 -1\n 502 b 0 5 5 5 0 0 0 1 99\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, OnlyNames, out, rep, &why));
        auto r = ReadInsts(out);
        CHECK(rep.added == 2 && rep.skipped == 1 && r.size() == 4);
        CHECK(r[2].id == 500 && r[2].lod == 0 && r[3].id == 502 && r[3].lod == 99 && rep.lodFixed == 0);   // lod 99 = indice no IPL principal: passa direto
        // tudo ignorado => sem mistura
        OverrideFile g = Parse("inst\n 501 naoexiste 0 4 4 4 0 0 0 1 -1\nend\n");
        CHECK(!Merge(bin.data(), bin.size(), { &g }, OnlyNames, out, rep, &why));
    }

    // --- 5. varios arquivos: o ultimo vence; remove depois de replace
    {
        std::vector<Inst> v = { MkInst(10,0,0,0,-1), MkInst(11,1,0,0,-1), MkInst(12,2,0,0,-1) };
        auto bin = MakeBin(v, 0);
        OverrideFile a = Parse("replace\n 0, 10, 20, x, 0, 9,9,9, 0,0,0,1, -1\nend\n");
        OverrideFile b = Parse("replace\n 0, 10, 21, y, 0, 8,8,8, 0,0,0,1, -1\nend\nremove\n 2, 12\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &a, &b }, Ok, out, rep, &why));
        auto r = ReadInsts(out);
        CHECK(r.size() == 2 && r[0].id == 21 && r[0].pos[0] == 8.f);
        OverrideFile c = Parse("remove\n 0, 10\nend\n");
        CHECK(Merge(bin.data(), bin.size(), { &a, &c }, Ok, out, rep, &why));     // remove (ultimo) vence o replace
        CHECK(ReadInsts(out).size() == 2 && ReadInsts(out)[0].id == 11);
    }

    // --- 6. buffers invalidos / limites
    {
        std::vector<uint8_t> junk(200, 0x41); std::vector<uint8_t> out; MergeReport rep; std::string why;
        OverrideFile f = Parse("inst\n 1 a 0 0 0 0 0 0 0 1 -1\nend\n");
        CHECK(!Merge(junk.data(), junk.size(), { &f }, Ok, out, rep, &why) && !why.empty());
        auto bin = MakeBin({ MkInst(10,0,0,0,-1) }, 0);
        std::vector<uint8_t> trunc(bin.begin(), bin.begin() + 0x50);                 // tabela passa do fim
        BinView v; CHECK(ReadBin(trunc.data(), trunc.size(), v) == BinStatus::Truncated);
        std::vector<uint8_t> small(10, 0); CHECK(ReadBin(small.data(), small.size(), v) == BinStatus::TooSmall);
        auto bad = bin; WrU32(bad.data() + 4, 40000); CHECK(ReadBin(bad.data(), bad.size(), v) == BinStatus::BadCount);
        bad = bin; WrU32(bad.data() + 4, 0x10001); CHECK(ReadBin(bad.data(), bad.size(), v) == BinStatus::BadCount);   // bits altos: o jogo leria 1
        bad = bin; WrU32(bad.data() + 0x1C, 0x4E); CHECK(ReadBin(bad.data(), bad.size(), v) == BinStatus::BadOffset);
        // IPL vazio (0 instancias) aceita acrescimos
        auto emptyBin = MakeBin({}, 0);
        CHECK(Merge(emptyBin.data(), emptyBin.size(), { &f }, Ok, out, rep, &why) && ReadInsts(out).size() == 1);
        // estouro de 32767
        std::string big = "inst\n"; for (int i = 0; i < 32770; i++) big += "1 a 0 0 0 0 0 0 0 1 -1\n"; big += "end\n";
        OverrideFile bf = Parse(big);
        CHECK(!Merge(emptyBin.data(), emptyBin.size(), { &bf }, Ok, out, rep, &why) && why.find("32767") != std::string::npos);
    }

    // --- 7. aviso de posicao longe da area do IPL
    {
        auto bin = MakeBin({ MkInst(10,0,0,0,-1), MkInst(11,100,100,0,-1) }, 0);
        OverrideFile f = Parse("inst\n 5 a 0 5000 5000 0 0 0 0 1 -1\nend\n");
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why));
        bool warned = false; for (auto& n : rep.notes) if (n.find("AVISO") != std::string::npos) warned = true;
        CHECK(warned);
    }

    // --- 8. texto -> binario e' 1:1 (igual ao sscanf do jogo: nenhuma conversao do quaternion)
    {
        OverrideFile f = Parse("inst\n 321 foo 12 -1.25 2.5 3.75 0.1 0.2 0.3 0.9 -1\nend\n");
        auto bin = MakeBin({ MkInst(1,0,0,0,-1) }, 0);
        std::vector<uint8_t> out; MergeReport rep; std::string why;
        CHECK(Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why));
        auto r = ReadInsts(out)[1];
        CHECK(r.pos[0] == -1.25f && r.pos[1] == 2.5f && r.pos[2] == 3.75f && r.rot[0] == 0.1f && r.rot[1] == 0.2f && r.rot[2] == 0.3f && r.rot[3] == 0.9f && r.id == 321 && r.type == 12 && r.lod == -1);
    }

    // --- 7. hide (LOD do IPL principal): parse + casamento por id e raio
    {
        OverrideFile f = Parse("hide\n 13292, 166.6, -106.6, 4.5, 30   # casa 07\n 13293, 201.1, -106.6, 4.5, 30\nend\n");
        CHECK(f.hide.size() == 2 && f.linesBad == 0 && !f.Empty());
        const float a[3] = { 166.7f, -106.5f, 4.4f }, b[3] = { 201.0f, -106.5f, 4.4f }, far3[3] = { 900.f, 900.f, 4.f };
        CHECK(HideMatch(f.hide, 13292, a) && HideMatch(f.hide, 13293, b));
        CHECK(!HideMatch(f.hide, 13292, far3));      // mesmo id, longe
        CHECK(!HideMatch(f.hide, 12938, a));         // outro id, mesma posicao
        CHECK(!HideMatch(f.hide, 13292, b));         // 13292 a ~35u de b > raio 30
        OverrideFile g = Parse("hide\n 13292, 1, 2, 3\n 13292, 1, 2, 3, 0\n 13292, 1, 2, 3, 900\n 13292, x, 2, 3, 5\n 99999, 1, 2, 3, 5\n 13292, nan, 2, 3, 5\nend\n");
        CHECK(g.hide.empty() && g.linesBad == 6);    // sem raio, raio 0, raio > 500, numero ruim, id > 65535, nan: tudo recusado
        OverrideFile h = Parse("remove\n 209, 12938\nend\nhide\n 13292, 166.6, -106.6, 4.5, 30\nend\n");
        CHECK(h.rem.size() == 1 && h.hide.size() == 1 && h.linesBad == 0);   // remove + hide no mesmo arquivo
    }

    // --- 8. remove e hide SO POR ID (formas novas) + compatibilidade com (indice, id)
    {
        std::vector<Inst> v = { MkInst(10,0,0,0,350), MkInst(11,1,0,0,-1), MkInst(11,500,0,0,-1), MkInst(12,2,0,0,-1), MkInst(13,3,0,0,-1) };
        auto bin = MakeBin(v, 0);
        auto run = [&](const char* txt, std::vector<Inst>& res, MergeReport& rep) {
            OverrideFile f = Parse(txt); std::vector<uint8_t> out; std::string why;
            const bool ok = Merge(bin.data(), bin.size(), { &f }, Ok, out, rep, &why); if (ok) res = ReadInsts(out); return ok; };
        std::vector<Inst> r; MergeReport rep;
        // 1 token: remove TODAS as instancias do id (o id 11 aparece 2x) e avisa
        CHECK(run("remove\n 11\nend\n", r, rep) && r.size() == 3 && rep.removed == 2 && r[0].id == 10 && r[1].id == 12 && r[2].id == 13);
        bool avisou = false; for (auto& n : rep.notes) if (n.find("casou com 2") != std::string::npos) avisou = true; CHECK(avisou);
        CHECK(r[0].lod == 350);                                           // lod continua intacto
        // id + posicao: so a de perto
        CHECK(run("remove\n 11, 1, 0, 0, 5\nend\n", r, rep) && r.size() == 4 && rep.removed == 1 && r[1].id == 11 && r[1].pos[0] == 500.f);
        // id inexistente => ignorado, sem mudanca
        MergeReport rep2; std::vector<Inst> r2; CHECK(!run("remove\n 999\nend\n", r2, rep2) && rep2.skipped == 1);
        // forma antiga continua valendo, e pode misturar com a nova
        CHECK(run("remove\n 3, 12\n 13\nend\n", r, rep) && r.size() == 3 && rep.removed == 2);
        // hide so por id: casa em qualquer posicao; hide com posicao continua exigindo o raio
        OverrideFile h = Parse("hide\n 13292\n 13293, 1, 2, 3, 10\nend\n");
        CHECK(h.hide.size() == 2 && h.linesBad == 0 && h.hide[0].anyPos && !h.hide[1].anyPos);
        const float far3[3] = { 900.f, 900.f, 9.f }, near3[3] = { 1.f, 2.f, 3.f };
        CHECK(HideMatch(h.hide, 13292, far3) && HideMatch(h.hide, 13292, near3));
        CHECK(!HideMatch(h.hide, 13293, far3) && HideMatch(h.hide, 13293, near3));
        CHECK(!HideMatch(h.hide, 12938, near3));
        // entradas invalidas
        OverrideFile bad1 = Parse("remove\n 70000\n 1, 2, 3\n 5, x, 1, 1, 5\nend\nhide\n 99999\n -3\nend\n");
        CHECK(bad1.rem.empty() && bad1.hide.empty() && bad1.linesBad == 5);
    }

    std::cout << "ok=" << g_ok << " falhou=" << g_fail << "\n";
    return g_fail ? 1 : 0;
}
