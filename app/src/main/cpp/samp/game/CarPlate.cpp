// CarPlate.cpp - placa Mercosul dinamica por veiculo. Ver CarPlate.h
#include "CarPlate.h"

#include "../main.h"
#include "../log.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "Entity/CVehicleGTA.h"
#include "vehicle.h"
#include "../net/netgame.h"
#include "../net/vehiclepool.h"

// stb_truetype privado deste arquivo (STBTT_STATIC: nao colide com as outras copias)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/imgui/imstb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <strings.h>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

extern CNetGame* pNetGame;

namespace CarPlate {
namespace {

// ---------------------------------------------------------------- configuracao -----------------------------------------
bool                     s_installed = false;
bool                     s_cfgRead   = false;
bool                     s_enabled   = true;
bool                     s_debug     = false;
std::vector<std::string> s_names;                 // nomes de textura que contam como "placa" (minusculos)

std::string Root()
{
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    return p;
}

std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

#define PLOG(...) do { if (s_debug) FLog(__VA_ARGS__); } while (0)

void ReadCfg()
{
    s_cfgRead = true;
    s_names.clear();
    std::string list = "carplate,brasil mercosul";
    const std::string r = Root();
    if (!r.empty()) {
        if (FILE* f = fopen((r + "SAMP/placas.ini").c_str(), "r")) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                std::string l = Trim(line);
                if (l.empty() || l[0] == ';' || l[0] == '#' || l[0] == '[') continue;
                const size_t eq = l.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = Lower(Trim(l.substr(0, eq)));
                std::string v = Trim(l.substr(eq + 1));
                const size_t sc = v.find(';'); if (sc != std::string::npos) v = Trim(v.substr(0, sc));
                if (k == "enabled") s_enabled = atoi(v.c_str()) != 0;
                else if (k == "debug") s_debug = atoi(v.c_str()) != 0;
                else if (k == "textures" && !v.empty()) list = v;
            }
            fclose(f);
        }
    }
    size_t p = 0;
    while (p <= list.size()) {
        size_t c = list.find(',', p);
        if (c == std::string::npos) c = list.size();
        const std::string n = Lower(Trim(list.substr(p, c - p)));
        if (!n.empty()) s_names.push_back(n);
        p = c + 1;
    }
    FLog("[PLACA] config: enabled=%d debug=%d texturas=%s", s_enabled ? 1 : 0, s_debug ? 1 : 0, list.c_str());
}

bool IsPlateName(const char* name)
{
    if (!name || !name[0]) return false;
    for (const std::string& n : s_names) if (strcasecmp(name, n.c_str()) == 0) return true;
    return false;
}

uint64_t NowMs() { timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }

// ---------------------------------------------------------------- texto da placa ---------------------------------------
uint32_t Hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

// LLLNLNN estavel a partir de um numero (ex.: ID do veiculo)
std::string Synth(uint32_t seed)
{
    uint32_t h = Hash32(seed * 2654435761u + 12345u);
    auto nextLetter = [&]() { h = Hash32(h + 0x9e3779b9u); return (char)('A' + (h % 26)); };
    auto nextDigit  = [&]() { h = Hash32(h + 0x9e3779b9u); return (char)('0' + (h % 10)); };
    std::string s;
    s.push_back(nextLetter()); s.push_back(nextLetter()); s.push_back(nextLetter());
    s.push_back(nextDigit());
    s.push_back(nextLetter());
    s.push_back(nextDigit()); s.push_back(nextDigit());
    return s;
}
} // namespace

std::string NormalizePlate(const char* raw, int vehicleId)
{
    std::string s;
    if (raw) {
        for (int i = 0; i < 32 && raw[i]; ++i) {
            const unsigned char c = (unsigned char)raw[i];
            if (isalnum(c) && c < 128) s.push_back((char)toupper(c));
        }
    }
    if (s.size() > 8) s.resize(8);
    if (s.size() < 4) return Synth((uint32_t)(vehicleId < 0 ? 0 : vehicleId) + 1u);
    return s;
}

