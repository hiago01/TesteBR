#include <GLES2/gl2.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "../net/netgame.h"
#include "../gui/gui.h"
#include "Textures/TextureDatabase.h"
#include "Textures/TextureDatabaseEntry.h"
#include "Textures/TextureDatabaseRuntime.h"
#include "Scene.h"
#include "sprite2d.h"
#include "Entity/PlayerPedGta.h"
#include "Pools.h"
#include "java/jniutil.h"
#include "game/Models/ModelInfo.h"
#include "MatrixLink.h"
#include "MatrixLinkList.h"
#include "game/Collision/Collision.h"
#include "TxdStore.h"
#include "util/CUtil.h"
#include "Coronas.h"
#include "multitouch.h"
#include "Streaming.h"
#include "References.h"
#include "VisibilityPlugins.h"
#include "game/Animation/AnimManager.h"
#include "FileLoader.h"
#include "Renderer.h"
#include "CrossHair.h"
#include "World.h"
#include "playerped.h"
#include "Entity/CPedGTA.h"
#include "PlayerPedData.h"
#include "PedClothesDesc.h"
#include "ClothesTest.h"
#include "Plugins/RpAnimBlendPlugin/RpAnimBlend.h"
//#include "CPedGTA.h"
extern UI* pUI;
extern CGame* pGame;
extern CNetGame *pNetGame;
extern MaterialTextGenerator* pMaterialTextGenerator;

// Clothing changes are queued by /testeclothes and applied only after the
// original CGame::Process call returns. The descriptor mutation and native
// RebuildPlayer call therefore run together on the game-processing thread.
struct PendingPlayerClothesRequest
{
    CPlayerPedGta* player = nullptr;
    bool applyClothesChange = false;
    const char* texture = nullptr; // test uses string literals with static lifetime
    const char* model = nullptr;
    int component = -1;
};

static std::mutex g_PendingClothesMutex;
static PendingPlayerClothesRequest g_PendingClothesRequest{};
static std::atomic<bool> g_ClothesBuildDebugActive{false};
static void ProcessPendingPlayerClothesRebuild();

uint8_t byteInternalPlayer = 0;
CPedGTA* dwCurPlayerActor = 0;
uint8_t byteCurPlayer = 0;

extern "C" uintptr_t get_lib()
{
	return g_libGTASA;
}
// 0.3.7
PLAYERID FindPlayerIDFromGtaPtr(CEntityGTA* pEntity)
{
	if (pEntity == nullptr) return INVALID_PLAYER_ID;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();

	PLAYERID PlayerID = pPlayerPool->FindRemotePlayerIDFromGtaPtr((CPedGTA*)pEntity);
	if (PlayerID != INVALID_PLAYER_ID) return PlayerID;

	VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr((CVehicleGTA*)pEntity);
	if (VehicleID != INVALID_VEHICLE_ID)
	{
		for (PLAYERID i = 0; i < MAX_PLAYERS; i++)
		{
			CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(i);
			if (pRemotePlayer && pRemotePlayer->CurrentVehicleID() == VehicleID) {
				return i;
			}
		}
	}

	return INVALID_PLAYER_ID;
}
// 0.3.7
PLAYERID FindActorIDFromGtaPtr(CPedGTA* pPed)
{
	if (pPed) {
		return pNetGame->GetActorPool()->FindIDFromGtaPtr(pPed);
	}

	return INVALID_PLAYER_ID;
}
/* RADAR REAL POSITION - START
 *
 * The radar screen rectangle is reached through:
 *
 *   g_libGTASA + 0x850910 -> radar/UI state pointer
 *   state + 0x508          -> screen rect pointer
 *   rect + 0x2C            -> LEFT
 *   rect + 0x30            -> TOP
 *   rect + 0x34            -> RIGHT
 *   rect + 0x38            -> BOTTOM
 *
 * The normal radar transform also uses:
 *
 *   g_libGTASA + 0x8519B0 -> radar transform state
 *   +0x8C                  -> transform mode flag
 *   +0x78                  -> scale
 *   +0x7C                  -> X screen offset
 *   +0x80                  -> 2 * Y screen offset
 *
 * We temporarily move both coordinate systems while CHud::DrawRadar()
 * is executing, then restore the original values immediately.
 *
 * This keeps the GTA radar world/map logic intact while moving:
 *   - radar map
 *   - radar background
 *   - blips
 *   - player/"you are here" marker
 *   - radar mask/transform-based elements
 *
 * TEST POSITION:
 *   X = +300.0f
 *   Y =   +0.0f
 *
 * Change only g_RadarOffsetX/Y to choose another position.
 */

/* RADAR MOVE CONFIG */
static float g_RadarOffsetX = 0.0f;//antigo 300.0f
static float g_RadarOffsetY = 0.0f;

struct RadarScreenRect
{
    float left;
    float top;
    float right;
    float bottom;
};

static RadarScreenRect* GetRadarScreenRect()
{
    if (!g_libGTASA)
        return nullptr;

    /*
     * Disassembly:
     *
     *   adrp x8, 0x850000
     *   ldr  x8, [x8, #0x910]
     *   ldr  x21,[x8, #0x508]
     *
     * So:
     *   *(uintptr_t*)(base + 0x850910) -> radar state
     *   *(uintptr_t*)(radar state + 0x508) -> screen rect
     */
    uintptr_t radarState =
        *reinterpret_cast<uintptr_t*>(
            g_libGTASA + 0x850910
        );

    if (!radarState)
        return nullptr;

    uintptr_t rectAddress =
        *reinterpret_cast<uintptr_t*>(
            radarState + 0x508
        );

    if (!rectAddress)
        return nullptr;

    return reinterpret_cast<RadarScreenRect*>(
        rectAddress + 0x2C
    );
}

static uintptr_t GetRadarTransformState()
{
    if (!g_libGTASA)
        return 0;

    /*
     * TransformRadarPointToScreenSpace and DrawRadarMask use:
     *
     *   adrp x8, 0x851000
     *   ldr  x8, [x8, #0x9B0]
     */
    return *reinterpret_cast<uintptr_t*>(
        g_libGTASA + 0x8519B0
    );
}

/*
 * Move the radar coordinate systems before the original
 * CHud::DrawRadar() starts.
 */
static void BeginRadarMove(
    RadarScreenRect*& rect,
    uintptr_t& transformState,
    float& oldLeft,
    float& oldTop,
    float& oldRight,
    float& oldBottom,
    bool& rectChanged,
    float& oldTransformX,
    float& oldTransformY,
    bool& transformChanged
)
{
    rect = GetRadarScreenRect();

    transformState = GetRadarTransformState();

    rectChanged = false;
    transformChanged = false;

    if (rect)
    {
        oldLeft   = rect->left;
        oldTop    = rect->top;
        oldRight  = rect->right;
        oldBottom = rect->bottom;

	const float radarScale = 0.70f;//tamanho scale 2

	float centerX = (oldLeft + oldRight) * 0.5f;
	float centerY = (oldTop + oldBottom) * 0.5f;

	float halfWidth  = (oldRight - oldLeft) * 0.5f;
	float halfHeight = (oldBottom - oldTop) * 0.5f;

	halfWidth  *= radarScale;
	halfHeight *= radarScale;

	rect->left   = centerX - halfWidth + g_RadarOffsetX;
	rect->right  = centerX + halfWidth + g_RadarOffsetX;
	rect->top    = centerY - halfHeight + g_RadarOffsetY;
	rect->bottom = centerY + halfHeight + g_RadarOffsetY;

	rectChanged = true;

    }

    /*
     * Normal TransformRadarPointToScreenSpace() path:
     *
     *   OUT.X = IN.X * state[0x78] + state[0x7C]
     *
     *   OUT.Y = state[0x80] * 0.5
     *            - IN.Y * state[0x78]
     *
     * Therefore a screen movement of:
     *
     *   +X -> +g_RadarOffsetX in 0x7C
     *   +Y -> +2*g_RadarOffsetY in 0x80
     *
     * is required.
     *
     * We only modify these when the transform mode flag (+0x8C)
     * is enabled. In the other mode the screen rect above is used.
     */
    if (transformState)
    {
        uint8_t transformMode =
            *reinterpret_cast<uint8_t*>(
                transformState + 0x8C
            );

        if (transformMode)
        {
            float* transformX =
                reinterpret_cast<float*>(
                    transformState + 0x7C
                );

            float* transformY =
                reinterpret_cast<float*>(
                    transformState + 0x80
                );

            oldTransformX = *transformX;
            oldTransformY = *transformY;

            *transformX += g_RadarOffsetX;
            *transformY += g_RadarOffsetY * 2.0f;

            transformChanged = true;
        }
    }
}

static void EndRadarMove(
    RadarScreenRect* rect,
    uintptr_t transformState,
    float oldLeft,
    float oldTop,
    float oldRight,
    float oldBottom,
    bool rectChanged,
    float oldTransformX,
    float oldTransformY,
    bool transformChanged
)
{
    if (transformChanged && transformState)
    {
        *reinterpret_cast<float*>(
            transformState + 0x7C
        ) = oldTransformX;

        *reinterpret_cast<float*>(
            transformState + 0x80
        ) = oldTransformY;
    }

    if (rectChanged && rect)
    {
        rect->left   = oldLeft;
        rect->top    = oldTop;
        rect->right  = oldRight;
        rect->bottom = oldBottom;
    }
}

/*void (*CHud__Initialise)() = nullptr;

void CHud__Initialise_hook()
{
    if (pUI && pUI->chat())
        pUI->chat()->addDebugMessage(
            "[RADAR INIT] CHud::Initialise ENTROU"
        );

       FLog("[CHUD INITIALISE] >>> ENTROU");

    if (CHud__Initialise)
        CHud__Initialise();

    if (pUI && pUI->chat())
        pUI->chat()->addDebugMessage(
            "[RADAR INIT] CHud::Initialise TERMINOU"
        );
       FLog("[CHUD INITIALISE] <<< SAIU");
}*/

// ============================================================
// RADAR MASK - alteração segura dos 8 vértices
// ============================================================

static bool g_InRadarMask = false;
static int g_RadarMaskDebugCount = 0;

void (*CRadar__DrawRadarMask)(void);

void (*CSprite2d__SetMaskVertices)(
    int count,
    float* vertices
);

void CRadar__DrawRadarMask_hook()
{
    static int debugCount = 0;

    if (pUI && pUI->chat() && debugCount < 3)
    {
        pUI->chat()->addDebugMessage(
            "[RADAR MASK] DrawRadarMask #%d",
            debugCount + 1
        );
        debugCount++;
    }

    bool previous = g_InRadarMask;
    g_InRadarMask = true;

    CRadar__DrawRadarMask();

    g_InRadarMask = previous;
}

void CSprite2d__SetMaskVertices_hook(
    int count,
    float* vertices
)
{
    if (g_InRadarMask && count == 8 && vertices)
    {
        float centerX = 0.0f;
        float centerY = 0.0f;

        // Centro dos 8 vértices
        for (int i = 0; i < 8; i++)
        {
            centerX += vertices[i * 2];
            centerY += vertices[i * 2 + 1];
        }

        centerX /= 8.0f;
        centerY /= 8.0f;

        // Raio horizontal/vertical original
        float radiusX = 0.0f;
        float radiusY = 0.0f;

        for (int i = 0; i < 8; i++)
        {
            float dx = fabsf(vertices[i * 2] - centerX);
            float dy = fabsf(vertices[i * 2 + 1] - centerY);

            if (dx > radiusX)
                radiusX = dx;

            if (dy > radiusY)
                radiusY = dy;
        }

        if (radiusX > 1.0f && radiusY > 1.0f)
        {
            // 2.0 = círculo/ellipse original
            // 3.0 = levemente quadrado
            // 4.0 = quadrado arredondado
            // 6.0 = mais quadrado
            const float exponent = 6.0f;//antigo 4.0f
            const float invExponent = 1.0f / exponent;//antigo 1.0f
           const float radarScale = 1.10f;//off tamanho scale off nao pega
            for (int i = 0; i < 8; i++)
            {
                float dx = vertices[i * 2] - centerX;
                float dy = vertices[i * 2 + 1] - centerY;

                float x = dx / radiusX;
                float y = dy / radiusY;

                float ax = fabsf(x);
                float ay = fabsf(y);

                float denominator =
                    powf(
                        powf(ax, exponent) +
                        powf(ay, exponent),
                        invExponent
                    );

                if (denominator > 0.0001f)
                {
                    x /= denominator;
                    y /= denominator;
                }
vertices[i * 2] =
    centerX + (x * radiusX * radarScale);

vertices[i * 2 + 1] =
    centerY + (y * radiusY * radarScale);

            }
        }
    }

    CSprite2d__SetMaskVertices(
        count,
        vertices
    );
}

void (*CHud__DrawRadar)(void);

void CHud__DrawRadar_hook()
{
    RadarScreenRect* radarRect = nullptr;
    uintptr_t transformState = 0;

    float oldLeft = 0.0f;
    float oldTop = 0.0f;
    float oldRight = 0.0f;
    float oldBottom = 0.0f;

    bool rectChanged = false;

    float oldTransformX = 0.0f;
    float oldTransformY = 0.0f;

    bool transformChanged = false;

    BeginRadarMove(
        radarRect,
        transformState,
        oldLeft,
        oldTop,
        oldRight,
        oldBottom,
        rectChanged,
        oldTransformX,
        oldTransformY,
        transformChanged
    );

    CHud__DrawRadar();

    /*
     * Restore the original GTA values immediately after
     * the complete radar rendering pass.
     */
    EndRadarMove(
        radarRect,
        transformState,
        oldLeft,
        oldTop,
        oldRight,
        oldBottom,
        rectChanged,
        oldTransformX,
        oldTransformY,
        transformChanged
    );
}

/* RADAR REAL POSITION - END */

/* =============================================================================== */

void RenderEffects() {
//	RenderEffects();
    CHook::CallFunction<void>(g_libGTASA + 0x6C1D6C);
    CHook::CallFunction<void>(g_libGTASA + 0x6E2FB4);
//    CRopes::Render();
//    CGlass::Render();
    CHook::CallFunction<void>(g_libGTASA + 0x6CA5D0);
    CVisibilityPlugins::RenderReallyDrawLastObjects();
    CCoronas::Render();

    // FIXME
    CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
    auto g_fx = *(uintptr_t *) (g_libGTASA + 0xA062A8);
    CHook::CallFunction<void>(g_libGTASA + 0x433F54, &g_fx, TheCamera.m_pRwCamera, false);

    CHook::CallFunction<void>(g_libGTASA + 0x6F054C);
    CHook::CallFunction<void>(g_libGTASA + 0x6C0268);
    CHook::CallFunction<void>(g_libGTASA + 0x6C552C);
    //   CClouds::VolumetricCloudsRender();
////    if (CHeli::NumberOfSearchLights || CTheScripts::NumberOfScriptSearchLights) {
////        CHeli::Pre_SearchLightCone();
////        CHeli::RenderAllHeliSearchLights();
////        CTheScripts::RenderAllSearchLights();
////        CHeli::Post_SearchLightCone();
////    }
    CHook::CallFunction<void>(g_libGTASA + 0x708DF0);
////    if (CReplay::Mode != MODE_PLAYBACK && !CPad::GetPad(0)->DisablePlayerControls) {
////        FindPlayerPed()->DrawTriangleForMouseRecruitPed();
////    }
    CHook::CallFunction<void>(g_libGTASA + 0x6E50CC);
//    //CVehicleRecording::Render();
    CHook::CallFunction<void>(g_libGTASA + 0x6D6068);
//    //CRenderer::RenderFirstPersonVehicle();
    CHook::CallFunction<void>(g_libGTASA + 0x6DA2B8);

    //DebugModules::Render3D();
}

