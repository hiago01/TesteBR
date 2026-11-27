// Teste de host (x86) do DffSplit.h:  g++ -std=c++17 -O2 -I../app/src/main/cpp/samp/game tools/dffsplit_test.cpp -o dffsplit_test
//
// Gera DFFs sinteticos (grade de triangulos encadeados, varios materiais) com 10k..150k vertices, passa por DffSplit::Process e
// VERIFICA com um leitor independente (nao usa nada de DffSplit::detail):
//   * estrutura: todo chunk cabe no pai, numAtomics/numGeometries conferem, geometryIndex valido;
//   * cada geometria <= 65535 vertices e todo indice (Bin Mesh e Struct) < numVertices;
//   * os triangulos (posicao, normal, 2 UVs, cor pre-calculada, indices de osso, 4 pesos, night color, material) sao os MESMOS
//     da entrada (comparacao de multiconjuntos, com o sentido do triangulo preservado);
//   * esfera de contorno de cada parte contem todos os vertices.
// E simula o jogo (libGTASA arm64 2.10): RpGeometryCreate recusa numVertices >> 16 != 0 (0x2bc7d0) e _rpMeshRead trunca
// cada indice do Bin Mesh para uint16 (strh 0x2c01a8) -> conta indices corrompidos na ENTRADA e na SAIDA.
#include "DffSplit.h"
#include "DffPreflight.h"
#include <cstdio>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <string>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  [FALHA] "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static uint64_t Mix(uint64_t x) { x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31); }
static float Fl(uint64_t h) { return (float)((h >> 11) & 0xFFFFF) / 1048576.0f * 200.0f - 100.0f; }

// ------------------------------------------------------------------------------------------ dados por vertice
struct Opt { bool skin = false, night = false, prelit = true; int texSets = 1; bool strip = false; int mats = 6; int geoms = 1; int atomicsOnBig = 1; bool refFraction = false; };
struct VData { float pos[3], nrm[3], uv[2][2]; uint8_t pre[4]; uint8_t bi[4]; float w[4]; uint32_t night; };
static VData Make(uint32_t id, uint32_t salt)
{
    VData v{}; uint64_t h = Mix(id * 2654435761ull + salt);
    for (int k = 0; k < 3; ++k) { v.pos[k] = Fl(Mix(h + k)); v.nrm[k] = Fl(Mix(h + 10 + k)) / 100.0f; }
    for (int s = 0; s < 2; ++s) for (int k = 0; k < 2; ++k) v.uv[s][k] = Fl(Mix(h + 20 + s * 2 + k)) / 100.0f;
    uint64_t c = Mix(h + 30); for (int k = 0; k < 4; ++k) v.pre[k] = (uint8_t)(c >> (k * 8));
    uint64_t b = Mix(h + 31); for (int k = 0; k < 4; ++k) v.bi[k] = (uint8_t)((b >> (k * 8)) % 32);
    float s = 0; for (int k = 0; k < 4; ++k) { v.w[k] = (float)((Mix(h + 40 + k) >> 20) & 0xFF) + 1.0f; s += v.w[k]; } for (int k = 0; k < 4; ++k) v.w[k] /= s;
    v.night = (uint32_t)Mix(h + 50);
    return v;
}
static uint64_t HashV(const VData& v, const Opt& o)
{
    uint64_t h = 1469598103934665603ull;
    auto add = [&](const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; for (size_t i = 0; i < n; ++i) { h ^= q[i]; h *= 1099511628211ull; } };
    add(v.pos, 12); add(v.nrm, 12);
    for (int s = 0; s < o.texSets; ++s) add(v.uv[s], 8);
    if (o.prelit) add(v.pre, 4);
    if (o.skin) { add(v.bi, 4); add(v.w, 16); }
    if (o.night) add(&v.night, 4);
    return h;
}
// triangulo canonico: gira para o menor hash de vertice primeiro (preserva o sentido); inclui o material
static uint64_t HashTri(uint64_t a, uint64_t b, uint64_t c, uint32_t mat)
{
    uint64_t v[3] = { a, b, c }; int m = 0; for (int i = 1; i < 3; ++i) if (v[i] < v[m]) m = i;
    uint64_t h = Mix(mat + 7); for (int i = 0; i < 3; ++i) h = Mix(h ^ v[(m + i) % 3]); return h;
}

// ------------------------------------------------------------------------------------------ escritor do gerador (independente)
struct GW
{
    std::vector<uint8_t> b;
    void u32(uint32_t v) { const uint8_t* p = (const uint8_t*)&v; b.insert(b.end(), p, p + 4); }
    void f32(float v) { const uint8_t* p = (const uint8_t*)&v; b.insert(b.end(), p, p + 4); }
    void u16(uint16_t v) { const uint8_t* p = (const uint8_t*)&v; b.insert(b.end(), p, p + 2); }
    void u8(uint8_t v) { b.push_back(v); }
    size_t begin(uint32_t t, uint32_t ver = 0x1803FFFF) { size_t at = b.size(); u32(t); u32(0); u32(ver); return at; }
    void end(size_t at) { uint32_t s = (uint32_t)(b.size() - at - 12); std::memcpy(&b[at + 4], &s, 4); }
};
struct Truth { std::multiset<uint64_t> tris; uint64_t verts = 0; };