namespace {
// ---------------------------------------------------------------- fonte ------------------------------------------------
// s_fontA = caracteres da placa (FE-FONT.TTF = fonte das placas Mercosul; senao Barlow Condensed Black; senao a fonte padrao)
// s_fontB = textos pequenos ("BRASIL", "BR"): Barlow Condensed Black se existir, senao a mesma da placa
struct LoadedFont { std::vector<unsigned char> data; stbtt_fontinfo info; bool ok = false; std::string name; };
LoadedFont  s_fA, s_fB;
stbtt_fontinfo* s_cur = nullptr;      // fonte usada pelas funcoes de desenho abaixo
bool        s_fontTried = false, s_fontOk = false;

bool LoadFontFile(const std::string& path, LoadedFont& lf)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return false; }
    lf.data.resize((size_t)sz);
    const size_t rd = fread(lf.data.data(), 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return false;
    lf.ok = stbtt_InitFont(&lf.info, lf.data.data(), stbtt_GetFontOffsetForIndex(lf.data.data(), 0)) != 0;
    return lf.ok;
}

bool EnsureFont()
{
    if (s_fontTried) return s_fontOk;
    s_fontTried = true;
    const std::string r = Root();
    if (r.empty()) return false;
    const char* candA[] = { "fonts/FE-FONT.TTF", "fonts/FE-Font.ttf", "fonts/fe-font.ttf", "fonts/BarlowCondensed-Black.ttf", "fonts/" FONT_NAME };
    for (const char* c : candA)
        if (LoadFontFile(r + c, s_fA)) { s_fA.name = c; break; }
    if (!s_fA.ok) { FLog("[PLACA] AVISO: nenhuma fonte encontrada em %sfonts/ (placa dinamica desligada, fica a placa do DFF)", r.c_str()); return false; }
    if (!LoadFontFile(r + "fonts/BarlowCondensed-Black.ttf", s_fB)) { s_fB.data = s_fA.data; s_fB.ok = stbtt_InitFont(&s_fB.info, s_fB.data.data(), stbtt_GetFontOffsetForIndex(s_fB.data.data(), 0)) != 0; s_fB.name = s_fA.name; }
    else s_fB.name = "fonts/BarlowCondensed-Black.ttf";
    s_fontOk = s_fA.ok && s_fB.ok;
    FLog("[PLACA] fontes: caracteres=%s textos=%s", s_fA.name.c_str(), s_fB.name.c_str());
    return s_fontOk;
}

// ---------------------------------------------------------------- desenho da placa -------------------------------------
// Espaco da placa: largura PW x altura 1.0 (proporcao real ~3.1:1). A tela da textura e 512x256, entao x e esticado na
// quad; todo desenho e feito nesse espaco (circulos viram elipses na textura e voltam a ser redondos no carro).
constexpr int   kW = 512, kH = 256;
float kPW = 3.10f;                                    // largura da placa em unidades (altura = 1): 3.10 carro, 1.375 moto
float kSX = kW / 3.10f;                               // pixels por unidade (x)
constexpr float kSY = (float)kH;                      // pixels por unidade (y)
void SetAspect(float pw) { kPW = pw; kSX = kW / pw; }