/*void MainLoop();
void(*Render2dStuff)();
void Render2dStuff_hook()
{
    Render2dStuff();
    if(pNetGame)
    {
        CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
        if(pTextDrawPool) pTextDrawPool->Draw();
    }
    if (pUI) pUI->render();
    return;
}*/
void Render2dStuff()
{
    if( CHook::CallFunction<bool>(g_libGTASA + 0x24EA90) ) // emu_IsAltRenderTarget()
        CHook::CallFunction<void>(g_libGTASA + 0x24F5B8); // emu_FlushAltRenderTarget()

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDINVSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, RWRSTATE(rwRENDERSTATENARENDERSTATE));
    RwRenderStateSet(rwRENDERSTATECULLMODE, RWRSTATE(rwCULLMODECULLNONE));

    //((void (*)()) (g_libGTASA + 0x51CFF0))(); // CHud::DrawRadar
    //CHook::CallFunction<void>("_ZN4CHud14DrawScriptTextEh", true);
    CHook::CallFunction<void>("_ZN4CHud4DrawEv");
    //	GPS::Draw();
    //
    ((void(*)(bool) )(g_libGTASA + 0x36FB00) )(false); // CTouchInterface::DrawAll

    CHook::CallFunction<void>("_Z12emu_GammaSeth", 1);

    ((void (*)(bool)) (g_libGTASA + 0x66B678))(1u); // CMessages::Display - gametext
    ((void (*)(bool)) (g_libGTASA + 0x6CCEA0))(1u); // CFont::RenderFontBuffer
    CHook::CallFunction<void>("_Z12emu_GammaSeth", 0);

    if(pNetGame)
    {
        CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
        if(pTextDrawPool) pTextDrawPool->Draw();
    }

    if (pUI) pUI->render();
}

/* =============================================================================== */

int (*CRadar__SetCoordBlip)(int r0, float X, float Y, float Z, int r4, int r5, char* name);
int CRadar__SetCoordBlip_hook(int r0, float X, float Y, float Z, int r4, int r5, char* name)
{
	if(pNetGame && !strncmp(name, "CODEWAY", 7))
	{
		float fFindZ = CWorld::FindGroundZForCoord(X, Y) + 1.5f;

		if(pNetGame->GetGameState() != GAMESTATE_CONNECTED) return 0;

		RakNet::BitStream bsSend;
		bsSend.Write(X);
		bsSend.Write(Y);
		bsSend.Write(fFindZ);
		pNetGame->GetRakClient()->RPC(&RPC_MapMarker, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}

	return CRadar__SetCoordBlip(r0, X, Y, Z, r4, r5, name);
}

/* =============================================================================== */

void(*CRadar_DrawRadarGangOverlay)(uint32_t unk);
void CRadar_DrawRadarGangOverlay_hook(uint32_t unk)
{
	if (pNetGame)
	{
		CGangZonePool *pGangZonePool = pNetGame->GetGangZonePool();
		if (pGangZonePool) {
			pGangZonePool->Draw(unk);
		}
	}
}

/* =============================================================================== */

typedef struct {
    CVector     vecPosObject;
    CQuaternion m_qRotation;
    int32       wModelIndex;
    union {
        struct { // CFileObjectInstanceType
            uint32 m_nAreaCode : 8;
            uint32 m_bRedundantStream : 1;
            uint32 m_bDontStream : 1; // Merely assumed, no countercheck possible.
            uint32 m_bUnderwater : 1;
            uint32 m_bTunnel : 1;
            uint32 m_bTunnelTransition : 1;
            uint32 m_nReserved : 19;
        };
        uint32 m_nInstanceType;
    };
    int32 m_nLodInstanceIndex; // -1 - without LOD model
} stLoadObjectInstance;
VALIDATE_SIZE(stLoadObjectInstance, 0x28);

extern int iBuildingToRemoveCount;
extern REMOVEBUILDING_DATA BuildingToRemove[1000];

int (*CFileLoader__LoadObjectInstance)(stLoadObjectInstance *thiz);
int CFileLoader__LoadObjectInstance_hook(stLoadObjectInstance *thiz) {
	if (thiz) {
		if (iBuildingToRemoveCount >= 1) {
			for (int i = 0; i < iBuildingToRemoveCount; i++)
			{
				float fDistance = GetDistance(BuildingToRemove[i].vecPos, thiz->vecPosObject);
				if (fDistance <= BuildingToRemove[i].fRange) {
					if (BuildingToRemove[i].dwModel == -1 || thiz->wModelIndex == (uint16_t) BuildingToRemove[i].dwModel) {
						thiz->wModelIndex = 19300;
                        //thiz->vecPosObject = 0.0f;
						break;
					}
				}
			}
		}
	}

	return CFileLoader__LoadObjectInstance(thiz);
}

extern int iBuildingToRemoveCount;
extern std::list<REMOVE_BUILDING_DATA> RemoveBuildingData;
void (*CEntity_Render)(CEntityGTA* pEntity);
int g_iLastRenderedObject;
void CEntity_Render_hook(CEntityGTA* pEntity)
{
    if(iBuildingToRemoveCount > 1)
    {
        if(pEntity && *(uintptr_t*)pEntity != g_libGTASA+0x8300A0 && !pNetGame->GetObjectPool()->GetObjectFromGtaPtr(pEntity))
        {
            for (auto &entry : RemoveBuildingData)
            {
                float fDistance = GetDistance(entry.vecPos, pEntity->GetMatrix().m_pos);
                if(fDistance <= entry.fRange)
                {
                    if(pEntity->GetModelId() == entry.usModelIndex)
                    {
                        pEntity->m_bUsesCollision = 0;
                        pEntity->m_bCollisionProcessed = 0;
                        return;
                    }
                }
            }
        }
    }
    g_iLastRenderedObject = pEntity->GetModelId();
    CEntity_Render(pEntity);
}

/* =============================================================================== */

/* =============================================================================== */

void (*CObject_Render)(CObjectGta* thiz);
void CObject_Render_hook(CObjectGta* thiz)
{
	CObjectGta *object = thiz;
	if(pNetGame && object != 0)
	{
		CObject *pObject = pNetGame->GetObjectPool()->FindObjectFromGtaPtr(object);
		if(pObject && pObject->m_pEntity)
		{
			RwObject* rwObject = (RwObject*)pObject->m_pEntity->m_pRwObject;
			if(rwObject)
			{
				// SetObjectMaterial
				if(pObject->m_bHasMaterial || pObject->m_bHasMaterialText)
				{
					RwFrameForAllObjects((RwFrame*)rwObject->parent, (RwObject *(*)(RwObject *, void *))ObjectMaterialCallBack, pObject);
					//RpAtomic* atomic = (RpAtomic*)object->m_pRwAtomic;
					//RpGeometryForAllMaterials(atomic->geometry, ObjectMaterialCallBack, (void*)pObject);
				}
				// SetObjectMaterialText
				if(pObject->m_bHasMaterialText)
                {
                    RwFrameForAllObjects((RwFrame*)rwObject->parent, (RwObject *(*)(RwObject *, void *))ObjectMaterialTextCallBack, pObject);
                    //RpAtomic* atomic = (RpAtomic*)object->m_pRwAtomic;
                    //RpGeometryForAllMaterials(atomic->geometry, ObjectMaterialTextCallBack, (void*)pObject);
                }
			}


		}

        CObject_Render(object);
	}

    //((void (*)(void))(g_libGTASA + 0x6F6664))();
    //((void (*)(void))(g_libGTASA + 0x5D1F5C + 1))();
}

/*((void (*)(void))(g_libGTASA + 0x5D1F48 + 1))();
				CObject_Render(thiz);
				// ActivateDirectional
				((void (*)(void))(g_libGTASA + 0x5D1F5C + 1))();*/
/* =============================================================================== */

/* =============================================================================== */

bool NotifyEnterVehicle(CVehicleGTA *_pVehicle)
{
	if(!pNetGame) {
		return false;
	}

	CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();
	if(!pVehiclePool) {
		return false;
	}

	CVehicle *pVehicle = nullptr;
	VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr(_pVehicle);

	if(VehicleID <= 0 || VehicleID >= MAX_VEHICLES) {
		return false;
	}

	if(!pVehiclePool->GetSlotState(VehicleID)) {
		return false;
	}

	pVehicle = pVehiclePool->GetAt(VehicleID);
	if(!pVehicle) {
		return false;
	}

	CLocalPlayer *pLocalPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer();

	if(pLocalPlayer) {
		FLog("Vehicle ID: %d", VehicleID);
		pLocalPlayer->SendEnterVehicleNotification(VehicleID, false);
	}

	return true;
}

int (*TaskEnterVehicle)(uintptr_t a1, uintptr_t a2);
int TaskEnterVehicleHook(uintptr_t a1, uintptr_t a2)
{
    if(!NotifyEnterVehicle((CVehicleGTA*)a1)) {
        return false;
    }

    // CTask::operator new
    uintptr_t pTask = ((uintptr_t (*)(void))(g_libGTASA + 0x5D7414))();

    // CTaskComplexEnterCarAsDriver::CTaskComplexEnterCarAsDriver
    ((void (__fastcall *)(uintptr_t, uintptr_t))(g_libGTASA + 0x6007E0))(pTask, a1);

    // CTaskManager::SetTask
    ((int (__fastcall *)(uintptr_t, uintptr_t, int, int))(g_libGTASA + 0x64E084))(a2, pTask, 3, 0);

    return true;
}

void (*CTaskComplexLeaveCar)(uintptr_t** thiz, CVehicleGTA* pVehicle, int iTargetDoor, int iDelayTime, bool bSensibleLeaveCar, bool bForceGetOut);
void CTaskComplexLeaveCar_hook(uintptr_t** thiz, CVehicleGTA* pVehicle, int iTargetDoor, int iDelayTime, bool bSensibleLeaveCar, bool bForceGetOut)
{
	uintptr_t dwRetAddr = 0;
	__asm__ volatile ("mov %0, lr" : "=r" (dwRetAddr));
	dwRetAddr -= g_libGTASA;

	if (dwRetAddr == 0x409A42+1 || dwRetAddr == 0x40A818+1)
	{
		if (pNetGame)
		{
			if ((CVehicleGTA*)GamePool_FindPlayerPed()->pVehicle == pVehicle)
			{
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr((CVehicleGTA*)GamePool_FindPlayerPed()->pVehicle);
				if (VehicleID != INVALID_VEHICLE_ID)
				{
					CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);
					CLocalPlayer* pLocalPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer();
					if (pVehicle && pLocalPlayer)
					{
						if (pVehicle->IsATrainPart())
						{
							RwMatrix mat = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();
							pLocalPlayer->GetPlayerPed()->RemoveFromVehicleAndPutAt(mat.pos.x + 2.5f, mat.pos.y + 2.5f, mat.pos.z);
						}
						else
						{
							pLocalPlayer->SendExitVehicleNotification(VehicleID);
						}
					}
				}
			}
		}
	}

	(*CTaskComplexLeaveCar)(thiz, pVehicle, iTargetDoor, iDelayTime, bSensibleLeaveCar, bForceGetOut);
}

/* =============================================================================== */

uint32_t CRadar__GetRadarTraceColor(uint32_t color, uint8_t bright, uint8_t friendly)
{
    return TranslateColorCodeToRGBA(color);
}

uint32_t CHudColours__GetIntColour(uintptr* thiz, uint8 colour_id)
{
    return TranslateColorCodeToRGBA(colour_id);
}

/* =============================================================================== */

void (*AND_TouchEvent)(int type, int num, int posX, int posY);
void AND_TouchEvent_hook(int type, int num, int posX, int posY)
{
	// imgui
	//bool bRet = pUI->OnTouchEvent(type, num, posX, posY);

	if (pGame->IsGamePaused())
		return AND_TouchEvent(type, num, posX, posY);

	if (pUI != nullptr)
	{
		switch (type)
		{
			case 2: // push
				pUI->touchEvent(ImVec2(posX, posY), TouchType::push);
				break;

			case 3: // move
				pUI->touchEvent(ImVec2(posX, posY), TouchType::move);
				break;

			case 1: // pop
				pUI->touchEvent(ImVec2(posX, posY), TouchType::pop);
				break;
		}

        if (pUI->keyboard()->visible() || pUI->dialog()->visible()) {
            AND_TouchEvent(1, 0, 0, 0);
            return;
        }
        else
        {
            if (pNetGame && pNetGame->GetTextDrawPool())
            {
                if (!pNetGame->GetTextDrawPool()->onTouchEvent(type, num, posX, posY)) {
                    return AND_TouchEvent(1, 0, 0, 0);
                }
            }
        }
	}

	if (pGame->IsGameInputEnabled())
		AND_TouchEvent(type, num, posX, posY);
	else
		AND_TouchEvent(1, 0, 0, 0);
}

/* =============================================================================== */

/* =============================================================================== */

/* =============================================================================== */

uint32_t (*CPed__GetWeaponSkill)(CPedGTA *thiz);
uint32_t CPed__GetWeaponSkill_hook(CPedGTA *thiz)
{
	bool bWeaponSkillStored = false;

	dwCurPlayerActor = thiz;
	byteInternalPlayer = CWorld::PlayerInFocus;
	byteCurPlayer = FindPlayerNumFromPedPtr(dwCurPlayerActor);

	if(dwCurPlayerActor && byteCurPlayer != 0 && CWorld::PlayerInFocus == 0)
	{
		GameStoreLocalPlayerSkills();
		GameSetRemotePlayerSkills(byteCurPlayer);
		bWeaponSkillStored = true;
	}

	// CPed::GetWeaponSkill
	uint32_t result = (( uint32_t (*)(CPedGTA *, uint32_t))(g_libGTASA+0x4A55E2+1))(thiz, thiz->m_aWeapons[thiz->m_nActiveWeaponSlot].dwType);

	if(bWeaponSkillStored)
	{
		GameSetLocalPlayerSkills();
		bWeaponSkillStored = false;
	}

	return result;
}

/* =============================================================================== */

extern CPlayerPed* g_pCurrentFiredPed;
extern BULLET_DATA* g_pCurrentBulletData;

extern int g_iLagCompensationMode;

void SendBulletSync(CVector* vecOrigin, CVector* a2, CColPoint *colPoint, CEntityGTA** ppEntity)
{
    CMatrix mat1, mat2;

    static BULLET_DATA bulletData;
    memset(&bulletData, 0, sizeof(BULLET_DATA));

    bulletData.vecOrigin.x = vecOrigin->x;
    bulletData.vecOrigin.y = vecOrigin->y;
    bulletData.vecOrigin.z = vecOrigin->z;

    bulletData.vecPos.x = colPoint->m_vecPoint.x;
    bulletData.vecPos.y = colPoint->m_vecPoint.y;
    bulletData.vecPos.z = colPoint->m_vecPoint.z;

    if (ppEntity)
    {
        CEntityGTA* pEntity = *ppEntity;
        if (pEntity)
        {
            if (g_iLagCompensationMode != 0)
            {
                bulletData.vecOffset.x = colPoint->m_vecPoint.x - pEntity->m_matrix->m_pos.x;
                bulletData.vecOffset.y = colPoint->m_vecPoint.y - pEntity->m_matrix->m_pos.y;
                bulletData.vecOffset.z = colPoint->m_vecPoint.z - pEntity->m_matrix->m_pos.z;
            }
            else
            {
                memset(&mat1, 0, sizeof(CMatrix));
                memset(&mat2, 0, sizeof(CMatrix));
                // RwMatrixOrthoNormalize
                auto entMat = pEntity->GetMatrix().ToRwMatrix();
                RwMatrixOrthoNormalize(reinterpret_cast<RwMatrix *>(&mat2), &entMat);
                // RwMatrixInvert
                Invert(mat1, mat2);

                ProjectMatrix(&bulletData.vecOffset, &mat1, &colPoint->m_vecPoint);
            }

            bulletData.pEntity = pEntity;
        }
        else bulletData.vecOffset = 0;
    }

    pGame->FindPlayerPed()->ProcessBulletData(&bulletData);
}

