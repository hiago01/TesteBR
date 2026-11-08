#pragma once
// ---------------------------------------------------------------------------
// DPDIAG (cliente) - diagnostico de colisao/objeto no proprio jogo. Comando de chat LOCAL (nao vai pro servidor): /dp
//   /dp ajuda
//   /dp on | off        liga/desliga o rastro (posicao a cada 0.25 s) e a deteccao automatica de queda
//   /dp marca [texto]   grava no log (FLog = mesmo samp_log do [LOJA]) tudo que esta ao redor do jogador:
//                       posicao/angulo/velocidade, rastro dos ultimos ~20 s e, para CADA entidade (predio/dummy/objeto) cuja caixa
//                       de colisao contem o jogador (+ folga 2 m): id do modelo, nome, posicao, no de caixas/triangulos e as
//                       caixas de colisao a ate 1.5 m do jogador (em coordenadas locais do modelo e do mundo).
//   /dp quem            resumo em 1 linha no chat: modelo(s) mais proximo(s) do jogador
// Somente leitura: nao altera pools nem colisao. Usa apenas ponteiros ja usados no resto do projeto.
// ---------------------------------------------------------------------------
#include <string>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <vector>
#include <cstdarg>
#include <memory>
#include <functional>
#include <unordered_map>
#include "../main.h"
#include "game.h"
#include "Pools.h"
#include "Entity/CEntityGTA.h"
#include "game/Models/ModelInfo.h"
#include "game/Collision/ColModel.h"

extern CGame* pGame;   // definido em main.cpp

namespace DpDiag
{
    struct Sample { float t, x, y, z, spd, vz; };
    inline bool& On() { static bool v = false; return v; }
    inline std::vector<Sample>& Trail() { static std::vector<Sample> v; return v; }

    inline bool PlayerState(float& x, float& y, float& z, float& ang, float& sx, float& sy, float& sz)
    {
        if (!pGame) return false;
        auto* pp = pGame->FindPlayerPed();
        if (!pp || !pp->m_pPed || !pp->m_pPed->m_matrix) return false;
        auto* p = pp->m_pPed;
        x = p->m_matrix->m_pos.x; y = p->m_matrix->m_pos.y; z = p->m_matrix->m_pos.z;
        ang = p->m_fCurrentRotation * 57.29578f;
        sx = p->m_vecMoveSpeed.x; sy = p->m_vecMoveSpeed.y; sz = p->m_vecMoveSpeed.z;
        return true;
    }

    // Chamado 1x por frame (CGame::Process). Barato: so age quando ligado, a cada 0.25 s.
    inline void Tick()
    {
        if (!On()) return;
        static double last = 0;
        timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec / 1e9;
        if (now - last < 0.25) return;
        last = now;
        float x, y, z, a, sx, sy, sz;
        if (!PlayerState(x, y, z, a, sx, sy, sz)) return;
        auto& tr = Trail();
        tr.push_back({ (float)now, x, y, z, std::sqrt(sx * sx + sy * sy), sz });
        if (tr.size() > 80) tr.erase(tr.begin(), tr.begin() + (tr.size() - 80));
        // queda: velocidade vertical muito negativa por ~1.25 s seguidas
        static int fall = 0;
        if (sz < -0.30f) { if (++fall == 5) { FLog("[DPDIAG] QUEDA automatica em (%.3f, %.3f, %.3f) vz=%.3f", x, y, z, sz); } }
        else fall = 0;
    }

    // Coordenadas locais do ponto dentro do frame da entidade (usa a matriz da entidade se existir).
    inline void ToLocal(CEntityGTA* e, float wx, float wy, float wz, float& lx, float& ly, float& lz)
    {
        const CVector pos = e->GetPosition();
        const float dx = wx - pos.x, dy = wy - pos.y, dz = wz - pos.z;
        if (e->m_matrix)
        {
            const CVector& r = e->m_matrix->GetRight(); const CVector& f = e->m_matrix->GetForward(); const CVector& u = e->m_matrix->GetUp();
            lx = dx * r.x + dy * r.y + dz * r.z;
            ly = dx * f.x + dy * f.y + dz * f.z;
            lz = dx * u.x + dy * u.y + dz * u.z;
        }
        else { lx = dx; ly = dy; lz = dz; }
    }

