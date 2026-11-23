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
std::vector<unsigned char> s_fontData;
stbtt_fontinfo             s_font;
bool                       s_fontTried = false, s_fontOk = false;

bool LoadFontFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return false; }
    s_fontData.resize((size_t)sz);
    const size_t rd = fread(s_fontData.data(), 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return false;
    return stbtt_InitFont(&s_font, s_fontData.data(), stbtt_GetFontOffsetForIndex(s_fontData.data(), 0)) != 0;
}

bool EnsureFont()
{
    if (s_fontTried) return s_fontOk;
    s_fontTried = true;
    const std::string r = Root();
    if (r.empty()) return false;
    const char* cand[] = { "fonts/BarlowCondensed-Black.ttf", "fonts/" FONT_NAME };
    for (const char* c : cand) {
        if (LoadFontFile(r + c)) { s_fontOk = true; FLog("[PLACA] fonte: %s", c); return true; }
    }
    FLog("[PLACA] AVISO: nenhuma fonte encontrada em %sfonts/ (placa dinamica desligada, fica a placa do DFF)", r.c_str());
    return false;
}

// ---------------------------------------------------------------- desenho da placa -------------------------------------
// Espaco da placa: largura PW x altura 1.0 (proporcao real ~3.1:1). A tela da textura e 512x256, entao x e esticado na
// quad; todo desenho e feito nesse espaco (circulos viram elipses na textura e voltam a ser redondos no carro).
constexpr int   kW = 512, kH = 256;
constexpr float kPW = 3.10f;
constexpr float kSX = kW / kPW, kSY = (float)kH;     // pixels por unidade da placa

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
    stbtt_GetCodepointBitmapBox(&s_font, 'H', 1.f, 1.f, &x0, &y0, &x1, &y1);
    const float capPx = (float)(y1 - y0);
    return capPx > 0.f ? (capH * kSY) / capPx : 0.01f;
}

// desenha 1 caractere centralizado em (cu) com base em (baseV); comprime se passar de maxW (unidades da placa)
void Glyph(Canvas& c, int cp, float cu, float capTopV, float capH, float maxW, uint8_t r, uint8_t g, uint8_t b)
{
    const float sy = CapScaleY(capH);
    float sx = sy * (kSX / kSY);
    int ax, ay, bx, by;
    stbtt_GetCodepointBitmapBox(&s_font, cp, sx, sy, &ax, &ay, &bx, &by);
    float gwU = (bx - ax) / kSX;
    if (gwU > maxW && gwU > 0.f) { sx *= maxW / gwU; stbtt_GetCodepointBitmapBox(&s_font, cp, sx, sy, &ax, &ay, &bx, &by); }
    const int gw = bx - ax, gh = by - ay;
    if (gw <= 0 || gh <= 0 || gw > 400 || gh > 300) return;
    std::vector<unsigned char> bmp((size_t)gw * gh);
    stbtt_MakeCodepointBitmap(&s_font, bmp.data(), gw, gh, gw, sx, sy, cp);
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
    for (const char* p = s; *p;) { int cp = NextCp(p), adv = 0, lsb = 0; stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb); w += adv * sx; }
    float x = cu * kSX - w * 0.5f;
    const int baseline = (int)lroundf((capTopV + capH) * kSY);
    for (const char* p = s; *p;) {
        const int cp = NextCp(p); int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        int ax, ay, bx, by; stbtt_GetCodepointBitmapBox(&s_font, cp, sx, sy, &ax, &ay, &bx, &by);
        const int gw = bx - ax, gh = by - ay;
        if (gw > 0 && gh > 0 && gw < 400 && gh < 300) {
            std::vector<unsigned char> bmp((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&s_font, bmp.data(), gw, gh, gw, sx, sy, cp);
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    const float a = bmp[(size_t)yy * gw + xx] / 255.f;
                    if (a > 0.f) c.Put((int)x + ax + xx, baseline + ay + yy, r, g, b, a);
                }
        }
        x += adv * sx;
    }
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
    TextCentered(c, "BRASIL", kPW * .5f, 0.085f, 0.115f, 255, 255, 255);
    // bandeira (direita da faixa)
    c.Rect(2.72f, 0.085f, 2.95f, 0.215f, 0, 140, 60);
    c.Diamond(2.835f, 0.15f, 0.085f, 0.048f, 255, 214, 0);
    c.Circle(2.835f, 0.15f, 0.034f, 0, 48, 152);
    // emblema do Mercosul (esquerda da faixa): aro + ponto
    c.Ring(0.265f, 0.15f, 0.052f, 0.012f, 255, 255, 255);
    c.Circle(0.265f, 0.15f, 0.014f, 255, 255, 255);
    // caracteres
    const int n = (int)text.size();
    const float u0 = 0.20f, u1 = kPW - 0.20f;
    const float cell = (u1 - u0) / (float)n;
    const float capH = 0.52f, capTop = 0.33f;
    for (int i = 0; i < n; ++i)
        Glyph(c, (unsigned char)text[i], u0 + cell * (i + 0.5f), capTop, capH, cell * 0.94f, 20, 20, 22);
    // "BR" pequeno no canto inferior esquerdo
    TextCentered(c, "BR", 0.20f, 0.86f, 0.065f, 20, 20, 22);
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