extern bool g_customFire;
/* 0.3.7 */
uint32_t(*CWeapon__FireInstantHit)(CWeapon* thiz, CPedGTA* pFiringEntity, CVector* vecOrigin, CVector* muzzlePosn, CEntityGTA* targetEntity,
								  CVector* target, CVector* originForDriveBy, bool arg6, bool muzzle);
uint32_t CWeapon__FireInstantHit_hook(CWeapon* thiz, CPedGTA* pFiringEntity, CVector* vecOrigin, CVector* muzzlePosn, CEntityGTA* targetEntity,
									 CVector* target, CVector* originForDriveBy, bool arg6, bool muzzle)
{
	if (pNetGame && pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed()->m_pPed)		// CWeapon::Fire
    {
       	if(pFiringEntity != GamePool_FindPlayerPed())
			return muzzle;

		if(pNetGame)
		{
            pNetGame->GetPlayerPool()->ApplyCollisionChecking();
		}

		if(pGame)
		{
			CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
			if(pPlayerPed)
				pPlayerPed->FireInstant();
		}

		if(pNetGame)
		{
            pNetGame->GetPlayerPool()->ResetCollisionChecking();
		}

		return muzzle;
    }

    return CWeapon__FireInstantHit(thiz, pFiringEntity, vecOrigin, muzzlePosn, targetEntity,
                                  target, originForDriveBy, arg6, muzzle);
}

bool g_bForceWorldProcessLineOfSight = false;
uint32_t (*CWeapon__ProcessLineOfSight)(CVector *vecOrigin, CVector *vecEnd, CVector *vecPos, CPedGTA **ppEntity, CWeapon *pWeaponSlot, CPedGTA **ppEntity2, bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7);
uint32_t CWeapon__ProcessLineOfSight_hook(CVector *vecOrigin, CVector *vecEnd, CVector *vecPos, CPedGTA **ppEntity, CWeapon *pWeaponSlot, CPedGTA **ppEntity2, bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7)
{
    uintptr_t dwRetAddr = 0;
    GET_LR(dwRetAddr);

    FLog("dwRetAddr CWeapon__ProcessLineOfSight_hook 0x%llx", dwRetAddr);

    if(dwRetAddr >= 0x701494 && dwRetAddr <= 0x702B18)
        g_bForceWorldProcessLineOfSight = true;

    return CWeapon__ProcessLineOfSight(vecOrigin, vecEnd, vecPos, ppEntity, pWeaponSlot, ppEntity2, b1, b2, b3, b4, b5, b6, b7);
}

uint32_t(*CWorld__ProcessLineOfSight)(CVector*, CVector*, CColPoint *colPoint, CEntityGTA**, bool, bool, bool, bool, bool, bool, bool, bool);
uint32_t CWorld__ProcessLineOfSight_hook(CVector* vecOrigin, CVector* vecEnd, CColPoint *colPoint, CEntityGTA** ppEntity,
										bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7, bool b8)
{
    uintptr_t dwRetAddr = 0;
    GET_LR(dwRetAddr);

    if(dwRetAddr == 0x70253C || g_bForceWorldProcessLineOfSight)
    {
        g_bForceWorldProcessLineOfSight = false;
		//LOGI("CWorld_ProcessLineOfSight iLagCompensationMode: %d", g_iLagCompensationMode);
        static CVector vecPosPlusOffset;

		if (g_iLagCompensationMode != 2)
		{
			if (g_pCurrentFiredPed != pGame->FindPlayerPed())
			{
				if (g_pCurrentBulletData && g_pCurrentBulletData->pEntity)
				{
					if (*(uintptr_t*)(g_pCurrentBulletData->pEntity) != g_libGTASA+0x8300A0) // CPlaceable
					{
                        if (g_iLagCompensationMode)
                        {
                            vecPosPlusOffset.x = g_pCurrentBulletData->pEntity->GetPosition().x + g_pCurrentBulletData->vecOffset.x;
                            vecPosPlusOffset.y = g_pCurrentBulletData->pEntity->GetPosition().y + g_pCurrentBulletData->vecOffset.y;
                            vecPosPlusOffset.z = g_pCurrentBulletData->pEntity->GetPosition().z + g_pCurrentBulletData->vecOffset.z;
                        }
                        else
                        {
                            //FLog("vecPosPlusOffset %f %f %f", vecPosPlusOffset.x, vecPosPlusOffset.y, vecPosPlusOffset.z);
                            //FLog("pEntity->GetMatrix().m_up %f %f %f", g_pCurrentBulletData->pEntity->GetMatrix().m_up.x, g_pCurrentBulletData->pEntity->GetMatrix().m_up.y, g_pCurrentBulletData->pEntity->GetMatrix().m_up.z);
                            //FLog("g_pCurrentBulletData->vecOffset %f %f %f", g_pCurrentBulletData->vecOffset.x, g_pCurrentBulletData->vecOffset.y, g_pCurrentBulletData->vecOffset.z);
                            ProjectMatrix((CVector*)&vecPosPlusOffset, &g_pCurrentBulletData->pEntity->GetMatrix(), &g_pCurrentBulletData->vecOffset);
                            //vecPosPlusOffset.x = pEntity->GetMatrix().m_up.x * g_pCurrentBulletData->vecOffset.z + pEntity->GetMatrix().m_forward.x * g_pCurrentBulletData->vecOffset.y + pEntity->GetMatrix().m_right.x * g_pCurrentBulletData->vecOffset.x + pEntity->GetMatrix().m_pos.x;
                            //vecPosPlusOffset.y = pEntity->GetMatrix().m_up.y * g_pCurrentBulletData->vecOffset.z + pEntity->GetMatrix().m_forward.y * g_pCurrentBulletData->vecOffset.y + pEntity->GetMatrix().m_right.y * g_pCurrentBulletData->vecOffset.x + pEntity->GetMatrix().m_pos.y;
                            //vecPosPlusOffset.z = pEntity->GetMatrix().m_up.z * g_pCurrentBulletData->vecOffset.z + pEntity->GetMatrix().m_forward.z * g_pCurrentBulletData->vecOffset.y + pEntity->GetMatrix().m_right.z * g_pCurrentBulletData->vecOffset.x + pEntity->GetMatrix().m_pos.z;
                        }

                        vecEnd->x = vecPosPlusOffset.x - vecOrigin->x + vecPosPlusOffset.x;
                        vecEnd->y = vecPosPlusOffset.y - vecOrigin->y + vecPosPlusOffset.y;
                        vecEnd->z = vecPosPlusOffset.z - vecOrigin->z + vecPosPlusOffset.z;
					}
				}
			}
		}

		uint32_t result = CWorld__ProcessLineOfSight(vecOrigin, vecEnd, colPoint, ppEntity, b1, b2, b3, b4, b5, b6, b7, b8);

		if (g_iLagCompensationMode == 2)
		{
			if (g_pCurrentFiredPed == pGame->FindPlayerPed()) {
				SendBulletSync(vecOrigin, vecEnd, colPoint, ppEntity);
			}
			return result;
		}

		if (g_pCurrentFiredPed)
		{
			if (g_pCurrentFiredPed != pGame->FindPlayerPed())
			{
				if (g_pCurrentBulletData)
				{
					if (g_pCurrentBulletData->pEntity == nullptr)
					{
                        CPedGTA* pLocalPed = GamePool_FindPlayerPed();
						if (*ppEntity == GamePool_FindPlayerPed() ||
                                pLocalPed->IsInVehicle() && *ppEntity == pLocalPed->pVehicle)
						{
							result = 0;
							*ppEntity = nullptr;
                            colPoint->m_vecPoint.x = 0.0f;
                            colPoint->m_vecPoint.y = 0.0f;
                            colPoint->m_vecPoint.z = 0.0f;
							return result;
						}
					}
				}
			}
			else {
				SendBulletSync(vecOrigin, vecEnd, colPoint, ppEntity);
			}
		}

		return result;
	}

	return CWorld__ProcessLineOfSight(vecOrigin, vecEnd, colPoint, ppEntity, b1, b2, b3, b4, b5, b6, b7, b8);
}
// 0.3.7
uint32_t(*CWeapon__FireSniper)(CWeapon* thiz, CPedGTA* pFiringEntity, CEntityGTA* victim, CVector* target);
uint32_t CWeapon__FireSniper_hook(CWeapon* thiz, CPedGTA* pFiringEntity, CEntityGTA* victim, CVector* target)
{
	if (pFiringEntity == GamePool_FindPlayerPed())
	{
		if (pGame)
		{
			CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
			if (pPlayerPed) {
				pPlayerPed->FireInstant();
			}
		}
	}

	return true;
}
// 0.3.7
bool(*CBulletInfo_AddBullet)(CEntityGTA* creator, int weaponType, CVector pos, CVector velocity);
bool CBulletInfo_AddBullet_hook(CEntityGTA* creator, int weaponType, CVector pos, CVector velocity)
{
	velocity.x *= 50.0f;
	velocity.y *= 50.0f;
	velocity.z *= 50.0f;

	CBulletInfo_AddBullet(creator, weaponType, pos, velocity);

	// CBulletInfo::Update
	CHook::CallFunction<void>("_ZN11CBulletInfo6UpdateEv");
	return true;
}

