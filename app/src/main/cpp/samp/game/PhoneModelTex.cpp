#include "PhoneModelTex.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

#include "../main.h"
#include "../log.h"
#include "game.h"
#include "CustomSkinStream.h"
#include "RW/RenderWare.h"
#include "Models/ModelInfo.h"
#include "../vendor/stb/stb_image.h"      // a implementacao esta em sprite2d.cpp

namespace PhoneModelTex {
namespace {
constexpr int kMaxSide = 1024;

struct Item
{
    int         model;
    const char* file;
    const char* texName;
    RwTexture*  tex = nullptr;
    bool        tried = false;
    void*       lastObj = nullptr;
};
Item s_it[2] = { { 330, "iph17pro.png", "iph17pro" }, { 367, "iph16aqua.png", "iph16aqua" } };

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

RwTexture* LoadTex(const Item& it)
{
    const std::string path = Root() + "texdb/celular/" + it.file;
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px) { FLog("[CELTEX] %s: %s", path.c_str(), stbi_failure_reason()); return nullptr; }
    if (w > kMaxSide || h > kMaxSide) { FLog("[CELTEX] %s: %dx%d maior que %d: ignorado", it.file, w, h, kMaxSide); stbi_image_free(px); return nullptr; }
    RwRaster* r = MakeRaster(px, w, h);
    stbi_image_free(px);
    if (!r) { FLog("[CELTEX] %s: nao consegui criar o raster", it.file); return nullptr; }
    RwTexture* t = RwTextureCreate(r);
    if (!t) { FLog("[CELTEX] %s: RwTextureCreate falhou", it.file); if (RwRasterDestroy) RwRasterDestroy(r); return nullptr; }
    RwTextureSetName(t, it.texName);
    RwTextureSetFilterMode(t, rwFILTERLINEAR);
    RwTextureSetAddressing(t, rwTEXTUREADDRESSCLAMP);
    FLog("[CELTEX] %s: %dx%d carregada", it.file, w, h);
    return t;
}

// os DFFs destes modelos tem 1 material; troca a textura de todos (o modelo em memoria e' o do texdb/DFF, garantido por LooseApplied)
int ApplyToGeometry(RpGeometry* g, RwTexture* tex)
{
    if (!g || !g->matList.materials || g->matList.numMaterials <= 0 || g->matList.numMaterials > 16) return 0;
    int n = 0;
    for (int i = 0; i < g->matList.numMaterials; ++i) {
        RpMaterial* m = g->matList.materials[i];
        if (!m || m->texture == tex) continue;
        tex->refCount++;                  // o material passa a ser dono de 1 referencia
        m->texture = tex;
        ++n;
    }
    return n;
}
} // namespace

void Process()
{
    for (Item& it : s_it) {
        if (!CustomSkinStream::LooseApplied(it.model)) continue;
        if (it.model < 0) continue;
        CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[it.model];
        if (!mi) continue;
        RwObject* obj = mi->m_pRwObject;
        if (!obj) { it.lastObj = nullptr; continue; }          // fora de streaming: reaplica quando voltar
        if (obj == it.lastObj) continue;
        it.lastObj = obj;
        if (!it.tex && !it.tried) { it.tried = true; it.tex = LoadTex(it); }
        if (!it.tex) continue;
        int n = 0;
        if (obj->type == rpATOMIC) n = ApplyToGeometry(((RpAtomic*)obj)->geometry, it.tex);
        else if (obj->type == rpCLUMP) {
            struct Ctx { int n; RwTexture* t; } c{ 0, it.tex };
            RpClumpForAllAtomics((RpClump*)obj, [](RpAtomic* a, void* d) -> RpAtomic* { Ctx* x = (Ctx*)d; x->n += ApplyToGeometry(a->geometry, x->t); return a; }, &c);
            n = c.n;
        }
        FLog("[CELTEX] modelo %d obj=%p tipo=%d: %d material(is) com a textura %s", it.model, (void*)obj, (int)obj->type, n, it.texName);
    }
}
} // namespace PhoneModelTex