RwTexture* GetPlateTexture(const std::string& text)
{
    const uint64_t now = NowMs();
    auto it = s_cache.find(text);
    if (it != s_cache.end()) { it->second.last = now; return it->second.tex; }   // tex == nullptr: falhou antes, nao tenta de novo
    CacheEnt e; e.last = now;
    if (EnsureFont()) {
        Canvas c;
        DrawPlate(c, text);
        e.tex = MakeTexture(c.px, "mx_plate");
        if (!e.tex && !s_genWarned) { s_genWarned = true; FLog("[PLACA] AVISO: nao consegui criar a RwTexture da placa '%s'", text.c_str()); }
        PLOG("[PLACA] placa '%s' gerada em %llu ms (tex=%p)", text.c_str(), (unsigned long long)(NowMs() - now), (void*)e.tex);
    }
    s_cache[text] = e;
    Evict(now);
    return e.tex;
}

// ---------------------------------------------------------------- veiculo -> texto -------------------------------------
std::string PlateOfVehicle(CVehicleGTA* veh, int* idOut)
{
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
    RwTexture* tex = GetPlateTexture(text);
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
    s_installed = true;
    FLog("[PLACA] ganchos instalados (CAutomobile::CustomCarPlate_BeforeRenderingStart/AfterRenderingStop). O original nao e chamado.");
    return true;
}

// ---------------------------------------------------------------- patch do DFF dentro do IMG ---------------------------
namespace {
bool ReadFile(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); const long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64L * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)sz);
    const bool ok = fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
    fclose(f);
    return ok;
}
bool WriteFileIfAbsent(const std::string& p, const uint8_t* data, size_t n)
{
    struct stat st{};
    if (stat(p.c_str(), &st) == 0 && st.st_size > 0) return true;
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, n, f) == n;
    fclose(f);
    return ok;
}
}