// gera 1 geometria; 'used' = quantos vertices (do inicio) sao referenciados (para o teste de compactacao)
static void GenGeometry(GW& w, uint32_t nver, uint32_t used, const Opt& o, uint32_t salt, Truth& truth)
{
    const size_t gAt = w.begin(0x0F);
    // triangulos encadeados (i,i+1,i+2) sobre os 'used' primeiros vertices; material por blocos de 997
    std::vector<std::vector<uint32_t>> byMat(o.mats);
    for (uint32_t i = 0; i + 2 < used; ++i) { auto& m = byMat[(i / 997) % o.mats]; m.push_back(i); m.push_back(i + 1); m.push_back(i + 2); }
    uint32_t ntri = 0; for (auto& m : byMat) ntri += (uint32_t)m.size() / 3;
    // Struct
    { const size_t sAt = w.begin(0x01);
      uint32_t flags = 0x2 | 0x10 | 0x20 | 0x40 | (o.prelit ? 0x8u : 0u) | (o.texSets ? 0x4u : 0u) | ((uint32_t)o.texSets << 16) | (o.strip ? 1u : 0u);
      w.u32(flags); w.u32(ntri); w.u32(nver); w.u32(1);
      if (o.prelit) for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); w.u8(v.pre[0]); w.u8(v.pre[1]); w.u8(v.pre[2]); w.u8(v.pre[3]); }
      for (int s = 0; s < o.texSets; ++s) for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); w.f32(v.uv[s][0]); w.f32(v.uv[s][1]); }
      // triangulos da Struct como um exportador grava: indice TRUNCADO em 16 bits (so ha 16 bits ali)
      for (int m = 0; m < o.mats; ++m) for (size_t k = 0; k + 2 < byMat[m].size(); k += 3) { w.u16((uint16_t)byMat[m][k + 1]); w.u16((uint16_t)byMat[m][k]); w.u16((uint16_t)m); w.u16((uint16_t)byMat[m][k + 2]); }
      w.f32(0); w.f32(0); w.f32(0); w.f32(180); w.u32(1); w.u32(1);
      for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); w.f32(v.pos[0]); w.f32(v.pos[1]); w.f32(v.pos[2]); }
      for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); w.f32(v.nrm[0]); w.f32(v.nrm[1]); w.f32(v.nrm[2]); }
      w.end(sAt); }
    // MaterialList
    { const size_t lAt = w.begin(0x08);
      { const size_t s = w.begin(0x01); w.u32(o.mats); for (int i = 0; i < o.mats; ++i) w.u32(0xFFFFFFFFu); w.end(s); }
      for (int i = 0; i < o.mats; ++i)
      { const size_t mA = w.begin(0x07);
        { const size_t s = w.begin(0x01); w.u32(0); w.u32(0xFFFFFFFFu); w.u32(0); w.u32(0); w.f32(1); w.f32(1); w.f32(1); w.end(s); }
        { const size_t e = w.begin(0x03); w.end(e); }
        w.end(mA); }
      w.end(lAt); }
    // Extension: BinMesh (+ Skin) (+ Night)
    { const size_t eAt = w.begin(0x03);
      { const size_t bAt = w.begin(0x50E);
        // strip: um strip por bloco de material, juntados com 2 repeticoes (juncao padrao)
        std::vector<std::vector<uint32_t>> meshIdx(o.mats);
        uint32_t total = 0;
        if (!o.strip) { for (int m = 0; m < o.mats; ++m) { meshIdx[m] = byMat[m]; total += (uint32_t)byMat[m].size(); } }
        else
        {
            for (uint32_t i0 = 0; i0 + 2 < used; i0 += 997)
            {
                const uint32_t i1 = std::min(i0 + 997 + 1, used - 1);   // vertices i0..i1 formam o strip do bloco
                auto& s = meshIdx[(i0 / 997) % o.mats];
                if (!s.empty()) { s.push_back(s.back()); s.push_back(i0); if (s.size() & 1) s.push_back(i0); }   // juncao: preserva a paridade
                for (uint32_t v = i0; v <= i1; ++v) s.push_back(v);
            }
            for (auto& m : meshIdx) total += (uint32_t)m.size();
        }
        w.u32(o.strip ? 1u : 0u); w.u32(o.mats); w.u32(total);
        for (int m = 0; m < o.mats; ++m) { w.u32((uint32_t)meshIdx[m].size()); w.u32(m); for (uint32_t v : meshIdx[m]) w.u32(v); }
        w.end(bAt);
        // verdade (multiconjunto de triangulos) pelo MESMO criterio de leitura de lista/strip
        for (int m = 0; m < o.mats; ++m)
        {
            const auto& I = meshIdx[m];
            if (!o.strip) for (size_t k = 0; k + 2 < I.size(); k += 3) truth.tris.insert(HashTri(HashV(Make(I[k], salt), o), HashV(Make(I[k + 1], salt), o), HashV(Make(I[k + 2], salt), o), m));
            else for (size_t k = 0; k + 2 < I.size(); ++k)
            {
                uint32_t a = I[k], b2 = I[k + 1], c = I[k + 2]; if (a == b2 || b2 == c || a == c) continue; if (k & 1) std::swap(a, b2);
                truth.tris.insert(HashTri(HashV(Make(a, salt), o), HashV(Make(b2, salt), o), HashV(Make(c, salt), o), m));
            }
        }
      }
      if (o.skin)
      { const size_t kAt = w.begin(0x116);
        w.u8(32); w.u8(32); w.u8(4); w.u8(0);
        for (int i = 0; i < 32; ++i) w.u8((uint8_t)i);
        for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); w.u8(v.bi[0]); w.u8(v.bi[1]); w.u8(v.bi[2]); w.u8(v.bi[3]); }
        for (uint32_t i = 0; i < nver; ++i) { VData v = Make(i, salt); for (int k = 0; k < 4; ++k) w.f32(v.w[k]); }
        for (int i = 0; i < 32 * 16; ++i) w.f32((float)(i % 7) * 0.25f);
        w.u32(0); w.u32(0); w.u32(0);                          // cauda: boneLimit, numMeshes, numRLE
        w.end(kAt); }
      if (o.night)
      { const size_t kAt = w.begin(0x253F2F9); w.u32(1); for (uint32_t i = 0; i < nver; ++i) w.u32(Make(i, salt).night); w.end(kAt); }
      { const size_t kAt = w.begin(0x253F2F8); for (int i = 0; i < 6; ++i) w.u32(0xA0 + i); w.end(kAt); }   // 2dfx fictício
      w.end(eAt); }
    w.end(gAt);
    truth.verts += nver;
}

