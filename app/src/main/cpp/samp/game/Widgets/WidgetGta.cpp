//
// Created by x1y2z on 03.02.2023.
//

#include "WidgetGta.h"
#include "main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "vendor/armhook/patch.h"
#include "WidgetRegionLook.h"
#include "TouchInterface.h"
#include "WidgetButton.h"
#include "WidgetVisibility.h"
#include <cstdio>
#include "gui/samp_widgets/controlmode.h"

extern CNetGame *pNetGame;
extern CGame *pGame;

CWidgetGta* m_pWidgets[WidgetIDs::NUM_WIDGETS];

enum eWidgetState {
    STATE_NONE,
    STATE_FIXED
};

WidgetIDs GetWidgetTypeFromWidget(CWidgetGta* pWidget)
{
    if(pWidget)
    {
        if(m_pWidgets[WidgetIDs::WIDGET_ATTACK] && pWidget == m_pWidgets[WidgetIDs::WIDGET_ATTACK]) return WidgetIDs::WIDGET_ATTACK;
        if(m_pWidgets[WidgetIDs::WIDGET_SPRINT] && pWidget == m_pWidgets[WidgetIDs::WIDGET_SPRINT]) return WidgetIDs::WIDGET_SPRINT;
        if(m_pWidgets[WidgetIDs::WIDGET_ACCELERATE] && pWidget == m_pWidgets[WidgetIDs::WIDGET_ACCELERATE]) return WidgetIDs::WIDGET_ACCELERATE;
        if(m_pWidgets[WidgetIDs::WIDGET_ENTER_CAR] && pWidget == m_pWidgets[WidgetIDs::WIDGET_ENTER_CAR]) return WidgetIDs::WIDGET_ENTER_CAR;
        if(m_pWidgets[WidgetIDs::WIDGET_BRAKE] && pWidget == m_pWidgets[WidgetIDs::WIDGET_BRAKE]) return WidgetIDs::WIDGET_BRAKE;
    }

    return static_cast<WidgetIDs>(-1);
}

void SetWidgetFromId(int idWidget, CWidgetGta* pWidget)
{
    m_pWidgets[idWidget] = pWidget;
}

void SetWidgetFromName(const char* name, CWidgetGta* pWidget)
{
    if(!strcmp("accelerate", name)) SetWidgetFromId(WidgetIDs::WIDGET_ACCELERATE, pWidget);
    if(!strcmp("hud_car", name)) SetWidgetFromId(WidgetIDs::WIDGET_ENTER_CAR, pWidget);
    if(!strcmp("brake", name)) SetWidgetFromId(WidgetIDs::WIDGET_BRAKE, pWidget);
}

static int LookupWidgetId(CWidgetGta* pWidget)
{
    if(!pWidget || !CTouchInterface::m_pWidgets) return -1;
    for(int i = 0; i < WidgetIDs::NUM_WIDGETS; i++)
        if(CTouchInterface::m_pWidgets[i] == pWidget) return i;
    return -1;
}

eWidgetState ProcessFixedWidget(CWidgetGta* pWidget)
{
    WidgetIDs widgetType = GetWidgetTypeFromWidget(pWidget);

    CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
    if(!pPlayerPed) return STATE_NONE;

    // Botoes a pe x veiculo: o widget e reconhecido pela POSICAO em CTouchInterface::m_pWidgets (nao pelo nome de criacao,
    // que so cobria accelerate/brake/hud_car -> ATTACK/SPRINT nunca eram reconhecidos e buzina/setas ficavam sempre ligadas).
    // O estado usado e SEMPRE o do ped LOCAL (nunca o de jogadores proximos).
    {
        const int id = LookupWidgetId(pWidget);
        if(id >= 0 && WidgetVis::ShouldHide(id, pPlayerPed->IsInVehicle(), pPlayerPed->IsInJetpackMode()))
            return STATE_FIXED;
        // MODERNO a pe: os controles nativos substituidos nao podem ser religados pelo jogo
        if(id >= 0 && ControlMode::IsModern() && !pPlayerPed->IsInVehicle() && !pPlayerPed->IsInJetpackMode() && WidgetVis::ModernReplaces(id))
            return STATE_FIXED;
    }

    switch(widgetType)
    {
        case -1:
            return STATE_NONE;
        case WidgetIDs::WIDGET_ATTACK:
        case WidgetIDs::WIDGET_SPRINT:
            if(pPlayerPed->IsInVehicle() ||
               pPlayerPed->IsInJetpackMode())
            {
                return STATE_FIXED;
            }
            break;
        case WidgetIDs::WIDGET_ACCELERATE:
        case WidgetIDs::WIDGET_BRAKE:
            if(!pPlayerPed->IsInVehicle() &&
               !pPlayerPed->IsInJetpackMode())
            {
                return STATE_FIXED;
            }
            break;
        case WidgetIDs::WIDGET_ENTER_CAR:
            if(pPlayerPed->IsInJetpackMode()) return STATE_NONE;

            if(pNetGame)
            {
                CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();
                if(pVehiclePool)
                {
                    VEHICLEID vehicleId = pVehiclePool->FindNearestToLocalPlayerPed();
                    if(vehicleId == INVALID_VEHICLE_ID) return STATE_FIXED;

                    if(vehicleId != INVALID_VEHICLE_ID)
                    {
                        CVehicle *pVehicle = pVehiclePool->GetAt(vehicleId);
                        if(pVehicle)
                        {
                            if(!pPlayerPed->IsInVehicle() &&
                               pVehicle->m_pVehicle->GetDistanceFromLocalPlayerPed() > 10.0f)
                            {
                                return STATE_FIXED;
                            }
                        }
                    }
                }
            }
            break;
    }

    return STATE_NONE;
}

