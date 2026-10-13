#pragma once

// ---------------------------------------------------------------------------
// Pré-validação de DFF (RenderWare) SEM tocar no engine: só percorre os chunks
// com checagem de limites. Serve para recusar arquivo corrompido/inadequado
// antes de entregá-lo ao RpClumpStreamRead (o RW quase não valida limites).
//
// Estrutura lida: Clump > { Struct, FrameList, GeometryList, Atomic..., Extension }
//   FrameList > extensões por frame > HAnim PLG (0x11E)  (IDs dos ossos)
//   Geometry  > Struct (flags, tris, verts), MaterialList, Extension > Skin PLG (0x116)
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace DffPreflight
{
    struct MaterialInfo
    {
        int         geom    = 0;        // índice da geometria
        int         index   = 0;        // índice do material dentro da geometria
        std::string texture;            // nome da textura (vazio se não texturizado)
        std::string mask;
        uint8_t     r = 255, g = 255, b = 255, a = 255;
        bool        textured = false;
        uint32_t    tris     = 0;       // triângulos que o jogo desenha com este material
        bool        trisFromBinMesh = false;
    };

    struct Info
    {
        uint32_t stamp          = 0;      // versão RW (ex.: 0x1803FFFF = 3.6.0.3, GTA SA)
        int      atomicsDecl    = 0;      // numAtomics do struct do clump
        int      atomics        = 0;      // chunks Atomic encontrados
        int      frames         = 0;
        int      geoms          = 0;
        uint64_t tris           = 0;
        uint64_t verts          = 0;
        int      materials      = 0;
        bool     skin           = false;
        int      bones          = 0;
        int      usedBones      = 0;
        int      maxWeights     = 0;
        int      hanimFrames    = 0;
        bool     nativeGeometry = false;  // geometria "native" = dados específicos de plataforma
        std::set<int> hanimIds;
        std::set<std::string> frameNames;   // nomes dos frames (plugin 0x253F2FE)
        std::vector<MaterialInfo> materialsList;   // todos os materiais, na ordem das geometrias
    };

    // IDs de ossos do esqueleto de ped do SA
    inline bool IsPedBoneId(int id)
    {
        static const int ids[] = {0,1,2,3,4,5,6,7,8,21,22,23,24,25,26,27,31,32,33,34,35,36,37,
                                  41,42,43,44,51,52,53,54,201,301,302};
        for (int v : ids) if (v == id) return true;
        return false;
    }

    namespace detail
    {
        struct Chunk { uint32_t type, size, ver; size_t body, end; };

        inline uint32_t U32(const uint8_t* d, size_t o) { uint32_t v; std::memcpy(&v, d + o, 4); return v; }

        // lê o próximo chunk dentro de [pos, parentEnd); avança pos; false se não couber
        inline bool Next(const uint8_t* d, size_t n, size_t& pos, size_t parentEnd, Chunk& c)
        {
            if (parentEnd > n || pos > parentEnd || parentEnd - pos < 12) return false;
            c.type = U32(d, pos); c.size = U32(d, pos + 4); c.ver = U32(d, pos + 8);
            c.body = pos + 12;
            if (c.size > parentEnd - c.body) return false;
            c.end = c.body + c.size;
            pos = c.end;
            return true;
        }
    }

    inline bool Parse(const uint8_t* d, size_t n, Info& out, std::string& err)
    {
        using namespace detail;
        size_t pos = 0;
        Chunk clump{};
        if (n < 12 || !Next(d, n, pos, n, clump) || clump.type != 0x10)
        {
            err = "não é um DFF (chunk Clump 0x10 ausente ou tamanho inválido)";
            return false;
        }
        out.stamp = clump.ver;

        size_t p = clump.body;
        Chunk c{};
        while (Next(d, n, p, clump.end, c))
        {
            if (c.type == 0x01 && c.size >= 4)
            {
                out.atomicsDecl = (int)U32(d, c.body);
            }
            else if (c.type == 0x0E)            // FrameList
            {
                size_t q = c.body; Chunk s{};
                if (!Next(d, n, q, c.end, s) || s.type != 0x01 || s.size < 4)
                {
                    err = "FrameList inválida";
                    return false;
                }
                out.frames = (int)U32(d, s.body);
                if (out.frames <= 0 || out.frames > 1024)
                {
                    err = "número de frames fora do limite";
                    return false;
                }
                Chunk e{};
                while (Next(d, n, q, c.end, e))
                {
                    if (e.type != 0x03) continue;
                    size_t t = e.body; Chunk x{};
                    while (Next(d, n, t, e.end, x))
                    {
                        if (x.type == 0x11E && x.size >= 12)   // HAnim PLG: version, nodeId, numNodes
                        {
                            out.hanimIds.insert((int)U32(d, x.body + 4));
                            ++out.hanimFrames;
                        }
                        else if (x.type == 0x253F2FE && x.size > 0 && x.size < 64)   // nome do frame
                        {
                            std::string nm((const char*)d + x.body, x.size);
                            nm.resize(std::strlen(nm.c_str()));
                            out.frameNames.insert(nm);
                        }
                    }
                }
            }
            else if (c.type == 0x1A)            // GeometryList
            {
                size_t q = c.body; Chunk g{};
                while (Next(d, n, q, c.end, g))
                {
                    if (g.type != 0x0F) continue;
                    const int geomIndex = out.geoms++;
                    const size_t firstMat = out.materialsList.size();
                    std::vector<uint32_t> triByStructId;     // contagem pelos ids gravados nos triângulos
                    std::vector<uint32_t> triByBinMesh;      // contagem pelo BinMesh PLG (o que o jogo desenha)
                    bool haveBinMesh = false;
                    size_t t = g.body; Chunk x{};
                    while (Next(d, n, t, g.end, x))
                    {
                        if (x.type == 0x01 && x.size >= 16)
                        {
                            const uint32_t flags = U32(d, x.body);
                            const uint32_t ntri  = U32(d, x.body + 4);
                            const uint32_t nver  = U32(d, x.body + 8);
                            out.tris  += ntri;
                            out.verts += nver;
                            if (flags & 0x01000000u) out.nativeGeometry = true;

                            // ids de material gravados nos triângulos (formato 3.4+: sem as 3 floats de iluminação)
                            if (!(flags & 0x01000000u) && nver < 0x1000000u && ntri < 0x1000000u)
                            {
                                size_t p = x.body + 16;
                                if (flags & 0x8u) p += (size_t)nver * 4;
                                uint32_t texSets = (flags >> 16) & 0xFFu;
                                if (texSets == 0) texSets = (flags & 0x4u) ? 1u : ((flags & 0x80u) ? 2u : 0u);
                                p += (size_t)texSets * nver * 8;
                                const size_t need = (size_t)ntri * 8;
                                if (p <= x.end && need <= x.end - p)
                                {
                                    for (uint32_t k = 0; k < ntri; ++k)
                                    {
                                        const uint16_t mid = (uint16_t)(d[p + k * 8 + 4] | (d[p + k * 8 + 5] << 8));
                                        if (mid < 4096)
                                        {
                                            if (triByStructId.size() <= mid) triByStructId.resize((size_t)mid + 1, 0);
                                            ++triByStructId[mid];
                                        }
                                    }
                                }
                            }
                        }
                        else if (x.type == 0x08)       // MaterialList
                        {
                            size_t m = x.body; Chunk y{};
                            while (Next(d, n, m, x.end, y))
                            {
                                if (y.type != 0x07) continue;
                                ++out.materials;
                                MaterialInfo mi;
                                mi.geom  = geomIndex;
                                mi.index = (int)(out.materialsList.size() - firstMat);
                                size_t q = y.body; Chunk z{};
                                while (Next(d, n, q, y.end, z))
                                {
                                    if (z.type == 0x01 && z.size >= 16)   // flags, color RGBA, unused, textured
                                    {
                                        mi.r = d[z.body + 4]; mi.g = d[z.body + 5]; mi.b = d[z.body + 6]; mi.a = d[z.body + 7];
                                        mi.textured = U32(d, z.body + 12) != 0;
                                    }
                                    else if (z.type == 0x06)               // Texture: struct, nome, máscara
                                    {
                                        size_t w = z.body; Chunk s{};
                                        int strIdx = 0;
                                        while (Next(d, n, w, z.end, s))
                                        {
                                            if (s.type != 0x02 || s.size == 0 || s.size > 256) continue;
                                            std::string nm((const char*)d + s.body, s.size);
                                            nm.resize(std::strlen(nm.c_str()));
                                            if (strIdx == 0) mi.texture = nm; else if (strIdx == 1) mi.mask = nm;
                                            ++strIdx;
                                        }
                                    }
                                }
                                out.materialsList.push_back(mi);
                            }
                        }
                        else if (x.type == 0x03)       // Extension da geometria
                        {
                            size_t m = x.body; Chunk y{};
                            while (Next(d, n, m, x.end, y))
                            {
                                if (y.type == 0x116 && y.size >= 4)   // Skin PLG
                                {
                                    out.skin       = true;
                                    out.bones      = d[y.body];
                                    out.usedBones  = d[y.body + 1];
                                    out.maxWeights = d[y.body + 2];
                                }
                                else if (y.type == 0x50E && y.size >= 12)   // BinMesh PLG: flags, numMeshes, totalIndices
                                {
                                    const uint32_t bflags = U32(d, y.body);
                                    const uint32_t nmesh  = U32(d, y.body + 4);
                                    size_t r = y.body + 12;
                                    bool good = nmesh <= 4096;
                                    for (uint32_t k = 0; good && k < nmesh; ++k)
                                    {
                                        if (r + 8 > y.end) { good = false; break; }
                                        const uint32_t nidx = U32(d, r);
                                        const uint32_t midx = U32(d, r + 4);
                                        if (nidx > (y.end - r) / 4 || midx >= 4096) { good = false; break; }
                                        const uint32_t tris = (bflags == 0) ? nidx / 3 : (nidx > 2 ? nidx - 2 : 0);
                                        if (triByBinMesh.size() <= midx) triByBinMesh.resize((size_t)midx + 1, 0);
                                        triByBinMesh[midx] += tris;
                                        r += 8 + (size_t)nidx * 4;
                                    }
                                    haveBinMesh = good;
                                }
                            }
                        }
                    }
                    // triângulos por material: o BinMesh manda (é o que o RW desenha); senão, os ids dos triângulos
                    for (size_t k = firstMat; k < out.materialsList.size(); ++k)
                    {
                        MaterialInfo& mi = out.materialsList[k];
                        const std::vector<uint32_t>& src = haveBinMesh ? triByBinMesh : triByStructId;
                        mi.tris = (size_t)mi.index < src.size() ? src[(size_t)mi.index] : 0;
                        mi.trisFromBinMesh = haveBinMesh;
                    }
                }
            }
            else if (c.type == 0x14)            // Atomic
            {
                ++out.atomics;
            }
        }

        // ---- erros que impedem o uso como skin de ped ----
        if (out.atomics <= 0 || out.geoms <= 0) { err = "DFF sem atomic/geometria"; return false; }
        if (out.frames <= 1)                    { err = "DFF sem hierarquia de frames"; return false; }
        if (out.nativeGeometry)                 { err = "geometria 'native' (dados de plataforma); exporte como DFF PC/RenderWare normal"; return false; }
        if (!out.skin)                          { err = "geometria sem Skin PLG: não funciona como ped"; return false; }
        if (out.hanimFrames <= 0)               { err = "frames sem HAnim PLG (IDs de osso)"; return false; }
        return true;
    }

    // avisos não-fatais (ex.: modelo pesado, ossos fora do padrão)
    inline std::vector<std::string> Warnings(const Info& i)
    {
        std::vector<std::string> w;
        if (i.tris > 20000)
            w.push_back(std::to_string((unsigned long long)i.tris) + " triangulos (ped normal ~2k-5k): pesado no celular");
        if (i.materials > 25)
            w.push_back(std::to_string(i.materials) + " materiais (cada um vira 1 draw call por ped)");
        if (i.verts > 65000)
            w.push_back("mais de 65000 vertices (limite de indice 16 bits)");
        if (i.bones > 40)
            w.push_back("mais de 40 ossos no skin (limite de matrizes do shader GLES)");
        if (i.maxWeights > 4)
            w.push_back("mais de 4 pesos por vertice (o pipeline do SA usa 4)");
        // frames-marcador que CPedModelInfo::SetFrameIds procura por nome (tabela m_pPedIds da libGTASA)
        static const char* const markers[] = {"Smid","Shead","Supperarml","Supperarmr","SLhand","SRhand",
                                              "Supperlegl","Supperlegr","Sfootl","Sfootr","Slowerlegl","Slowerlegr"};
        int missing = 0;
        for (const char* m : markers) if (i.frameNames.find(m) == i.frameNames.end()) ++missing;
        if (missing > 0)
            w.push_back(std::to_string(missing) + " de 12 frames-marcador ausentes (Smid, Shead, Supperarml...): o jogo ignora, mas dano/impacto por parte do corpo pode falhar");
        int foreign = 0;
        for (int id : i.hanimIds) if (!IsPedBoneId(id)) ++foreign;
        if (foreign > 0)
            w.push_back(std::to_string(foreign) + " ID(s) HAnim fora do esqueleto padrao de ped do SA");
        return w;
    }

    inline std::string VersionString(uint32_t stamp)
    {
        char b[48];
        if (stamp & 0xFFFF0000u)
        {
            const uint32_t v = (((stamp >> 14) & 0x3FF00u) + 0x30000u) | ((stamp >> 16) & 0x3Fu);
            std::snprintf(b, sizeof(b), "%x.%x.%x.%x", (v >> 16) & 0xF, (v >> 12) & 0xF, (v >> 8) & 0xF, v & 0xFF);
        }
        else
        {
            std::snprintf(b, sizeof(b), "%x", stamp >> 8);
        }
        return b;
    }
}
