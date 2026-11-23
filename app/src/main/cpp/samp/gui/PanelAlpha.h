#pragma once

// ---------------------------------------------------------------------------
// Transparencia COMPARTILHADA das telas cheias (Menu jogador e Gerenciador de grupo). Uma regra so para as duas.
// O jogador ajusta em Menu jogador > Configuracoes; o valor fica em <storage>/ui_transparencia.txt (percentual de OPACIDADE do fundo, 30..100).
//   Get() = opacidade 0.30..1.00 (1.00 = fundo solido, 0.30 = bem transparente). Padrao 0.85 (o mesmo "um pouco transparente" que o Gerenciador de grupo ja tinha).
// So a thread de desenho usa. Arquivo ausente/ilegivel: padrao (nunca derruba o jogo).
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdio>
#include <string>

#include "../main.h"          // g_pszStorage

namespace PanelAlpha
{
    inline constexpr float kMin = 0.30f, kMax = 1.00f, kDefault = 0.85f, kStep = 0.05f;

    namespace detail
    {
        inline float& Val() { static float v = kDefault; return v; }
        inline bool&  Loaded() { static bool l = false; return l; }
        inline std::string Path()
        {
            if (!g_pszStorage || !g_pszStorage[0]) return {};
            std::string p(g_pszStorage);
            if (p.back() != '/') p.push_back('/');
            return p + "ui_transparencia.txt";
        }
        inline float Snap(float v)
        {
            if (!(v == v)) return kDefault;                                  // NaN
            v = std::fmin(kMax, std::fmax(kMin, v));
            return std::fmin(kMax, std::fmax(kMin, std::round(v / kStep) * kStep));
        }
    }

    inline float Get()
    {
        using namespace detail;
        if (!Loaded())
        {
            Loaded() = true;
            const std::string p = Path();
            if (!p.empty()) if (FILE* f = std::fopen(p.c_str(), "rb")) { int pc = 0; if (std::fscanf(f, "%d", &pc) == 1) Val() = Snap((float)pc / 100.0f); std::fclose(f); }
        }
        return Val();
    }

    inline void Set(float v) { Get(); detail::Val() = detail::Snap(v); }        // so memoria (arrastar o controle); Save() ao soltar

    inline void Save()
    {
        using namespace detail;
        const std::string p = Path();
        if (p.empty()) return;
        if (FILE* f = std::fopen(p.c_str(), "wb")) { std::fprintf(f, "%d\n", (int)std::lround(Val() * 100.0f)); std::fclose(f); }
    }
}
