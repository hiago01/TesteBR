#pragma once

// ---------------------------------------------------------------------------
// DffSplit: divide, nos BYTES do DFF, toda geometria com mais de 65.535 vertices em geometrias menores,
// antes de o arquivo ser entregue ao RpClumpStreamRead do jogo. Nao toca no engine nem em offsets.
//
// POR QUE DIVIDIR (auditoria da libGTASA.so arm64 2.10, endereco = vaddr; tudo conferido no desassembly):
//   * RpGeometryCreate (0x2bc7a8): "lsr w8,w22,#16 / cbnz" -> numVertices >= 65536 seta erro e DEVOLVE NULL.
//     Um DFF com 65.536+ vertices nem carrega (RpGeometryStreamRead falha). Limite util = 65.535.
//   * RpGeometryStreamRead (0x2bd4f8): os triangulos da Struct viram RpTriangle de 4 x uint16 (strh em 0x2bd6d0..0x2bd6e0).
//   * Bin Mesh PLG: no DISCO e' uint32 por indice, mas _rpMeshRead (0x2bff1c) le em blocos de 0x100 e grava cada
//     indice com STRH / XTN (0x2c01a8 / 0x2c0180) = TRUNCA para 16 bits, sem checar faixa. Memoria = totalIndices*2
//     (0x2bff84: "add w8,w8,w9,lsl #1"). numMeshes tambem e' uint16 (strh em 0x2bfffc).
//   * Renderizador: RQ_Command_rqDrawIndexed (0x260d54) termina em "mov w2,#0x1403" (GL_UNSIGNED_SHORT) FIXO
//     (0x260f84 / 0x260fac) e _rpMeshRead cria o buffer GL com 0x1403 (0x2c0200). O tipo nao vem do comando e o
//     libGTASA nao consulta GL_OES_element_index_uint. => indices de 32 bits NAO sao suportados pelo renderizador.
//   Logo a unica saida segura e' entregar ao jogo geometrias de ate' 65.535 vertices cada (indice maximo 65.534).
//
// O QUE FAZ (so quando alguma geometria passa do limite; senao o buffer sai IDENTICO, byte a byte):
//   * le os triangulos do Bin Mesh PLG (a verdade do que e' desenhado; a Struct tem so 16 bits) e agrupa em partes
//     de <= 65.535 vertices, na ordem original dos meshes (material), duplicando so os vertices de fronteira;
//   * cada parte vira uma Geometry completa: Struct (flags, posicoes, normais, cor pre-calculada, TODOS os conjuntos
//     de UV, triangulos com o material), esfera de contorno recalculada, MaterialList IDENTICA (indices de material
//     preservados), Bin Mesh PLG novo (indices < 65.535), Skin PLG (indices de osso e pesos remapeados; matrizes e
//     ossos usados intactos), Night Vertex Colors remapeado, 2dfx so na parte 0, demais plugins copiados;
//   * cada Atomic que usava a geometria original ganha um Atomic por parte (mesmo frame/flags/extensoes);
//   * recalcula os tamanhos de todos os chunks, numAtomics e numGeometries.
// ONDE USAR: so em modelos cujo SetClump guarda o clump INTEIRO (clump/veiculo/ped; CClumpModelInfo::SetClump @0x45b7f8 usa
//   RpClumpForAllAtomics). Em objeto/predio/time/LOD (CAtomicModelInfo) NAO: CFileLoader::LoadAtomicFile (@0x4d046c) liga cada
//   atomic a um modelo pelo nome do frame (FindRelatedModelInfoCB @0x5546c8) e CAtomicModelInfo::SetAtomic (@0x45aa6c) SUBSTITUI
//   o atomic anterior (str x20,[x19,#0x40]) -> varios atomics no mesmo frame deixariam so a ULTIMA parte visivel.
//   O CustomDff/ConvertLoose ja decide isso pelo tipo do modelo (CBaseModelInfo::GetModelType).
// NAO SUPORTA (recusa com mensagem, em vez de gerar lixo): geometria native (ja e' 16 bits), mais de 1 morph target,
//   Breakable (0x253F2FD), Skin com split/RLE (numMeshes/numRLE != 0), plugin de vertice com tamanho inesperado.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace DffSplit
{
    inline constexpr uint32_t kMaxVerts = 65535;      // RpGeometryCreate recusa numVertices >> 16 != 0

    enum class Result { Unchanged, Split, Failed };

    // fotografia de um DFF (usada na entrada e na saida; mesmo codigo => comparacao honesta)
    struct Side
    {
        size_t   bytes = 0;
        int      geoms = 0, atomics = 0;
        uint64_t verts = 0;          // soma de numVertices das geometrias
        uint64_t tris = 0;           // soma de numTriangles da Struct (campo uint32)
        uint64_t binTris = 0;        // triangulos que o Bin Mesh PLG desenha (lista: n/3; strip: n-2)
        uint64_t meshes = 0;         // meshes (grupos por material) no Bin Mesh PLG
        uint64_t binIndices = 0;     // soma de numIndices dos meshes
        uint32_t maxIndex = 0;       // maior indice de vertice usado em qualquer mesh
        uint32_t maxGeomVerts = 0;   // maior numVertices de uma geometria
        uint64_t engineMeshBytes = 0;// RpMeshHeader + RpMesh + indices uint16 (formula do _rpMeshRead @0x2bff6c/0x2bff84)
        uint64_t vertexBytes = 0;    // dados por vertice no arquivo (pos+normal, cor, UVs, skin)
        std::vector<uint32_t> geomVerts;   // numVertices por geometria, em ordem
    };

    struct Report
    {
        Side in, out;
        int      splitGeoms = 0;          // geometrias que precisaram ser divididas
        int      partsCreated = 0;        // geometrias novas criadas (partes alem da primeira)
        uint64_t droppedUnused = 0;       // vertices nao referenciados por nenhum triangulo (descartados nas geometrias divididas)
        uint64_t stripsConverted = 0;     // meshes em tri-strip convertidos em lista
        std::vector<std::string> notes;   // avisos nao fatais
    };

    namespace detail
    {
        inline uint32_t U32(const uint8_t* d, size_t o) { uint32_t v; std::memcpy(&v, d + o, 4); return v; }
        inline float    F32(const uint8_t* d, size_t o) { float v; std::memcpy(&v, d + o, 4); return v; }

        struct Ref { uint32_t type = 0, size = 0, ver = 0; size_t hdr = 0, body = 0, end = 0; };

        // proximo chunk em [pos, parentEnd); avanca pos; false se nao couber
        inline bool Next(const uint8_t* d, size_t n, size_t& pos, size_t parentEnd, Ref& c)
        {
            if (parentEnd > n || pos > parentEnd || parentEnd - pos < 12) return false;
            c.hdr = pos; c.type = U32(d, pos); c.size = U32(d, pos + 4); c.ver = U32(d, pos + 8);
            c.body = pos + 12;
            if (c.size > parentEnd - c.body) return false;
            c.end = c.body + c.size; pos = c.end;
            return true;
        }

        inline uint32_t LibVersion(uint32_t stamp)
        {
            if (stamp & 0xFFFF0000u) return (((stamp >> 14) & 0x3FF00u) + 0x30000u) | ((stamp >> 16) & 0x3Fu);
            return stamp << 8;
        }

        // escritor com chunks aninhados (tamanho corrigido no End)
        struct W
        {
            std::vector<uint8_t> b;
            void U32(uint32_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4); }
            void F32(float v)    { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4); }
            void U16(uint16_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 2); }
            void Raw(const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
            size_t Begin(uint32_t type, uint32_t ver) { const size_t at = b.size(); U32(type); U32(0); U32(ver); return at; }
            void End(size_t at) { const uint32_t sz = (uint32_t)(b.size() - at - 12); std::memcpy(b.data() + at + 4, &sz, 4); }
        };

        // ---------------------------------------------------------------- Geometry Struct
        struct GStruct
        {
            uint32_t flags = 0, ntri = 0, nver = 0, nmt = 0;
            bool     native = false;
            uint32_t texSets = 0;
            size_t   oOld = 0;          // inicio dos 12 bytes ambient/diffuse/specular (versao < 3.4), 0 = nao tem
            size_t   oPre = 0, oTex = 0, oTri = 0;   // deslocamentos dentro do corpo da Struct
            bool     hasV = false, hasN = false;
            size_t   oV = 0, oN = 0;
            float    bs[4] = {0, 0, 0, 0};
            size_t   oBs = 0;
        };

        inline uint32_t TexSetsOf(uint32_t flags)
        {
            uint32_t ts = (flags >> 16) & 0xFFu;
            if (ts == 0) ts = (flags & 0x4u) ? 1u : ((flags & 0x80u) ? 2u : 0u);
            return ts;
        }

        // light = so os 4 contadores; full = valida e localiza todos os blocos
        inline bool ParseGStruct(const uint8_t* d, const Ref& s, uint32_t stamp, bool full, GStruct& g, std::string& err)
        {
            if (s.type != 0x01 || s.size < 16) { err = "Struct da geometria invalida"; return false; }
            g.flags = U32(d, s.body); g.ntri = U32(d, s.body + 4); g.nver = U32(d, s.body + 8); g.nmt = U32(d, s.body + 12);
            g.native = (g.flags & 0x01000000u) != 0;
            if (!full) return true;
            if (g.native) { err = "geometria 'native' (dados de plataforma, ja com indices de 16 bits) nao pode ser dividida"; return false; }
            if (g.nmt != 1) { err = "geometria com " + std::to_string(g.nmt) + " morph targets (so 1 e' suportado ao dividir)"; return false; }
            const size_t size = s.size;
            size_t p = 16;
            if (LibVersion(stamp) < 0x34000u) { g.oOld = p; p += 12; }
            auto take = [&](uint64_t bytes, size_t& at) -> bool {
                if (bytes > size || p > size - bytes) return false;
                at = p; p += (size_t)bytes; return true;
            };
            g.texSets = TexSetsOf(g.flags);
            if (g.flags & 0x8u) { if (!take((uint64_t)g.nver * 4, g.oPre)) { err = "Struct: cor pre-calculada fora dos limites"; return false; } }
            if (g.texSets)      { if (!take((uint64_t)g.nver * 8 * g.texSets, g.oTex)) { err = "Struct: UVs fora dos limites"; return false; } }
            if (!take((uint64_t)g.ntri * 8, g.oTri)) { err = "Struct: triangulos fora dos limites"; return false; }
            if (!take(24, g.oBs)) { err = "Struct: morph target fora dos limites"; return false; }
            for (int i = 0; i < 4; ++i) g.bs[i] = F32(d, s.body + g.oBs + (size_t)i * 4);
            g.hasV = U32(d, s.body + g.oBs + 16) != 0; g.hasN = U32(d, s.body + g.oBs + 20) != 0;
            if (g.hasV) { if (!take((uint64_t)g.nver * 12, g.oV)) { err = "Struct: posicoes fora dos limites"; return false; } }
            if (g.hasN) { if (!take((uint64_t)g.nver * 12, g.oN)) { err = "Struct: normais fora dos limites"; return false; } }
            if (p != size) { err = "Struct da geometria com " + std::to_string(size - p) + " byte(s) inesperados no fim"; return false; }
            return true;
        }

        // ---------------------------------------------------------------- Bin Mesh PLG
        struct Mesh { uint32_t mat = 0; std::vector<uint32_t> idx; };
        struct BinMesh { uint32_t flags = 0; std::vector<Mesh> meshes; };

        inline bool ParseBinMesh(const uint8_t* d, const Ref& c, BinMesh& bm, std::string& err)
        {
            if (c.size < 12) { err = "Bin Mesh PLG menor que o cabecalho"; return false; }
            bm.flags = U32(d, c.body);
            const uint32_t nmesh = U32(d, c.body + 4);
            const uint32_t total = U32(d, c.body + 8);
            size_t r = c.body + 12;
            if (nmesh > 65535u) { err = "Bin Mesh PLG com " + std::to_string(nmesh) + " meshes (numMeshes e' uint16 no jogo)"; return false; }
            uint64_t sum = 0;
            bm.meshes.resize(nmesh);
            for (uint32_t k = 0; k < nmesh; ++k)
            {
                if (c.end - r < 8) { err = "Bin Mesh PLG truncado"; return false; }
                const uint32_t nidx = U32(d, r), mat = U32(d, r + 4); r += 8;
                if ((uint64_t)nidx * 4 > c.end - r) { err = "Bin Mesh PLG: indices fora dos limites"; return false; }
                bm.meshes[k].mat = mat; bm.meshes[k].idx.resize(nidx);
                if (nidx) std::memcpy(bm.meshes[k].idx.data(), d + r, (size_t)nidx * 4);
                r += (size_t)nidx * 4; sum += nidx;
            }
            if (sum != total) { /* tolerado: exportadores gravam totais diferentes; o que vale sao os meshes */ }
            return true;
        }

        // ---------------------------------------------------------------- inspecao (entrada e saida)
        inline bool Inspect(const uint8_t* d, size_t n, Side& s, std::string& err)
        {
            s = Side(); s.bytes = n;
            size_t pos = 0; Ref clump;
            if (n < 12 || !Next(d, n, pos, n, clump) || clump.type != 0x10) { err = "nao e um DFF (Clump 0x10)"; return false; }
            size_t p = clump.body; Ref c;
            while (Next(d, n, p, clump.end, c))
            {
                if (c.type == 0x14) { ++s.atomics; continue; }
                if (c.type != 0x1A) continue;
                size_t q = c.body; Ref g;
                while (Next(d, n, q, c.end, g))
                {
                    if (g.type != 0x0F) continue;
                    ++s.geoms;
                    size_t t = g.body; Ref x; bool haveStruct = false; GStruct gs; uint32_t texSets = 0;
                    while (Next(d, n, t, g.end, x))
                    {
                        if (x.type == 0x01 && !haveStruct)
                        {
                            std::string e2;
                            if (!ParseGStruct(d, x, g.ver, false, gs, e2)) { err = e2; return false; }
                            haveStruct = true;
                            s.verts += gs.nver; s.tris += gs.ntri;
                            s.maxGeomVerts = std::max(s.maxGeomVerts, gs.nver);
                            s.geomVerts.push_back(gs.nver);
                            texSets = TexSetsOf(gs.flags);
                            uint64_t vb = (uint64_t)gs.nver * 12;                          // posicoes
                            vb += (uint64_t)gs.nver * 12;                                  // normais (quando existem)
                            if (gs.flags & 0x8u) vb += (uint64_t)gs.nver * 4;
                            vb += (uint64_t)gs.nver * 8 * texSets;
                            s.vertexBytes += vb;
                        }
                        else if (x.type == 0x03)
                        {
                            size_t m = x.body; Ref y;
                            while (Next(d, n, m, x.end, y))
                            {
                                if (y.type == 0x116 && y.size >= 4)
                                {
                                    s.vertexBytes += (uint64_t)gs.nver * 20;               // 4 indices de osso (u8) + 4 pesos (float)
                                }
                                else if (y.type == 0x50E)
                                {
                                    BinMesh bm; std::string e2;
                                    if (!ParseBinMesh(d, y, bm, e2)) { err = e2; return false; }
                                    uint64_t tot = 0;
                                    for (const Mesh& me : bm.meshes)
                                    {
                                        ++s.meshes; tot += me.idx.size();
                                        s.binIndices += me.idx.size();
                                        s.binTris += (bm.flags == 0) ? me.idx.size() / 3 : (me.idx.size() > 2 ? me.idx.size() - 2 : 0);
                                        for (uint32_t v : me.idx) s.maxIndex = std::max(s.maxIndex, v);
                                    }
                                    // _rpMeshRead @0x2bff6c: alloc = numMeshes*0x28 + 0x18 (+ totalIndices*2 no caminho nao-native)
                                    s.engineMeshBytes += (uint64_t)bm.meshes.size() * 0x28 + 0x18 + tot * 2;
                                }
                            }
                        }
                    }
                }
            }
            return true;
        }
    } // namespace detail

    // =====================================================================================================
    // Process: in -> out.  Unchanged: 'out' NAO e' tocado (use 'in').  Split: 'out' tem o DFF novo.  Failed: 'err'.
    // =====================================================================================================
    inline Result Process(const uint8_t* in, size_t n, std::vector<uint8_t>& out, Report& rep, std::string& err)
    {
        using namespace detail;
        rep = Report(); err.clear();
        if (!Inspect(in, n, rep.in, err)) return Result::Failed;
        rep.out = rep.in;
        if (rep.in.maxGeomVerts <= kMaxVerts) return Result::Unchanged;

        size_t pos = 0; Ref clump;
        Next(in, n, pos, n, clump);
        if (clump.size != n - 12) { err = "tamanho do Clump difere do arquivo"; return Result::Failed; }

        // ---- filhos do clump
        std::vector<Ref> kids; { size_t p = clump.body; Ref c; while (Next(in, n, p, clump.end, c)) kids.push_back(c); }
        int glIdx = -1;
        for (size_t i = 0; i < kids.size(); ++i) if (kids[i].type == 0x1A) { if (glIdx >= 0) { err = "mais de uma GeometryList"; return Result::Failed; } glIdx = (int)i; }
        if (glIdx < 0) { err = "DFF sem GeometryList"; return Result::Failed; }
        const Ref gl = kids[glIdx];
        Ref glStruct; std::vector<Ref> geoms;
        { size_t p = gl.body; Ref c; bool first = true;
          while (Next(in, n, p, gl.end, c)) { if (first && c.type == 0x01) { glStruct = c; first = false; continue; } first = false; if (c.type == 0x0F) geoms.push_back(c); } }
        if (glStruct.type != 0x01 || glStruct.size < 4) { err = "GeometryList sem Struct"; return Result::Failed; }

        // ---- divide cada geometria que precisa
        std::vector<std::vector<std::vector<uint8_t>>> parts(geoms.size());   // [geom][parte] = chunk Geometry completo
        std::vector<int> firstExtra(geoms.size(), -1);                        // indice (na lista nova) da parte 1 de cada geometria
        int appended = 0;
        for (size_t gi = 0; gi < geoms.size(); ++gi)
        {
            const Ref& g = geoms[gi];
            // acha filhos
            Ref st{}, ml{}, ext{}; bool hs = false, hm = false, he = false;
            { size_t p = g.body; Ref c;
              while (Next(in, n, p, g.end, c)) {
                  if (c.type == 0x01 && !hs) { st = c; hs = true; }
                  else if (c.type == 0x08 && !hm) { ml = c; hm = true; }
                  else if (c.type == 0x03 && !he) { ext = c; he = true; } } }
            if (!hs) { err = "geometria sem Struct"; return Result::Failed; }
            GStruct gs; std::string e2;
            if (!ParseGStruct(in, st, g.ver, false, gs, e2)) { err = e2; return Result::Failed; }
            if (gs.nver <= kMaxVerts) continue;   // fica como esta (copia literal)

            if (!ParseGStruct(in, st, g.ver, true, gs, e2)) { err = "geometria " + std::to_string(gi) + ": " + e2; return Result::Failed; }
            if (!hm) { err = "geometria " + std::to_string(gi) + " sem MaterialList"; return Result::Failed; }

            // ---- plugins da extensao
            BinMesh bm; bool haveBin = false;
            Ref skin{}, night{}; bool haveSkin = false, haveNight = false;
            std::vector<Ref> extKids;
            if (he) { size_t p = ext.body; Ref c;
                while (Next(in, n, p, ext.end, c)) {
                    extKids.push_back(c);
                    if (c.type == 0x50E) { if (!ParseBinMesh(in, c, bm, e2)) { err = "geometria " + std::to_string(gi) + ": " + e2; return Result::Failed; } haveBin = true; }
                    else if (c.type == 0x116) { skin = c; haveSkin = true; }
                    else if (c.type == 0x253F2F9) { night = c; haveNight = true; }
                    else if (c.type == 0x253F2FD) { err = "geometria " + std::to_string(gi) + ": plugin Breakable (0x253F2FD) nao pode ser dividido"; return Result::Failed; }
                } }

            // ---- triangulos (a verdade do desenho = Bin Mesh; sem ele, os da Struct)
            std::vector<Mesh> src;
            bool stripsIn = false;
            if (haveBin)
            {
                if (bm.flags != 0 && bm.flags != 1) { err = "geometria " + std::to_string(gi) + ": Bin Mesh com flags " + std::to_string(bm.flags) + " (so lista=0 e strip=1)"; return Result::Failed; }
                stripsIn = (bm.flags == 1);
                for (Mesh& m : bm.meshes)
                {
                    Mesh t; t.mat = m.mat;
                    if (bm.flags == 0)
                    {
                        const size_t k = m.idx.size() / 3;
                        if (m.idx.size() % 3) rep.notes.push_back("geometria " + std::to_string(gi) + ": mesh com indices sobrando (nao multiplo de 3), ignorados");
                        for (size_t i = 0; i < k; ++i) { t.idx.push_back(m.idx[i * 3]); t.idx.push_back(m.idx[i * 3 + 1]); t.idx.push_back(m.idx[i * 3 + 2]); }
                    }
                    else
                    {
                        for (size_t i = 0; i + 2 < m.idx.size(); ++i)
                        {
                            uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
                            if (a == b || b == c || a == c) continue;              // degenerado (juncao de strip)
                            if (i & 1) std::swap(a, b);                            // mantem o sentido (winding)
                            t.idx.push_back(a); t.idx.push_back(b); t.idx.push_back(c);
                        }
                        ++rep.stripsConverted;
                    }
                    src.push_back(std::move(t));
                }
            }
            else
            {
                // sem Bin Mesh: triangulos da Struct (16 bits) agrupados por material, ordem de aparicao
                Mesh cur; std::vector<Mesh> byMat;
                for (uint32_t i = 0; i < gs.ntri; ++i)
                {
                    const size_t o = st.body + gs.oTri + (size_t)i * 8;
                    const uint16_t v2 = (uint16_t)(in[o] | (in[o + 1] << 8)), v1 = (uint16_t)(in[o + 2] | (in[o + 3] << 8));
                    const uint16_t mt = (uint16_t)(in[o + 4] | (in[o + 5] << 8)), v3 = (uint16_t)(in[o + 6] | (in[o + 7] << 8));
                    size_t k = 0; for (; k < byMat.size(); ++k) if (byMat[k].mat == mt) break;
                    if (k == byMat.size()) { Mesh m; m.mat = mt; byMat.push_back(m); }
                    byMat[k].idx.push_back(v1); byMat[k].idx.push_back(v2); byMat[k].idx.push_back(v3);
                }
                src = std::move(byMat);
            }
            for (const Mesh& m : src) for (uint32_t v : m.idx)
                if (v >= gs.nver) { err = "geometria " + std::to_string(gi) + ": indice " + std::to_string(v) + " >= numVertices " + std::to_string(gs.nver); return Result::Failed; }

            // ---- particao gulosa: <= kMaxVerts vertices por parte, meshes na ordem original
            struct MeshOut { uint32_t mat; std::vector<uint32_t> idx; };
            struct Part { std::vector<uint32_t> oldIdx; std::vector<MeshOut> meshes; uint32_t tris = 0; };
            std::vector<Part> plist;
            std::vector<int32_t> remap(gs.nver, -1);
            Part cur; int meshSlot = -1;
            auto flush = [&]() {
                for (uint32_t o : cur.oldIdx) remap[o] = -1;
                if (cur.tris) plist.push_back(std::move(cur));
                cur = Part(); meshSlot = -1;
            };
            std::vector<uint8_t> used(gs.nver, 0);
            for (const Mesh& m : src)
            {
                meshSlot = -1;
                for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
                {
                    const uint32_t v[3] = { m.idx[i], m.idx[i + 1], m.idx[i + 2] };
                    if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) continue;
                    auto countNew = [&]() { uint32_t add = 0; for (int k = 0; k < 3; ++k) { if (remap[v[k]] >= 0) continue; bool dup = false; for (int j = 0; j < k; ++j) if (v[j] == v[k]) dup = true; if (!dup) ++add; } return add; };
                    uint32_t add = countNew();
                    if (cur.oldIdx.size() + add > kMaxVerts) { flush(); add = countNew(); }
                    if (meshSlot < 0) { cur.meshes.push_back({ m.mat, {} }); meshSlot = (int)cur.meshes.size() - 1; }
                    for (int k = 0; k < 3; ++k)
                    {
                        if (remap[v[k]] < 0) { remap[v[k]] = (int32_t)cur.oldIdx.size(); cur.oldIdx.push_back(v[k]); }
                        used[v[k]] = 1;
                        cur.meshes[meshSlot].idx.push_back((uint32_t)remap[v[k]]);
                    }
                    ++cur.tris;
                }
            }
            flush();
            if (plist.empty()) { err = "geometria " + std::to_string(gi) + " sem triangulos"; return Result::Failed; }
            for (uint32_t i = 0; i < gs.nver; ++i) if (!used[i]) ++rep.droppedUnused;

            // ---- valida plugins por vertice
            // Skin PLG: 4 bytes cabecalho, usedBones, 4 idx/vertice, 16 B pesos/vertice, matrizes (64 ou 68 B/osso), cauda (0 ou 12 B)
            size_t skUsed = 0, skMatBytes = 0, skTail = 0; uint32_t skBones = 0;
            if (haveSkin)
            {
                if (skin.size < 4) { err = "geometria " + std::to_string(gi) + ": Skin PLG truncado"; return Result::Failed; }
                skBones = in[skin.body]; skUsed = in[skin.body + 1];
                const uint64_t fixed = 4 + (uint64_t)skUsed + (uint64_t)gs.nver * 20;
                if (fixed > skin.size) { err = "geometria " + std::to_string(gi) + ": Skin PLG menor que os dados por vertice"; return Result::Failed; }
                const uint64_t rest = skin.size - fixed;
                bool ok = false;
                for (uint32_t stride : { 64u, 68u }) for (size_t tail : { (size_t)12, (size_t)0 })
                    if (!ok && rest == (uint64_t)skBones * stride + tail) { ok = true; skMatBytes = (size_t)skBones * stride; skTail = tail; }
                if (!ok) { err = "geometria " + std::to_string(gi) + ": Skin PLG com layout nao reconhecido (restam " + std::to_string(rest) + " bytes para " + std::to_string(skBones) + " ossos)"; return Result::Failed; }
                if (skTail == 12)
                {
                    const size_t tp = skin.body + (size_t)fixed + skMatBytes;
                    if (U32(in, tp + 4) != 0 || U32(in, tp + 8) != 0) { err = "geometria " + std::to_string(gi) + ": Skin PLG dividido (numMeshes/numRLE != 0) nao suportado"; return Result::Failed; }
                }
            }
            if (haveNight)
            {
                if (!(night.size == 4 || night.size == 4 + (uint64_t)gs.nver * 4)) { err = "geometria " + std::to_string(gi) + ": Night Vertex Colors com tamanho inesperado"; return Result::Failed; }
            }

            // ---- monta cada parte como um chunk Geometry completo
            const Ref* matList = &ml;
            for (const Part& pt : plist)
            {
                const uint32_t nv = (uint32_t)pt.oldIdx.size();
                W w; const size_t gAt = w.Begin(0x0F, g.ver);
                // Struct
                { const size_t sAt = w.Begin(0x01, st.ver);
                  uint32_t flags = gs.flags; if (stripsIn) flags &= ~1u;     // agora sao listas
                  w.U32(flags); w.U32(pt.tris); w.U32(nv); w.U32(1);
                  if (gs.oOld) w.Raw(in + st.body + gs.oOld, 12);
                  if (gs.flags & 0x8u) for (uint32_t o : pt.oldIdx) w.Raw(in + st.body + gs.oPre + (size_t)o * 4, 4);
                  for (uint32_t s = 0; s < gs.texSets; ++s) for (uint32_t o : pt.oldIdx) w.Raw(in + st.body + gs.oTex + ((size_t)s * gs.nver + o) * 8, 8);
                  for (const MeshOut& mo : pt.meshes)
                      for (size_t i = 0; i + 2 < mo.idx.size(); i += 3)
                      { w.U16((uint16_t)mo.idx[i + 1]); w.U16((uint16_t)mo.idx[i]); w.U16((uint16_t)mo.mat); w.U16((uint16_t)mo.idx[i + 2]); }   // [v2, v1, mat, v3]
                  float bs[4] = { gs.bs[0], gs.bs[1], gs.bs[2], gs.bs[3] };
                  if (gs.hasV && nv)
                  {
                      float mn[3] = {  1e30f,  1e30f,  1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
                      for (uint32_t o : pt.oldIdx) for (int k = 0; k < 3; ++k) { const float f = F32(in, st.body + gs.oV + (size_t)o * 12 + (size_t)k * 4); mn[k] = std::min(mn[k], f); mx[k] = std::max(mx[k], f); }
                      for (int k = 0; k < 3; ++k) bs[k] = (mn[k] + mx[k]) * 0.5f;
                      float r2 = 0;
                      for (uint32_t o : pt.oldIdx) { float s2 = 0; for (int k = 0; k < 3; ++k) { const float dlt = F32(in, st.body + gs.oV + (size_t)o * 12 + (size_t)k * 4) - bs[k]; s2 += dlt * dlt; } r2 = std::max(r2, s2); }
                      bs[3] = std::sqrt(r2);
                  }
                  for (int k = 0; k < 4; ++k) w.F32(bs[k]);
                  w.U32(gs.hasV ? 1u : 0u); w.U32(gs.hasN ? 1u : 0u);
                  if (gs.hasV) for (uint32_t o : pt.oldIdx) w.Raw(in + st.body + gs.oV + (size_t)o * 12, 12);
                  if (gs.hasN) for (uint32_t o : pt.oldIdx) w.Raw(in + st.body + gs.oN + (size_t)o * 12, 12);
                  w.End(sAt); }
                // MaterialList identica (indices de material preservados)
                w.Raw(in + matList->hdr, matList->end - matList->hdr);
                // Extension
                { const size_t eAt = w.Begin(0x03, he ? ext.ver : g.ver);
                  for (const Ref& c : extKids)
                  {
                      if (c.type == 0x50E)
                      {
                          const size_t bAt = w.Begin(0x50E, c.ver);
                          uint32_t tot = 0; for (const MeshOut& mo : pt.meshes) tot += (uint32_t)mo.idx.size();
                          w.U32(0); w.U32((uint32_t)pt.meshes.size()); w.U32(tot);
                          for (const MeshOut& mo : pt.meshes) { w.U32((uint32_t)mo.idx.size()); w.U32(mo.mat); for (uint32_t v : mo.idx) w.U32(v); }
                          w.End(bAt);
                      }
                      else if (c.type == 0x116)
                      {
                          const size_t kAt = w.Begin(0x116, c.ver);
                          w.Raw(in + c.body, 4 + skUsed);
                          const size_t pIdx = c.body + 4 + skUsed, pW = pIdx + (size_t)gs.nver * 4, pMat = pW + (size_t)gs.nver * 16;
                          for (uint32_t o : pt.oldIdx) w.Raw(in + pIdx + (size_t)o * 4, 4);
                          for (uint32_t o : pt.oldIdx) w.Raw(in + pW + (size_t)o * 16, 16);
                          w.Raw(in + pMat, skMatBytes + skTail);
                          w.End(kAt);
                      }
                      else if (c.type == 0x253F2F9)
                      {
                          const size_t kAt = w.Begin(0x253F2F9, c.ver);
                          w.Raw(in + c.body, 4);
                          if (c.size > 4) for (uint32_t o : pt.oldIdx) w.Raw(in + c.body + 4 + (size_t)o * 4, 4);
                          w.End(kAt);
                      }
                      else if (c.type == 0x253F2F8)
                      {
                          // 2dfx (luzes/particulas/efeitos): coordenadas de modelo, nao dependem dos vertices -> so na parte 0
                          if (&pt == &plist[0]) w.Raw(in + c.hdr, c.end - c.hdr);
                      }
                      else
                      {
                          if (&pt == &plist[0] && c.type != 0x105 && c.type != 0x116)
                              { char nb[96]; std::snprintf(nb, sizeof(nb), "geometria %zu: plugin 0x%x copiado para todas as partes", gi, c.type); rep.notes.push_back(nb); }
                          w.Raw(in + c.hdr, c.end - c.hdr);
                      }
                  }
                  w.End(eAt); }
                w.End(gAt);
                parts[gi].push_back(std::move(w.b));
            }
            rep.splitGeoms += 1;
            rep.partsCreated += (int)plist.size() - 1;
            firstExtra[gi] = (int)geoms.size() + appended;
            appended += (int)plist.size() - 1;
        }

        // ---- remonta o clump
        W w; const size_t cAt = w.Begin(0x10, clump.ver);
        // atomics por geometria: so os que apontam para uma geometria dividida ganham copias
        std::vector<int> atomicNew;   // numero de atomics extras
        int extraAtomics = 0;
        std::vector<int> geomOfAtomic(kids.size(), -1);
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const Ref& a = kids[i];
            if (a.type != 0x14) continue;
            size_t q = a.body; Ref s;
            if (!Next(in, n, q, a.end, s) || s.type != 0x01 || s.size < 16) { err = "Atomic sem Struct"; return Result::Failed; }
            const uint32_t gidx = U32(in, s.body + 4);
            if (gidx < geoms.size()) { geomOfAtomic[i] = (int)gidx; if (!parts[gidx].empty()) extraAtomics += (int)parts[gidx].size() - 1; }
        }
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const Ref& c = kids[i];
            if (c.type == 0x01 && i == 0 && c.size >= 4)
            {
                const size_t at = w.b.size();
                w.Raw(in + c.hdr, c.end - c.hdr);
                const uint32_t na = U32(in, c.body) + (uint32_t)extraAtomics;
                std::memcpy(w.b.data() + at + 12, &na, 4);
            }
            else if (c.type == 0x1A)
            {
                const size_t lAt = w.Begin(0x1A, c.ver);
                const size_t sAt = w.Begin(0x01, glStruct.ver);
                w.U32((uint32_t)(geoms.size() + appended));
                if (glStruct.size > 4) w.Raw(in + glStruct.body + 4, glStruct.size - 4);
                w.End(sAt);
                for (size_t gi = 0; gi < geoms.size(); ++gi)
                {
                    if (parts[gi].empty()) w.Raw(in + geoms[gi].hdr, geoms[gi].end - geoms[gi].hdr);
                    else w.Raw(parts[gi][0].data(), parts[gi][0].size());
                }
                for (size_t gi = 0; gi < geoms.size(); ++gi)
                    for (size_t k = 1; k < parts[gi].size(); ++k) w.Raw(parts[gi][k].data(), parts[gi][k].size());
                w.End(lAt);
            }
            else if (c.type == 0x14)
            {
                w.Raw(in + c.hdr, c.end - c.hdr);
                const int gi = geomOfAtomic[i];
                if (gi >= 0 && !parts[gi].empty())
                    for (size_t k = 1; k < parts[gi].size(); ++k)
                    {
                        const size_t at = w.b.size();
                        w.Raw(in + c.hdr, c.end - c.hdr);
                        const uint32_t ng = (uint32_t)(firstExtra[gi] + (int)k - 1);
                        std::memcpy(w.b.data() + at + 12 + 12 + 4, &ng, 4);   // Atomic > Struct > geometryIndex
                    }
            }
            else w.Raw(in + c.hdr, c.end - c.hdr);
        }
        w.End(cAt);

        out = std::move(w.b);
        std::string e3;
        if (!Inspect(out.data(), out.size(), rep.out, e3)) { err = "saida invalida: " + e3; return Result::Failed; }
        if (rep.out.maxGeomVerts > kMaxVerts) { err = "saida ainda tem geometria > 65535 vertices"; return Result::Failed; }
        return Result::Split;
    }

    // resumo de uma linha para o log ([DFF])
    inline std::string Summary(const Report& r)
    {
        char b[320];
        std::snprintf(b, sizeof(b),
            "geometrias %d->%d atomics %d->%d verts %llu->%llu tris(bin) %llu->%llu maxIdx %u->%u maxGeomVerts %u->%u bytes %zu->%zu",
            r.in.geoms, r.out.geoms, r.in.atomics, r.out.atomics,
            (unsigned long long)r.in.verts, (unsigned long long)r.out.verts,
            (unsigned long long)r.in.binTris, (unsigned long long)r.out.binTris,
            r.in.maxIndex, r.out.maxIndex, r.in.maxGeomVerts, r.out.maxGeomVerts, r.in.bytes, r.out.bytes);
        return b;
    }
}
