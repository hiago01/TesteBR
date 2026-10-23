#pragma once

// ---------------------------------------------------------------------------
// Leitor PURO (sem dependencia do jogo) de arquivo .COL "arquivo de colisoes"
// (varios modelos dentro de um unico .col, ex.: countrye_10.col com CE_brewery).
//
// Formato de cada entrada (padrao GTA SA, todas as versoes COLL/COL2/COL3/COL4):
//   +0   char[4]  ident   "COLL" | "COL2" | "COL3" | "COL4"
//   +4   uint32   size    bytes que vem DEPOIS destes 8 (inclui name[22] + modelId[2])
//   +8   char[22] name    nome do modelo (ex.: "CE_brewery"), pode nao ter '\0' final
//   +30  uint16   modelId (em arquivos soltos costuma ser 0; o jogo liga pelo NOME)
//   +32  ...      dados da colisao (size - 24 bytes)
//   proxima entrada = offset + 8 + size
//
// Usado pelo CustomCol.h (jogo) e pelo testes/host_test_customcol.cpp (host, g++).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <strings.h>
#include <vector>

namespace CustomColParse
{
    enum class Status : uint8_t
    {
        Ok = 0,           // arquivo percorrido ate o fim, sem sobra estranha
        Empty,            // 0 bytes (ou so zeros)
        TruncatedHeader,  // sobrou < 8 bytes diferentes de zero no fim
        BadIdent,         // ident nao e COLL/COL2/COL3/COL4 (arquivo corrompido ou nao e .col)
        BadSize,          // size < 24 (nao cabe nome + modelId)
        Overrun           // size passa do fim do arquivo (arquivo cortado)
    };

    struct Entry
    {
        uint32_t offset;     // onde o ident comeca
        uint32_t size;       // campo size do cabecalho
        uint16_t modelId;
        uint8_t  version;    // 1=COLL 2=COL2 3=COL3 4=COL4
        char     ident[5];
        char     name[23];
    };

    struct Result
    {
        Status             status = Status::Empty;
        uint32_t           failOffset = 0;   // offset onde parou (se status != Ok)
        uint32_t           bytesOk = 0;      // bytes consumidos por entradas validas
        std::vector<Entry> entries;
    };

    inline const char* StatusStr(Status s)
    {
        switch (s)
        {
            case Status::Ok:              return "OK";
            case Status::Empty:           return "VAZIO";
            case Status::TruncatedHeader: return "CABECALHO TRUNCADO no fim";
            case Status::BadIdent:        return "IDENT INVALIDO (nao e COLL/COL2/COL3/COL4)";
            case Status::BadSize:         return "SIZE INVALIDO (< 24)";
            case Status::Overrun:         return "ENTRADA PASSA DO FIM DO ARQUIVO (arquivo cortado?)";
        }
        return "?";
    }

    inline uint8_t VersionOf(const uint8_t* p)
    {
        if (std::memcmp(p, "COLL", 4) == 0) return 1;
        if (std::memcmp(p, "COL2", 4) == 0) return 2;
        if (std::memcmp(p, "COL3", 4) == 0) return 3;
        if (std::memcmp(p, "COL4", 4) == 0) return 4;
        return 0;
    }

    inline Result Parse(const uint8_t* d, size_t n)
    {
        Result r;
        size_t off = 0;
        while (off < n)
        {
            const size_t left = n - off;
            if (left < 8)
            {
                bool allZero = true;
                for (size_t i = 0; i < left; i++) if (d[off + i]) { allZero = false; break; }
                r.status = allZero ? Status::Ok : Status::TruncatedHeader;
                r.failOffset = (uint32_t)off;
                if (allZero && r.entries.empty()) r.status = Status::Empty;
                return r;
            }
            const uint8_t ver = VersionOf(d + off);
            if (!ver)
            {
                // preenchimento com zeros no fim e' tolerado (alguns empacotadores alinham em setor)
                bool allZero = true;
                for (size_t i = off; i < n; i++) if (d[i]) { allZero = false; break; }
                r.failOffset = (uint32_t)off;
                r.status = (allZero && !r.entries.empty()) ? Status::Ok : Status::BadIdent;
                return r;
            }
            uint32_t size = 0;
            std::memcpy(&size, d + off + 4, 4);
            if (size < 24) { r.status = Status::BadSize; r.failOffset = (uint32_t)off; return r; }
            if ((uint64_t)off + 8u + size > n) { r.status = Status::Overrun; r.failOffset = (uint32_t)off; return r; }

            Entry e{};
            e.offset = (uint32_t)off;
            e.size = size;
            e.version = ver;
            std::memcpy(e.ident, d + off, 4); e.ident[4] = 0;
            std::memcpy(e.name, d + off + 8, 22); e.name[22] = 0;
            std::memcpy(&e.modelId, d + off + 30, 2);
            r.entries.push_back(e);
            off += 8u + size;
            r.bytesOk = (uint32_t)off;
        }
        r.status = r.entries.empty() ? Status::Empty : Status::Ok;
        r.failOffset = (uint32_t)n;
        return r;
    }

    // indice da entrada com esse nome (sem diferenciar maiusculas), ou -1
    inline int Find(const Result& r, const char* name)
    {
        for (size_t i = 0; i < r.entries.size(); i++)
            if (::strncasecmp(r.entries[i].name, name, 22) == 0) return (int)i;
        return -1;
    }
}
