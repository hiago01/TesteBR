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
                    ++out.geoms;
                    size_t t = g.body; Chunk x{};
                    while (Next(d, n, t, g.end, x))
                    {
                        if (x.type == 0x01 && x.size >= 16)
                        {
                            const uint32_t flags = U32(d, x.body);
                            out.tris  += U32(d, x.body + 4);
                            out.verts += U32(d, x.body + 8);
                            if (flags & 0x01000000u) out.nativeGeometry = true;
                        }
                        else if (x.type == 0x08)       // MaterialList
                        {
                            size_t m = x.body; Chunk y{};
                            while (Next(d, n, m, x.end, y)) if (y.type == 0x07) ++out.materials;
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
                            }
                        }
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