#pragma pack(push, 1)
struct CPedDamageResponseCalculator
{
    CPedGTA* m_pDamager;
	float m_fDamageFactor;
	int m_pedPieceType;
	int m_weaponType;
};
#pragma pack(pop)
// 0.3.7
bool ComputeDamageResponse(CPedDamageResponseCalculator* calculator, CPedGTA* pPed)
{
    CPedGTA* pGamePed = GamePool_FindPlayerPed();
	bool isLocalPed = false;

	if (!pNetGame) return false;

    CPedGTA* pDamager = calculator->m_pDamager;
	if (pDamager != pGamePed && IsValidGamePed(pGamePed)) /* CCivilianPed */
		return true;

	if (pPed == pGamePed) {
		isLocalPed = true;
	}
	else if (pDamager != pGamePed) {
		return false;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	PLAYERID PlayerID;

	if (isLocalPed)
	{
		PlayerID = FindPlayerIDFromGtaPtr(pDamager);
		pLocalPlayer->SendTakeDamageEvent(PlayerID,
										  calculator->m_fDamageFactor,
										  calculator->m_weaponType,
										  calculator->m_pedPieceType);
	}
	else
	{
		PlayerID = FindPlayerIDFromGtaPtr(pPed);
		if (PlayerID != INVALID_PLAYER_ID)
		{
			pLocalPlayer->SendGiveDamageEvent(PlayerID,
											  calculator->m_fDamageFactor,
											  calculator->m_weaponType,
											  calculator->m_pedPieceType);
			if (pPlayerPool->GetAt(PlayerID)->IsNPC())
				return true;
		}
		else
		{
			PLAYERID ActorID = FindActorIDFromGtaPtr(pPed);
			if (ActorID != INVALID_PLAYER_ID) {
				pLocalPlayer->SendGiveDamageEvent(ActorID,
												  calculator->m_fDamageFactor,
												  calculator->m_weaponType,
												  calculator->m_pedPieceType);
				return true;
			}
		}
	}


	// :check_friendly_fire
	if (!pNetGame->m_pNetSet->bFriendlyFire)
		return false;
	uint8_t byteTeam = pPlayerPool->GetLocalPlayer()->m_byteTeam;
	if (byteTeam == NO_TEAM ||
		PlayerID == INVALID_PLAYER_ID ||
		pPlayerPool->GetAt(PlayerID)->m_byteTeam != byteTeam) {
		return false;
	}

	return true;
}

// 0.3.7
void (*CPedDamageResponseCalculator__ComputeDamageResponse)(CPedDamageResponseCalculator* thiz, CPedGTA* pPed, uintptr_t* a3, uint32_t a4);
void CPedDamageResponseCalculator__ComputeDamageResponse_hook(CPedDamageResponseCalculator* thiz, CPedGTA* pPed, uintptr_t *a3, uint32_t a4)
{
	if (thiz == nullptr || pPed == nullptr || a3 == nullptr) return;

    if (ComputeDamageResponse(thiz, pPed))
        return;

	CPedDamageResponseCalculator__ComputeDamageResponse(thiz, pPed, a3, a4);
}

void (*CRenderer_RenderEverythingBarRoads)();
void CRenderer_RenderEverythingBarRoads_hook() {

	CRenderer_RenderEverythingBarRoads();

	if (pNetGame) {
		CObjectPool* pObjectPool = pNetGame->GetObjectPool();
		if (pObjectPool) {
			for (OBJECTID i = 0; i < MAX_OBJECTS; i++) {
				CObject* pObject = pObjectPool->GetAt(i);
				if (pObject && pObject->m_bForceRender) {
                    // CEntity::PreRender
                    ((void (*)(CEntityGTA*))(*(void**)(pObject->m_pEntity + 0x48*2)))(pObject->m_pEntity);

                    // CRenderer::RenderOneNonRoad
                    ((void (*)(CEntityGTA*))(g_libGTASA+ 0x4F56E0))(pObject->m_pEntity);
				}
			}
		}
	}
}

#include "CFPSFix.h"
#include "ES2VertexBuffer.h"
#include "RQ_Commands.h"
#include "Pickups.h"
#include "TimeCycle.h"
#include "game/Pipelines/CustomCar/CustomCarEnvMapPipeline.h"
#include "game/Pipelines/CustomBuilding/CustomBuildingDNPipeline.h"
#include "COcclusion.h"
#include "RealTimeShadowManager.h"
#include "game/Widgets/WidgetGta.h"

CFPSFix g_fps;

void (*ANDRunThread)(void* a1);
void ANDRunThread_hook(void* a1)
{
	g_fps.PushThread(gettid());

	ANDRunThread(a1);
}

static constexpr float ar43 = 4.0f/3.0f;
float *ms_fAspectRatio;
void (*DrawCrosshair)(uintptr_t* thiz);
void DrawCrosshair_hook(uintptr_t* thiz)
{
	float save1 = CCamera::m_f3rdPersonCHairMultX;
	CCamera::m_f3rdPersonCHairMultX = 0.530f - (*ms_fAspectRatio - ar43) * 0.01125f;

	float save2 = CCamera::m_f3rdPersonCHairMultY;
	CCamera::m_f3rdPersonCHairMultY = 0.400f + (*ms_fAspectRatio - ar43) * 0.03600f;

	DrawCrosshair(thiz);

	CCamera::m_f3rdPersonCHairMultX = save1;
	CCamera::m_f3rdPersonCHairMultY = save2;
}

CVector& (*FindPlayerSpeed)(int a1);
CVector& FindPlayerSpeed_hook(int a1)
{
	uintptr_t dwRetAddr = 0;
	__asm__ volatile ("mov %0, lr":"=r" (dwRetAddr));
	dwRetAddr -= g_libGTASA;

	if(dwRetAddr == 0x43E1F6 + 1)
	{
		if(pNetGame)
		{
			CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
			if(pPlayerPed &&
			   pPlayerPed->IsInVehicle() &&
			   pPlayerPed->IsAPassenger())
			{
				CVector vec = CVector(-1.0f);
				return vec;
			}
		}
	}

	return FindPlayerSpeed(a1);
}

int (*RwFrameAddChild)(int a1, int a2);
int RwFrameAddChild_hook(int a1, int a2)
{
	if(a1 == 0 || a2 == 0) return 0;
	return RwFrameAddChild(a1, a2);
}

int iLastTouchedWidgetId = -1;

int iLastReleasedWidgetId = -1;

int (*CTouchInterface__IsReleased)(int iWidgetId, int iUnk, int iEnableWidget);
int CTouchInterface__IsReleased_hook(int iWidgetId, int iUnk, int iEnableWidget)
{
	uintptr_t dwRetAddr = 0;
	__asm__ volatile ("mov %0, lr" : "=r" (dwRetAddr));
	dwRetAddr -= g_libGTASA;

	int iReleased = CTouchInterface__IsReleased(iWidgetId, iUnk, iEnableWidget);
	if(iReleased && iEnableWidget)
	{
		iLastReleasedWidgetId = iWidgetId;
	}

	return iReleased;
}

int (*CTextureDatabaseRuntime__GetEntry)(uintptr_t thiz, const char* a2, bool* a3);
int CTextureDatabaseRuntime__GetEntry_hook(uintptr_t thiz, const char* a2, bool* a3)
{
	if (!thiz)
	{
		return -1;
	}
	return CTextureDatabaseRuntime__GetEntry(thiz, a2, a3);
}

uintptr_t (*CTxdStore__TxdStoreFindCB)(const char *a1);
uintptr_t CTxdStore__TxdStoreFindCB_hook(const char *a1)
{
	static char* texdbs[] = { "samp", "gta_int", "gta3" };
	for(auto &texdb : texdbs)
	{
		// TextureDatabaseRuntime::GetDatabase
		uintptr_t db_handle = ((uintptr_t (*)(const char *))(g_libGTASA+0x1EAC8C+1))(texdb);

		// TextureDatabaseRuntime::registered
		uint32_t unk_61B8D4 = *(uint32_t*)(g_libGTASA+0x6BD174+4);
		if(unk_61B8D4)
		{
			// TextureDatabaseRuntime::registered
			uintptr_t dword_61B8D8 = *(uintptr_t*)(g_libGTASA+0x6BD174+8);

			int index = 0;
			while(*(uint32_t*)(dword_61B8D8 + 4 * index) != db_handle)
			{
				if(++index >= unk_61B8D4)
					goto GetTheTexture;
			}

			continue;
		}

		GetTheTexture:
		// TextureDatabaseRuntime::Register
		((void (*)(int))(g_libGTASA+0x1E9BC8+1))(db_handle);

		// TextureDatabaseRuntime::GetTexture
		uintptr_t tex = ((uintptr_t (*)(const char *))(g_libGTASA+0x1E9C64+1))(a1);

		// TextureDatabaseRuntime::Unregister
		((void (*)(int))(g_libGTASA+0x1E9C80+1))(db_handle);

		if(tex) return tex;
	}

	// RwTexDictionaryGetCurrent
	int current = ((int (*)(void))(g_libGTASA+0x1DBA64+1))();
	if(current)
	{
		while(true)
		{
			// RwTexDictionaryFindNamedTexture
			uintptr_t tex = ((int (*)(int, const char *))(g_libGTASA+0x1DB9B0+1))(current, a1);
			if(tex) return tex;

			// CTxdStore::GetTxdParent
			current = ((int (*)(int))(g_libGTASA+0x5D428C+1))(current);
			if(!current) return 0;
		}
	}

	return 0;
}

int (*CCustomRoadsignMgr_RenderRoadsignAtomic)(int a1, int a2);
int CCustomRoadsignMgr_RenderRoadsignAtomic_hook(int a1, int a2)
{
	if ( a1 )
		return CCustomRoadsignMgr_RenderRoadsignAtomic(a1, a2);
}

int (*_RwTextureDestroy)(int a1);
int _RwTextureDestroy_hook(int a1)
{
	int result; // r0

	if ( (unsigned int)(a1 + 1) >= 2 )
		result = _RwTextureDestroy(a1);
	else
		result = 0;
	return result;
}

int (*CPed_UpdatePosition)(CPedGTA* a1);
int CPed_UpdatePosition_hook(CPedGTA* a1)
{
	int result; // r0

	if ( GamePool_FindPlayerPed() == a1 )
		result = CPed_UpdatePosition(a1);
	return result;
}

void (*CCamera__Process)(uintptr_t thiz);
void CCamera__Process_hook(uintptr_t thiz)
{
	//if(pGame->GetCamera())
		//pGame->GetCamera()->Update();

	CCamera__Process(thiz);
}

extern CJavaWrapper* pJavaWrapper;
void (*MainMenuScreen__OnExit)();
void MainMenuScreen__OnExit_hook()
{
	pGame->bIsGameExiting = true;

	pNetGame->GetRakClient()->Disconnect(0);

	pJavaWrapper->exitGame();
}

void (*rqVertexBufferSelect)(unsigned int **result);
void rqVertexBufferSelect_hook(unsigned int **result)
{
	uint32_t buffer = *(uint32_t *)*result;
	*result += 4;
	if ( buffer )
	{
		glBindBuffer(34962, *(uint32_t *)(buffer + 8));
		*(uint32_t*)(g_libGTASA + 0x6B8AF0) = 0;
	}
	else
	{
		glBindBuffer(34962, 0);
	}
}

uintptr_t* (*rpMaterialListDeinitialize)(RpMaterialList* matList);
uintptr_t* rpMaterialListDeinitialize_hook(RpMaterialList* matList)
{
	if(!matList || !matList->materials)
		return nullptr;

	return rpMaterialListDeinitialize(matList);
}

void (*rqVertexBufferDelete)(unsigned int **result);
void rqVertexBufferDelete_hook(unsigned int **result)
{
	uint32_t* buffer = *(uint32_t **)*result;
	*result += 4;
	glDeleteBuffers(1, reinterpret_cast<const GLuint *>(buffer + 2));
	buffer[2] = 0;
	if ( buffer )
		(*(void (**)(uint32_t *))(*buffer + 4))(buffer);
}

void rotate_ped_if_local(unsigned int *a1, unsigned int *a2)
{
	if ( GamePool_FindPlayerPed() == (CPedGTA*)a2 )
		*(uint32_t *)(a2 + 0x560) = *a1;
}

void (*player_control_zelda)(unsigned int *a2, unsigned int *a3);
void player_control_zelda_hook(unsigned int *a2, unsigned int *a3)
{
	rotate_ped_if_local(a2, a3);
}

// 006778B0
int (*rxOpenGLDefaultAllInOneRenderCB)(RwResEntry* resEntry, uintptr_t object, uint8_t type, uint32_t flags);
int rxOpenGLDefaultAllInOneRenderCB_hook(RwResEntry* resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
	if(!resEntry || !flags)
		return 0;

	return rxOpenGLDefaultAllInOneRenderCB(resEntry, object, type, flags);
}

// 00677CB4
int (*CCustomBuildingDNPipeline__CustomPipeRenderCB)(RwResEntry* resEntry, uintptr_t object, uint8_t type, uint32_t flags);
int CCustomBuildingDNPipeline__CustomPipeRenderCB_hook(RwResEntry* resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
    if(!resEntry || !flags)
        return 0;

	return CCustomBuildingDNPipeline__CustomPipeRenderCB(resEntry, object, type, flags);
}

int (*EmuShader_Select)(uintptr_t *result);
int EmuShader_Select_hook(uintptr_t *result)
{
	int result1;
	if ( *result >= 0x1000 )
		return EmuShader_Select(result);
	return 0;
}

float float_4DD9E8;
float ms_fTimeStep;
float fMagic = 50.0f / 30.0f;
void (*CTaskSimpleUseGun__SetMoveAnim)(uintptr_t *thiz, uintptr_t *a2);
void CTaskSimpleUseGun__SetMoveAnim_hook(uintptr_t *thiz, uintptr_t *a2)
{
	ms_fTimeStep = *(float*)(g_libGTASA + 0x96B500);
	float_4DD9E8 = *(float*)(g_libGTASA + 0x4DD9E8);
	float_4DD9E8 = (fMagic) * (0.1f / ms_fTimeStep);
	CTaskSimpleUseGun__SetMoveAnim(thiz, a2);
}

int (*CAnimManager_UncompressAnimation)(int result);
int CAnimManager_UncompressAnimation_hook(int result)
{
	if ( result )
		return CAnimManager_UncompressAnimation(result);
	return 0;
}

void readVehiclesAudioSettings();

void (*CVehicleModelInfo__SetupCommonData)();

void CVehicleModelInfo__SetupCommonData_hook() {
	CVehicleModelInfo__SetupCommonData();
	readVehiclesAudioSettings();
}

extern VehicleAudioPropertiesStruct VehicleAudioProperties[20000];
static uintptr_t addr_veh_audio = (uintptr_t) &VehicleAudioProperties[0];

void (*CAEVehicleAudioEntity__GetVehicleAudioSettings)(uintptr_t thiz, int16_t a2, int a3);

void CAEVehicleAudioEntity__GetVehicleAudioSettings_hook(uintptr_t dest, int16_t a2, int ID) {
	memcpy((void *) dest, &VehicleAudioProperties[(ID - 400)], sizeof(VehicleAudioPropertiesStruct));
}

void (*CRadar_ClearBlip)(uint32_t a2);
void CRadar_ClearBlip_hook(uint32_t a2)
{
	uintptr_t dwRetAddr = 0;
	GET_LR(dwRetAddr);

	//LOGI("[CRadar::ClearBlip]: %d called from 0x%X", (uint16_t)a2, dwRetAddr);

	if ( (uint16_t)a2 > 249 )
	{
		LOGI("[CRadar::ClearBlip]: Invalid blip ID (%d) called from 0x%X", (uint16_t)a2, dwRetAddr);
	}
	else
	{
		CRadar_ClearBlip(a2);
	}
}

/* =============================================================================== */

void InstallHuaweiCrashFixHooks()
{
	CHook::InstallPLT(g_libGTASA + 0x677498, (uintptr_t)rqVertexBufferSelect_hook, (uintptr_t*)&rqVertexBufferSelect);
	CHook::InstallPLT(g_libGTASA + 0x679B14, (uintptr_t)rqVertexBufferDelete_hook, (uintptr_t*)&rqVertexBufferDelete);
	//CHook::InstallPLT(g_libGTASA + 0x677B6C, (uintptr_t)rqSetAlphaTest_hook, (uintptr_t*)&rqSetAlphaTest);
}

void InstallCrashFixHooks()
{
	// some crashfixes
	CHook::InstallPLT(g_libGTASA + 0x66F5AC, (uintptr_t)CCustomRoadsignMgr_RenderRoadsignAtomic_hook, (uintptr_t*)&CCustomRoadsignMgr_RenderRoadsignAtomic);
	CHook::InstallPLT(g_libGTASA + 0x67332C, (uintptr_t)_RwTextureDestroy_hook, (uintptr_t*)&_RwTextureDestroy);
	CHook::InstallPLT(g_libGTASA + 0x671458, (uintptr_t)CPed_UpdatePosition_hook, (uintptr_t*)&CPed_UpdatePosition);
	CHook::InstallPLT(g_libGTASA + 0x675490, (uintptr_t)RwFrameAddChild_hook, (uintptr_t*)&RwFrameAddChild);
	CHook::InstallPLT(g_libGTASA + 0x672D14, (uintptr_t)CTextureDatabaseRuntime__GetEntry_hook, (uintptr_t*)&CTextureDatabaseRuntime__GetEntry);
	//CHook::InstallPLT(g_libGTASA + 0x66FBD0, (uintptr_t)RpClumpForAllAtomics_hook, (uintptr_t*)&RpClumpForAllAtomics);
	CHook::InstallPLT(g_libGTASA + 0x6730F0, (uintptr_t)rpMaterialListDeinitialize_hook, (uintptr_t*)&rpMaterialListDeinitialize);
	//CHook::InstallPLT(g_libGTASA + 0x6778B0, (uintptr_t)rxOpenGLDefaultAllInOneRenderCB_hook, (uintptr_t*)&rxOpenGLDefaultAllInOneRenderCB);
	//CHook::InstallPLT(g_libGTASA + 0x677CB4, (uintptr_t)CCustomBuildingDNPipeline__CustomPipeRenderCB_hook, (uintptr_t*)&CCustomBuildingDNPipeline__CustomPipeRenderCB);
	//CHook::InstallPLT(g_libGTASA + 0x66F9E8, (uintptr_t)EmuShader_Select_hook, (uintptr_t*)&EmuShader_Select);
	CHook::InstallPLT(g_libGTASA + 0x6750D4, (uintptr_t)CAnimManager_UncompressAnimation_hook, (uintptr_t*)&CAnimManager_UncompressAnimation);
	//CHook::InstallPLT(g_libGTASA + 0x670E1C, (uintptr_t)CStreaming__MakeSpaceFor_hook, (uintptr_t*)&CStreaming__MakeSpaceFor);
}

void InstallWeaponFireHooks()
{
	//CHook::InstallPLT(g_libGTASA + 0x6716D0, (uintptr_t)CWeapon_FireInstantHit_hook, (uintptr_t*)&CWeapon_FireInstantHit);
	//CHook::InstallPLT(g_libGTASA + 0x671F10, (uintptr_t)CWorld_ProcessLineOfSight_hook, (uintptr_t*)&CWorld_ProcessLineOfSight);
	//CHook::InstallPLT(g_libGTASA + 0x670A10, (uintptr_t)CWeapon_FireSniper_hook, (uintptr_t*)&CWeapon_FireSniper);
	//CHook::InstallPLT(g_libGTASA + 0x66EAC4, (uintptr_t)CBulletInfo_AddBullet_hook, (uintptr_t*)&CBulletInfo_AddBullet);
}

void InstallSAMPHooks()
{
	//CHook::InstallPLT(g_libGTASA + 0x677EA0, (uintptr_t)MainMenuScreen__OnExit_hook, (uintptr_t*)&MainMenuScreen__OnExit);
	// samp main loop
	//CHook::InstallPLT(g_libGTASA + 0x67589C, (uintptr_t)Render2dStuff_hook, (uintptr_t*)&Render2dStuff);
	// imgui
	//CHook::InstallPLT(g_libGTASA + 0x6710C4, (uintptr_t)Idle_hook, (uintptr_t*)&Idle);
	//CHook::InstallPLT(g_libGTASA + 0x675DE4, (uintptr_t)AND_TouchEvent_hook, (uintptr_t*)&AND_TouchEvent);
	// splashscreen
	//ARMHook::installHook(g_libGTASA + 0x43AF28, (uintptr_t)DisplayScreen_hook, (uintptr_t*)&DisplayScreen);
	// gangzones
	//CHook::InstallPLT(g_libGTASA + 0x67196C, (uintptr_t)CRadar_DrawRadarGangOverlay_hook, (uintptr_t*)&CRadar_DrawRadarGangOverlay);
	// radar
	//CHook::InstallPLT(g_libGTASA+0x675914, (uintptr_t)CRadar__SetCoordBlip_hook, (uintptr_t*)&CRadar__SetCoordBlip);
	// removebuilding
	//CHook::InstallPLT(g_libGTASA + 0x675E6C, (uintptr_t)CFileLoader__LoadObjectInstance_hook, (uintptr_t*)&CFileLoader__LoadObjectInstance);
	// obj material
	//ARMHook::installHook(g_libGTASA + 0x454EF0, (uintptr_t)CObject_Render_hook, (uintptr_t*)& CObject_Render);
	// textdraw models
	//CHook::InstallPLT(g_libGTASA + 0x66FE58, (uintptr_t)CGame_Process_hook, (uintptr_t*)& CGame_Process);
	// enter vehicle as driver
	//ARMHook::codeInject(g_libGTASA + 0x40AC28, (uintptr_t)TaskEnterVehicle_hook, 0);
    //CHook::InstallPLT(g_libGTASA+0x6733F0, (uintptr_t)TaskEnterVehicle_hook, (uintptr_t*)&TaskEnterVehicle);
	// radar color
	//CHook::InstallPLT(g_libGTASA + 0x673950, (uintptr_t)CHudColours__GetIntColour_hook, (uintptr_t*)& CHudColours__GetIntColour);
	// exit vehicle
	CHook::InstallPLT(g_libGTASA + 0x671984, (uintptr_t)CTaskComplexLeaveCar_hook, (uintptr_t*)& CTaskComplexLeaveCar);
    CHook::InstallPLT(g_libGTASA + 0x675320, (uintptr_t)CTaskComplexLeaveCar_hook, (uintptr_t*)& CTaskComplexLeaveCar);
    // attach obj to ped
	//CHook::InstallPLT(g_libGTASA + 0x675C68, (uintptr_t)CWorld_ProcessPedsAfterPreRender_Hook, (uintptr_t*)&CWorld_ProcessPedsAfterPreRender);
	// game pause
	//CHook::InstallPLT(g_libGTASA + 0x672644, (uintptr_t)CTimer_StartUserPause_hook, (uintptr_t*)&CTimer_StartUserPause);
	//CHook::InstallPLT(g_libGTASA + 0x67056C, (uintptr_t)CTimer_EndUserPause_hook, (uintptr_t*)&CTimer_EndUserPause);
	// aim
	// Crosshair Fix
	//ms_fAspectRatio = (float*)(g_libGTASA+0xCC7F00);
	//CHook::InstallPLT(g_libGTASA + 0x672880, (uintptr_t)DrawCrosshair_hook, (uintptr_t*)&DrawCrosshair);

	// fix radar in passenger
	CHook::InstallPLT(g_libGTASA+0x671BBC, (uintptr_t)FindPlayerSpeed_hook, (uintptr_t*)&FindPlayerSpeed);

	// fix texture loading
	CHook::InstallPLT(g_libGTASA + 0x676034, (uintptr_t)CTxdStore__TxdStoreFindCB_hook, (uintptr_t*)&CTxdStore__TxdStoreFindCB);

	// interpolate camera fix
	CHook::InstallPLT(g_libGTASA + 0x6717BC, (uintptr_t)CCamera__Process_hook, (uintptr_t*)&CCamera__Process);

	// for surfing
	//CHook::InstallPLT(g_libGTASA + 0x66EAE8, (uintptr_t)CWorld_ProcessAttachedEntities_Hook, (uintptr_t*)&CWorld_ProcessAttachedEntities);

	//CHook::InstallPLT(g_libGTASA + 0x67193C, (uintptr_t)player_control_zelda_hook, (uintptr_t*)&player_control_zelda);

	//ARMHook::installHook(g_libGTASA + 0x4DD5E8, (uintptr_t)CTaskSimpleUseGun__SetMoveAnim_hook, (uintptr_t*)&CTaskSimpleUseGun__SetMoveAnim);

    // hueta ne rabotaet no pust budet (tipo ne kak v 1.08)
	CHook::InstallPLT(g_libGTASA + 0x674280, (uintptr_t) CVehicleModelInfo__SetupCommonData_hook, (uintptr_t*)&CVehicleModelInfo__SetupCommonData);
	CHook::InstallPLT(g_libGTASA + 0x06D008, (uintptr_t) CAEVehicleAudioEntity__GetVehicleAudioSettings_hook, (uintptr_t*)&CAEVehicleAudioEntity__GetVehicleAudioSettings);

	CHook::InstallPLT(g_libGTASA + 0x66FF0C, (uintptr_t)CRadar_ClearBlip_hook, (uintptr_t*)&CRadar_ClearBlip);

	// skills
	CHook::InstallPLT(g_libGTASA + 0x6749D0, (uintptr_t)CPed__GetWeaponSkill_hook, (uintptr_t*)&CPed__GetWeaponSkill);

    //InstallHuaweiCrashFixHooks();
	InstallCrashFixHooks();
	InstallWeaponFireHooks();
	HookCPad();
}

void ReadSettingFile();
void ApplyFPSPatch(uint8_t fps);
void (*NvUtilInit)();
void NvUtilInit_hook()
{
    FLog("NvUtilInit");

    NvUtilInit();

    g_pszStorage = (char*)(g_libGTASA + 0x8B46A8); // StorageRootBuffer

    ReadSettingFile();

    ApplyFPSPatch(120);
}

struct stFile
{
    int isFileExist;
    FILE *f;
};

char lastFile[123];

stFile* NvFOpen(const char* r0, const char* r1, int r2, int r3)
{
    strcpy(lastFile, r1);

    static char path[255]{};
    memset(path, 0, sizeof(path));

    sprintf(path, "%s%s", g_pszStorage, r1);

    // ----------------------------
    if(!strncmp(r1+12, "mainV1.scm", 10))
    {
        sprintf(path, "%sSAMP/main.scm", g_pszStorage);
        FLog("Loading %s", path);
    }
    // ----------------------------
    if(!strncmp(r1+12, "SCRIPTV1.IMG", 12))
    {
        sprintf(path, "%sSAMP/script.img", g_pszStorage);
        FLog("Loading script.img..");
    }
    // ----------------------------
    if(!strncmp(r1, "DATA/PEDS.IDE", 13))
    {
        sprintf(path, "%sSAMP/peds.ide", g_pszStorage);
        FLog("Loading peds.ide..");
    }
    // ----------------------------
    if(!strncmp(r1, "DATA/VEHICLES.IDE", 17))
    {
        sprintf(path, "%sSAMP/vehicles.ide", g_pszStorage);
        FLog("Loading vehicles.ide..");
    }

    if (!strncmp(r1, "DATA/GTA.DAT", 12))
    {
        sprintf(path, "%sSAMP/gta.dat", g_pszStorage);
        FLog("Loading gta.dat..");
    }

    if (!strncmp(r1, "DATA/HANDLING.CFG", 17))
    {
        sprintf(path, "%sSAMP/handling.cfg", g_pszStorage);
        FLog("Loading handling.cfg..");
    }

    if (!strncmp(r1, "DATA/WEAPON.DAT", 15))
    {
        sprintf(path, "%sSAMP/weapon.dat", g_pszStorage);
        FLog("Loading weapon.dat..");
    }

    if (!strncmp(r1, "DATA/FONTS.DAT", 15))
    {
        sprintf(path, "%sdata/fonts.dat", g_pszStorage);
        FLog("Loading weapon.dat..");
    }

    if (!strncmp(r1, "DATA/PEDSTATS.DAT", 15))
    {
        sprintf(path, "%sdata/pedstats.dat", g_pszStorage);
        FLog("Loading weapon.dat..");
    }

    if (!strncmp(r1, "DATA/TIMECYC.DAT", 15))
    {
        sprintf(path, "%sdata/timecyc.dat", g_pszStorage);
        FLog("Loading weapon.dat..");
    }

    if (!strncmp(r1, "DATA/POPCYCLE.DAT", 15))
    {
        sprintf(path, "%sdata/popcycle.dat", g_pszStorage);
        FLog("Loading weapon.dat..");
    }

    auto *st = (stFile*)malloc(0x10);

    st->isFileExist = false;

    FILE *f  = fopen(path, "rb");

    if(f)
    {
        st->isFileExist = true;
        st->f = f;
        return st;
    }
    else
    {
//        FLog("NVFOpen hook | Error: file not found (%s)", path);
        free(st);
        return nullptr;
    }
}

bool g_bPlaySAMP = false;

void MainMenu_OnStartSAMP()
{
    if(g_bPlaySAMP) return;

    //InitInMenu();
    pGame->StartGame();

    // StartGameScreen::OnNewGameCheck()
    (( void (*)())(g_libGTASA + 0x365EA0))();

    g_bPlaySAMP = true;
}

unsigned int (*MainMenuScreen__Update)(uintptr_t thiz, float a2);
unsigned int MainMenuScreen__Update_hook(uintptr_t thiz, float a2)
{
    unsigned int ret = MainMenuScreen__Update(thiz, a2);
    MainMenu_OnStartSAMP();
    return ret;
}

void (*StartGameScreen__OnNewGameCheck)();
void StartGameScreen__OnNewGameCheck_hook()
{
    // отключить кнопку начать игру
    if(g_bPlaySAMP)
        return;

    StartGameScreen__OnNewGameCheck();
}

void (*CTaskSimpleUseGun__RemoveStanceAnims)(uintptr* thiz, void* ped, float a3);
void CTaskSimpleUseGun__RemoveStanceAnims_hook(uintptr* thiz, void* ped, float a3)
{
    if(!thiz)
        return;

    uintptr* m_pAnim = (uintptr*)(thiz + 0x2c);
    if(m_pAnim) {
        if (!((uintptr *)(m_pAnim + 0x14)))
            return;
    }
    CTaskSimpleUseGun__RemoveStanceAnims(thiz, ped, a3);
}

int (*CCollision__ProcessVerticalLine)(float *a1, float *a2, int a3, int a4, int *a5, int a6, int a7, int a8);
int CCollision__ProcessVerticalLine_hook(float *a1, float *a2, int a3, int a4, int *a5, int a6, int a7, int a8)
{
    int result; // r0

    if (a3)
        result = CCollision__ProcessVerticalLine(a1, a2, a3, a4, a5, a6, a7, a8);
    else
        result = 0;
    return result;
}

int(*CUpsideDownCarCheck__IsCarUpsideDown)(int, int);
int CUpsideDownCarCheck__IsCarUpsideDown_hook(int a1, int a2)
{
    /* Passengers leave the vehicle out of fear if it overturns */

//	if (*(uintptr_t*)(a2 + 20))
//	{
//		return CUpsideDownCarCheck__IsCarUpsideDown(a1, a2);
//	}
    return 0;
}

int (*CTaskSimpleGetUp__ProcessPed)(uintptr_t* thiz, CPedGTA* ped);
int CTaskSimpleGetUp__ProcessPed_hook(uintptr_t* thiz, CPedGTA* ped)
{
    //return false;
    if(!ped)return 0;
    int res = 0;
    try {
        res = CTaskSimpleGetUp__ProcessPed(thiz, ped);
    }
    catch(...) {
        return 0;
    }

    return res;
}

int64 getmip()
{
    return 1;
}

uint64_t* RQCommand_rqSetAlphaTest(uint64_t *result)
{
    *result += 8;
    return result;
}

int64 GetInputType(void)
{
    return 0LL;
}

int (*CAnimBlendNode__FindKeyFrame)(int64_t, float, int, int);
int CAnimBlendNode__FindKeyFrame_hook(int64_t a1, float a2, int a3, int a4)
{
    if (*(uintptr_t*)(a1 + 16))
    {
        return CAnimBlendNode__FindKeyFrame(a1, a2, a3, a4);
    }
    else return 0;
}

RwFrame* CClumpModelInfo_GetFrameFromId_Post(RwFrame* pFrameResult, RpClump* pClump, int id)
{
    if (pFrameResult)
        return pFrameResult;

    uintptr_t calledFrom = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    calledFrom -= g_libGTASA;

    if (calledFrom == 0x00515708                // CVehicle::SetWindowOpenFlag
        || calledFrom == 0x00515730             // CVehicle::ClearWindowOpenFlag
        || calledFrom == 0x00338698             // CVehicleModelInfo::GetOriginalCompPosition
        || calledFrom == 0x00338B2C)            // CVehicleModelInfo::CreateInstance
    {
        return nullptr;
    }

    for (uint32_t i = 2; i < 40; i++)
    {
        uint32_t uiNewId = id + (i / 2) * ((i & 1) ? -1 : 1);
        RwFrame* pNewFrameResult = ((RwFrame* (*)(RpClump*, int))(g_libGTASA + 0x0045BE40))(pClump, uiNewId);

        if (pNewFrameResult)
        {
            return pNewFrameResult;
        }
    }

    return nullptr;
}

RwFrame* (*CClumpModelInfo_GetFrameFromId)(RpClump*, int);
RwFrame* CClumpModelInfo_GetFrameFromId_hook(RpClump* a1, int a2)
{
    return CClumpModelInfo_GetFrameFromId_Post(CClumpModelInfo_GetFrameFromId(a1, a2), a1, a2);
}

void (*FxEmitterBP_c__Render)(uintptr_t* a1, int a2, int a3, float a4, char a5);
void FxEmitterBP_c__Render_hook(uintptr_t* a1, int a2, int a3, float a4, char a5)
{
    if(!a1 || !a2) return;
    uintptr_t* temp = *((uintptr_t**)a1 + 3);
    if (!temp)
    {
        return;
    }
    FxEmitterBP_c__Render(a1, a2, a3, a4, a5);
}

bool (*RwResourcesFreeResEntry)(void* entry);
bool RwResourcesFreeResEntry_hook(void* entry)
{
    bool result;
    if (entry) result = RwResourcesFreeResEntry(entry);
    else result = false;
    return result;
}

static uint32_t dwRLEDecompressSourceSize = 0;

size_t (*OS_FileRead)(OSFile a1, void *buffer, size_t numBytes);
size_t OS_FileRead_hook(OSFile a1, void *buffer, size_t numBytes)
{
    dwRLEDecompressSourceSize = numBytes;

    return OS_FileRead(a1, buffer, numBytes);
}
void (*RLEDecompress)(
    uint8_t* pDest,
    size_t uiDestSize,
    const uint8_t* pSrc,
    size_t uiSegSize,
    uint32_t uiEscape
) = nullptr;


void RLEDecompress_hook(
    uint8_t* pDest,
    size_t uiDestSize,
    const uint8_t* pSrc,
    size_t uiSegSize,
    uint32_t uiEscape)
{
    uintptr_t lr = 0;

    asm volatile(
        "mov %0, x30"
        : "=r"(lr)
    );

    static int count = 0;
    const int n = ++count;

    /*
     * Validação básica.
     */
    if (!pDest ||
        !pSrc ||
        uiDestSize == 0 ||
        uiSegSize == 0)
    {
        if (n <= 30)
        {
            FLog(
                "[RLED INVALID] #%d "
                "dest=%p src=%p destSize=%zu segSize=%zu escape=0x%X",
                n,
                pDest,
                pSrc,
                uiDestSize,
                uiSegSize,
                uiEscape
            );
        }

        return;
    }


    /*
     * Se a função original existe, usamos a implementação original.
     *
     * Isso evita alterar o comportamento normal do GTA.
     */
    if (RLEDecompress)
    {
        RLEDecompress(
            pDest,
            uiDestSize,
            pSrc,
            uiSegSize,
            uiEscape
        );

        return;
    }


    /*
     * Fallback somente se a função original não estiver disponível.
     *
     * IMPORTANTE:
     * Não usamos mais dwRLEDecompressSourceSize
     * como limite de leitura.
     *
     * Sem conhecer o tamanho real do buffer de origem,
     * não é seguro inventar um pEndOfSrc.
     */
    if (n <= 30)
    {
        FLog(
            "[RLED WARNING] Original RLEDecompress=NULL "
            "caller=0x%lx offset=0x%lx",
            (unsigned long)lr,
            (unsigned long)(lr - g_libGTASA)
        );
    }

    return;
}
void (*CGame_Process)();
void CGame_Process_hook()
{
    if(pGame->bIsGameExiting)return;

    CGame_Process();

    if (pNetGame)
    {
        if(pGame && pGame->FindPlayerPed() && pUI && pUI->buttonpanel() && pUI->buttonpanel()->m_bH)
        {
            if(pGame->FindPlayerPed()->IsInVehicle())
            {
                pUI->buttonpanel()->m_bH->setCaption("D/B");
            }
            else
                pUI->buttonpanel()->m_bH->setCaption("H");
        }

        CObjectPool* pObjectPool = pNetGame->GetObjectPool();
        if (pObjectPool) {
            pObjectPool->Process();
            pObjectPool->ProcessMaterialText();
        }

        CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
        if (pTextDrawPool) {
            pTextDrawPool->SnapshotProcess();
        }
    }
}

// Dedicated CGame::Process hook for clothes only. Do not enable the older
// CGame_Process_hook above: it also processes unrelated objects/textdraws.
// Always call the original first; only then drain a queued clothes request.
static void (*CGame_Process_ClothesDeferredOriginal)() = nullptr;
static void CGame_Process_ClothesDeferred_hook()
{
    if (!CGame_Process_ClothesDeferredOriginal)
    {
        // No safe fallback exists if InstallPLT failed to capture the original.
        // The installer logs this; this path should never be reached normally.
        return;
    }

    CGame_Process_ClothesDeferredOriginal();

    if (pGame && pGame->bIsGameExiting)
        return;

    ProcessPendingPlayerClothesRebuild();
}

float (*CDraw__SetFOV)(float thiz, float a2);
float CDraw__SetFOV_hook(float thiz, float a2)
{
    float tmp = (float)((float)((float)(*(float *)&*(float *)(g_libGTASA + 0xCC7F00) - 1.3333) * 11.0) / 0.44444) + thiz;
    if(tmp > 100) tmp = 100.0;
    *(float *)(g_libGTASA + 0x88E6BC) = tmp;
    return thiz;
}

void(*CStreaming__Init2)();
void CStreaming__Init2_hook()
{
    CStreaming__Init2();
    *(uint32_t*)(g_libGTASA+0x85EBD8) = 536870912;
}

int(*mpg123_param)(void* mh, int key, long val, int ZERO, double fval);
int mpg123_param_hook(void* mh, int key, long val, int ZERO, double fval)
{
    // 0x2000 = MPG123_SKIP_ID3V2
    // 0x200  = MPG123_FUZZY
    // 0x100  = MPG123_SEEKBUFFER
    // 0x40   = MPG123_GAPLESS
    return mpg123_param(mh, key, val | (0x2000 | 0x200 | 0x100 | 0x40), ZERO, fval);
}

#include "Widgets/TouchInterface.h"
void InjectHooks()
{
    FLog("InjectHooks");
    CHook::Write(g_libGTASA + 0x84F2D0, &Scene);

    CHook::RET("_ZN11CPopulation10InitialiseEv");

    CCustomCarEnvMapPipeline::InjectHooks();
    CCamera::InjectHooks(); //
    CReferences::InjectHooks(); //
    CModelInfo::injectHooks(); //
    CTimer::InjectHooks(); //
    //cTransmission::InjectHooks(); //
    CAnimBlendAssociation::InjectHooks(); //
    //cHandlingDataMgr::InjectHooks(); //
    CPools::InjectHooks(); //
    CVehicleGTA::InjectHooks(); //
    CMatrixLink::InjectHooks(); //
    CMatrixLinkList::InjectHooks(); //
    CStreaming::InjectHooks();
    CPlaceable::InjectHooks(); //
    CMatrix::InjectHooks(); //
    CCollision::InjectHooks(); //
    //CIdleCam::InjectHooks(); //
    CTouchInterface::InjectHooks(); //
    CWidgetGta::InjectHooks();
    CEntityGTA::InjectHooks(); //
    CPhysical::InjectHooks(); //
    CAnimManager::InjectHooks(); //
    //CCarEnterExit::InjectHooks();
    CPlayerPedGta::InjectHooks(); //
    CTaskManager::InjectHooks(); //
    //CPedIntelligence::InjectHooks(); //
    CWorld::InjectHooks(); //
    CGame::InjectHooks();
    ES2VertexBuffer::InjectHooks();
    CRQ_Commands::InjectHooks();
    CTxdStore::InjectHooks();
    CVisibilityPlugins::InjectHooks();
    //CAdjustableHUD::InjectHooks();

    // new
    //CClouds::InjectHooks();
    //CWeather::InjectHooks();
    //RenderBuffer::InjectHooks();
    CTimeCycle::InjectHooks();
    CCoronas::InjectHooks();
    //CDraw::InjectHooks();
    //CClock::InjectHooks();
    //CBirds::Init();
    CVehicleModelInfo::InjectHooks();
    //CPathFind::InjectHooks();
    CSprite2d::InjectHooks();
    //CFileLoader::InjectHooks();
    //CShadows::InjectHooks();
    CPickups::InjectHooks();
    CRenderer::InjectHooks();
    CStreamingInfo::InjectHooks();
    TextureDatabase::InjectHooks();
    TextureDatabaseEntry::InjectHooks();
    TextureDatabaseRuntime::InjectHooks();
    CCustomBuildingDNPipeline::InjectHooks();
    //CWidgetRadar::InjectHooks();

    //CRealTimeShadowManager::InjectHooks();
    CHook::Write(g_libGTASA+0xCE3EE8, &COcclusion::aOccluders);
    CHook::Write(g_libGTASA+0xCE8538, &COcclusion::NumOccludersOnMap);
}

void InstallSpecialHooks()
{
    InjectHooks();
    CHook::Redirect("_ZN5CGame20InitialiseRenderWareEv", &CGame::InitialiseRenderWare);
    CHook::InstallPLT(g_libGTASA + 0x84EC20, &StartGameScreen__OnNewGameCheck_hook, &StartGameScreen__OnNewGameCheck);

    CHook::InlineHook("_Z10NvUtilInitv", &NvUtilInit_hook, &NvUtilInit);

    CHook::RET("_ZN12CCutsceneMgr16LoadCutsceneDataEPKc"); // LoadCutsceneData
    CHook::RET("_ZN12CCutsceneMgr10InitialiseEv");			// CCutsceneMgr::Initialise

    CHook::Redirect("_Z7NvFOpenPKcS0_bb", &NvFOpen);

    CHook::InlineHook("_ZN14MainMenuScreen6UpdateEf", &MainMenuScreen__Update_hook, &MainMenuScreen__Update);

    CHook::RET("_ZN4CPed31RemoveWeaponWhenEnteringVehicleEi"); // CPed::RemoveWeaponWhenEnteringVehicle

    CHook::InstallPLT(g_libGTASA + 0x840708, &RLEDecompress_hook, &RLEDecompress);

    CHook::InlineHook("_Z11OS_FileReadPvS_i", &OS_FileRead_hook, &OS_FileRead);

    CHook::InlineHook("_Z32_rxOpenGLDefaultAllInOneRenderCBP10RwResEntryPvhj", &rxOpenGLDefaultAllInOneRenderCB_hook, &rxOpenGLDefaultAllInOneRenderCB);
    CHook::InlineHook("_ZN25CCustomBuildingDNPipeline18CustomPipeRenderCBEP10RwResEntryPvhj", &CCustomBuildingDNPipeline__CustomPipeRenderCB_hook, &CCustomBuildingDNPipeline__CustomPipeRenderCB);
}
//////-------------------///---------------------//--------

static constexpr uintptr_t ADDR_CONSTRUCT_PED_MODEL = 0x541764;

void DebugRebuildStep4(CPlayerPedGta* player)
{
    FLog("[REBUILD DEBUG] ===== STEP 4 =====");

    if (!player)
    {
        FLog("[REBUILD DEBUG] player=NULL");
        return;
    }

    uintptr_t ped =
        reinterpret_cast<uintptr_t>(player);

    uintptr_t playerData =
        *reinterpret_cast<uintptr_t*>(ped + 0x540);

    if (!playerData)
    {
        FLog("[REBUILD DEBUG] playerData=NULL");
        return;
    }

    uintptr_t clothesDesc =
        *reinterpret_cast<uintptr_t*>(playerData + 0x08);

    if (!clothesDesc)
    {
        FLog("[REBUILD DEBUG] clothesDesc=NULL");
        return;
    }
FLog("[CLOTHES DESC] ===== DUMP =====");

for (int i = 0; i < 10; i++)
{
    uint32_t model =
        *reinterpret_cast<uint32_t*>(
            clothesDesc + (i * 4)
        );

    FLog(
        "[CLOTHES DESC] model[%d] = 0x%08X (%u)",
        i,
        model,
        model
    );
}

for (int i = 0; i < 18; i++)
{
    uint32_t texture =
        *reinterpret_cast<uint32_t*>(
            clothesDesc + 0x28 + (i * 4)
        );

    FLog(
        "[CLOTHES DESC] texture[%d] = 0x%08X (%u)",
        i,
        texture,
        texture
    );
}

FLog("[CLOTHES DESC] ===== FIM DUMP =====");
    int16_t modelId =
        *reinterpret_cast<int16_t*>(ped + 0x32);

    uintptr_t defaultClothes =
        g_libGTASA + 0xC3EBA0;

    using FnConstructPedModel =
        bool (*)(unsigned int,
                 void*,
                 void*,
                 bool);

    auto fn =
        reinterpret_cast<FnConstructPedModel>(
            g_libGTASA + ADDR_CONSTRUCT_PED_MODEL
        );

    FLog(
        "[REBUILD DEBUG] ConstructPedModel=%p",
        (void*)fn
    );

    FLog(
        "[REBUILD DEBUG] modelId=%d",
        static_cast<int>(modelId)
    );

    FLog(
        "[REBUILD DEBUG] clothesDesc=%p",
        (void*)clothesDesc
    );

    FLog(
        "[REBUILD DEBUG] defaultClothes=%p",
        (void*)defaultClothes
    );

    FLog(
        "[REBUILD DEBUG] chamando ConstructPedModel..."
    );

    // O endereco esta hookado em InstallHooks(), portanto esta chamada
    // passa pelo hook de ConstructPedModel e depois segue para o original.
    bool result =
        fn(
            static_cast<unsigned int>(
                static_cast<uint16_t>(modelId)
            ),
            reinterpret_cast<void*>(clothesDesc),
            reinterpret_cast<void*>(defaultClothes),
            false
        );

    FLog(
        "[REBUILD DEBUG] ConstructPedModel retornou=%d",
        result ? 1 : 0
    );

    FLog("[REBUILD DEBUG] ===== STEP 4 OK =====");
}

////////----------//////------/////-----/////----------/////
void DebugRebuildStep3(CPlayerPedGta* player)
{
    FLog("[REBUILD DEBUG] ===== STEP 3 =====");

    if (!player)
    {
        FLog("[REBUILD DEBUG] player=NULL");
        return;
    }

    uintptr_t ped = reinterpret_cast<uintptr_t>(player);

    uintptr_t playerData =
        *reinterpret_cast<uintptr_t*>(ped + 0x540);

    FLog(
        "[REBUILD DEBUG] ped=%p playerData=%p",
        (void*)ped,
        (void*)playerData
    );

    if (!playerData)
    {
        FLog("[REBUILD DEBUG] ERRO: playerData=NULL");
        return;
    }

    uintptr_t clothesDesc =
        *reinterpret_cast<uintptr_t*>(playerData + 0x08);

    FLog(
        "[REBUILD DEBUG] playerData+0x08 clothesDesc=%p",
        (void*)clothesDesc
    );

    if (!clothesDesc)
    {
        FLog("[REBUILD DEBUG] ERRO: clothesDesc=NULL");
        return;
    }

    int16_t modelId =
        *reinterpret_cast<int16_t*>(ped + 0x32);

    FLog(
        "[REBUILD DEBUG] modelId=%d (0x%X)",
        static_cast<int>(modelId),
        static_cast<unsigned int>(
            static_cast<uint16_t>(modelId)
        )
    );

    uintptr_t defaultClothes =
        g_libGTASA + 0xC3EBA0;

    FLog(
        "[REBUILD DEBUG] defaultClothes=%p",
        (void*)defaultClothes
    );

    FLog(
        "[REBUILD DEBUG] ConstructPedModel=%p",
        (void*)(g_libGTASA + 0x541764)
    );

    FLog("[REBUILD DEBUG] ===== STEP 3 OK =====");
}
////////////////////////-------------------------------------
void DebugRebuildStep2(CPlayerPedGta* player)
{
    FLog("[REBUILD DEBUG] ===== STEP 2 =====");

    if (!player)
    {
        FLog("[REBUILD DEBUG] player=NULL");
        return;
    }

    uintptr_t ped =
        reinterpret_cast<uintptr_t>(player);

    uintptr_t vtable =
        *reinterpret_cast<uintptr_t*>(ped);

    FLog(
        "[REBUILD DEBUG] ped=%p",
        (void*)ped
    );

    FLog(
        "[REBUILD DEBUG] vtable=%p",
        (void*)vtable
    );

    if (!vtable)
    {
        FLog("[REBUILD DEBUG] ERRO: vtable=NULL");
        return;
    }

    uintptr_t method48 =
        *reinterpret_cast<uintptr_t*>(
            vtable + 0x48
        );

    FLog(
        "[REBUILD DEBUG] vtable+0x48=%p",
        (void*)method48
    );

    if (!method48)
    {
        FLog(
            "[REBUILD DEBUG] ERRO: método vtable+0x48=NULL"
        );
        return;
    }

    // Verificar se o endereço pertence à libGTASA.
    uintptr_t gtaBase =
        reinterpret_cast<uintptr_t>(g_libGTASA);

    uintptr_t gtaEnd =
        gtaBase + 0x9000000;

    if (method48 >= gtaBase && method48 < gtaEnd)
    {
        FLog(
            "[REBUILD DEBUG] método parece estar dentro de libGTASA"
        );

        FLog(
            "[REBUILD DEBUG] offset GTA=0x%lX",
            (unsigned long)(method48 - gtaBase)
        );
    }
    else
    {
        FLog(
            "[REBUILD DEBUG] método FORA da faixa estimada da libGTASA"
        );
    }

    FLog("[REBUILD DEBUG] ===== STEP 2 OK =====");
}
//////============================×==================//
static constexpr uintptr_t ADDR_ANIM_EXTRACT = 0x46AEF4;
static constexpr uintptr_t ADDR_GET_TASK_SECONDARY = 0x21D590;

void DebugRebuildStep1(CPlayerPedGta* player)
{
    FLog("[REBUILD DEBUG] ===== STEP 1 =====");

    if (!player)
    {
        FLog("[REBUILD DEBUG] player=NULL");
        return;
    }

    uintptr_t ped =
        reinterpret_cast<uintptr_t>(player);

    uintptr_t clump =
        *reinterpret_cast<uintptr_t*>(ped + 0x20);

    uintptr_t taskManager =
        *reinterpret_cast<uintptr_t*>(ped + 0x538);

    uintptr_t playerData =
        *reinterpret_cast<uintptr_t*>(ped + 0x540);

    FLog(
        "[REBUILD DEBUG] ped=%p",
        (void*)ped
    );

    FLog(
        "[REBUILD DEBUG] clump=%p",
        (void*)clump
    );

    FLog(
        "[REBUILD DEBUG] taskManager=%p",
        (void*)taskManager
    );

    FLog(
        "[REBUILD DEBUG] playerData=%p",
        (void*)playerData
    );

    if (!clump)
    {
        FLog("[REBUILD DEBUG] ERRO clump=NULL");
        return;
    }

    if (!taskManager)
    {
        FLog("[REBUILD DEBUG] ERRO taskManager=NULL");
        return;
    }

    // ---------------------------------------------------------
    // NÃO chamar ExtractAssociations ainda.
    // Ele é destrutivo.
    // ---------------------------------------------------------

    FLog(
        "[REBUILD DEBUG] ExtractAssociations seria chamado aqui: %p",
        (void*)(g_libGTASA + ADDR_ANIM_EXTRACT)
    );

    // ---------------------------------------------------------
    // GetTaskSecondary(5)
    //
    // Native:
    //
    // ldr x8,[player+0x538]
    // add x0,x8,#8
    // mov w1,#5
    // bl GetTaskSecondary
    // ---------------------------------------------------------

    using FnGetTaskSecondary =
        void* (*)(void*, int);

    auto getTaskSecondary =
        reinterpret_cast<FnGetTaskSecondary>(
            g_libGTASA + ADDR_GET_TASK_SECONDARY
        );

    void* task =
        getTaskSecondary(
            reinterpret_cast<void*>(taskManager + 0x8),
            5
        );

    FLog(
        "[REBUILD DEBUG] GetTaskSecondary(5)=%p",
        task
    );

    FLog("[REBUILD DEBUG] ===== STEP 1 OK =====");
}
///////////////////////////////////////////////////

static constexpr uintptr_t ADDR_ANIM_GET_NUM_ASSOC = 0x46AE54;

void DebugPlayerAnimState(CPlayerPedGta* player)
{
    FLog("[ANIM DEBUG] ===== INICIO =====");

    if (!player)
    {
        FLog("[ANIM DEBUG] player=NULL");
        return;
    }

    uintptr_t playerAddr = reinterpret_cast<uintptr_t>(player);

    uintptr_t clump =
        *reinterpret_cast<uintptr_t*>(playerAddr + 0x20);

    FLog(
        "[ANIM DEBUG] player=%p clump=%p",
        (void*)playerAddr,
        (void*)clump
    );

    if (!clump)
    {
        FLog("[ANIM DEBUG] ERRO: clump=NULL");
        return;
    }

    // ---------------------------------------------------------
    // RpAnimBlendClumpGetNumAssociations
    // ---------------------------------------------------------

    using FnGetNum = int (*)(void*);

    auto getNum = reinterpret_cast<FnGetNum>(
        g_libGTASA + ADDR_ANIM_GET_NUM_ASSOC
    );

    int count = getNum(reinterpret_cast<void*>(clump));

    FLog(
        "[ANIM DEBUG] NumAssociations=%d",
        count
    );

    // ---------------------------------------------------------
    // Descobrir ClumpOffset exatamente como o código nativo
    //
    // Native:
    //
    // adrp x8, 0x84b000
    // ldr  x8, [x8,#0x148]
    // ldrsw x8,[x8]
    //
    // ---------------------------------------------------------

    uintptr_t offsetPointerAddress =
        g_libGTASA + 0x84B000 + 0x148;

    uintptr_t offsetPointer =
        *reinterpret_cast<uintptr_t*>(
            offsetPointerAddress
        );

    FLog(
        "[ANIM DEBUG] offsetPointerAddress=%p",
        (void*)offsetPointerAddress
    );

    FLog(
        "[ANIM DEBUG] offsetPointer=%p",
        (void*)offsetPointer
    );

    if (!offsetPointer)
    {
        FLog(
            "[ANIM DEBUG] ERRO: offsetPointer=NULL"
        );

        FLog("[ANIM DEBUG] ===== FIM =====");
        return;
    }

    int32_t clumpOffset =
        *reinterpret_cast<int32_t*>(
            offsetPointer
        );

    FLog(
        "[ANIM DEBUG] ClumpOffset runtime=0x%X (%d)",
        static_cast<unsigned int>(clumpOffset),
        static_cast<int>(clumpOffset)
    );

    // ---------------------------------------------------------
    // Validar offset antes de acessar clump + offset
    // ---------------------------------------------------------

    if (clumpOffset < -0x100000 ||
        clumpOffset > 0x100000)
    {
        FLog(
            "[ANIM DEBUG] ERRO: ClumpOffset suspeito: 0x%X",
            static_cast<unsigned int>(clumpOffset)
        );

        FLog("[ANIM DEBUG] ===== FIM =====");
        return;
    }

    uintptr_t animDataAddress =
        clump + static_cast<intptr_t>(clumpOffset);

    FLog(
        "[ANIM DEBUG] animDataAddress=%p",
        (void*)animDataAddress
    );

    uintptr_t animData =
        *reinterpret_cast<uintptr_t*>(
            animDataAddress
        );

    FLog(
        "[ANIM DEBUG] animData=%p",
        (void*)animData
    );

    if (!animData)
    {
        FLog(
            "[ANIM DEBUG] ERRO: animData=NULL"
        );

        FLog("[ANIM DEBUG] ===== FIM =====");
        return;
    }

    // ---------------------------------------------------------
    // Mesmo teste utilizado pelo native IsInitialized:
    //
    // ldr w8,[x8,#0x10]
    // cmp w8,#0
    // ---------------------------------------------------------

    uint32_t initialized =
        *reinterpret_cast<uint32_t*>(
            animData + 0x10
        );

    FLog(
        "[ANIM DEBUG] animData+0x10=%u",
        initialized
    );

    // ---------------------------------------------------------
    // Primeiro elemento da lista de associações
    // ---------------------------------------------------------

    uintptr_t associationHead =
        *reinterpret_cast<uintptr_t*>(
            animData
        );

    FLog(
        "[ANIM DEBUG] associationHead=%p",
        (void*)associationHead
    );

    FLog("[ANIM DEBUG] ===== FIM =====");
}
static constexpr uintptr_t ADDR_CLOTHES_REBUILD_PLAYER = 0x540CDC;
static constexpr uintptr_t ADDR_CLOTHES_CONSTRUCT_PED_MODEL = 0x541764;
static constexpr uintptr_t ADDR_CLOTHES_BUILDER_CREATE_SKINNED_CLUMP = 0x5424F0;
static constexpr uintptr_t ADDR_CLOTHES_BUILDER_GET_CLOTHES_TEXTURE = 0x5437B8;

static void (*CPedClothesDesc__SetTextureAndModel)(
        void* pThis,
        const char* texture,
        const char* model,
        int component
);

static void QueuePendingPlayerClothesRebuild(CPlayerPedGta* player)
{
    if (!player)
    {
        FLog("[CLOTHES QUEUE] player=NULL; pedido ignorado");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_PendingClothesMutex);
        g_PendingClothesRequest.player = player;
        g_PendingClothesRequest.applyClothesChange = false;
        g_PendingClothesRequest.texture = nullptr;
        g_PendingClothesRequest.model = nullptr;
        g_PendingClothesRequest.component = -1;
    }
    FLog("[CLOTHES QUEUE] rebuild armazenado player=%p; aguardando CGame::Process", player);
}