    template <class P>
    inline int ScanPool(P* pool, const char* kind, float x, float y, float z, bool verbose, std::string* nearestName)
    {
        if (!pool) return 0;
        int hits = 0;
        const int n = pool->GetSize();
        for (int i = 0; i < n; i++)
        {
            CEntityGTA* e = (CEntityGTA*)pool->GetAt(i);
            if (!e) continue;
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[e->m_nModelIndex];
            if (!mi || !mi->m_pColModel) continue;
            CColModel* cm = mi->m_pColModel;
            float lx, ly, lz; ToLocal(e, x, y, z, lx, ly, lz);
            const auto& bb = cm->m_boundBox;
            const float m = 2.0f;
            if (lx < bb.m_vecMin.x - m || lx > bb.m_vecMax.x + m || ly < bb.m_vecMin.y - m || ly > bb.m_vecMax.y + m ||
                lz < bb.m_vecMin.z - m || lz > bb.m_vecMax.z + m) continue;
            ++hits;
            const CVector pos = e->GetPosition();
            unsigned nb = cm->m_pColData ? cm->m_pColData->m_nNumBoxes : 0u;
            if (nearestName && nearestName->empty()) *nearestName = mi->m_modelName;
            if (!verbose) continue;
            FLog("[DPDIAG]  %s id=%d '%s' pos=(%.3f,%.3f,%.3f) player_local=(%.3f,%.3f,%.3f) boxes=%u tris=%u bound=[%.1f %.1f %.1f .. %.1f %.1f %.1f] usaColisao=%d",
                 kind, (int)e->m_nModelIndex, mi->m_modelName, pos.x, pos.y, pos.z, lx, ly, lz, nb, (unsigned)cm->GetTriCount(),
                 bb.m_vecMin.x, bb.m_vecMin.y, bb.m_vecMin.z, bb.m_vecMax.x, bb.m_vecMax.y, bb.m_vecMax.z, (int)e->m_bUsesCollision);
            if (!cm->m_pColData) continue;
            int shown = 0;
            for (const auto& b : cm->m_pColData->GetBoxes())
            {
                const float r = 1.5f;
                if (lx < b.m_vecMin.x - r || lx > b.m_vecMax.x + r || ly < b.m_vecMin.y - r || ly > b.m_vecMax.y + r ||
                    lz < b.m_vecMin.z - r || lz > b.m_vecMax.z + r) continue;
                if (++shown > 14) { FLog("[DPDIAG]    ... (mais caixas perto, cortado)"); break; }
                FLog("[DPDIAG]    caixa local min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) tam=(%.2f x %.2f x %.2f)%s",
                     b.m_vecMin.x, b.m_vecMin.y, b.m_vecMin.z, b.m_vecMax.x, b.m_vecMax.y, b.m_vecMax.z,
                     b.m_vecMax.x - b.m_vecMin.x, b.m_vecMax.y - b.m_vecMin.y, b.m_vecMax.z - b.m_vecMin.z,
                     (lx >= b.m_vecMin.x && lx <= b.m_vecMax.x && ly >= b.m_vecMin.y && ly <= b.m_vecMax.y && lz >= b.m_vecMin.z && lz <= b.m_vecMax.z) ? "  <== JOGADOR DENTRO DESTA CAIXA" : "");
            }
            if (shown == 0 && nb) FLog("[DPDIAG]    nenhuma caixa a menos de 1.5 m (parede/chao vem de triangulos ou nao existe colisao aqui)");
        }
        return hits;
    }

    inline std::string Command(const std::string& args)
    {
        float x, y, z, a, sx, sy, sz;
        if (args.empty() || args == "ajuda")
            return "[DPDIAG] /dp on|off | /dp marca [texto] | /dp quem  (resultado no log do cliente, linhas [DPDIAG])";
        if (args == "on") { On() = true; Trail().clear(); FLog("[DPDIAG] ligado"); return "[DPDIAG] ligado (rastro + queda automatica)"; }
        if (args == "off") { On() = false; FLog("[DPDIAG] desligado"); return "[DPDIAG] desligado"; }
        if (!PlayerState(x, y, z, a, sx, sy, sz)) return "[DPDIAG] jogador ainda nao existe";
        if (args == "quem")
        {
            std::string n1, n2;
            int h = ScanPool(GetBuildingPool(), "predio", x, y, z, false, &n1);
            h += ScanPool(GetDummyPool(), "dummy", x, y, z, false, &n2);
            char b[200]; snprintf(b, sizeof b, "[DPDIAG] pos %.2f %.2f %.2f | %d entidades ao redor | %s %s", x, y, z, h, n1.c_str(), n2.c_str());
            return b;
        }
        if (args.rfind("marca", 0) == 0)
        {
            std::string nota = args.size() > 6 ? args.substr(6) : "sem nota";
            FLog("[DPDIAG] ===== MARCA: %s", nota.c_str());
            FLog("[DPDIAG] jogador pos=(%.3f, %.3f, %.3f) ang=%.1f vel=(%.3f, %.3f, %.3f)", x, y, z, a, sx, sy, sz);
            const auto& tr = Trail();
            if (!tr.empty())
                for (const auto& s : tr) FLog("[DPDIAG] rastro t=-%.2fs (%.3f, %.3f, %.3f) vel=%.3f vz=%.3f", tr.back().t - s.t, s.x, s.y, s.z, s.spd, s.vz);
            else FLog("[DPDIAG] (rastro vazio: use /dp on antes de andar)");
            int h = ScanPool(GetBuildingPool(), "predio", x, y, z, true, nullptr);
            h += ScanPool(GetDummyPool(), "dummy", x, y, z, true, nullptr);
            if (GetObjectPoolGta()) h += ScanPool(GetObjectPoolGta(), "objeto", x, y, z, true, nullptr);
            FLog("[DPDIAG] ===== fim (%d entidades)", h);
            char b[200]; snprintf(b, sizeof b, "[DPDIAG] marca gravada: %.2f %.2f %.2f (%d entidades) - veja o log", x, y, z, h);
            return b;
        }
        return "[DPDIAG] comando desconhecido. /dp ajuda";
    }
}
