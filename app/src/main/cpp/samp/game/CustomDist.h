#pragma once
// ---------------------------------------------------------------------------
// CustomDist - ajusta, em tempo de execucao, por modelo:
//   * distancia de desenho (m_fDrawDistance)
//   * raio da esfera de visibilidade (CColModel::m_boundSphere.m_fRadius) -> usada no teste de frustum da camera.
// Motivo: PershingSq1_LAn (3985) tem esfera de apenas ~54.6 m centrada na origem, mas o predio vai a ~70 m do centro
// (cantos do telhado/base). Com a esfera pequena, o jogo descarta o predio inteiro quando o CENTRO sai da tela e as
// pontas "piscam". Aumentar o raio so desliga esse corte precoce desse modelo.
// Padrao: 3985 dist 300 raio 110 ; 4210 (LODshingSq1_LAn) raio 110.
// Opcional: <files>/SAMP/draw_dist.txt, 1 linha por modelo:  <id> <distancia|0> [raio]   (# comenta; 0 = nao muda).
// Se o arquivo existir, as linhas dele substituem o padrao. Leve: so floats, a cada ~120 frames.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../main.h"
#include "Models/ModelInfo.h"
#include "Collision/ColModel.h"

namespace CustomDist
{
    struct Entry { int id; float dist; float radius; bool logged; };
    inline std::vector<Entry>& List() { static std::vector<Entry> v; return v; }
    inline bool& Loaded() { static bool b = false; return b; }

    inline void LoadConfig()
    {
        auto& L = List();
        L.push_back({3985, 300.0f, 110.0f, false});
        L.push_back({4210, 0.0f, 110.0f, false});
        std::string path;
        if (g_pszStorage && g_pszStorage[0]) { path = g_pszStorage; if (path.back() != '/') path.push_back('/'); path += "SAMP/draw_dist.txt"; }
        FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
        if (!f) return;
        char line[128];
        bool first = true;
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            int id; float d = 0, r = 0;
            int n = sscanf(line, "%d %f %f", &id, &d, &r);
            if (n < 2 || id < 0) continue;
            if (d != 0.0f && (d < 10.0f || d > 1500.0f)) continue;
            if (r != 0.0f && (r < 5.0f || r > 1500.0f)) continue;
            if (first) { L.clear(); first = false; }
            L.push_back({id, d, r, false});
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
            float oldD = mi->m_fDrawDistance, oldR = -1.0f;
            if (e.dist > 0.0f && mi->m_fDrawDistance != e.dist) mi->m_fDrawDistance = e.dist;
            if (e.radius > 0.0f && mi->m_pColModel) {
                oldR = mi->m_pColModel->m_boundSphere.m_fRadius;
                if (oldR < e.radius) mi->m_pColModel->m_boundSphere.m_fRadius = e.radius;
            }
            if (!e.logged && (oldD != mi->m_fDrawDistance || (oldR >= 0 && oldR != mi->m_pColModel->m_boundSphere.m_fRadius))) {
                FLog("[DIST] modelo %d: distancia %.0f -> %.0f | raio esfera %.1f -> %.1f", e.id, oldD, mi->m_fDrawDistance, oldR, mi->m_pColModel ? mi->m_pColModel->m_boundSphere.m_fRadius : -1.0f);
                e.logged = true;
            }
        }
    }
}
