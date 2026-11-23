#pragma once

// ---------------------------------------------------------------------------
// Opcoes simples de tela (Menu jogador > Configuracoes), gravadas em <storage>/ui_opcoes.txt:  linhas "mapa=1".
//   MapOn() : minimapa + nome do local SEMPRE visiveis a pe e de carro (padrao: LIGADO). Desligado = nunca aparece.
// (Os botoes Inventario e Corrida-lock usam o ControlMode::ModernConfig: showInv / showRunLock, padrao DESLIGADOS.)
// So a thread de desenho usa. Arquivo ausente/ilegivel = padroes (nunca derruba o jogo).
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <string>

#include "../main.h"          // g_pszStorage

namespace UiPrefs
{
    namespace detail
    {
        inline bool& Map() { static bool v = true; return v; }
        inline bool& Loaded() { static bool l = false; return l; }
        inline std::string Path()
        {
            if (!g_pszStorage || !g_pszStorage[0]) return {};
            std::string p(g_pszStorage);
            if (p.back() != '/') p.push_back('/');
            return p + "ui_opcoes.txt";
        }
        inline void Load()
        {
            if (Loaded()) return;
            Loaded() = true;
            const std::string p = Path();
            if (p.empty()) return;
            if (FILE* f = std::fopen(p.c_str(), "rb"))
            {
                char line[64];
                while (std::fgets(line, sizeof line, f))
                {
                    int v = 0;
                    if (std::sscanf(line, "mapa=%d", &v) == 1) Map() = (v != 0);
                }
                std::fclose(f);
            }
        }
    }

    inline bool MapOn() { detail::Load(); return detail::Map(); }
    inline void SetMapOn(bool on) { detail::Load(); detail::Map() = on; }
    inline void Save()
    {
        const std::string p = detail::Path();
        if (p.empty()) return;
        if (FILE* f = std::fopen(p.c_str(), "wb")) { std::fprintf(f, "mapa=%d\n", detail::Map() ? 1 : 0); std::fclose(f); }
    }
}