struct Canvas {
    std::vector<uint8_t> px = std::vector<uint8_t>((size_t)kW * kH * 4, 0);
    void Put(int x, int y, uint8_t r, uint8_t g, uint8_t b, float a)
    {
        if (x < 0 || y < 0 || x >= kW || y >= kH || a <= 0.f) return;
        uint8_t* d = &px[((size_t)y * kW + x) * 4];
        if (a >= 1.f) { d[0] = r; d[1] = g; d[2] = b; d[3] = 255; return; }
        d[0] = (uint8_t)(r * a + d[0] * (1.f - a)); d[1] = (uint8_t)(g * a + d[1] * (1.f - a)); d[2] = (uint8_t)(b * a + d[2] * (1.f - a)); d[3] = 255;
    }
    // preenche pixels cujo ponto (no espaco da placa) satisfaz o teste (distancia assinada em unidades da placa: <0 dentro)
    template <class F> void Fill(float u0, float v0, float u1, float v1, uint8_t r, uint8_t g, uint8_t b, F sd)
    {
        const int x0 = std::max(0, (int)floorf(u0 * kSX) - 1), x1 = std::min(kW - 1, (int)ceilf(u1 * kSX) + 1);
        const int y0 = std::max(0, (int)floorf(v0 * kSY) - 1), y1 = std::min(kH - 1, (int)ceilf(v1 * kSY) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float u = (x + 0.5f) / kSX, v = (y + 0.5f) / kSY;
                const float d = sd(u, v) * kSY;                 // ~pixels
                const float a = std::clamp(0.5f - d, 0.f, 1.f);  // borda suave de ~1 px
                Put(x, y, r, g, b, a);
            }
    }
    void Rect(float u0, float v0, float u1, float v1, uint8_t r, uint8_t g, uint8_t b)
    {
        Fill(u0, v0, u1, v1, r, g, b, [=](float u, float v) { return std::max(std::max(u0 - u, u - u1), std::max(v0 - v, v - v1)); });
    }
    void RoundRect(float u0, float v0, float u1, float v1, float rad, uint8_t r, uint8_t g, uint8_t b)
    {
        Fill(u0, v0, u1, v1, r, g, b, [=](float u, float v) {
            const float cx = (u0 + u1) * .5f, cy = (v0 + v1) * .5f, hx = (u1 - u0) * .5f - rad, hy = (v1 - v0) * .5f - rad;
            const float qx = fabsf(u - cx) - hx, qy = fabsf(v - cy) - hy;
            return sqrtf(std::max(qx, 0.f) * std::max(qx, 0.f) + std::max(qy, 0.f) * std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - rad;
        });
    }
    void Circle(float cu, float cv, float rad, uint8_t r, uint8_t g, uint8_t b)
    {
        Fill(cu - rad, cv - rad, cu + rad, cv + rad, r, g, b, [=](float u, float v) { return sqrtf((u - cu) * (u - cu) + (v - cv) * (v - cv)) - rad; });
    }
    void Ring(float cu, float cv, float rad, float th, uint8_t r, uint8_t g, uint8_t b)
    {
        Fill(cu - rad - th, cv - rad - th, cu + rad + th, cv + rad + th, r, g, b, [=](float u, float v) { return fabsf(sqrtf((u - cu) * (u - cu) + (v - cv) * (v - cv)) - rad) - th * .5f; });
    }
    void Diamond(float cu, float cv, float hu, float hv, uint8_t r, uint8_t g, uint8_t b)
    {
        Fill(cu - hu, cv - hv, cu + hu, cv + hv, r, g, b, [=](float u, float v) { return (fabsf(u - cu) / hu + fabsf(v - cv) / hv - 1.f) * hv * 0.7f; });
    }
};

int NextCp(const char*& s) { const unsigned char c = (unsigned char)*s; ++s; return c; }

// largura (em unidades da placa) de um caractere com altura de capitular `capH` (unidades da placa)
float CapScaleY(float capH)   // escala stb para que a altura de 'H' valha capH unidades
{
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(s_cur, 'H', 1.f, 1.f, &x0, &y0, &x1, &y1);
    const float capPx = (float)(y1 - y0);
    return capPx > 0.f ? (capH * kSY) / capPx : 0.01f;
}

// desenha 1 caractere centralizado em (cu) com base em (baseV); comprime se passar de maxW (unidades da placa)
void Glyph(Canvas& c, int cp, float cu, float capTopV, float capH, float maxW, uint8_t r, uint8_t g, uint8_t b)
{
    const float sy = CapScaleY(capH);
    float sx = sy * (kSX / kSY);
    int ax, ay, bx, by;
    stbtt_GetCodepointBitmapBox(s_cur, cp, sx, sy, &ax, &ay, &bx, &by);
    float gwU = (bx - ax) / kSX;
    if (gwU > maxW && gwU > 0.f) { sx *= maxW / gwU; stbtt_GetCodepointBitmapBox(s_cur, cp, sx, sy, &ax, &ay, &bx, &by); }
    const int gw = bx - ax, gh = by - ay;
    if (gw <= 0 || gh <= 0 || gw > 400 || gh > 300) return;
    std::vector<unsigned char> bmp((size_t)gw * gh);
    stbtt_MakeCodepointBitmap(s_cur, bmp.data(), gw, gh, gw, sx, sy, cp);
    const int baseline = (int)lroundf((capTopV + capH) * kSY);
    const int x0 = (int)lroundf(cu * kSX - gw * 0.5f) ;
    for (int yy = 0; yy < gh; ++yy)
        for (int xx = 0; xx < gw; ++xx) {
            const float a = bmp[(size_t)yy * gw + xx] / 255.f;
            if (a > 0.f) c.Put(x0 + xx, baseline + ay + yy, r, g, b, a);
        }
}

// texto centralizado em cu, comprimento natural (sem comprimir), altura capH
void TextCentered(Canvas& c, const char* s, float cu, float capTopV, float capH, uint8_t r, uint8_t g, uint8_t b)
{
    const float sy = CapScaleY(capH);
    const float sx = sy * (kSX / kSY);
    float w = 0.f;
    for (const char* p = s; *p;) { int cp = NextCp(p), adv = 0, lsb = 0; stbtt_GetCodepointHMetrics(s_cur, cp, &adv, &lsb); w += adv * sx; }
    float x = cu * kSX - w * 0.5f;
    const int baseline = (int)lroundf((capTopV + capH) * kSY);
    for (const char* p = s; *p;) {
        const int cp = NextCp(p); int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(s_cur, cp, &adv, &lsb);
        int ax, ay, bx, by; stbtt_GetCodepointBitmapBox(s_cur, cp, sx, sy, &ax, &ay, &bx, &by);
        const int gw = bx - ax, gh = by - ay;
        if (gw > 0 && gh > 0 && gw < 400 && gh < 300) {
            std::vector<unsigned char> bmp((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(s_cur, bmp.data(), gw, gh, gw, sx, sy, cp);
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    const float a = bmp[(size_t)yy * gw + xx] / 255.f;
                    if (a > 0.f) c.Put((int)x + ax + xx, baseline + ay + yy, r, g, b, a);
                }
        }
        x += adv * sx;
    }
}

// carro sem placa: so o suporte (chapa escura com dois furos), sem texto e sem fonte
void DrawNoPlate(Canvas& c)
{
    c.Rect(0, 0, kPW, 1.f, 28, 29, 31);
    c.RoundRect(0.04f, 0.06f, kPW - 0.04f, 0.94f, 0.07f, 40, 41, 44);
    c.Circle(0.30f, 0.5f, 0.045f, 18, 18, 20);
    c.Circle(kPW - 0.30f, 0.5f, 0.045f, 18, 18, 20);
}

void DrawPlate(Canvas& c, const std::string& text)
{
    // moldura escura + fundo claro
    c.Rect(0, 0, kPW, 1.f, 22, 22, 24);
    c.RoundRect(0.03f, 0.03f, kPW - 0.03f, 0.97f, 0.08f, 238, 238, 236);
    // faixa azul superior (cantos de cima arredondados como o fundo; a parte de baixo arredondada e coberta pelo fundo claro)
    c.RoundRect(0.03f, 0.03f, kPW - 0.03f, 0.34f, 0.08f, 0, 52, 160);
    c.Rect(0.03f, 0.26f, kPW - 0.03f, 0.40f, 238, 238, 236);
    // "BRASIL"
    s_cur = &s_fB.info;
    TextCentered(c, "BRASIL", kPW * .5f, 0.085f, 0.115f, 255, 255, 255);
    // bandeira (direita da faixa)
    c.Rect(2.72f, 0.085f, 2.95f, 0.215f, 0, 140, 60);
    c.Diamond(2.835f, 0.15f, 0.085f, 0.048f, 255, 214, 0);
    c.Circle(2.835f, 0.15f, 0.034f, 0, 48, 152);
    // emblema do Mercosul (esquerda da faixa): aro + ponto
    c.Ring(0.265f, 0.15f, 0.052f, 0.012f, 255, 255, 255);
    c.Circle(0.265f, 0.15f, 0.014f, 255, 255, 255);
    // caracteres (fonte da placa)
    s_cur = &s_fA.info;
    const int n = (int)text.size();
    const float u0 = 0.30f, u1 = kPW - 0.17f;
    const float cell = (u1 - u0) / (float)n;
    const float capH = 0.52f, capTop = 0.33f;
    for (int i = 0; i < n; ++i)
        Glyph(c, (unsigned char)text[i], u0 + cell * (i + 0.5f), capTop, capH, cell * 0.94f, 16, 22, 66);   // azul-escuro (FE-Schrift das placas Mercosul)
    // QR code decorativo (quadradinhos fixos) a esquerda dos caracteres
    {
        uint32_t h = 0x51ED2701u;
        const float q0u = 0.075f, q0v = 0.40f, cs = 0.026f;
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 7; ++gx) {
                h = Hash32(h + (uint32_t)(gy * 7 + gx));
                const bool corner = (gx < 2 && gy < 2) || (gx > 4 && gy < 2) || (gx < 2 && gy > 4);
                if (corner || (h & 3u) == 0u) c.Rect(q0u + gx * cs * 0.64f, q0v + gy * cs, q0u + (gx + 1) * cs * 0.64f - 0.002f, q0v + (gy + 1) * cs - 0.002f, 16, 22, 66);
            }
    }
    // "BR" pequeno no canto inferior esquerdo
    s_cur = &s_fB.info;
    TextCentered(c, "BR", 0.16f, 0.82f, 0.065f, 16, 22, 66);
}

// placa de MOTO (Mercosul, 2 linhas, ~187x136 mm => proporcao 1.375:1): faixa azul, 3 letras em cima, 4 caracteres embaixo
void DrawPlateMoto(Canvas& c, const std::string& text)
{
    const float W = kPW;
    c.Rect(0, 0, W, 1.f, 22, 22, 24);
    c.RoundRect(0.03f, 0.03f, W - 0.03f, 0.97f, 0.08f, 238, 238, 236);
    c.RoundRect(0.03f, 0.03f, W - 0.03f, 0.27f, 0.08f, 0, 52, 160);
    c.Rect(0.03f, 0.19f, W - 0.03f, 0.30f, 238, 238, 236);
    s_cur = &s_fB.info;
    TextCentered(c, "BRASIL", W * .5f, 0.075f, 0.12f, 255, 255, 255);
    c.Rect(W - 0.30f, 0.075f, W - 0.11f, 0.195f, 0, 140, 60);                 // bandeira
    c.Diamond(W - 0.205f, 0.135f, 0.075f, 0.043f, 255, 214, 0);
    c.Circle(W - 0.205f, 0.135f, 0.030f, 0, 48, 152);
    c.Ring(0.20f, 0.135f, 0.048f, 0.011f, 255, 255, 255);                       // emblema do Mercosul
    c.Circle(0.20f, 0.135f, 0.013f, 255, 255, 255);
    s_cur = &s_fA.info;
    const std::string l1 = text.substr(0, std::min<size_t>(3, text.size()));
    const std::string l2 = text.size() > 3 ? text.substr(3) : std::string();
    auto line = [&](const std::string& t, float capTop, float capH) {
        const int n = (int)t.size(); if (n <= 0) return;
        const float u0 = 0.11f, u1 = W - 0.11f, cell = (u1 - u0) / (float)std::max(n, 4);
        const float used = cell * (float)n, start = (W - used) * .5f;           // 3 letras ficam centradas na linha de cima
        for (int i = 0; i < n; ++i) Glyph(c, (unsigned char)t[i], start + cell * (i + 0.5f), capTop, capH, cell * 0.94f, 16, 22, 66);
    };
    line(l1, 0.33f, 0.27f);
    line(l2, 0.655f, 0.27f);
}

// ---------------------------------------------------------------- RwTexture --------------------------------------------
RwTexture* MakeTexture(const std::vector<uint8_t>& rgba, const char* name)
{
    if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage || !RwTextureCreate) return nullptr;
    RwImage* image = RwImageCreate(kW, kH, 32);
    if (!image) return nullptr;
    if (!RwImageAllocatePixels(image)) { RwImageDestroy(image); return nullptr; }
    for (int y = 0; y < kH; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, rgba.data() + (size_t)y * kW * 4, (size_t)kW * 4);
    int rw = kW, rh = kH, rd = 32, rf = 0;
    RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
    rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
    if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4)) {
        rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
        rf |= rwRASTERFORMAT8888; rd = 32;
    }
    if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;
    RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
    if (!raster) { RwImageDestroy(image); return nullptr; }
    if (!RwRasterSetFromImage(raster, image)) { if (RwRasterDestroy) RwRasterDestroy(raster); RwImageDestroy(image); return nullptr; }
    RwImageDestroy(image);
    RwTexture* tex = RwTextureCreate(raster);
    if (!tex) { if (RwRasterDestroy) RwRasterDestroy(raster); return nullptr; }
    RwTextureSetName(tex, name);
    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwTextureSetAddressing(tex, rwTEXTUREADDRESSCLAMP);
    return tex;
}

