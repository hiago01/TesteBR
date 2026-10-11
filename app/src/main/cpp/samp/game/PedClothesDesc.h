#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Componentes = "partes de textura" (0..17) usadas por SetTextureAndModel().
// Atenção: componente NÃO é o mesmo que slot de modelo (0..9).
//
// Confirmado na libGTASA.so 2.10 arm64 deste projeto (tabelas em 0x754D40 /
// 0x754D90 / 0x754DE0 e clothesTextureNames):
//
//   componente  nome            slot de modelo
//        0      torso               0
//        1      cabelo ("head")     1
//        2      pernas              3
//        3      sapatos             4
//      4..12    tatuagens (9 áreas) -  (só textura, sem modelo)
//       13      colar               5
//       14      relógio             6
//       15      óculos              7
//       16      chapéu              8
//       17      extra ("extra1")    9
//
// A ordem exata das 9 áreas de tatuagem (4..12) não foi verificada no binário.
// ---------------------------------------------------------------------------
enum eClothesComponent : int
{
    CLOTHES_COMPONENT_TORSO        = 0,
    CLOTHES_COMPONENT_HAIR         = 1,
    CLOTHES_COMPONENT_LEGS         = 2,
    CLOTHES_COMPONENT_SHOES        = 3,
    CLOTHES_COMPONENT_TATTOO_FIRST = 4,
    CLOTHES_COMPONENT_TATTOO_LAST  = 12,
    CLOTHES_COMPONENT_NECKLACE     = 13,
    CLOTHES_COMPONENT_WATCH        = 14,
    CLOTHES_COMPONENT_GLASSES      = 15,
    CLOTHES_COMPONENT_HAT          = 16,
    CLOTHES_COMPONENT_EXTRA        = 17,

    CLOTHES_NUM_COMPONENTS         = 18,
    CLOTHES_NUM_MODEL_SLOTS        = 10,
};

class CPedClothesDesc
{
public:
    uint32_t m_nModel[10];      // 0x00 - 0x27  hashes de modelo (indexado por SLOT de modelo)
    uint32_t m_nTexture[18];    // 0x28 - 0x6F  hashes de textura (indexado por COMPONENTE)
    float    m_fFatStat;        // 0x70  (RebuildPlayer(ped,false) sobrescreve com CStats[21])
    float    m_fMuscleStat;     // 0x74  (RebuildPlayer(ped,false) sobrescreve com CStats[23])

    // componente (0..17) -> slot de modelo (0..9); -1 = componente só de textura
    static constexpr int ModelSlotForComponent(int component)
    {
        constexpr int kMap[CLOTHES_NUM_COMPONENTS] = {
            0, 1, 3, 4,                         // torso, cabelo, pernas, sapatos
            -1, -1, -1, -1, -1, -1, -1, -1, -1, // tatuagens
            5, 6, 7, 8, 9                       // colar, relógio, óculos, chapéu, extra
        };
        return (component >= 0 && component < CLOTHES_NUM_COMPONENTS) ? kMap[component] : -1;
    }

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

    // Atenção: o 2o parâmetro é o SLOT de modelo (0..9), não o componente.
    void SetModel(
        uint32_t model,
        int component
    );
};

// O jogo usa esta estrutura inteira (gOldClothesState tem 120 bytes = 0x78).
static_assert(sizeof(CPedClothesDesc) == 0x78, "CPedClothesDesc deve ter 0x78 bytes");
