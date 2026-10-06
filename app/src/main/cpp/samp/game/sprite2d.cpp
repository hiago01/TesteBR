#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../vendor/stb/stb_image.h"

#include <sys/stat.h>
#include "../main.h"
#include <cstdarg>
#include <cstdio>
#include "game.h"
#include "sprite2d.h"
#include "../vendor/armhook/patch.h"
#include "Scene.h"
#include "RW/RenderWare.h"
#include "../gui/gui.h"

extern UI* pUI;

// ============================================================
// DEBUG DO RADAR DISC - LOG + CHAT
// ============================================================
static void RadarDiscDebug(const char* message)
{
    FLog("%s", message);

    if (pUI && pUI->chat())
        pUI->chat()->addDebugMessage("%s", message);
}

static void RadarDiscDebugFmt(const char* format, ...)
{
    char buffer[512];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    FLog("%s", buffer);

    if (pUI && pUI->chat())
        pUI->chat()->addDebugMessage("%s", buffer);
}


void CSprite2d::Draw(float x, float y, float width, float height, CRGBA* color)
{
	CHook::CallFunction<void>(g_libGTASA + 0x6ED440, this, x, y, width, height, color);
}

void CSprite2d::Draw(const CRect& posn, const CRGBA& color)
{
	Draw(posn, color, color, color, color);
}

void CSprite2d::Draw(const CRect& posn, const CRGBA& color1, const CRGBA& color2, const CRGBA& color3, const CRGBA& color4)
{
	SetVertices(posn, color1, color2, color3, color4);
	SetRenderState();
	RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, maVertices, 4);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(NULL));
}

// different kinds of vertices' defining
void CSprite2d::SetVertices(const CRect& posn, const CRGBA& color1, const CRGBA& color2, const CRGBA& color3, const CRGBA& color4)
{
	float offset = 1.0f / 1024.0f;
	SetVertices(posn, color1, color2, color3, color4,
				offset,       offset,
				1.f + offset,       offset,
				offset, 1.f + offset,
				1.f + offset, 1.f + offset);
}

void CSprite2d::SetVertices(int32 numVerts, const CVector2D* posn, const CVector2D* texCoors, const CRGBA& color)
{
	for (int32 i = 0; i < numVerts; ++i) {
		RwIm2DVertexSetScreenX(&maVertices[i], posn[i].x);
		RwIm2DVertexSetScreenY(&maVertices[i], posn[i].y);
		RwIm2DVertexSetScreenZ(&maVertices[i], NearScreenZ + 0.0001f);
		RwIm2DVertexSetRecipCameraZ(&maVertices[i], RecipNearClip);
		RwIm2DVertexSetU(&maVertices[i], texCoors[i].x, RecipNearClip);
		RwIm2DVertexSetV(&maVertices[i], texCoors[i].y, RecipNearClip);
		RwIm2DVertexSetIntRGBA(&maVertices[i], color.r, color.g, color.b, color.a);
	}
}

void CSprite2d::SetVertices(int32 numVerts, const CVector2D* posn, const CRGBA& color)
{
	for (int32 i = 0; i < numVerts; ++i) {
		RwIm2DVertexSetScreenX(&maVertices[i], posn[i].x);
		RwIm2DVertexSetScreenY(&maVertices[i], posn[i].y);
		RwIm2DVertexSetScreenZ(&maVertices[i], NearScreenZ);
		RwIm2DVertexSetRecipCameraZ(&maVertices[i], RecipNearClip);
		RwIm2DVertexSetU(&maVertices[i], 1.f, RecipNearClip);
		RwIm2DVertexSetV(&maVertices[i], 1.f, RecipNearClip);
		RwIm2DVertexSetIntRGBA(&maVertices[i], color.r, color.g, color.b, color.a);
	}
}

void CSprite2d::SetVertices(const CRect& posn, const CRGBA& color1, const CRGBA& color2, const CRGBA& color3, const CRGBA& color4,
							float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4)
{
	SetVertices(maVertices, posn, color1, color2, color3, color4, u1, v1, u2, v2, u3, v3, u4, v4);
}