static void QueuePendingPlayerClothesChange(
    CPlayerPedGta* player,
    const char* texture,
    const char* model,
    int component)
{
    if (!player || !texture || !model || component < 0 || component >= 10)
    {
        FLog("[CLOTHES QUEUE] pedido inválido (para este teste component deve ser 0..9) player=%p texture=%s model=%s component=%d",
             player, texture ? texture : "(null)", model ? model : "(null)", component);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_PendingClothesMutex);
        g_PendingClothesRequest.player = player;
        g_PendingClothesRequest.applyClothesChange = true;
        g_PendingClothesRequest.texture = texture;
        g_PendingClothesRequest.model = model;
        g_PendingClothesRequest.component = component;
    }
    FLog("[CLOTHES QUEUE] troca armazenada player=%p texture=%s model=%s component=%d; aguardando CGame::Process",
         player, texture, model, component);
}

// Keep the existing helper safe as well: it queues, never rebuilds immediately.
void RebuildPlayerClothes(CPlayerPedGta* player)
{
    QueuePendingPlayerClothesRebuild(player);
}
void TestPlayerClothesDescHG()
{
    FLog("[CLOTHES TEST] ===== INICIO =====");

	CPlayerPedGta* player = FindPlayerPed(-1);
    if (!player)
    {
        FLog("[CLOTHES TEST] player=NULL");
        FLog("[CLOTHES TEST] ===== FIM =====");
        return;
    }
	DebugRebuildStep1(player);
	DebugRebuildStep2(player);
	DebugRebuildStep3(player);
	DebugRebuildStep4(player);
    FLog("[CLOTHES TEST] ===== FIM =====");
}