// ---------------------------------------------------------------- cache ------------------------------------------------
struct CacheEnt { RwTexture* tex = nullptr; uint64_t last = 0; bool failed = false; };
std::unordered_map<std::string, CacheEnt> s_cache;
constexpr size_t kCacheMax = 32;
bool s_genWarned = false;

struct Saved { RpMaterial* m; RwTexture* orig; };
Saved s_saved[16];
int   s_nSaved = 0;

bool InUse(RwTexture* t) { for (int i = 0; i < s_nSaved; ++i) if (s_saved[i].m->texture == t) return true; return false; }

void Evict(uint64_t now)
{
    while (s_cache.size() > kCacheMax) {
        auto victim = s_cache.end();
        for (auto it = s_cache.begin(); it != s_cache.end(); ++it) {
            if (it->second.tex && (InUse(it->second.tex) || now - it->second.last < 3000)) continue;
            if (victim == s_cache.end() || it->second.last < victim->second.last) victim = it;
        }
        if (victim == s_cache.end()) return;
        PLOG("[PLACA] cache cheio: descarta '%s'", victim->first.c_str());
        if (victim->second.tex) RwTextureDestroy(victim->second.tex);
        s_cache.erase(victim);
    }
}

RwTexture* GetPlateTexture(const std::string& text, bool moto)
{
    const uint64_t now = NowMs();
    const std::string key = (moto ? "M:" : "C:") + text;
    SetAspect(moto ? 1.375f : 3.10f);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) { it->second.last = now; return it->second.tex; }   // tex == nullptr: falhou antes, nao tenta de novo
    CacheEnt e; e.last = now;
    if (text == "-") {
        Canvas c; DrawNoPlate(c);
        e.tex = MakeTexture(c.px, "mx_plate");
        PLOG("[PLACA] carro sem placa: suporte vazio (tex=%p)", (void*)e.tex);
    } else if (EnsureFont()) {
        Canvas c;
        if (moto) DrawPlateMoto(c, text); else DrawPlate(c, text);
        e.tex = MakeTexture(c.px, "mx_plate");
        if (!e.tex && !s_genWarned) { s_genWarned = true; FLog("[PLACA] AVISO: nao consegui criar a RwTexture da placa '%s'", text.c_str()); }
        PLOG("[PLACA] placa '%s' gerada em %llu ms (tex=%p)", text.c_str(), (unsigned long long)(NowMs() - now), (void*)e.tex);
    }
    s_cache[key] = e;
    Evict(now);
    return e.tex;
}