void CWidgetGta::SetEnabled(bool bEnabled) {
    m_bEnabled = bEnabled;
}

bool (*CWidget__IsTouched)(uintptr_t *thiz, CVector2D *pVecOut);
bool CWidget__IsTouched_hook(uintptr_t *thiz, CVector2D *pVecOut) {
//    if(*thiz == CWidgetGta::pWidgets[WIDGET_POSITION_HORN]) {
//        return true;
//    }
//    if(!CHUD::bIsShow)
//        return false;


    return CWidget__IsTouched(thiz, pVecOut);
}

uintptr_t (*CWidget)(CWidgetButton* thiz, const char* name, uintptr_t* a3, int a4, uintptr_t* a5);
uintptr_t CWidget_hook(CWidgetButton* thiz, const char* name, uintptr_t*  a3, int a4, uintptr_t* a5)
{
    FLog("New Widget: \"%s\" 0x%X", name, thiz-g_libGTASA);

    SetWidgetFromName(name, thiz);
    return CWidget(thiz, name, a3, a4, a5);
}

void (*CWidget__SetEnabled)(CWidgetGta* pWidget, bool bEnabled);
void CWidget__SetEnabled_hook(CWidgetGta* pWidget, bool bEnabled)
{
    if(pNetGame)
    {
        switch(ProcessFixedWidget(pWidget))
        {
            case STATE_NONE: break;
            case STATE_FIXED:
                bEnabled = false;
                break;
        }
    }

    CWidget__SetEnabled(pWidget, bEnabled);
}

void (*CWidgetButton__Update)(CWidgetButton* thiz);
void CWidgetButton__Update_hook(CWidgetButton* thiz) {
    if(pNetGame)
    {
        switch(ProcessFixedWidget(thiz))
        {
            case STATE_NONE: break;
            case STATE_FIXED: return;
        }
    }

    CWidgetButton__Update(thiz);
}

// Depois do Update nativo do HUD de toque, forca desligados os botoes que nao pertencem ao estado atual do jogador LOCAL
// (cobre widgets que o jogo nao religa/desliga por SetEnabled a cada quadro e subclasses com Update proprio, como a buzina).
void (*CTouchInterface__Update)();
void PadAimFrameTick();   // game/pad.cpp
void ModernControlsSoundTick();   // gui/samp_widgets/moderncontrols.cpp
void CTouchInterface__Update_hook()
{
    CTouchInterface__Update();
    if(pNetGame && pGame) { PadAimFrameTick(); ModernControlsSoundTick(); }
    if(!pNetGame || !pGame || !CTouchInterface::m_pWidgets) return;
    CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
    if(!pPlayerPed) return;
    const bool inVeh = pPlayerPed->IsInVehicle(), jet = pPlayerPed->IsInJetpackMode();
    const bool modernFoot = ControlMode::IsModern() && !inVeh && !jet;
    {   // diagnostico (1x por entrada no moderno): quais widgets nativos seguem ligados a pe -> samp_log.txt
        static bool s_logged = false;
        if(!modernFoot) s_logged = false;
        else if(!s_logged)
        {
            s_logged = true;
            char buf[512]; int n = 0;
            for(int i = 0; i < WidgetIDs::NUM_WIDGETS && n < 480; i++)
            {
                CWidgetGta* w = CTouchInterface::m_pWidgets[i];
                if(w && w->m_bEnabled) n += snprintf(buf + n, sizeof(buf) - n, "%X%s ", i, WidgetVis::ModernReplaces(i) ? "(oculto)" : "");
            }
            buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1] = 0;
            FLog("[CONTROLES] widgets nativos ligados a pe no moderno: %s", buf);
        }
    }
    for(int i = 0; i < WidgetIDs::NUM_WIDGETS; i++)
    {
        CWidgetGta* w = CTouchInterface::m_pWidgets[i];
        if(w && w->m_bEnabled && WidgetVis::ShouldHide(i, inVeh, jet))
            CWidget__SetEnabled(w, false);
        // MODERNO a pe: desliga os controles nativos que o sistema moderno substitui (no CLASSICO esta condicao e sempre falsa).
        else if(w && modernFoot && WidgetVis::ModernReplaces(i))
        {
            if(w->m_bEnabled) CWidget__SetEnabled(w, false);
            w->m_Color.a = 0;      // desligar nao apaga o icone: o jogo ainda o desenhava (video do teste); alpha 0 esconde o desenho
        }
    }
}

void CWidgetGta::InjectHooks() {
    CHook::InstallPLT(g_libGTASA + 0x842408, &CWidgetButton__Update_hook, &CWidgetButton__Update);
    CHook::InlineHook("_ZN13CWidgetButtonC2EPKcRK14WidgetPositionjj10HIDMapping", &CWidget_hook, &CWidget);
    CHook::InlineHook("_ZN7CWidget10SetEnabledEb", &CWidget__SetEnabled_hook, &CWidget__SetEnabled);
    CHook::InlineHook("_ZN15CTouchInterface6UpdateEv", &CTouchInterface__Update_hook, &CTouchInterface__Update);
}

void CWidgetGta::SetTexture(const char *name) {
    m_Sprite.m_pTexture = CUtil::LoadTextureFromDB("mobile", name);
}

bool CWidgetGta::IsReleased(CVector2D *pVecOut) {
    return CHook::CallFunction<bool>(g_libGTASA + 0x372794, this, pVecOut);
}

bool CWidgetGta::IsTouched(CVector2D *pVecOut) {
    return CHook::CallFunction<bool>(g_libGTASA + 0x3725D0, this, pVecOut);
}