// ============================================================
// DEBUG DA PRÓXIMA ETAPA - CClothesBuilder::GetClothesTexture
//
// Objetivo:
// descobrir qual busca de textura/modelo é a última concluída antes
// de um possível travamento dentro de CreateSkinnedClump.
//
// O hook fica SILENCIOSO fora do /testeclothes.
// Não altera nenhum retorno/argumento.
// ============================================================

static void* (*CClothesBuilder__GetClothesTexture)(
    unsigned int modelHash,
    const char* textureName
);

static void* CClothesBuilder__GetClothesTexture_hook(
    unsigned int modelHash,
    const char* textureName
)
{
    if (g_ClothesBuildDebugActive.load(std::memory_order_acquire))
    {
        FLog(
            "[CLOTHES TEX] ENTER hash=0x%08X name=%s",
            modelHash,
            textureName ? textureName : "(null)"
        );
    }

    if (!CClothesBuilder__GetClothesTexture)
    {
        if (g_ClothesBuildDebugActive.load(std::memory_order_acquire))
            FLog("[CLOTHES TEX] original NULL; textura retornada como NULL");
        return nullptr;
    }

    void* result = CClothesBuilder__GetClothesTexture(
        modelHash,
        textureName
    );

    if (g_ClothesBuildDebugActive.load(std::memory_order_acquire))
    {
        FLog(
            "[CLOTHES TEX] RETURN hash=0x%08X name=%s texture=%p",
            modelHash,
            textureName ? textureName : "(null)",
            result
        );
    }

    return result;
}

