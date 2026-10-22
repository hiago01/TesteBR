#pragma once

// ---------------------------------------------------------------------------
// CorpTexMap.h - mapa entre o que o CorpTex pede (rosto, cabelo, pele, roupa) e a pasta "pack" do corpo male.
// Código PURO (sem RenderWare): compila e é testado no host (tools_corptex_hosttest.cpp).
//
//   pack no aparelho: <raiz>/SAMP/skins/pack/<categoria>/<pasta>/<n>.png
//
//   CorpTex pede (relativo a textures/272/aparencia/)      -> arquivo no pack
//     rosto/<tom 1-6>/<variante>.png                       -> heads/<tom>/<variante>.png
//     cabelo/<estilo 1-4>/<cor>.png                        -> hair/<estilo>/<cor>.png
//     pele/torso/<tom>.png                                 -> torso/1/<tom>.png   (tronco, braços, ombros, mãos)
//     pele/leg/<tom>.png                                   -> leg/1/<tom>.png
//     pele/feet/<tom>.png                                  -> feet/1/<tom>.png
//     roupa/boot/<1-4>/<var>.png                           -> boot/<pasta>/<var>.png
//     roupa/boot/<5-7>/<var>.png                           -> slipper/<pasta-4>/<var>.png   (chinelos, mesmo comando /bota)
//     roupa/pants/<1-7>/<var>.png                          -> pants/<pasta>/<var>.png       (5-7 = bermudas: HIPÓTESE)
//     roupa/tshirts/<1-4>/<var>.png                        -> tshirts/<pasta>/<var>.png
//
//   A última variante de cada pasta (n+1) é um PNG transparente criado para "tirar a peça": fica FORA dos
//   limites abaixo (o jogador não pode escolher um rosto/cabelo invisível por engano).
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <string>
#include <vector>

namespace CorpMap
{
    constexpr int kTones      = 6;                                   // tons de pele (heads/1..6, torso|leg|feet/1/1..6)
    constexpr int kFaceVariants[kTones + 1] = { 0, 6, 8, 8, 7, 5, 12 };   // rostos válidos por tom (sem a variante transparente)
    constexpr int kHairStyles = 4;                                   // hair/1..4  <-> fantasy_m_hair1..4
    constexpr int kHairColors[kHairStyles + 1] = { 0, 6, 6, 5, 5 };       // cores válidas por estilo (sem a transparente)

    inline std::vector<std::string> Split(const std::string& s, char sep)
    {
        std::vector<std::string> v;
        std::string cur;
        for (char c : s) { if (c == sep) { v.push_back(cur); cur.clear(); } else cur.push_back(c); }
        v.push_back(cur);
        return v;
    }

    inline bool AllDigits(const std::string& s)
    {
        if (s.empty() || s.size() > 5) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    }

    // rel (estilo CorpTex) -> caminho relativo dentro de pack/. Retorna false se não há equivalente.
    inline bool PackRelFor(const std::string& rel, std::string& out)
    {
        const std::vector<std::string> p = Split(rel, '/');
        auto isPng = [](const std::string& f, std::string& stem) {
            if (f.size() < 5 || f.compare(f.size() - 4, 4, ".png") != 0) return false;
            stem = f.substr(0, f.size() - 4);
            return AllDigits(stem);
        };
        std::string stem;
        if (p.size() == 3 && p[0] == "rosto" && AllDigits(p[1]) && isPng(p[2], stem)) { out = "heads/" + p[1] + "/" + p[2]; return true; }
        if (p.size() == 3 && p[0] == "cabelo" && AllDigits(p[1]) && isPng(p[2], stem)) { out = "hair/" + p[1] + "/" + p[2]; return true; }
        if (p.size() == 3 && p[0] == "pele" && isPng(p[2], stem))
        {
            if (p[1] == "torso" || p[1] == "leg" || p[1] == "feet") { out = p[1] + "/1/" + p[2]; return true; }
            return false;
        }
        if (p.size() == 4 && p[0] == "roupa" && AllDigits(p[2]) && isPng(p[3], stem))
        {
            const int pasta = std::atoi(p[2].c_str());
            if (p[1] == "boot")
            {
                if (pasta >= 1 && pasta <= 4) { out = "boot/" + p[2] + "/" + p[3]; return true; }
                if (pasta >= 5 && pasta <= 7) { out = "slipper/" + std::to_string(pasta - 4) + "/" + p[3]; return true; }
                return false;
            }
            if (p[1] == "pants" || p[1] == "tshirts") { out = p[1] + "/" + p[2] + "/" + p[3]; return true; }
        }
        return false;
    }
}