void CSprite2d::SetVertices(RwIm2DVertex* vertices, const CRect& posn, const CRGBA& color1, const CRGBA& color2, const CRGBA& color3, const CRGBA& color4,
							float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4)
{
	RwIm2DVertexSetScreenX(&vertices[0], posn.left);
	RwIm2DVertexSetScreenY(&vertices[0], posn.bottom);
	RwIm2DVertexSetScreenZ(&vertices[0], NearScreenZ);
	RwIm2DVertexSetRecipCameraZ(&vertices[0], RecipNearClip);
	RwIm2DVertexSetU(&vertices[0], u1, RecipNearClip);
	RwIm2DVertexSetV(&vertices[0], v1, RecipNearClip);
	RwIm2DVertexSetIntRGBA(&vertices[0], color3.r, color3.g, color3.b, color3.a);

	RwIm2DVertexSetScreenX(&vertices[1], posn.right);
	RwIm2DVertexSetScreenY(&vertices[1], posn.bottom);
	RwIm2DVertexSetScreenZ(&vertices[1], NearScreenZ);
	RwIm2DVertexSetRecipCameraZ(&vertices[1], RecipNearClip);
	RwIm2DVertexSetU(&vertices[1], u2, RecipNearClip);
	RwIm2DVertexSetV(&vertices[1], v2, RecipNearClip);
	RwIm2DVertexSetIntRGBA(&vertices[1], color4.r, color4.g, color4.b, color4.a);

	RwIm2DVertexSetScreenX(&vertices[2], posn.right);
	RwIm2DVertexSetScreenY(&vertices[2], posn.top);
	RwIm2DVertexSetScreenZ(&vertices[2], NearScreenZ);
	RwIm2DVertexSetRecipCameraZ(&vertices[2], RecipNearClip);
	RwIm2DVertexSetU(&vertices[2], u4, RecipNearClip);
	RwIm2DVertexSetV(&vertices[2], v4, RecipNearClip);
	RwIm2DVertexSetIntRGBA(&vertices[2], color2.r, color2.g, color2.b, color2.a);

	RwIm2DVertexSetScreenX(&vertices[3], posn.left);
	RwIm2DVertexSetScreenY(&vertices[3], posn.top);
	RwIm2DVertexSetScreenZ(&vertices[3], NearScreenZ);
	RwIm2DVertexSetRecipCameraZ(&vertices[3], RecipNearClip);
	RwIm2DVertexSetU(&vertices[3], u3, RecipNearClip);
	RwIm2DVertexSetV(&vertices[3], v3, RecipNearClip);
	RwIm2DVertexSetIntRGBA(&vertices[3], color1.r, color1.g, color1.b, color1.a);
}


// Sets sprite texture as current for device rendering
// 0x727B30
void CSprite2d::SetRenderState()
{
	if (m_pTexture)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(RwTextureGetRaster(m_pTexture)));
	else
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(NULL));
}

CSprite2d::~CSprite2d()
{
    if(m_pTexture) {
        RwTextureDestroy(m_pTexture);
        m_pTexture = 0;
    }
}

RwTexture* CSprite2d::LoadPngTex(const char* name) {
	auto sprite = new CSprite2d();
	sprite->SetTexture(name);

	return sprite->m_pTexture;
}

// ============================================================
// CUSTOM RADAR DISC
// Carrega SAMP/radar/radardisc.png e substitui a textura
// nativa "radardisc".
// ============================================================

static RwTexture* g_CustomRadarDiscTexture = nullptr;
static bool g_CustomRadarDiscTried = false;

