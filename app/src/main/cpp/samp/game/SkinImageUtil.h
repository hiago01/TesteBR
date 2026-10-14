#pragma once

// ---------------------------------------------------------------------------
// Utilidades PURAS (sem RenderWare) para o fluxo de texturas do skin no Android.
// Ficam separadas do CustomSkin.cpp para poderem ser testadas no host.
//
//  * PotFor/ResizeRgba : o GLES do Android do GTA SA trabalha melhor com texturas de
//                        dimensões potência de 2 (sem mipmap/repeat em NPOT) e com um teto de
//                        tamanho; PNG "solto" de PC costuma ser NPOT (ex.: 600x800) -> partes
//                        brancas/pretas. Aqui a imagem é reamostrada antes de virar RwRaster.
//  * IndexPngDir       : o sistema de arquivos do Android DIFERENCIA maiúscula de minúscula, o
//                        RenderWare NÃO (nome da textura no DFF "Jaqueta" x arquivo "jaqueta.png").
//                        Indexamos a pasta por nome em minúsculas.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>

namespace SkinImg
{
    inline std::string Lower(std::string s)
    {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // Potência de 2 mais próxima (escala geométrica), entre 1 e maxSize (maxSize deve ser potência de 2).
    inline int PotFor(int v, int maxSize)
    {
        if (v <= 1) return 1;
        int lo = 1;
        while (lo * 2 <= v && lo * 2 <= maxSize) lo *= 2;
        if (lo >= maxSize) return maxSize;       // v >= maxSize
        if (lo == v) return lo;                  // já é potência de 2
        const int hi = lo * 2;
        return ((long long)v * v > (long long)lo * hi) ? hi : lo;
    }

    inline bool IsPot(int v) { return v > 0 && (v & (v - 1)) == 0; }

    // Reamostra RGBA8 (w x h) para (nw x nh) por média de área, ponderando a cor pelo alfa
    // (evita franja escura em bordas transparentes). out = nw*nh*4 bytes.
    inline bool ResizeRgba(const uint8_t* src, int w, int h, int nw, int nh, std::vector<uint8_t>& out)
    {
        if (!src || w <= 0 || h <= 0 || nw <= 0 || nh <= 0) return false;
        out.assign((size_t)nw * (size_t)nh * 4u, 0);

        const double sx = (double)w / (double)nw;
        const double sy = (double)h / (double)nh;

        for (int dy = 0; dy < nh; ++dy)
        {
            const double y0 = dy * sy, y1 = (dy + 1) * sy;
            const int iy0 = (int)y0;
            int iy1 = (int)std::ceil(y1);
            if (iy1 > h) iy1 = h;

            for (int dx = 0; dx < nw; ++dx)
            {
                const double x0 = dx * sx, x1 = (dx + 1) * sx;
                const int ix0 = (int)x0;
                int ix1 = (int)std::ceil(x1);
                if (ix1 > w) ix1 = w;

                double accR = 0, accG = 0, accB = 0, accA = 0, accW = 0;
                double plainR = 0, plainG = 0, plainB = 0;

                for (int iy = iy0; iy < iy1; ++iy)
                {
                    const double wy = std::min((double)iy + 1, y1) - std::max((double)iy, y0);
                    if (wy <= 0) continue;
                    for (int ix = ix0; ix < ix1; ++ix)
                    {
                        const double wx = std::min((double)ix + 1, x1) - std::max((double)ix, x0);
                        if (wx <= 0) continue;
                        const double wgt = wx * wy;
                        const uint8_t* p = src + ((size_t)iy * (size_t)w + (size_t)ix) * 4u;
                        const double a = p[3] / 255.0;
                        accR += p[0] * a * wgt;
                        accG += p[1] * a * wgt;
                        accB += p[2] * a * wgt;
                        accA += a * wgt;
                        accW += wgt;
                        plainR += p[0] * wgt; plainG += p[1] * wgt; plainB += p[2] * wgt;
                    }
                }

                uint8_t* o = out.data() + ((size_t)dy * (size_t)nw + (size_t)dx) * 4u;
                if (accW <= 0) continue;
                double r, g, b;
                if (accA > 1e-9) { r = accR / accA; g = accG / accA; b = accB / accA; }
                else             { r = plainR / accW; g = plainG / accW; b = plainB / accW; }
                const double a = accA / accW;
                auto cl = [](double v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : (int)(v + 0.5))); };
                o[0] = cl(r); o[1] = cl(g); o[2] = cl(b); o[3] = cl(a * 255.0);
            }
        }
        return true;
    }

    // "<dir>/Nome.PNG" -> { "nome" : "<dir>/Nome.PNG" } (extensão .png em qualquer caixa)
    inline std::unordered_map<std::string, std::string> IndexPngDir(const std::string& dir)
    {
        std::unordered_map<std::string, std::string> idx;
        DIR* d = ::opendir(dir.c_str());
        if (!d) return idx;
        std::string base = dir;
        if (!base.empty() && base.back() != '/') base.push_back('/');
        while (dirent* e = ::readdir(d))
        {
            const std::string fn = e->d_name;
            if (fn.size() < 5) continue;
            const std::string ext = Lower(fn.substr(fn.size() - 4));
            if (ext != ".png") continue;
            const std::string key = Lower(fn.substr(0, fn.size() - 4));
            if (idx.find(key) == idx.end()) idx[key] = base + fn;
        }
        ::closedir(d);
        return idx;
    }
}