void ApplyImgPatch(const std::string& root)
{
    if (root.empty()) return;
    const std::string dff = root + "texdb/patch/mx_cheetah_plate.dff";
    struct stat st{};
    if (stat(dff.c_str(), &st) != 0) return;                      // sem arquivo = nada a fazer
    uint8_t ph[8];
    {   // so o cabecalho (8 bytes): o arquivo inteiro (4,5 MB) so e lido se for mesmo preciso aplicar
        FILE* pf = fopen(dff.c_str(), "rb");
        if (!pf || fread(ph, 1, 8, pf) != 8) { if (pf) fclose(pf); FLog("[PLACA] patch: nao li %s", dff.c_str()); return; }
        fclose(pf);
    }
    uint32_t t0, s0;
    memcpy(&t0, ph, 4); memcpy(&s0, ph + 4, 4);
    if (t0 != 0x10 || (size_t)s0 + 12 != (size_t)st.st_size) { FLog("[PLACA] patch: %s nao e um DFF valido (tipo=%u tamanho=%u arquivo=%lld): ignorado", dff.c_str(), t0, s0, (long long)st.st_size); return; }

    const std::string imgPath = root + "texdb/viaturas.img";
    FILE* f = fopen(imgPath.c_str(), "r+b");
    if (!f) { FLog("[PLACA] patch: %s nao abriu para escrita (%s): ignorado", imgPath.c_str(), strerror(errno)); return; }
    struct Closer { FILE* f; ~Closer() { if (f) fclose(f); } } closer{ f };
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "VER2", 4) != 0) { FLog("[PLACA] patch: viaturas.img nao e IMG v2: ignorado"); return; }
    uint32_t count; memcpy(&count, hdr + 4, 4);
    if (count == 0 || count > 20000) { FLog("[PLACA] patch: viaturas.img com %u entradas: ignorado", count); return; }
    uint32_t off = 0, sz = 0; bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t e[32];
        if (fread(e, 1, 32, f) != 32) break;
        char name[25]; memcpy(name, e + 8, 24); name[24] = 0;
        if (strcasecmp(name, "mx_cheetah.dff") == 0) { memcpy(&off, e, 4); uint16_t a, b; memcpy(&a, e + 4, 2); memcpy(&b, e + 6, 2); sz = b ? b : a; found = true; break; }
    }
    if (!found) { FLog("[PLACA] patch: 'mx_cheetah.dff' nao existe em viaturas.img: ignorado"); return; }
    const uint64_t slot = (uint64_t)sz * 2048u, at = (uint64_t)off * 2048u;
    if ((uint64_t)st.st_size > slot) { FLog("[PLACA] patch: DFF novo (%lld B) maior que o espaco da entrada (%llu B): ignorado", (long long)st.st_size, (unsigned long long)slot); return; }

    // ja aplicado? o DFF novo tem tamanho de clump diferente do original: compara o cabecalho gravado no IMG
    uint8_t cur[8];
    if (fseeko(f, (off_t)at, SEEK_SET) != 0 || fread(cur, 1, 8, f) != 8) { FLog("[PLACA] patch: nao li a entrada no IMG"); return; }
    uint32_t ct, cs; memcpy(&ct, cur, 4); memcpy(&cs, cur + 4, 4);
    if (ct == 0x10 && cs == s0) { FLog("[PLACA] patch: viaturas.img ja tem o mx_cheetah com placa (posn=%u)", off); return; }
    if (ct != 0x10 || (uint64_t)cs + 12 > slot) { FLog("[PLACA] patch: a entrada no IMG nao parece um DFF (tipo=%u tam=%u): ignorado", ct, cs); return; }

    std::vector<uint8_t> nw;
    if (!ReadFile(dff, nw) || nw.size() != (size_t)st.st_size) { FLog("[PLACA] patch: nao li %s inteiro", dff.c_str()); return; }

    // guarda o original (so 1 vez) para poder voltar
    std::vector<uint8_t> old((size_t)cs + 12);
    if (fseeko(f, (off_t)at, SEEK_SET) != 0 || fread(old.data(), 1, old.size(), f) != old.size()) { FLog("[PLACA] patch: nao li o DFF original"); return; }
    if (!WriteFileIfAbsent(root + "texdb/patch/mx_cheetah.orig.dff", old.data(), old.size()))
        FLog("[PLACA] AVISO: nao salvei texdb/patch/mx_cheetah.orig.dff (sem backup do original)");

    if (fseeko(f, (off_t)at, SEEK_SET) != 0) return;
    bool ok = fwrite(nw.data(), 1, nw.size(), f) == nw.size();
    // zera o resto da entrada (o DFF novo e menor)
    std::vector<uint8_t> zeros(4096, 0);
    uint64_t left = slot - nw.size();
    while (ok && left > 0) { const size_t n = (size_t)std::min<uint64_t>(left, zeros.size()); ok = fwrite(zeros.data(), 1, n, f) == n; left -= n; }
    ok = ok && fflush(f) == 0;
    FLog("[PLACA] patch: mx_cheetah.dff (posn=%u, espaco %llu B) substituido por %zu B: %s", off, (unsigned long long)slot, nw.size(), ok ? "OK" : "FALHOU (IMG pode estar incompleto: apague texdb/patch/mx_cheetah_plate.dff e restaure o viaturas.img)");
}

} // namespace CarPlate