// ---------------------------------------------------------------- veiculo -> texto -------------------------------------
std::string PlateOfVehicle(CVehicleGTA* veh, int* idOut)
{   // texto "-" (servidor: placa removida pelo jogador) = carro SEM placa: desenha so o suporte vazio
    int id = -1;
    const char* raw = nullptr;
    if (pNetGame && pNetGame->GetVehiclePool()) {
        CVehiclePool* vp = pNetGame->GetVehiclePool();
        const VEHICLEID vid = vp->FindIDFromGtaPtr(veh);
        if (vid != INVALID_VEHICLE_ID) {
            id = (int)vid;
            if (CVehicle* v = vp->GetAt(vid)) { v->m_szPlateText[31] = '\0'; raw = v->m_szPlateText; }
        }
    }
    if (id < 0) id = (int)(((uintptr_t)veh >> 4) & 0xFFFFu) + 5000;   // carro fora do SAMP (transito): texto estavel pelo endereco
    if (idOut) *idOut = id;
    if (raw && raw[0] == '-' && raw[1] == '\0') return "-";
    return NormalizePlate(raw, id);
}

// ---------------------------------------------------------------- ganchos ----------------------------------------------
void (*orig_Before)(CVehicleGTA*, void*) = nullptr;
void (*orig_After)(CVehicleGTA*, void*)  = nullptr;
int  s_callsBefore = 0, s_callsAfter = 0, s_swaps = 0, s_noMatchLogged = 0;

