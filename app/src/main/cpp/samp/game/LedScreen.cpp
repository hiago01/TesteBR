#include "LedScreen.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <dirent.h>
#include <strings.h>

#include "../main.h"
#include "../log.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "Models/ModelInfo.h"
#include "../vendor/stb/stb_image.h"      // a implementacao esta em sprite2d.cpp

namespace LedScreen {
namespace {
constexpr int   kModel = 12002;               // mx_posto (SAMP/mapasmaximo.ide)
constexpr int   kLightsModel = 12003;         // mx_luzes: pisca-pisca de Natal na cobertura (materiais 0..3 = vermelho, amarelo, verde, azul)
constexpr size_t kMaxFrames = 96;             // limite de seguranca (memoria)
constexpr int   kMaxSide = 256;               // quadros maiores sao ignorados

struct Screen {
    const char* folder;                       // texdb/led/<folder>/ (quadros) ou, se "file", o PNG unico texdb/led/<folder>
    bool        isFile = false;               // true = textura estatica (1 PNG)
    const char* matTex;                       // nome da textura do material que vira tela
    std::vector<RwRaster*> frames;
    RwTexture* tex = nullptr;                 // textura propria (so esta placa muda; a TXD compartilhada fica intacta)
    bool loaded = false;
};
// placa de precos (animada, 3 lados do outdoor) e friso da cobertura (PNG estatico). A placa do alto mostra o logo BR MANIA (PNG estatico).
Screen s_scr[3] = { { "board", false, "gassign1_256" }, { "beira.png", true, "closedblank1_256" }, { "brmania_topo.png", true, "cargo7_128" } };   // 3a: logo BR MANIA da placa do alto (substitui o CAR WASH)
void* s_lastObj = nullptr;
int   s_fps = 8;
bool  s_cfgRead = false;
uint64_t s_lastLog = 0;

uint64_t NowMs() { timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }

std::string Root() { if (!g_pszStorage || !g_pszStorage[0]) return {}; std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/'); return p; }

RwRaster* MakeRaster(const unsigned char* src, int w, int h)
{
    if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) return nullptr;
    RwImage* image = RwImageCreate(w, h, 32);
    if (!image || !RwImageAllocatePixels(image)) { if (image) RwImageDestroy(image); return nullptr; }
    for (int y = 0; y < h; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, src + (size_t)y * w * 4, (size_t)w * 4);
    int rw = w, rh = h, rd = 32, rf = 0;
    RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
    rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
    if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4)) {
        rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
        rf |= rwRASTERFORMAT8888; rd = 32;
    }
    if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;
    RwRaster* r = RwRasterCreate(rw, rh, rd, rf);
    if (!r) { RwImageDestroy(image); return nullptr; }
    if (!RwRasterSetFromImage(r, image)) { if (RwRasterDestroy) RwRasterDestroy(r); RwImageDestroy(image); return nullptr; }
    RwImageDestroy(image);
    return r;
}

void ReadCfg()
{
    s_cfgRead = true;
    const std::string r = Root(); if (r.empty()) return;
    FILE* f = fopen((r + "texdb/led/led.txt").c_str(), "r"); if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) { int v = 0; if (sscanf(line, " fps = %d", &v) == 1 && v >= 1 && v <= 30) s_fps = v; }
    fclose(f);
}

// Carrega TODOS os quadros da pasta (1 vez). Falha = tela fica nas placas originais.
void LoadScreen(Screen& s)
{
    s.loaded = true;   // nao tenta de novo todo frame
    std::string dir = Root() + "texdb/led/" + s.folder + "/";
    std::vector<std::string> names;
    if (s.isFile) { dir = Root() + "texdb/led/"; names.push_back(s.folder); }
    else if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) { const size_t n = strlen(e->d_name); if (n > 4 && strcasecmp(e->d_name + n - 4, ".png") == 0) names.push_back(e->d_name); }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    if (names.size() > kMaxFrames) names.resize(kMaxFrames);
    for (const std::string& n : names) {
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load((dir + n).c_str(), &w, &h, &ch, 4);
        if (!px) { FLog("[LED] %s: %s", n.c_str(), stbi_failure_reason()); continue; }
        if (w > kMaxSide || h > kMaxSide) { FLog("[LED] %s: %dx%d maior que %d, ignorado", n.c_str(), w, h, kMaxSide); stbi_image_free(px); continue; }
        if (RwRaster* r = MakeRaster(px, w, h)) s.frames.push_back(r);
        stbi_image_free(px);
    }
    if (s.frames.empty()) { FLog("[LED] %s: sem quadros em %s (placa original mantida)", s.folder, dir.c_str()); return; }
    s.tex = RwTextureCreate(s.frames[0]);
    if (!s.tex) { FLog("[LED] %s: RwTextureCreate falhou", s.folder); s.frames.clear(); return; }
    RwTextureSetName(s.tex, s.isFile ? (strstr(s.folder, "brmania") ? "mx_brlogo" : "mx_beira") : "mx_led");
    RwTextureSetFilterMode(s.tex, rwFILTERLINEAR);
    RwTextureSetAddressing(s.tex, rwTEXTUREADDRESSWRAP);   // a beira repete (UV > 1)
    FLog("[LED] %s: %zu quadros carregados, %d fps", s.folder, s.frames.size(), s_fps);
}

