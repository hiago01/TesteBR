#pragma once

#include <cstdint>

class CPedClothesDesc
{
public:
    uint32_t m_nModel[10];      // 0x00 - 0x24
    uint32_t m_nTexture[18];    // 0x28 - 0x6C

    void SetTextureAndModel(
        const char* texture,
        const char* model,
        int component
    );

    void SetTextureAndModel(
        uint32_t texture,
        uint32_t model,
        int component
    );

    void SetModel(
        uint32_t model,
        int component
    );
};