// ============================================================
// DEBUG: CClothes::ConstructPedModel
// GTA SA Mobile 2.10
// Offset: 0x541764
//
// Assinatura nativa observada:
// bool CClothes::ConstructPedModel(
//     unsigned int modelId,
//     CPedClothesDesc const& clothes,
//     CPedClothesDesc const* defaultClothes,
//     bool force
// );
//
// IMPORTANTE:
// - O retorno e bool/sucesso, NAO um RpClump*.
// - O clump interno e criado/atualizado por CreateSkinnedClump.
// - Este hook somente registra os argumentos e o retorno.
// ============================================================

static bool (*CClothes__ConstructPedModel)(
    unsigned int modelId,
    void* clothes,
    void* defaultClothes,
    bool force
);

static void DumpClothesBrief(
    const char* tag,
    void* clothes
)
{
    if (!clothes)
    {
        FLog(
            "[CLOTHES DESC] %s=NULL",
            tag ? tag : "(null)"
        );
        return;
    }

    uint32_t* d = reinterpret_cast<uint32_t*>(clothes);

    FLog(
        "[CLOTHES DESC] %s ptr=%p",
        tag ? tag : "(null)",
        clothes
    );

    FLog(
        "[CLOTHES DESC] %s models=%08X %08X %08X %08X %08X",
        tag ? tag : "(null)",
        d[0], d[1], d[2], d[3], d[4]
    );

    FLog(
        "[CLOTHES DESC] %s textures=%08X %08X %08X %08X",
        tag ? tag : "(null)",
        d[10], d[11], d[12], d[13]
    );

    FLog(
        "[CLOTHES DESC] %s stats70=%f stats74=%f",
        tag ? tag : "(null)",
        *reinterpret_cast<float*>(
            reinterpret_cast<uintptr_t>(clothes) + 0x70
        ),
        *reinterpret_cast<float*>(
            reinterpret_cast<uintptr_t>(clothes) + 0x74
        )
    );
}

// Runs after the original CGame::Process call returns. We remove the request
// from the shared slot first, validate the current player, then optionally
// update its CPedClothesDesc and call the native RebuildPlayer exactly once.
static void ProcessPendingPlayerClothesRebuild()
{
    PendingPlayerClothesRequest request{};
    {
        std::lock_guard<std::mutex> lock(g_PendingClothesMutex);
        request = g_PendingClothesRequest;
        g_PendingClothesRequest = PendingPlayerClothesRequest{};
    }

    if (!request.player)
        return;

    if (!g_libGTASA)
    {
        FLog("[CLOTHES DEFERRED] g_libGTASA=NULL; pedido descartado");
        return;
    }

    CPlayerPedGta* currentPlayer = FindPlayerPed(-1);
    if (!currentPlayer || currentPlayer != request.player)
    {
        FLog("[CLOTHES DEFERRED] player mudou (pedido=%p atual=%p); pedido descartado",
             request.player, currentPlayer);
        return;
    }

    CPlayerPedData* playerData = currentPlayer->m_pPlayerData;
    if (!playerData || !playerData->m_pPedClothesDesc)
    {
        FLog("[CLOTHES DEFERRED] playerData/clothesDesc=NULL; pedido descartado player=%p",
             currentPlayer);
        return;
    }

    CPedClothesDesc* clothes = playerData->m_pPedClothesDesc;
    bool descriptorChanged = false;

    if (request.applyClothesChange)
    {
        if (!CPedClothesDesc__SetTextureAndModel)
        {
            FLog("[CLOTHES DEFERRED] setter trampoline NULL; pedido descartado antes de alterar o descritor");
            return;
        }

        // Current test uses component 2. The offsets reflect this build's
        // CPedClothesDesc layout: models at 0x00, textures at 0x28.
        const uintptr_t descAddress = reinterpret_cast<uintptr_t>(clothes);
        const uint32_t oldModel = *reinterpret_cast<uint32_t*>(descAddress + request.component * sizeof(uint32_t));
        const uint32_t oldTexture = *reinterpret_cast<uint32_t*>(descAddress + 0x28 + request.component * sizeof(uint32_t));

        FLog("[CLOTHES DEFERRED] SET BEFORE player=%p desc=%p component=%d model=0x%08X texture=0x%08X textureName=%s modelName=%s",
             currentPlayer, clothes, request.component, oldModel, oldTexture,
             request.texture ? request.texture : "(null)", request.model ? request.model : "(null)");
        DumpClothesBrief("DEFERRED_BEFORE_SET", clothes);

        CPedClothesDesc__SetTextureAndModel(
            clothes, request.texture, request.model, request.component);

        const uint32_t newModel = *reinterpret_cast<uint32_t*>(descAddress + request.component * sizeof(uint32_t));
        const uint32_t newTexture = *reinterpret_cast<uint32_t*>(descAddress + 0x28 + request.component * sizeof(uint32_t));
        descriptorChanged = (oldModel != newModel) || (oldTexture != newTexture);

        FLog("[CLOTHES DEFERRED] SET AFTER component=%d model=0x%08X texture=0x%08X changed=%d",
             request.component, newModel, newTexture, descriptorChanged ? 1 : 0);
        DumpClothesBrief("DEFERRED_AFTER_SET", clothes);

        if (!descriptorChanged)
        {
            FLog("[CLOTHES DEFERRED] descriptor não mudou; RebuildPlayer não será chamado");
            return;
        }
    }

    using RebuildFn = void (*)(void*, bool);
    RebuildFn rebuild = reinterpret_cast<RebuildFn>(
        g_libGTASA + ADDR_CLOTHES_REBUILD_PLAYER);

    FLog("[CLOTHES DEFERRED] ENTER player=%p data=%p desc=%p fn=%p ignoreFatMuscle=0 change=%d",
         currentPlayer, playerData, clothes, reinterpret_cast<void*>(rebuild),
         request.applyClothesChange ? 1 : 0);
    DumpClothesBrief("DEFERRED_BEFORE_REBUILD", clothes);

    // Keep the detailed internal hooks quiet during ordinary gameplay.
    g_ClothesBuildDebugActive.store(true, std::memory_order_release);
    rebuild(static_cast<void*>(currentPlayer), false);
    g_ClothesBuildDebugActive.store(false, std::memory_order_release);

    // Re-fetch player data after rebuilding instead of assuming its storage
    // stayed at the same address during native model reconstruction.
    CPlayerPedData* playerDataAfter = currentPlayer->m_pPlayerData;
    CPedClothesDesc* clothesAfter = playerDataAfter ? playerDataAfter->m_pPedClothesDesc : nullptr;
    DumpClothesBrief("DEFERRED_AFTER_REBUILD", clothesAfter);
    FLog("[CLOTHES DEFERRED] RETURN RebuildPlayer terminou");
}

