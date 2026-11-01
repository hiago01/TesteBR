// Quais botoes do HUD de toque pertencem so ao "a pe" e quais so ao "veiculo". Logica pura (sem Android) para teste em PC.
// Os valores sao os de CTouchInterface::WidgetIDs (TouchInterface.h); aqui em numeros para nao depender do jogo.
#pragma once

namespace WidgetVis
{
    enum : int {
        ID_ATTACK = 0x01, ID_ACCELERATE = 0x02, ID_BRAKE = 0x03, ID_HANDBRAKE = 0x04,
        ID_STEER_LEFT = 0x05, ID_STEER_RIGHT = 0x06, ID_HORN = 0x07,
        ID_ENTER_TARGETING = 0x13, ID_EXIT_TARGETING = 0x14,
        ID_HYDRAULICS = 0x0C, ID_AUTO_HYDRAULICS = 0x0D,
        ID_VEHICLE_SHOOT_LEFT = 0x10, ID_VEHICLE_SHOOT_RIGHT = 0x11, ID_CAM_TOGGLE = 0x12,
        ID_VEHICLE_BOMB = 0x19, ID_TURRET_LEFT = 0x1A, ID_TURRET_RIGHT = 0x1B,
        ID_BUTTON_SPRINT = 0x1F, ID_BUTTON_CROUCH = 0x20, ID_BUTTON_DIVE = 0x21, ID_BUTTON_SWIM = 0x22,
        ID_PED_MOVE = 0xA7, ID_SPRINT = 0xA8
    };

    /** Botoes que so fazem sentido A PE: somem dentro de veiculo/jetpack. */
    inline bool FootOnly(int id)
    {
        switch (id) {
            case ID_ATTACK: case ID_BUTTON_SPRINT: case ID_BUTTON_CROUCH: case ID_BUTTON_DIVE: case ID_BUTTON_SWIM: case ID_SPRINT:
                return true;
        }
        return false;
    }

    /** Botoes que so fazem sentido DENTRO de veiculo: somem a pe. */
    inline bool VehicleOnly(int id)
    {
        switch (id) {
            case ID_ACCELERATE: case ID_BRAKE: case ID_HANDBRAKE: case ID_STEER_LEFT: case ID_STEER_RIGHT: case ID_HORN:
            case ID_HYDRAULICS: case ID_AUTO_HYDRAULICS: case ID_VEHICLE_SHOOT_LEFT: case ID_VEHICLE_SHOOT_RIGHT: case ID_CAM_TOGGLE:
            case ID_VEHICLE_BOMB: case ID_TURRET_LEFT: case ID_TURRET_RIGHT:
                return true;
        }
        return false;
    }

    /** Controles MODERNOS (a pe): botoes/joystick nativos que o sistema moderno substitui. Nadar/mergulhar (DIVE/SWIM) e o
     *  botao de entrar no carro NAO entram aqui: o moderno nao os reimplementa, entao continuam nativos. */
    inline bool ModernReplaces(int id)
    {
        switch (id) {
            case ID_ATTACK: case ID_BUTTON_SPRINT: case ID_BUTTON_CROUCH: case ID_SPRINT: case ID_PED_MOVE:
            case ID_ENTER_TARGETING: case ID_EXIT_TARGETING:   // mira: no moderno o Ataque segurado mira (pad.cpp)
                return true;
        }
        return false;
    }

    /** inVehicle = ped LOCAL em veiculo; onFootLike=false em jetpack (acelerar/frear continuam validos no jetpack). */
    inline bool ShouldHide(int id, bool inVehicle, bool inJetpack)
    {
        if (id < 0) return false;
        if (inVehicle || inJetpack) { if (FootOnly(id)) return true; }
        if (!inVehicle && !inJetpack) { if (VehicleOnly(id)) return true; }
        return false;
    }
}