void Restore()
{
    for (int i = 0; i < s_nSaved; ++i) s_saved[i].m->texture = s_saved[i].orig;
    s_nSaved = 0;
}

struct ScanCtx { RwTexture* tex; int n; };
RpAtomic* AtomicCB(RpAtomic* a, void* d)
{
    ScanCtx* c = (ScanCtx*)d;
    RpGeometry* g = a ? a->geometry : nullptr;
    if (!g || !g->matList.materials || g->matList.numMaterials <= 0 || g->matList.numMaterials > 64) return a;
    for (int i = 0; i < g->matList.numMaterials; ++i) {
        RpMaterial* m = g->matList.materials[i];
        if (!m || !m->texture || m->texture == c->tex) continue;
        if (!IsPlateName(m->texture->name)) continue;
        if (s_nSaved >= 16) return a;
        s_saved[s_nSaved].m = m; s_saved[s_nSaved].orig = m->texture; ++s_nSaved;
        m->texture = c->tex;            // atribuicao direta (sem refcount): devolvemos a original logo apos o desenho
        ++c->n;
    }
    return a;
}

void Hook_Before(CVehicleGTA* veh, void* /*modelInfo*/)
{
    ++s_callsBefore;
    if (s_nSaved) Restore();                  // seguranca: um Before sem After anterior nunca deixa textura trocada
    if (!s_enabled || !veh) return;
    RpClump* clump = veh->m_pRwClump;
    if (!clump) return;
    int id = -1;
    const std::string text = PlateOfVehicle(veh, &id);
    RwTexture* tex = GetPlateTexture(text, veh->m_nVehicleType == VEHICLE_TYPE_BIKE || veh->m_nVehicleType == VEHICLE_TYPE_BMX);
    if (!tex) return;                         // sem textura: fica a placa do DFF
    ScanCtx c{ tex, 0 };
    RpClumpForAllAtomics(clump, AtomicCB, &c);
    s_swaps += c.n;
    if (s_debug) {
        if (c.n) { static int shown = 0; if (shown < 12) { ++shown; FLog("[PLACA] veiculo id=%d modelo=%d placa='%s': %d material(is) trocado(s)", id, (int)veh->m_nModelIndex, text.c_str(), c.n); } }
        else if (s_noMatchLogged < 6) { ++s_noMatchLogged; FLog("[PLACA] veiculo id=%d modelo=%d: nenhum material com textura de placa (%s...)", id, (int)veh->m_nModelIndex, s_names.empty() ? "?" : s_names[0].c_str()); }
    }
}

