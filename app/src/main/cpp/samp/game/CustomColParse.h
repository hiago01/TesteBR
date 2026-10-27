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
#include <cstdio>

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
    // ======================================================================
    //  VALIDACAO PROFUNDA (somente leitura) de UMA entrada COL2/COL3/COL4
    //  Layout (confirmado no countrye_10.col do usuario; offsets sao relativos a (entry+4),
    //  ou seja, ao byte logo depois do ident):
    //    +32 bounds[40]: min(3f) max(3f) center(3f) radius(f)
    //    +72 u16 nSph | u16 nBox | u16 nFaces | u8 nLines | u8 pad | u32 flags
    //    +84 u32 offSph, offBox, offLines, offVerts, offFaces      (COL2: dados comecam em +100)
    //    +100 u32 offTriPlanes, nShadowFaces, offShadowVerts, offShadowFaces (COL3/4, dados comecam em +116)
    //  flags: bit1(0x2)=nao vazio, bit3(0x8)=tem face groups (u32 n + n*28 bytes imediatamente ANTES das faces)
    //  Vertices ficam entre offVerts e (face groups | offFaces): 3 x int16 (1/128). Face = u16 a,b,c + u8 material + u8 luz.
    // ======================================================================
    inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
    inline uint16_t Rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
    inline float    RdF (const uint8_t* p) { float    v; std::memcpy(&v, p, 4); return v; }

    // retorna true se a entrada e' segura de entregar ao loader nativo. 'why' recebe o motivo da recusa.
    inline bool ValidateEntry(const uint8_t* d, size_t n, const Entry& e, char* why, size_t whySz)
    {
        auto fail = [&](const char* m, uint32_t a = 0, uint32_t b = 0) {
            std::snprintf(why, whySz, "%s (%s: %u / %u)", m, e.name, a, b); return false; };
        if (why && whySz) why[0] = 0;
        if (e.version < 2) return true;                       // COLL (v1): sem offsets, o loader nativo valida
        const uint32_t hdr = (e.version == 2) ? 100u : 116u;  // 1o offset possivel (relativo a entry+4) = fim do cabecalho
        const uint32_t lim = 4u + e.size;                     // fim dos dados, relativo a entry+4
        if ((uint64_t)e.offset + 8u + e.size > n) return fail("entrada passa do fim do buffer");
        if (lim < hdr) return fail("size menor que o cabecalho", e.size, hdr);
        const uint8_t* b = d + e.offset + 4;                  // base dos offsets
        for (int i = 0; i < 10; i++) { float f = RdF(b + 28 + 4 * i); if (!(f == f) || f > 1e7f || f < -1e7f) return fail("bounds invalido (NaN/inf/enorme)", i); }
        if (RdF(b + 28) > RdF(b + 40) || RdF(b + 32) > RdF(b + 44) || RdF(b + 36) > RdF(b + 48)) return fail("bounds min > max");
        const uint32_t ns = Rd16(b + 68), nb = Rd16(b + 70), nf = Rd16(b + 72), nl = b[74];
        const uint32_t flags = Rd32(b + 76);
        const uint32_t oSph = Rd32(b + 80), oBox = Rd32(b + 84), oLin = Rd32(b + 88), oVer = Rd32(b + 92), oFac = Rd32(b + 96);
        (void)nl; (void)oLin;
        if (ns && (oSph < hdr || (uint64_t)oSph + ns * 20ull > lim)) return fail("esferas fora dos limites", oSph, ns);
        if (nb && (oBox < hdr || (uint64_t)oBox + nb * 28ull > lim)) return fail("caixas fora dos limites", oBox, nb);
        if (nf)
        {
            if (oFac < hdr || (uint64_t)oFac + nf * 8ull > lim) return fail("faces fora dos limites", oFac, nf);
            if (oVer < hdr || oVer >= oFac) return fail("offset de vertices invalido", oVer, oFac);
            uint32_t groupBytes = 0, ngrp = 0;
            if (flags & 8u)
            {
                if (oFac < oVer + 4u) return fail("face groups: sem espaco", oVer, oFac);
                ngrp = Rd32(b + oFac - 4);
                if ((uint64_t)ngrp * 28ull + 4ull > (uint64_t)(oFac - oVer)) return fail("face groups: contagem nao cabe", ngrp, oFac - oVer);
                groupBytes = 4u + ngrp * 28u;
            }
            const uint32_t nv = (oFac - oVer - groupBytes) / 6u;
            if (nv == 0) return fail("sem vertices");
            uint32_t maxIdx = 0;
            for (uint32_t i = 0; i < nf; i++)
            {
                const uint8_t* f = b + oFac + 8u * i;
                for (int k = 0; k < 3; k++) { uint32_t ix = Rd16(f + 2 * k); if (ix > maxIdx) maxIdx = ix; }
            }
            if (maxIdx >= nv) return fail("indice de triangulo >= numero de vertices", maxIdx, nv);
            for (uint32_t g = 0; g < ngrp; g++)
            {
                const uint8_t* gp = b + oFac - 4u - ngrp * 28u + g * 28u;
                const uint32_t s = Rd16(gp + 24), en = Rd16(gp + 26);
                if (s > en || en >= nf) return fail("face group com intervalo invalido", s, en);
            }
        }
        return true;
    }

    // valida todas as entradas; devolve true se TODAS passam
    inline bool ValidateAll(const uint8_t* d, size_t n, const Result& r, char* why, size_t whySz)
    {
        if (r.status != Status::Ok || r.entries.empty()) { std::snprintf(why, whySz, "estrutura do arquivo: %s", StatusStr(r.status)); return false; }
        for (const auto& e : r.entries) if (!ValidateEntry(d, n, e, why, whySz)) return false;
        return true;
    }

    // O buffer 'in' (entregue pelo streaming ao CColStore::LoadCol) e' o MESMO arquivo que 'custom'?
    // Regra: 1a entrada com o mesmo nome E pelo menos metade dos nomes de 'in' existem em 'custom'.
    // So percorre cabecalhos (8 bytes por entrada): barato mesmo chamado para todo .col do jogo.
    inline bool SameCollection(const Result& custom, const uint8_t* in, size_t n)
    {
        if (custom.entries.empty() || !in || n < 32) return false;
        size_t off = 0, total = 0, common = 0;
        while (off + 32 <= n)
        {
            if (!VersionOf(in + off)) break;
            uint32_t sz = 0; std::memcpy(&sz, in + off + 4, 4);
            if (sz < 24 || (uint64_t)off + 8u + sz > n) break;
            char nm[23]; std::memcpy(nm, in + off + 8, 22); nm[22] = 0;
            if (total == 0 && ::strncasecmp(nm, custom.entries[0].name, 22) != 0) return false;
            total++;
            for (const auto& c : custom.entries) if (::strncasecmp(nm, c.name, 22) == 0) { common++; break; }
            off += 8u + sz;
        }
        return total > 0 && common * 2 >= total;
    }
}
