#pragma once
// ---------------------------------------------------------------------------
// CustomDist - aumenta a distancia de desenho (m_fDrawDistance) de modelos especificos.
// Padrao: 3985 PershingSq1_LAn (predio/base da praca ao norte da DP) 150 -> 300 m.
// Opcional: <files>/SAMP/draw_dist.txt, 1 linha por modelo:  <id> <distancia>   (# comenta). Linhas do arquivo substituem o padrao.
// Leve: so mexe em 1 float por modelo, a cada ~120 frames (reaplica caso o ModelInfo seja recriado). Nao altera IDE/IPL/colisao.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../main.h"
#include "Models/ModelInfo.h"

namespace CustomDist
{
    struct Entry { int id; float dist; float orig; bool logged; };
    inline std::vector<Entry>& List() { static std::vector<Entry> v; return v; }
    inline bool& Loaded() { static bool b = false; return b; }

    inline void LoadConfig()
    {
        auto& L = List();
        L.push_back({3985, 300.0f, -1.0f, false});
        std::string path;
        if (g_pszStorage && g_pszStorage[0]) { path = g_pszStorage; if (path.back() != '/') path.push_back('/'); path += "SAMP/draw_dist.txt"; }
        FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
        if (!f) return;
        char line[128];
        bool first = true;
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            int id; float d;
            if (sscanf(line, "%d %f", &id, &d) != 2 || id < 0 || d < 10.0f || d > 1500.0f) continue;
            if (first) { L.clear(); first = false; }
            L.push_back({id, d, -1.0f, false});
        }
        fclose(f);
        FLog("[DIST] draw_dist.txt: %d modelo(s)", (int)L.size());
    }

    // Chamar 1x por frame (CGame::Process).
    inline void Tick()
    {
        static int cnt = 0;
        if (++cnt < 120) return;
        cnt = 0;
        if (!Loaded()) { Loaded() = true; LoadConfig(); }
        for (auto& e : List()) {
            if (e.id < 0 || e.id >= (int)TOTAL_DFF_MODEL_IDS) continue;
            auto* mi = CModelInfo::GetModelInfo(e.id);
            if (!mi) continue;
            if (mi->m_fDrawDistance != e.dist) {
                if (e.orig < 0) e.orig = mi->m_fDrawDistance;
                mi->m_fDrawDistance = e.dist;
                if (!e.logged) { FLog("[DIST] modelo %d: distancia %.0f -> %.0f", e.id, e.orig, e.dist); e.logged = true; }
            }
        }
    }
}