void Hook_After(CVehicleGTA* /*veh*/, void* /*modelInfo*/)
{
    ++s_callsAfter;
    if (s_nSaved) Restore();
}

// ---- MOTOS: o jogo so chama CustomCarPlate_Before/After para AUTOMOVEL (CVehicle::SetupRender testa m_nVehicleType == 0).
// Para moto, ganchamos SetupRender (troca a textura logo depois do original) e ResetAfterRender (devolve a original).
void (*orig_Setup)(CVehicleGTA*)  = nullptr;
void (*orig_Reset)(CVehicleGTA*)  = nullptr;

RpAtomic* HasPlateCB(RpAtomic* a, void* d)
{
    RpGeometry* g = a ? a->geometry : nullptr;
    if (!g || !g->matList.materials || g->matList.numMaterials <= 0 || g->matList.numMaterials > 64) return a;
    for (int i = 0; i < g->matList.numMaterials; ++i) {
        RpMaterial* m = g->matList.materials[i];
        if (m && m->texture && IsPlateName(m->texture->name)) { *(bool*)d = true; break; }
    }
    return a;
}

bool BikeModelHasPlate(CVehicleGTA* veh)
{   // 1x por modelo: so motos cujo DFF tem material de placa (ex.: mx_bf400, 1194) pagam o custo
    static std::unordered_map<int, bool> s_has;
    const int mid = (int)veh->m_nModelIndex;
    auto it = s_has.find(mid);
    if (it != s_has.end()) return it->second;
    bool has = false;
    if (veh->m_pRwClump) RpClumpForAllAtomics(veh->m_pRwClump, HasPlateCB, &has);
    s_has[mid] = has;
    if (s_debug) FLog("[PLACA] moto modelo=%d: material de placa no DFF = %s", mid, has ? "SIM" : "nao");
    return has;
}