static bool CClothes__ConstructPedModel_hook(
    unsigned int modelId,
    void* clothes,
    void* defaultClothes,
    bool force)
{
    const bool debug = g_ClothesBuildDebugActive.load(std::memory_order_acquire);
    if (debug)
    {
        FLog("[CLOTHES CONSTRUCT] ENTER modelId=%u (0x%X) clothes=%p default=%p force=%d original=%p",
             modelId, modelId, clothes, defaultClothes, force ? 1 : 0,
             reinterpret_cast<void*>(CClothes__ConstructPedModel));
        DumpClothesBrief("construct_clothes", clothes);
        DumpClothesBrief("construct_default", defaultClothes);
    }

    if (!CClothes__ConstructPedModel)
    {
        if (debug)
            FLog("[CLOTHES CONSTRUCT] original NULL; retorno false para evitar chamada nula");
        return false;
    }

    bool result = CClothes__ConstructPedModel(modelId, clothes, defaultClothes, force);
    if (debug)
        FLog("[CLOTHES CONSTRUCT] RETURN rawValue=%d", result ? 1 : 0);
    return result;
}

// ============================================================
// DEBUG: CClothesBuilder::CreateSkinnedClump
// GTA SA Mobile 2.10
// Offset: 0x5424F0
// ============================================================

static void* (*CClothesBuilder__CreateSkinnedClump)(
    void* clump,
    void* texDictionary,
    void* clothes,
    void* defaultClothes,
    bool flag
);

static void* CClothesBuilder__CreateSkinnedClump_hook(
    void* clump,
    void* texDictionary,
    void* clothes,
    void* defaultClothes,
    bool flag)
{
    const bool debug = g_ClothesBuildDebugActive.load(std::memory_order_acquire);
    if (debug)
    {
        FLog("[CLOTHES BUILD] ENTER clump=%p texDict=%p clothes=%p default=%p flag=%d original=%p",
             clump, texDictionary, clothes, defaultClothes, flag ? 1 : 0,
             reinterpret_cast<void*>(CClothesBuilder__CreateSkinnedClump));
        DumpClothesBrief("build_clothes", clothes);
        DumpClothesBrief("build_default", defaultClothes);
    }

    if (!CClothesBuilder__CreateSkinnedClump)
    {
        if (debug)
            FLog("[CLOTHES BUILD] original NULL; retorno NULL para evitar chamada nula");
        return nullptr;
    }

    void* result = CClothesBuilder__CreateSkinnedClump(
        clump, texDictionary, clothes, defaultClothes, flag);
    if (debug)
        FLog("[CLOTHES BUILD] RETURN result=%p", result);
    return result;
}

static void CPedClothesDesc__SetTextureAndModel_hook(
        void* pThis,
        const char* texture,
        const char* model,
        int component)
{
    FLog(
        "[CLOTHES] this=%p texture=%s model=%s component=%d",
        pThis,
        texture ? texture : "(null)",
        model ? model : "(null)",
        component
    );

    if (!CPedClothesDesc__SetTextureAndModel)
    {
        FLog("[CLOTHES] original/trampoline NULL; setter ignorado para evitar chamada nula");
        return;
    }

    // Keep the native setter as a transparent passthrough. Rebuild is queued
    // separately and never invoked from this hook.
    CPedClothesDesc__SetTextureAndModel(
        pThis,
        texture,
        model,
        component
    );
}


void TestPlayerClothesDesc()
{
    FLog("[CLOTHES TEST] ===== INICIO =====");

    CPlayerPedGta* player = FindPlayerPed(-1);

    if (!player)
    {
        FLog("[CLOTHES TEST] player=NULL");
        return;
    }

    if (!pGame)
    {
        FLog("[CLOTHES TEST] pGame=NULL");
        return;
    }

    if (!CPedClothesDesc__SetTextureAndModel)
    {
        FLog("[CLOTHES TEST] setter trampoline NULL");
        return;
    }

    if (!CClothes__ConstructPedModel ||
        !CClothesBuilder__CreateSkinnedClump ||
        !CClothesBuilder__GetClothesTexture)
    {
        FLog("[CLOTHES TEST] hooks nativos incompletos");
        FLog("[CLOTHES TEST] Construct=%p Build=%p Texture=%p",
             reinterpret_cast<void*>(CClothes__ConstructPedModel),
             reinterpret_cast<void*>(CClothesBuilder__CreateSkinnedClump),
             reinterpret_cast<void*>(CClothesBuilder__GetClothesTexture));
        return;
    }

    CPlayerPedData* playerData = player->m_pPlayerData;

    if (!playerData || !playerData->m_pPedClothesDesc)
    {
        FLog("[CLOTHES TEST] playerData/clothesDesc=NULL");
        return;
    }

    FLog("[CLOTHES TEST] player=%p desc=%p",
         player, playerData->m_pPedClothesDesc);

    QueuePendingPlayerClothesChange(
        player, "trackytop1pro", "trackytop1", 0
    );

    QueuePendingPlayerClothesChange(
        player, "tracktrpro", "tracktr", 2
    );

   QueuePendingPlayerClothesChange(
        player, "sandalsock", "flipflop", 3
    );

    CGame::PostToMainThread([]()
    {
        ProcessPendingPlayerClothesRebuild();
    });

    FLog("[CLOTHES TEST] pedido enfileirado");
    FLog("[CLOTHES TEST] ===== FIM =====");
}


#include <EGL/egl.h>
#include <GLES2/gl2.h>   // If using OpenGL ES 2.0 or 3.0
void SetUpGLHooks();
void InstallHooks()
{

CHook::InlineHook(
    "_ZN15CPedClothesDesc18SetTextureAndModelEPKcS1_i",
    &CPedClothesDesc__SetTextureAndModel_hook,
    &CPedClothesDesc__SetTextureAndModel
);
FLog("[CLOTHES HOOK] SetTextureAndModel original/trampoline=%p",
     reinterpret_cast<void*>(CPedClothesDesc__SetTextureAndModel));

// Main-thread/frame callback. 0x66FE58 is the CGame_Process PLT point already
// present (commented out) in this project's InstallSAMPHooks. Check that the
// slot currently contains a target before installing anything; don't patch a
// zero/unresolved slot. Keep the pre-hook target as a fallback original pointer.

CHook::InlineHook(
    g_libGTASA + ADDR_CLOTHES_CONSTRUCT_PED_MODEL,
    &CClothes__ConstructPedModel_hook,
    &CClothes__ConstructPedModel
);
FLog("[CLOTHES HOOK] ConstructPedModel original/trampoline=%p",
     reinterpret_cast<void*>(CClothes__ConstructPedModel));
CHook::InlineHook(
    g_libGTASA + ADDR_CLOTHES_BUILDER_CREATE_SKINNED_CLUMP,
    &CClothesBuilder__CreateSkinnedClump_hook,
    &CClothesBuilder__CreateSkinnedClump
);
FLog("[CLOTHES HOOK] CreateSkinnedClump original/trampoline=%p",
     reinterpret_cast<void*>(CClothesBuilder__CreateSkinnedClump));
CHook::InlineHook(
    g_libGTASA + ADDR_CLOTHES_BUILDER_GET_CLOTHES_TEXTURE,
    &CClothesBuilder__GetClothesTexture_hook,
    &CClothesBuilder__GetClothesTexture
);
FLog("[CLOTHES HOOK] GetClothesTexture original/trampoline=%p",
     reinterpret_cast<void*>(CClothesBuilder__GetClothesTexture));
/*FLog("[RADAR DEBUG] ANTES CHud::Initialise hook");
CHook::InlineHook(
    g_libGTASA + 0x55C1C8,
    &CHud__Initialise_hook,
    &CHud__Initialise
);
FLog(
    "[RADAR DEBUG] DEPOIS CHud::Initialise hook original=%p",
    (void*)CHud__Initialise
);*/

CHook::InlineHook(
    "_ZN6CRadar13DrawRadarMaskEv",
    &CRadar__DrawRadarMask_hook,
    &CRadar__DrawRadarMask
);

CHook::InlineHook(
    "_ZN9CSprite2d15SetMaskVerticesEiPff",
    &CSprite2d__SetMaskVertices_hook,
    &CSprite2d__SetMaskVertices
);

CHook::InlineHook(
    "_ZN9CSprite2d10SetTextureEPc",
    &CSprite2d__SetTexture_hook,
    &CSprite2d__SetTexture
);


    //SetUpGLHooks();
CHook::InlineHook(
        "_ZN4CHud9DrawRadarEv",
        &CHud__DrawRadar_hook,
        &CHud__DrawRadar
    );

CHook::Redirect("_Z13Render2dStuffv", &Render2dStuff);
    CHook::Redirect("_Z13RenderEffectsv", &RenderEffects);
    CHook::InlineHook("_Z14AND_TouchEventiiii", &AND_TouchEvent_hook, &AND_TouchEvent);

    CHook::Redirect("_ZN11CHudColours12GetIntColourEh", &CHudColours__GetIntColour); // dangerous
    CHook::Redirect("_ZN6CRadar19GetRadarTraceColourEjhh", &CRadar__GetRadarTraceColor); // dangerous
    CHook::InlineHook("_ZN6CRadar12SetCoordBlipE9eBlipType7CVectorj12eBlipDisplayPc", &CRadar__SetCoordBlip_hook, &CRadar__SetCoordBlip);
    CHook::InlineHook("_ZN6CRadar20DrawRadarGangOverlayEb", & CRadar_DrawRadarGangOverlay_hook, &CRadar_DrawRadarGangOverlay);

    CHook::Redirect("_Z10GetTexturePKc", &CUtil::GetTexture);

    CHook::InlineHook("_ZN14MainMenuScreen6OnExitEv", &MainMenuScreen__OnExit_hook, &MainMenuScreen__OnExit);

    CHook::InlineHook("_ZN17CTaskSimpleUseGun17RemoveStanceAnimsEP4CPedf", &CTaskSimpleUseGun__RemoveStanceAnims_hook, &CTaskSimpleUseGun__RemoveStanceAnims);

    // Bullet sync
    CHook::InlineHook("_ZN7CWeapon14FireInstantHitEP7CEntityP7CVectorS3_S1_S3_S3_bb", &CWeapon__FireInstantHit_hook, &CWeapon__FireInstantHit);
    CHook::InlineHook("_ZN7CWeapon10FireSniperEP4CPedP7CEntityP7CVector", &CWeapon__FireSniper_hook, &CWeapon__FireSniper);
    CHook::InlineHook("_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb", &CWorld__ProcessLineOfSight_hook, &CWorld__ProcessLineOfSight);
    CHook::InlineHook("_ZN28CPedDamageResponseCalculator21ComputeDamageResponseEP4CPedR18CPedDamageResponseb", &CPedDamageResponseCalculator__ComputeDamageResponse_hook, &CPedDamageResponseCalculator__ComputeDamageResponse);
    CHook::InlineHook("_ZN7CWeapon18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntity11eWeaponTypeS6_bbbbbbb", &CWeapon__ProcessLineOfSight_hook, &CWeapon__ProcessLineOfSight);
    CHook::InlineHook("_ZN11CBulletInfo9AddBulletEP7CEntity11eWeaponType7CVectorS3_", &CBulletInfo_AddBullet_hook, &CBulletInfo_AddBullet);

    //CHook::InlineHook("_ZN11CFileLoader18LoadObjectInstanceEPKc", &CFileLoader__LoadObjectInstance_hook, &CFileLoader__LoadObjectInstance);

    CHook::InlineHook("_ZN6CRadar9ClearBlipEi", &CRadar_ClearBlip_hook, &CRadar_ClearBlip);

    CHook::InlineHook("_ZN10CCollision19ProcessVerticalLineERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbbP15CStoredCollPoly", &CCollision__ProcessVerticalLine_hook, &CCollision__ProcessVerticalLine);

    CHook::InlineHook("_ZN19CUpsideDownCarCheck15IsCarUpsideDownEPK8CVehicle", &CUpsideDownCarCheck__IsCarUpsideDown_hook, &CUpsideDownCarCheck__IsCarUpsideDown);

    CHook::InlineHook("_ZN16CTaskSimpleGetUp10ProcessPedEP4CPed", &CTaskSimpleGetUp__ProcessPed_hook, &CTaskSimpleGetUp__ProcessPed); // CTaskSimpleGetUp::ProcessPed
    CHook::InlineHook("_ZN7CObject6RenderEv", &CObject_Render_hook, & CObject_Render);

    CHook::Redirect("_Z19PlayerIsEnteringCarv", &PlayerIsEnteringCar);
    if(*(uint8_t *)(g_libGTASA + 0x896135))
    {
        CHook::Redirect("_ZNK14TextureListing11GetMipCountEv", &getmip);
    }

    if (!eglGetProcAddress("glAlphaFuncQCOM")) {
        // If "glAlphaFuncQCOM" is not available, try "glAlphaFunc"

        if (eglGetProcAddress("glAlphaFunc")) {
            // If "glAlphaFunc" is found, store the address in the global library
            *((void**)(g_libGTASA + 0x89A1B0)) = (void*)eglGetProcAddress("glAlphaFunc");
        } else {
            // If neither function is available, hook the fallback symbol
            CHook::Redirect("_Z25RQ_Command_rqSetAlphaTestRPc", &RQCommand_rqSetAlphaTest);
        }
    }

    CHook::Redirect("_ZN4CHID12GetInputTypeEv", &GetInputType);

    CHook::InlineHook("_ZN14CAnimBlendNode12FindKeyFrameEf", &CAnimBlendNode__FindKeyFrame_hook, &CAnimBlendNode__FindKeyFrame);
    CHook::InlineHook("_ZN15CClumpModelInfo14GetFrameFromIdEP7RpClumpi", &CClumpModelInfo_GetFrameFromId_hook, &CClumpModelInfo_GetFrameFromId);

    CHook::InlineHook("_ZN13FxEmitterBP_c6RenderEP8RwCamerajfh", &FxEmitterBP_c__Render_hook, &FxEmitterBP_c__Render);
    CHook::InlineHook("_Z23RwResourcesFreeResEntryP10RwResEntry", &RwResourcesFreeResEntry_hook, &RwResourcesFreeResEntry);

    //CHook::InlineHook("_ZN9CRenderer24RenderEverythingBarRoadsEv", &CRenderer_RenderEverythingBarRoads_hook, &CRenderer_RenderEverythingBarRoads);

    ms_fAspectRatio = (float*)(g_libGTASA+0xCC7F00);
    CHook::InlineHook("_ZN4CHud14DrawCrossHairsEv", &DrawCrosshair_hook, &DrawCrosshair);

    // retexture
    CHook::InlineHook("_ZN7CEntity6RenderEv", &CEntity_Render_hook, &CEntity_Render);

    //CHook::InlineHook("_ZN26CAEGlobalWeaponAudioEntity21ServiceAmbientGunFireEv", &TaskEnterVehicleHook, &TaskEnterVehicle);

    CHook::Write(g_libGTASA + 0x5DF790, 0x90000AA9);
    CHook::Write(g_libGTASA + 0x5DF794, 0xBD48D521);

    CHook::InlineHook("_ZN5CDraw6SetFOVEfb", &CDraw__SetFOV_hook, &CDraw__SetFOV);

    CHook::InlineHook("_ZN10CStreaming5Init2Ev", &CStreaming__Init2_hook, &CStreaming__Init2);


    HookCPad();
}