static RwTexture* LoadCustomRadarDisc()
{
    if (g_CustomRadarDiscTexture)
        return g_CustomRadarDiscTexture;

    if (!g_pszStorage)
    {
        RadarDiscDebug("[RADAR DISC] g_pszStorage ainda NULL");
        return nullptr;
    }

    if (g_CustomRadarDiscTried)
        return nullptr;

    g_CustomRadarDiscTried = true;

    char path[512];

    snprintf(
        path,
        sizeof(path),
        "%sSAMP/radar/radardisc.png",
        g_pszStorage
    );

    RadarDiscDebugFmt(
        "[RADAR DISC] stb_image carregando: %s",
        path
    );

    int width = 0;
    int height = 0;
    int channels = 0;

    unsigned char* pixels = stbi_load(
        path,
        &width,
        &height,
        &channels,
        4
    );

    if (!pixels)
    {
        RadarDiscDebugFmt(
            "[RADAR DISC] stbi_load FALHOU: %s",
            stbi_failure_reason()
        );

        return nullptr;
    }

    RadarDiscDebugFmt(
        "[RADAR DISC] PNG OK: %dx%d canais=%d",
        width,
        height,
        channels
    );

    if (!RwImageCreate)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwImageCreate NULL"
        );

        stbi_image_free(pixels);
        return nullptr;
    }

    if (!RwImageAllocatePixels)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwImageAllocatePixels NULL"
        );

        stbi_image_free(pixels);
        return nullptr;
    }

    RwImage* image = RwImageCreate(
        width,
        height,
        32
    );

    if (!image)
    {
        RadarDiscDebug(
            "[RADAR DISC] ERRO: RwImageCreate"
        );

        stbi_image_free(pixels);
        return nullptr;
    }

    if (!RwImageAllocatePixels(image))
    {
        RadarDiscDebug(
            "[RADAR DISC] ERRO: RwImageAllocatePixels"
        );

        RwImageDestroy(image);
        stbi_image_free(pixels);

        return nullptr;
    }

    RadarDiscDebugFmt(
        "[RADAR DISC] RwImage criado: stride=%d",
        image->stride
    );

    const int srcStride = width * 4;

    for (int y = 0; y < height; y++)
    {
        memcpy(
            image->cpPixels + (y * image->stride),
            pixels + (y * srcStride),
            srcStride
        );
    }

    stbi_image_free(pixels);

    RadarDiscDebug(
        "[RADAR DISC] Pixels copiados para RwImage"
    );

    if (!RwImageFindRasterFormat)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwImageFindRasterFormat NULL"
        );

        RwImageDestroy(image);
        return nullptr;
    }

    int rasterWidth = width;
    int rasterHeight = height;
    int rasterDepth = 32;
    int rasterFormat = 0;

    RwImageFindRasterFormat(
        image,
        rwRASTERTYPETEXTURE,
        &rasterWidth,
        &rasterHeight,
        &rasterDepth,
        &rasterFormat
    );

    RadarDiscDebugFmt(
        "[RADAR DISC] Raster format: %d %dx%d depth=%d",
        rasterFormat,
        rasterWidth,
        rasterHeight,
        rasterDepth
    );

    if (!RwRasterCreate)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwRasterCreate NULL"
        );

        RwImageDestroy(image);
        return nullptr;
    }

    RwRaster* raster = RwRasterCreate(
        rasterWidth,
        rasterHeight,
        rasterDepth,
        rasterFormat
    );

    if (!raster)
    {
        RadarDiscDebug(
            "[RADAR DISC] ERRO: RwRasterCreate"
        );

        RwImageDestroy(image);
        return nullptr;
    }

    if (!RwRasterSetFromImage)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwRasterSetFromImage NULL"
        );

        if (RwRasterDestroy)
            RwRasterDestroy(raster);

        RwImageDestroy(image);
        return nullptr;
    }

    if (!RwRasterSetFromImage(raster, image))
    {
        RadarDiscDebug(
            "[RADAR DISC] ERRO: RwRasterSetFromImage"
        );

        if (RwRasterDestroy)
            RwRasterDestroy(raster);

        RwImageDestroy(image);
        return nullptr;
    }

    RwImageDestroy(image);

    RadarDiscDebug(
        "[RADAR DISC] RwRaster criado com sucesso"
    );

    if (!RwTextureCreate)
    {
        RadarDiscDebug(
            "[RADAR DISC] RwTextureCreate NULL"
        );

        if (RwRasterDestroy)
            RwRasterDestroy(raster);

        return nullptr;
    }

    g_CustomRadarDiscTexture = RwTextureCreate(raster);

    if (!g_CustomRadarDiscTexture)
    {
        RadarDiscDebug(
            "[RADAR DISC] ERRO: RwTextureCreate retornou NULL"
        );

        if (RwRasterDestroy)
            RwRasterDestroy(raster);

        return nullptr;
    }

    RadarDiscDebugFmt(
        "[RADAR DISC] TEXTURA CUSTOM CRIADA: %p ref=%d",
        g_CustomRadarDiscTexture,
        g_CustomRadarDiscTexture->refCount
    );

    return g_CustomRadarDiscTexture;
}

