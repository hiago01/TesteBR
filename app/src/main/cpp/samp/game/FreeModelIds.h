#pragma once

// ---------------------------------------------------------------------------
// IDs de modelo LIVRES (para objetos novos via IDE/IPL). So le: nao altera nada do jogo.
// Depois que TODOS os IDEs foram lidos (CGame::Initialize), um ID esta livre quando CModelInfo::ms_modelInfoPtrs[id] == nullptr.
// Loga (samp_log.txt, prefixo [MODELID]) os maiores blocos livres e grava SAMP/model_ids_livres.txt (um bloco por linha).
// O limite de IDs deste cliente e' TOTAL_DFF_MODEL_IDS (constants.h): 0..19999.
// ---------------------------------------------------------------------------

#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <functional>

#include "../main.h"          // FLog, g_pszStorage
#include "game/Models/ModelInfo.h"
#include "constants.h"

namespace FreeModelIds
{
    struct Range { int first, last; int Len() const { return last - first + 1; } };

    // blocos contiguos de IDs livres em [lo, hi]; isFree(id) = true quando nao ha ModelInfo
    inline std::vector<Range> FreeRanges(int lo, int hi, const std::function<bool(int)>& isFree)
    {
        std::vector<Range> v; int start = -1;
        for (int id = lo; id <= hi; id++)
        {
            if (isFree(id)) { if (start < 0) start = id; }
            else if (start >= 0) { v.push_back({ start, id - 1 }); start = -1; }
        }
        if (start >= 0) v.push_back({ start, hi });
        return v;
    }

    inline void Report(const char* quando)
    {
        const int hi = (int)TOTAL_DFF_MODEL_IDS - 1;
        std::vector<Range> r = FreeRanges(0, hi, [](int id) { return CModelInfo::ms_modelInfoPtrs[id] == nullptr; });
        int total = 0; for (auto& x : r) total += x.Len();
        std::sort(r.begin(), r.end(), [](const Range& a, const Range& b) { return a.Len() != b.Len() ? a.Len() > b.Len() : a.first < b.first; });
        FLog("[MODELID][%s] ids livres: %d de %d | %zu bloco(s). Maiores blocos (inicio-fim, tamanho):", quando, total, hi + 1, r.size());
        for (size_t i = 0; i < r.size() && i < 12; i++) FLog("[MODELID]   %d-%d  (%d)", r[i].first, r[i].last, r[i].Len());

        if (g_pszStorage && g_pszStorage[0])
        {
            std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/'); p += "SAMP/model_ids_livres.txt";
            if (FILE* f = std::fopen(p.c_str(), "w"))
            {
                std::fprintf(f, "# ids de modelo livres depois de ler todos os IDEs (%s). inicio-fim tamanho\n", quando);
                std::sort(r.begin(), r.end(), [](const Range& a, const Range& b) { return a.first < b.first; });
                for (auto& x : r) std::fprintf(f, "%d-%d %d\n", x.first, x.last, x.Len());
                std::fclose(f);
            }
        }
    }
}