// Troca a textura dos materiais cuja textura tem o nome da placa (so neste modelo; a TXD barrio1_lae nao e tocada).
int ApplyToGeometry(RpGeometry* g)
{
    if (!g || !g->matList.materials || g->matList.numMaterials <= 0 || g->matList.numMaterials > 64) return 0;
    int n = 0;
    for (int i = 0; i < g->matList.numMaterials; ++i) {
        RpMaterial* m = g->matList.materials[i];
        if (!m || !m->texture) continue;
        for (Screen& s : s_scr) {
            if (!s.tex || m->texture == s.tex) continue;
            if (strcasecmp(m->texture->name, s.matTex) != 0) continue;
            s.tex->refCount++;                 // o material passa a ser dono de 1 referencia: destruir o modelo nao libera a nossa textura
            m->texture = s.tex;
            ++n;
        }
    }
    return n;
}
// Pisca-pisca: 2 das 4 cores acesas, girando a cada ~450 ms. Muda so a COR dos materiais 0..3 do modelo 12003 (o resto nao e tocado).
void ProcessLights()
{
    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[kLightsModel];
    if (!mi || !mi->m_pRwObject) return;
    RwObject* obj = mi->m_pRwObject;
    RpGeometry* g = nullptr;
    if (obj->type == rpATOMIC) g = ((RpAtomic*)obj)->geometry;
    else return;                                    // o modelo e' um atomic simples (DFF de 1 atomic)
    if (!g || !g->matList.materials || g->matList.numMaterials < 4) return;
    static const unsigned char base[4][3] = { {255,30,30}, {255,200,20}, {30,230,50}, {40,90,255} };
    const unsigned step = (unsigned)(NowMs() / 450u);
    for (int i = 0; i < 4; ++i) {
        RpMaterial* m = g->matList.materials[i];
        if (!m) continue;
        const bool on = ((step + (unsigned)i) & 3u) < 2u;
        const int k = on ? 100 : 22;                // % do brilho
        m->color.red   = (unsigned char)(base[i][0] * k / 100);
        m->color.green = (unsigned char)(base[i][1] * k / 100);
        m->color.blue  = (unsigned char)(base[i][2] * k / 100);
        m->color.alpha = 255;
    }
}
} // namespace

void Process()
{
    ProcessLights();
    CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[kModel];
    if (!mi) return;
    RwObject* obj = mi->m_pRwObject;
    if (!obj) { s_lastObj = nullptr; return; }       // modelo fora de streaming: reaplica quando voltar

    if (!s_cfgRead) ReadCfg();
    for (Screen& s : s_scr) if (!s.loaded) LoadScreen(s);

    if (s_lastObj != obj) {                          // modelo (re)criado: materiais novos com a textura original
        int n = 0;
        if (obj->type == rpATOMIC) n = ApplyToGeometry(((RpAtomic*)obj)->geometry);
        else if (obj->type == rpCLUMP) {
            struct Ctx { int n; } c{0};
            RpClumpForAllAtomics((RpClump*)obj, [](RpAtomic* a, void* d) -> RpAtomic* { ((Ctx*)d)->n += ApplyToGeometry(a->geometry); return a; }, &c);
            n = c.n;
        }
        s_lastObj = obj;
        FLog("[LED] modelo %d obj=%p tipo=%d: %d material(is) trocado(s)", kModel, (void*)obj, (int)obj->type, n);
    }

    const uint64_t now = NowMs();
    const uint64_t step = now * (uint64_t)s_fps / 1000u;   // indice global do quadro
    for (Screen& s : s_scr)
        if (s.tex && !s.frames.empty()) s.tex->raster = s.frames[step % s.frames.size()];
    (void)s_lastLog;
}
} // namespace LedScreen
