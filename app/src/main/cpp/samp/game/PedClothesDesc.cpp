#include "PedClothesDesc.h"

#include "../game.h"

static constexpr uintptr_t ADDR_SET_TEXTURE_MODEL_STR =
    0x540A88;

static constexpr uintptr_t ADDR_SET_TEXTURE_MODEL_HASH =
    0x540A24;

static constexpr uintptr_t ADDR_SET_MODEL =
    0x540B28;

void CPedClothesDesc::SetTextureAndModel(
    const char* texture,
    const char* model,
    int component)
{
    using Fn = void (*)(CPedClothesDesc*,
                        const char*,
                        const char*,
                        int);

    auto fn = reinterpret_cast<Fn>(
        g_libGTASA + ADDR_SET_TEXTURE_MODEL_STR
    );

    fn(this, texture, model, component);
}

void CPedClothesDesc::SetTextureAndModel(
    uint32_t texture,
    uint32_t model,
    int component)
{
    using Fn = void (*)(CPedClothesDesc*,
                        uint32_t,
                        uint32_t,
                        int);

    auto fn = reinterpret_cast<Fn>(
        g_libGTASA + ADDR_SET_TEXTURE_MODEL_HASH
    );

    fn(this, texture, model, component);
}

void CPedClothesDesc::SetModel(
    uint32_t model,
    int component)
{
    using Fn = void (*)(CPedClothesDesc*,
                        uint32_t,
                        int);

    auto fn = reinterpret_cast<Fn>(
        g_libGTASA + ADDR_SET_MODEL
    );

    fn(this, model, component);
}