struct Built { std::vector<uint8_t> dff; Truth truth; };
// o.geoms geometrias: a 0 e' a grande (nver); as demais pequenas (1000 vertices). Atomics: o.atomicsOnBig apontam para a 0.
static Built BuildDff(uint32_t nver, uint32_t used, const Opt& o)
{
    Built r; GW w;
    const int natom = o.atomicsOnBig + (o.geoms - 1);
    const size_t cAt = w.begin(0x10);
    { const size_t s = w.begin(0x01); w.u32(natom); w.u32(0); w.u32(0); w.end(s); }
    const int frames = o.skin ? 2 : 1;
    { const size_t fAt = w.begin(0x0E);
      { const size_t s = w.begin(0x01); w.u32(frames);
        for (int f = 0; f < frames; ++f) { for (int k = 0; k < 9; ++k) w.f32(k % 4 == 0 ? 1.0f : 0.0f); w.f32(0); w.f32(0); w.f32(0); w.u32(f == 0 ? 0xFFFFFFFFu : 0u); w.u32(0); }
        w.end(s); }
      for (int f = 0; f < frames; ++f) { const size_t e = w.begin(0x03); if (o.skin) { const size_t h = w.begin(0x11E); w.u32(0x100); w.u32(f); w.u32(0); w.end(h); } w.end(e); }
      w.end(fAt); }
    { const size_t lAt = w.begin(0x1A);
      { const size_t s = w.begin(0x01); w.u32(o.geoms); w.end(s); }
      GenGeometry(w, nver, used, o, 1, r.truth);
      for (int g = 1; g < o.geoms; ++g) { Opt s = o; GenGeometry(w, 1000, 1000, s, 100 + g, r.truth); }
      w.end(lAt); }
    auto atomic = [&](uint32_t frame, uint32_t geom) { const size_t a = w.begin(0x14); { const size_t s = w.begin(0x01); w.u32(frame); w.u32(geom); w.u32(5); w.u32(0); w.end(s); }
                                                        { const size_t e = w.begin(0x03); { const size_t k = w.begin(0x0000001F); w.u32(0x120); w.u32(0); w.end(k); } w.end(e); } w.end(a); };
    for (int i = 0; i < o.atomicsOnBig; ++i) atomic(o.skin ? 1 : 0, 0);
    for (int g = 1; g < o.geoms; ++g) atomic(0, g);
    { const size_t e = w.begin(0x03); w.end(e); }
    w.end(cAt);
    r.dff = std::move(w.b);
    return r;
}