// Ponteiro para a funcao original.
void (*CSprite2d__SetTexture)(
    CSprite2d* thiz,
    char* name
) = nullptr;


// Hook do SetTexture.
// Somente "radardisc" sera substituido.
void CSprite2d__SetTexture_hook(
    CSprite2d* thiz,
    char* name
)
{
    if (!thiz || !name)
    {
        if (CSprite2d__SetTexture)
            CSprite2d__SetTexture(thiz, name);

        return;
    }

    if (strcmp(name, "radardisc") != 0)
    {
        CSprite2d__SetTexture(thiz, name);
        return;
    }

    RadarDiscDebug("[RADAR DISC] SetTexture(\"radardisc\")");

    RwTexture* custom = LoadCustomRadarDisc();

    // Se nao conseguiu carregar, deixa o jogo original trabalhar.
    if (!custom)
    {
        RadarDiscDebug("[RADAR DISC] Fallback para textura original");

        CSprite2d__SetTexture(thiz, name);
        return;
    }

    // Ja esta usando nossa textura.
    // Evita aumentar refCount varias vezes.
    if (thiz->m_pTexture == custom)
    {
        RadarDiscDebug("[RADAR DISC] Ja esta usando textura custom");
        return;
    }

    // Primeiro deixa a funcao original criar/configurar
    // a textura normalmente. Isso tambem cuida da textura
    // anterior do objeto.
    CSprite2d__SetTexture(thiz, name);

    // A funcao original acabou de colocar a radardisc nativa.
    // Descartamos essa textura e colocamos a nossa.
    if (thiz->m_pTexture && thiz->m_pTexture != custom)
    {
        RwTextureDestroy(thiz->m_pTexture);
    }

    // O CSprite2d passa a possuir uma referencia da textura.
    ++custom->refCount;

    thiz->m_pTexture = custom;

    RadarDiscDebugFmt(
        "[RADAR DISC] SUBSTITUIDA! tex=%p ref=%d",
        custom,
        custom->refCount
    );
}
// Ponteiro para a sobrecarga original:
// CSprite2d::SetTexture(char* name, char* mask)
void (*CSprite2d__SetTexture2)(
    CSprite2d* thiz,
    char* name,
    char* mask
) = nullptr;

// Hook da sobrecarga de 2 argumentos.
// Por enquanto apenas diagnostica o uso de radardisc.
void CSprite2d__SetTexture2_hook(
    CSprite2d* thiz,
    char* name,
    char* mask
)
{
    if (name && strcmp(name, "radardisc") == 0)
    {
        RadarDiscDebugFmt(
            "[RADAR DISC] SetTexture2 name=%s mask=%s",
            name,
            mask ? mask : "(null)"
        );
    }

    if (CSprite2d__SetTexture2)
    CSprite2d__SetTexture2(thiz, name, mask);
}
bool CSprite2d_TestCustomRadarDisc()
{
    // Permite tentar novamente mesmo se uma tentativa anterior falhou
    g_CustomRadarDiscTried = false;

    RwTexture* texture = LoadCustomRadarDisc();

    if (texture)
    {
        RadarDiscDebugFmt(
            "[RADAR DISC] TESTE OK! textura=%p ref=%d",
            texture,
            texture->refCount
        );

        return true;
    }

    RadarDiscDebug("[RADAR DISC] TESTE FALHOU!");

    return false;
}
// set texture by name from current txd
// 0x727270
void CSprite2d::SetTexture(const char* name)
{
	CHook::CallFunction<void>("_ZN9CSprite2d10SetTextureEPc", this, name);
}

void CSprite2d::InjectHooks() {
	CHook::Write(g_libGTASA + 0x84ADC8, &CSprite2d::RecipNearClip);
	CHook::Write(g_libGTASA + 0x849FA0, &CSprite2d::NearScreenZ);
}

void CSprite2d::SetRecipNearClip() {

}