void Hook_SetupRender(CVehicleGTA* veh)
{
    orig_Setup(veh);
    if (!s_enabled || !veh) return;
    if (veh->m_nVehicleType != VEHICLE_TYPE_BIKE && veh->m_nVehicleType != VEHICLE_TYPE_BMX) return;
    if (!veh->m_pRwClump || !BikeModelHasPlate(veh)) return;
    Hook_Before(veh, nullptr);
}

void Hook_ResetAfterRender(CVehicleGTA* veh)
{
    if (s_nSaved) Restore();
    orig_Reset(veh);
}
} // namespace

bool Installed() { return s_installed; }

bool InstallHooks()
{
    if (s_installed) return true;
    if (!s_cfgRead) ReadCfg();
    if (!s_enabled) { FLog("[PLACA] desligado em SAMP/placas.ini (enabled=0): mantidos os 'ret' nativos"); return false; }
    static const char* kBefore = "_ZN11CAutomobile35CustomCarPlate_BeforeRenderingStartEP17CVehicleModelInfo";
    static const char* kAfter  = "_ZN11CAutomobile33CustomCarPlate_AfterRenderingStopEP17CVehicleModelInfo";
    if (!CHook::getSymSoft(kBefore) || !CHook::getSymSoft(kAfter)) return false;   // ja registrou no log; os "ret" nativos continuam
    CHook::InlineHook(kBefore, &Hook_Before, &orig_Before);
    CHook::InlineHook(kAfter,  &Hook_After,  &orig_After);
    if (CHook::getSymSoft("_ZN8CVehicle11SetupRenderEv") && CHook::getSymSoft("_ZN8CVehicle16ResetAfterRenderEv")) {
        CHook::InlineHook("_ZN8CVehicle11SetupRenderEv", &Hook_SetupRender, &orig_Setup);
        CHook::InlineHook("_ZN8CVehicle16ResetAfterRenderEv", &Hook_ResetAfterRender, &orig_Reset);
        FLog("[PLACA] ganchos de MOTO instalados (CVehicle::SetupRender/ResetAfterRender)");
    }
    s_installed = true;
    FLog("[PLACA] ganchos instalados (CAutomobile::CustomCarPlate_BeforeRenderingStart/AfterRenderingStop). O original nao e chamado.");
    return true;
}

} // namespace CarPlate