// ------------------------------------------------------------------------------------------ leitor independente da saida
struct OutGeom { uint32_t nver = 0; uint32_t flags = 0; std::vector<std::array<uint32_t, 4>> structTris; std::vector<std::vector<uint32_t>> meshIdx; std::vector<uint32_t> meshMat; uint32_t binFlags = 0;
                 std::vector<VData> v; bool hasSkin = false, hasNight = false, has2dfx = false; float bs[4] = {}; };
struct Rd { const uint8_t* d; size_t n; };
static uint32_t RU32(const uint8_t* d, size_t o) { uint32_t v; std::memcpy(&v, d + o, 4); return v; }
static float RF(const uint8_t* d, size_t o) { float v; std::memcpy(&v, d + o, 4); return v; }
struct Ch { uint32_t t, sz, ver; size_t body, end; };
static bool NextCh(const uint8_t* d, size_t n, size_t& p, size_t pe, Ch& c)
{ if (pe > n || p > pe || pe - p < 12) return false; c.t = RU32(d, p); c.sz = RU32(d, p + 4); c.ver = RU32(d, p + 8); c.body = p + 12; if (c.sz > pe - c.body) return false; c.end = c.body + c.sz; p = c.end; return true; }

static bool ReadOut(const std::vector<uint8_t>& o, const Opt& /*opt*/, std::vector<OutGeom>& geoms, int& natomDecl, int& ngeomDecl, std::vector<uint32_t>& atomGeom, std::string& why)
{
    const uint8_t* d = o.data(); size_t n = o.size(); size_t p = 0; Ch clump;
    if (!NextCh(d, n, p, n, clump) || clump.t != 0x10 || clump.end != n) { why = "clump"; return false; }
    size_t q = clump.body; Ch c; int firstStruct = 1;
    while (NextCh(d, n, q, clump.end, c))
    {
        if (c.t == 0x01 && firstStruct) { natomDecl = (int)RU32(d, c.body); firstStruct = 0; }
        else if (c.t == 0x14) { size_t r = c.body; Ch s; if (!NextCh(d, n, r, c.end, s) || s.t != 0x01 || s.sz < 16) { why = "atomic"; return false; } atomGeom.push_back(RU32(d, s.body + 4)); }
        else if (c.t == 0x1A)
        {
            size_t r = c.body; Ch s; if (!NextCh(d, n, r, c.end, s) || s.t != 0x01) { why = "glist"; return false; } ngeomDecl = (int)RU32(d, s.body);
            Ch g;
            while (NextCh(d, n, r, c.end, g))
            {
                if (g.t != 0x0F) continue;
                OutGeom og; size_t t = g.body; Ch x;
                while (NextCh(d, n, t, g.end, x))
                {
                    if (x.t == 0x01)
                    {
                        og.flags = RU32(d, x.body); const uint32_t ntri = RU32(d, x.body + 4); og.nver = RU32(d, x.body + 8);
                        size_t u = x.body + 16; const uint32_t nv = og.nver; og.v.assign(nv, VData{});
                        if (og.flags & 8) { for (uint32_t i = 0; i < nv; ++i) { for (int k = 0; k < 4; ++k) og.v[i].pre[k] = d[u + i * 4 + k]; } u += (size_t)nv * 4; }
                        const int ts = (og.flags >> 16) & 0xFF;
                        for (int s = 0; s < ts; ++s) { for (uint32_t i = 0; i < nv; ++i) { og.v[i].uv[s][0] = RF(d, u + i * 8); og.v[i].uv[s][1] = RF(d, u + i * 8 + 4); } u += (size_t)nv * 8; }
                        for (uint32_t i = 0; i < ntri; ++i) { const size_t b = u + (size_t)i * 8; og.structTris.push_back({ (uint32_t)(d[b + 2] | d[b + 3] << 8), (uint32_t)(d[b] | d[b + 1] << 8), (uint32_t)(d[b + 6] | d[b + 7] << 8), (uint32_t)(d[b + 4] | d[b + 5] << 8) }); }
                        u += (size_t)ntri * 8;
                        for (int k = 0; k < 4; ++k) og.bs[k] = RF(d, u + k * 4);
                        const bool hv = RU32(d, u + 16), hn = RU32(d, u + 20); u += 24;
                        if (hv) { for (uint32_t i = 0; i < nv; ++i) for (int k = 0; k < 3; ++k) og.v[i].pos[k] = RF(d, u + i * 12 + k * 4); u += (size_t)nv * 12; }
                        if (hn) { for (uint32_t i = 0; i < nv; ++i) for (int k = 0; k < 3; ++k) og.v[i].nrm[k] = RF(d, u + i * 12 + k * 4); u += (size_t)nv * 12; }
                        if (u != x.end) { why = "struct com sobra/falta"; return false; }
                    }
                    else if (x.t == 0x03)
                    {
                        size_t m = x.body; Ch y;
                        while (NextCh(d, n, m, x.end, y))
                        {
                            if (y.t == 0x50E)
                            {
                                og.binFlags = RU32(d, y.body); const uint32_t nm = RU32(d, y.body + 4); size_t b = y.body + 12;
                                for (uint32_t k = 0; k < nm; ++k) { const uint32_t ni = RU32(d, b), mt = RU32(d, b + 4); b += 8; std::vector<uint32_t> I(ni); std::memcpy(I.data(), d + b, (size_t)ni * 4); b += (size_t)ni * 4; og.meshIdx.push_back(I); og.meshMat.push_back(mt); }
                            }
                            else if (y.t == 0x116)
                            {
                                og.hasSkin = true; const size_t used = d[y.body + 1]; size_t b = y.body + 4 + used;
                                for (uint32_t i = 0; i < og.nver; ++i) for (int k = 0; k < 4; ++k) og.v[i].bi[k] = d[b + (size_t)i * 4 + k];
                                b += (size_t)og.nver * 4;
                                for (uint32_t i = 0; i < og.nver; ++i) for (int k = 0; k < 4; ++k) og.v[i].w[k] = RF(d, b + (size_t)i * 16 + k * 4);
                            }
                            else if (y.t == 0x253F2F9) { og.hasNight = true; for (uint32_t i = 0; i < og.nver; ++i) og.v[i].night = RU32(d, y.body + 4 + (size_t)i * 4); }
                            else if (y.t == 0x253F2F8) og.has2dfx = true;
                        }
                    }
                }
                geoms.push_back(std::move(og));
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------------------------------ simulacao do jogo
struct EngineSim { bool createOk = true; uint64_t truncated = 0, indices = 0; };
static EngineSim SimEngine(const DffSplit::Side& s, const std::vector<uint8_t>& dff)
{
    EngineSim e; e.createOk = s.maxGeomVerts <= 65535;    // RpGeometryCreate: numVertices >> 16 != 0 => NULL
    // _rpMeshRead: cada indice de 32 bits do disco vira uint16 (strh)
    const uint8_t* d = dff.data(); size_t n = dff.size(); size_t p = 0; Ch clump{}; NextCh(d, n, p, n, clump);
    size_t q = clump.body; Ch c;
    while (NextCh(d, n, q, clump.end, c)) { if (c.t != 0x1A) continue; size_t r = c.body; Ch g; while (NextCh(d, n, r, c.end, g)) { if (g.t != 0x0F) continue; size_t t = g.body; Ch x;
        while (NextCh(d, n, t, g.end, x)) { if (x.t != 0x03) continue; size_t m = x.body; Ch y;
            while (NextCh(d, n, m, x.end, y)) { if (y.t != 0x50E) continue; const uint32_t nm = RU32(d, y.body + 4); size_t b = y.body + 12;
                for (uint32_t k = 0; k < nm; ++k) { const uint32_t ni = RU32(d, b); b += 8; for (uint32_t i = 0; i < ni; ++i) { const uint32_t v = RU32(d, b + (size_t)i * 4); ++e.indices; if ((uint16_t)v != v) ++e.truncated; } b += (size_t)ni * 4; } } } } }
    return e;
}

// ------------------------------------------------------------------------------------------ verificacao completa
static bool Verify(const char* tag, const Built& in, const std::vector<uint8_t>& out, const Opt& o, const DffSplit::Report& rep, bool wasSplit)
{
    const int f0 = g_fail;
    std::vector<OutGeom> geoms; int natom = -1, ngeom = -1; std::vector<uint32_t> atomGeom; std::string why;
    if (!ReadOut(out, o, geoms, natom, ngeom, atomGeom, why)) { CHECK(false, "%s: leitura da saida falhou (%s)", tag, why.c_str()); return false; }
    CHECK((int)geoms.size() == ngeom, "%s: numGeometries=%d mas ha %zu", tag, ngeom, geoms.size());
    CHECK((int)atomGeom.size() == natom, "%s: numAtomics=%d mas ha %zu", tag, natom, atomGeom.size());
    std::set<uint32_t> ref; for (uint32_t g : atomGeom) { CHECK(g < geoms.size(), "%s: geometryIndex %u fora da lista", tag, g); ref.insert(g); }
    CHECK(ref.size() == geoms.size(), "%s: ha geometria sem atomic (%zu de %zu referenciadas)", tag, ref.size(), geoms.size());
    std::multiset<uint64_t> got, gotStruct; uint64_t verts = 0;
    for (size_t gi = 0; gi < geoms.size(); ++gi)
    {
        const OutGeom& g = geoms[gi]; verts += g.nver;
        CHECK(g.nver <= 65535, "%s: geometria %zu com %u vertices", tag, gi, g.nver);
        CHECK(g.has2dfx == (gi < (size_t)o.geoms), "%s: 2dfx na geometria %zu (as originais mantem o seu; partes adicionais nao repetem)", tag, gi);
        if (o.skin) CHECK(g.hasSkin, "%s: geometria %zu sem Skin", tag, gi);
        if (o.night) CHECK(g.hasNight, "%s: geometria %zu sem Night colors", tag, gi);
        for (size_t m = 0; m < g.meshIdx.size(); ++m)
        {
            const auto& I = g.meshIdx[m];
            for (uint32_t v : I) CHECK(v < g.nver, "%s: indice %u >= %u", tag, v, g.nver);
            if (g.binFlags == 0) for (size_t k = 0; k + 2 < I.size(); k += 3) got.insert(HashTri(HashV(g.v[I[k]], o), HashV(g.v[I[k + 1]], o), HashV(g.v[I[k + 2]], o), g.meshMat[m]));
            else for (size_t k = 0; k + 2 < I.size(); ++k) { uint32_t a = I[k], b = I[k + 1], c = I[k + 2]; if (a == b || b == c || a == c) continue; if (k & 1) std::swap(a, b); got.insert(HashTri(HashV(g.v[a], o), HashV(g.v[b], o), HashV(g.v[c], o), g.meshMat[m])); }
        }
        for (const auto& t : g.structTris) { CHECK(t[0] < g.nver && t[1] < g.nver && t[2] < g.nver, "%s: triangulo da Struct fora do intervalo", tag); gotStruct.insert(HashTri(HashV(g.v[t[0]], o), HashV(g.v[t[1]], o), HashV(g.v[t[2]], o), t[3])); }
        // esfera de contorno cobre os vertices
        for (const VData& v : g.v) { float s2 = 0; for (int k = 0; k < 3; ++k) { float dl = v.pos[k] - g.bs[k]; s2 += dl * dl; } if (std::sqrt(s2) > g.bs[3] * 1.0001f + 1e-3f) { CHECK(false, "%s: vertice fora da esfera de contorno", tag); break; } }
    }
    CHECK(got == in.truth.tris, "%s: triangulos DIFERENTES da entrada (%zu vs %zu) - dados por vertice/material/sentido nao preservados", tag, got.size(), in.truth.tris.size());
    if (wasSplit) CHECK(got == gotStruct, "%s: triangulos da Struct != Bin Mesh (%zu vs %zu)", tag, gotStruct.size(), got.size());
    (void)verts; (void)rep;
    return g_fail == f0;
}

static void Row(const char* label, uint32_t nver, const Opt& o, uint32_t used = 0)
{
    const uint32_t u = used ? used : nver;
    Built b = BuildDff(nver, u, o);
    DffSplit::Report rep; std::string err; std::vector<uint8_t> out;
    const DffSplit::Result r = DffSplit::Process(b.dff.data(), b.dff.size(), out, rep, err);
    const EngineSim ein = SimEngine(rep.in, b.dff);
    const std::vector<uint8_t>& fin = (r == DffSplit::Result::Split) ? out : b.dff;
    DffSplit::Side so; std::string e2; DffSplit::detail::Inspect(fin.data(), fin.size(), so, e2);
    const EngineSim eout = SimEngine(so, fin);
    std::printf("%-26s | in: %2dg %7llu v  maxIdx %6u  jogo:%s trunc %llu/%llu | out: %2dg %2da %7llu v %7llu t(bin) maxIdx %5u maxG %5u | eng %6.2f MB | trunc %llu | %s\n",
        label, rep.in.geoms, (unsigned long long)rep.in.verts, rep.in.maxIndex, ein.createOk ? "ok  " : "NULL", (unsigned long long)ein.truncated, (unsigned long long)ein.indices,
        so.geoms, so.atomics, (unsigned long long)so.verts, (unsigned long long)so.binTris, so.maxIndex, so.maxGeomVerts, so.engineMeshBytes / 1048576.0,
        (unsigned long long)eout.truncated, r == DffSplit::Result::Unchanged ? "inalterado" : r == DffSplit::Result::Split ? "DIVIDIDO" : ("FALHOU: " + err).c_str());
    CHECK(r != DffSplit::Result::Failed, "%s: Process falhou: %s", label, err.c_str());
    if (r == DffSplit::Result::Failed) return;
    if (nver <= 65535) { CHECK(r == DffSplit::Result::Unchanged, "%s: DFF antigo (<=65535) foi alterado", label); CHECK(out.empty(), "%s: out tocado em Unchanged", label); }
    else { CHECK(r == DffSplit::Result::Split, "%s: nao dividiu", label); CHECK(eout.createOk, "%s: saida ainda seria recusada pelo RpGeometryCreate", label); CHECK(eout.truncated == 0, "%s: indices truncados na saida", label); }
    if (r == DffSplit::Result::Split)
    {
        Verify(label, b, out, o, rep, true);
        if (nver > 65535 && rep.in.maxIndex > 65535) CHECK(ein.truncated > 0 || !ein.createOk, "%s: simulacao devia mostrar corrupcao/recusa na entrada", label);
        // o DffPreflight do projeto (leitor independente) tambem precisa aceitar a saida de skin
        if (o.skin)
        {
            DffPreflight::Info info; std::string pe;
            const bool ok = DffPreflight::Parse(out.data(), out.size(), info, pe);
            CHECK(ok, "%s: DffPreflight::Parse reprovou a saida: %s", label, pe.c_str());
            CHECK(info.verts == rep.out.verts && info.geoms == rep.out.geoms, "%s: DffPreflight conta geoms/verts diferente", label);
        }
    }
    else Verify(label, b, b.dff, o, rep, false);
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "--dump") == 0)   // grava DFFs de exemplo (grade sintetica) em <pasta> para testar no aparelho
    {
        const std::string dir = argv[2]; Opt p1; p1.skin = false; p1.night = false; p1.texSets = 1; p1.prelit = true;
        for (uint32_t n : { 65535u, 65536u, 100000u, 150000u })
        {
            Built b = BuildDff(n, n, p1); const std::string f = dir + "/teste_" + std::to_string(n) + "v.dff";
            if (FILE* fp = std::fopen(f.c_str(), "wb")) { std::fwrite(b.dff.data(), 1, b.dff.size(), fp); std::fclose(fp); std::printf("gravado %s (%zu bytes)\n", f.c_str(), b.dff.size()); }
        }
        return 0;
    }
    const int fuzzIters = argc > 1 ? std::atoi(argv[1]) : 3000;
    const bool quick = argc > 2;   // 2o argumento qualquer: pula os casos de 150000 vertices (para rodar sob ASan)
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("== DffSplit: leitor do jogo simulado (RpGeometryCreate recusa >=65536; _rpMeshRead trunca indice para uint16) ==\n");
    std::printf("legenda: g=geometrias a=atomics v=vertices; 'jogo:NULL' = RpGeometryCreate devolveria NULL; trunc a/b = indices alterados pelo strh / indices totais\n\n");
    Opt plain; plain.skin = false; plain.night = false; plain.texSets = 1; plain.prelit = true;
    std::printf("-- A) tamanhos pedidos (geometria simples: pos+normal+UV+cor, 6 materiais, 1 atomic)\n");
    for (uint32_t n : { 10000u, 40000u, 65000u, 65535u, 65536u, 100000u, 150000u }) { if (quick && n > 100000) continue; char l[48]; std::snprintf(l, sizeof(l), "plain %u", n); Row(l, n, plain); }

    Opt full; full.skin = true; full.night = true; full.texSets = 2; full.prelit = true;
    std::printf("\n-- B) skin (32 ossos, 4 pesos) + night colors + 2 conjuntos de UV + cor pre-calculada + 2dfx\n");
    for (uint32_t n : { 65535u, 65536u, 100000u, 150000u }) { if (quick && n > 100000) continue; char l[48]; std::snprintf(l, sizeof(l), "skin+night+2uv %u", n); Row(l, n, full); }

    Opt strip = plain; strip.strip = true;
    std::printf("\n-- C) Bin Mesh em tri-strip (convertido em lista; sentido preservado)\n");
    for (uint32_t n : { 40000u, 100000u, 150000u }) { if (quick && n > 100000) continue; char l[48]; std::snprintf(l, sizeof(l), "strip %u", n); Row(l, n, strip); }

    Opt multi = plain; multi.geoms = 3; multi.atomicsOnBig = 2;
    std::printf("\n-- D) clump com 3 geometrias (so a 0 e' grande) e 2 atomics apontando para ela (cada um ganha copias)\n");
    for (uint32_t n : { 40000u, 100000u, 150000u }) { if (quick && n > 100000) continue; char l[48]; std::snprintf(l, sizeof(l), "multi %u", n); Row(l, n, multi); }

    std::printf("\n-- E) compactacao: 100000 vertices declarados mas so 50000 referenciados (nao precisa dividir, so descartar)\n");
    Row("unreferenced 100000/50000", 100000, plain, 50000);

    int geomFail = 0;
    std::printf("\n-- F) recusas esperadas (nada de lixo silencioso)\n");
    {
        Built b = BuildDff(70000, 70000, plain);
        // F1: flag native na Struct
        { std::vector<uint8_t> x = b.dff; size_t pos = 0; DffSplit::detail::Ref cl; DffSplit::detail::Next(x.data(), x.size(), pos, x.size(), cl);
          size_t p = cl.body; DffSplit::detail::Ref c; while (DffSplit::detail::Next(x.data(), x.size(), p, cl.end, c)) if (c.type == 0x1A) { size_t q = c.body; DffSplit::detail::Ref s; DffSplit::detail::Next(x.data(), x.size(), q, c.end, s); DffSplit::detail::Ref g; DffSplit::detail::Next(x.data(), x.size(), q, c.end, g); uint32_t fl = DffSplit::detail::U32(x.data(), g.body + 12); fl |= 0x01000000u; std::memcpy(&x[g.body + 12], &fl, 4); }
          DffSplit::Report r; std::string e; std::vector<uint8_t> o; auto res = DffSplit::Process(x.data(), x.size(), o, r, e);
          std::printf("  native      -> %s (%s)\n", res == DffSplit::Result::Failed ? "recusado" : "?", e.c_str()); CHECK(res == DffSplit::Result::Failed, "native devia ser recusado"); }
        // F2: Breakable
        { Opt ob = plain; Built bb = BuildDff(70000, 70000, ob); std::vector<uint8_t> x = bb.dff; /* troca o id do plugin 2dfx por Breakable */
          for (size_t i = 0; i + 4 <= x.size(); ++i) if (DffSplit::detail::U32(x.data(), i) == 0x253F2F8u) { uint32_t id = 0x253F2FDu; std::memcpy(&x[i], &id, 4); break; }
          DffSplit::Report r; std::string e; std::vector<uint8_t> o; auto res = DffSplit::Process(x.data(), x.size(), o, r, e);
          std::printf("  breakable   -> %s (%s)\n", res == DffSplit::Result::Failed ? "recusado" : "?", e.c_str()); CHECK(res == DffSplit::Result::Failed, "breakable devia ser recusado"); }
        // F3: Skin com split/RLE
        { Opt os = full; Built bs = BuildDff(70000, 70000, os); std::vector<uint8_t> x = bs.dff;
          for (size_t i = 0; i + 12 <= x.size(); ++i) if (DffSplit::detail::U32(x.data(), i) == 0x116u && DffSplit::detail::U32(x.data(), i + 8) == 0x1803FFFFu) { uint32_t sz = DffSplit::detail::U32(x.data(), i + 4); uint32_t one = 3; std::memcpy(&x[i + 12 + sz - 8], &one, 4); break; }
          DffSplit::Report r; std::string e; std::vector<uint8_t> o; auto res = DffSplit::Process(x.data(), x.size(), o, r, e);
          std::printf("  skin split  -> %s (%s)\n", res == DffSplit::Result::Failed ? "recusado" : "?", e.c_str()); CHECK(res == DffSplit::Result::Failed, "skin com split devia ser recusado"); }
        // F4: indice >= numVertices
        { std::vector<uint8_t> x = b.dff; for (size_t i = 0; i + 12 <= x.size(); ++i) if (DffSplit::detail::U32(x.data(), i) == 0x50Eu && DffSplit::detail::U32(x.data(), i + 8) == 0x1803FFFFu) { uint32_t bad = 99999999u; std::memcpy(&x[i + 12 + 12 + 8], &bad, 4); break; }
          DffSplit::Report r; std::string e; std::vector<uint8_t> o; auto res = DffSplit::Process(x.data(), x.size(), o, r, e);
          std::printf("  indice > nv -> %s (%s)\n", res == DffSplit::Result::Failed ? "recusado" : "?", e.c_str()); CHECK(res == DffSplit::Result::Failed, "indice invalido devia ser recusado"); }
        (void)geomFail;
    }

    std::printf("\n-- G) fuzz: %d DFFs corrompidos (truncados / bytes trocados) sobre um DFF de 66000 vertices com skin: sem crash/hang\n", fuzzIters);
    {
        Built b = BuildDff(66000, 66000, full); uint64_t seed = 12345; int split = 0, failed = 0, unchanged = 0;
        for (int it = 0; it < fuzzIters; ++it)
        {
            std::vector<uint8_t> x = b.dff; seed = Mix(seed);
            const int mode = (int)(seed % 3);
            if (mode == 0) x.resize((size_t)(Mix(seed + 1) % x.size()));
            else { const int flips = 1 + (int)(Mix(seed + 2) % 8); for (int k = 0; k < flips; ++k) x[(size_t)(Mix(seed + 3 + k) % x.size())] ^= (uint8_t)(1u << (Mix(seed + 20 + k) % 8)); }
            DffSplit::Report r; std::string e; std::vector<uint8_t> o; const auto res = DffSplit::Process(x.data(), x.size(), o, r, e);
            if (res == DffSplit::Result::Split) { ++split; DffSplit::Side s; std::string e2; if (!DffSplit::detail::Inspect(o.data(), o.size(), s, e2) || s.maxGeomVerts > 65535) CHECK(false, "fuzz %d: saida invalida", it); }
            else if (res == DffSplit::Result::Failed) ++failed; else ++unchanged;
        }
        std::printf("  %d casos: dividiu=%d recusou=%d inalterado=%d (nenhum crash; toda saida produzida e' valida)\n", fuzzIters, split, failed, unchanged);
    }

    std::printf("\n%s (%d falha%s)\n", g_fail ? "REPROVADO" : "APROVADO", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
