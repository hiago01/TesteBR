#pragma once

// ---------------------------------------------------------------------------
// Mapa "slot de roupa" -> material(is) do skin.dff (male) -> pasta do pack de texturas.
// Código PURO (sem RenderWare): compila no host. Usado por /skin7 slots|slot (CustomSkin.cpp).
//
//   pack no aparelho: <raiz>/SAMP/skins/pack/<pasta>/<n>.png      (gerado por tools_skin_pack.py)
//   texture : nome da textura gravado no material do DFF (vários separados por ',')
//   folder  : pasta do pack com as variantes desse slot
//   ev      : força da evidência do vínculo pasta <-> material
//               2 = confirmado por nome: as artes de marca (louisvuitton/versace) usam o prefixo
//                   "N_k" = número do slot (ex.: tshirts/2_1 -> torso2, boot/1_1 -> boot1)
//               1 = coincidência 1:1 de nome e quantidade (ex.: 4 pastas hair/ <-> 4 materiais hair)
//               0 = HIPÓTESE (quantidades não batem ou não há marca); confirme no jogo com /skin7 set
// ---------------------------------------------------------------------------

#include <cctype>
#include <cstring>
#include <vector>

namespace SkinSlots
{
    struct Slot
    {
        const char* name;
        const char* texture;
        const char* folder;
        int         ev;
    };

    static const Slot kSlots[] = {
        // roupas de tronco (pasta tshirts/N <-> fantasy_m_torsoN)
        { "torso1",   "fantasy_m_torso1",   "tshirts/1", 2 },
        { "torso2",   "fantasy_m_torso2",   "tshirts/2", 2 },
        { "torso3",   "fantasy_m_torso3",   "tshirts/3", 2 },
        { "torso4",   "fantasy_m_torso4",   "tshirts/4", 1 },
        // pernas: 7 pastas para 4 calças + 3 bermudas -> ordem presumida (HIPÓTESE)
        { "pants1",   "fantasy_m_pants1",   "pants/1",   0 },
        { "pants2",   "fantasy_m_pants2",   "pants/2",   0 },
        { "pants3",   "fantasy_m_pants3",   "pants/3",   0 },
        { "pants4",   "fantasy_m_pants4",   "pants/4",   0 },
        { "short1",   "fantasy_m_short1",   "pants/5",   0 },
        { "short2",   "fantasy_m_short2",   "pants/6",   0 },
        { "short3",   "fantasy_m_short3",   "pants/7",   0 },
        // calçados
        { "boot1",    "fantasy_m_boot1",    "boot/1",    2 },
        { "boot2",    "fantasy_m_boot2",    "boot/2",    1 },
        { "boot3",    "fantasy_m_boot3",    "boot/3",    1 },
        { "boot4",    "fantasy_m_boot4",    "boot/4",    1 },
        { "slipper1", "fantasy_m_slipper1", "slipper/1", 1 },
        { "slipper2", "fantasy_m_slipper2", "slipper/2", 2 },
        { "slipper3", "fantasy_m_slipper3", "slipper/3", 1 },
        // cabeça e acessórios
        { "hat1",     "fantasy_m_hat1",     "hat/1",     2 },
        { "mask1",    "fantasy_m_mask1",    "mask/1",    1 },
        { "mask2",    "fantasy_m_mask2",    "mask/2",    1 },
        { "glasse1",  "fantasy_m_glasse1",  "glasse/1",  1 },
        { "glasse2",  "fantasy_m_glasse2",  "glasse/2",  1 },
        { "ear1",     "fantasy_m_ear1",     "ear/1",     1 },
        { "ear2",     "fantasy_m_ear2",     "ear/2",     1 },
        { "ear3",     "fantasy_m_ear3",     "ear/3",     1 },
        { "ear4",     "fantasy_m_ear4",     "ear/4",     1 },
        { "necklace1","fantasy_m_necklace1","necklace/1",1 },
        { "clock1",   "fantasy_m_clock1",   "clock/1",   1 },
        { "clock2",   "fantasy_m_clock2",   "clock/2",   1 },
        { "backpack1","fantasy_m_backpack1","backpack/1",2 },
        { "backpack2","fantasy_m_backpack2","backpack/2",2 },
        { "backpack3","fantasy_m_backpack3","backpack/3",2 },
        { "hair1",    "fantasy_m_hair1",    "hair/1",    1 },
        { "hair2",    "fantasy_m_hair2",    "hair/2",    1 },
        { "hair3",    "fantasy_m_hair3",    "hair/3",    1 },
        { "hair4",    "fantasy_m_hair4",    "hair/4",    1 },
        { "underwear","fantasy_m_underwear","underwear/1",2 },
        // rosto: 6 pastas heads/ para 5 materiais body.head1..5 (HIPÓTESE); sobrancelha em pasta única
        { "head1",    "body.head1",         "heads/1",   0 },
        { "head2",    "body.head2",         "heads/2",   0 },
        { "head3",    "body.head3",         "heads/3",   0 },
        { "head4",    "body.head4",         "heads/4",   0 },
        { "head5",    "body.head5",         "heads/5",   0 },
        { "brow1",    "body.eyebrown1",     "eyebrown",  0 },
        { "brow2",    "body.eyebrown2",     "eyebrown",  0 },
        { "brow3",    "body.eyebrown3",     "eyebrown",  0 },
        { "brow4",    "body.eyebrown4",     "eyebrown",  0 },
        { "brow5",    "body.eyebrown5",     "eyebrown",  0 },
        { "beard1",   "body.beards1",       "beards/1",  0 },
        { "beard2",   "body.beards2",       "beards/1",  0 },
        { "beard3",   "body.beards3",       "beards/1",  0 },
        { "beard4",   "body.beards4",       "beards/1",  0 },
        { "beard5",   "body.beards5",       "beards/1",  0 },
        // pele do corpo (6 tons). Evidência (UV): torso+braços+mãos+ombros NÃO se sobrepõem entre si (cabem num atlas
        // só = torso/1), mas as pernas se sobrepõem ao torso e aos pés, e os pés ao torso (3 atlas = torso, leg, feet).
        { "skin_torso", "body.torso1,body.torso2,body.torso3,body.armright,body.armleft,body.hand,"
                        "body.shoulderleft,body.shoulderright", "torso/1", 1 },
        { "skin_leg",   "body.legleft1,body.legleft2,body.legleft3,body.legleft4,body.legleft5,"
                        "body.legright1,body.legright2,body.legright3,body.legright4,body.legright5", "leg/1", 1 },
        { "skin_feet",  "body.feetleft,body.feetright", "feet/1", 1 },
        // tatuagens (overlay sobre as mesmas UVs do corpo). Pastas por NOME de região: HIPÓTESE.
        { "tat_back",     "fantasy_tatto_torso_back",  "tattoo/torso_back_complete",  0 },
        { "tat_chest",    "fantasy_tatto_torso_front", "tattoo/torso_chest_complete", 0 },
        { "tat_arm",      "fantasy_tatto_arm_r,fantasy_tatto_arm_l", "tattoo/torso_arm_complete", 0 },
        { "tat_shoulder", "fantasy_tatto_shoulder_l,fantasy_tatto_shoulder_r", "tattoo/torso_arm_top", 0 },
        { "tat_legs",     "fantasy_tatto_leg_l_1,fantasy_tatto_leg_l_2,fantasy_tatto_leg_l_3,fantasy_tatto_leg_l_4,fantasy_tatto_leg_l_5,"
                          "fantasy_tatto_leg_r_1,fantasy_tatto_leg_r_2,fantasy_tatto_leg_r_3,fantasy_tatto_leg_r_4,fantasy_tatto_leg_r_5",
                          "tattoo/legs_top", 0 },
    };
    static constexpr int kCount = (int)(sizeof(kSlots) / sizeof(kSlots[0]));

    inline bool EqNoCase(const char* a, const char* b, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return true;
    }

    inline const Slot* Find(const char* name)
    {
        if (!name || !*name) return nullptr;
        const size_t n = std::strlen(name);
        for (int i = 0; i < kCount; ++i)
            if (std::strlen(kSlots[i].name) == n && EqNoCase(kSlots[i].name, name, n)) return &kSlots[i];
        return nullptr;
    }

    // "a,b,c" contém `tex` (sem diferenciar maiúscula/minúscula)?
    inline bool TextureListHas(const char* list, const char* tex)
    {
        if (!list || !tex || !*tex) return false;
        const size_t tn = std::strlen(tex);
        const char* p = list;
        while (*p)
        {
            const char* e = std::strchr(p, ',');
            const size_t len = e ? (size_t)(e - p) : std::strlen(p);
            if (len == tn && EqNoCase(p, tex, tn)) return true;
            if (!e) break;
            p = e + 1;
        }
        return false;
    }

    // ---------------------------------------------------------------------------------------------------
    // Exclusividade por categoria (/skin7 slot): só UMA variante visível por grupo.
    // Cada grupo lista slots que existem em kSlots acima (nomes conferidos; slot inexistente é ignorado).
    // Categorias com um único slot (hat1, necklace1, underwear, skin_*) já são exclusivas por natureza.
    // Fora dos grupos de propósito: brow*/beard* (ev 0; beard1..5 dividem a mesma pasta), skin_leg (um slot só
    // para as 10 pernas) e tat_* (cada tatuagem é uma região diferente do corpo).
    // ---------------------------------------------------------------------------------------------------
    static const char* const kExclusiveGroups[] = {
        "backpack1,backpack2,backpack3",
        "boot1,boot2,boot3,boot4",
        "clock1,clock2",
        "ear1,ear2,ear3,ear4",
        "glasse1,glasse2",
        "hair1,hair2,hair3,hair4",
        "head1,head2,head3,head4,head5",
        "mask1,mask2",
        "pants1,pants2,pants3,pants4,short1,short2,short3",
        "slipper1,slipper2,slipper3",
        "torso1,torso2,torso3,torso4",
    };
    static constexpr int kExclusiveGroupCount = (int)(sizeof(kExclusiveGroups) / sizeof(kExclusiveGroups[0]));

    // índice do grupo exclusivo do slot, ou -1 se ele não pertence a nenhum
    inline int GroupOf(const char* slotName)
    {
        for (int g = 0; g < kExclusiveGroupCount; ++g)
            if (TextureListHas(kExclusiveGroups[g], slotName)) return g;
        return -1;
    }

    // Materiais que precisam ser ocultados quando `target` é ativado: os materiais dos OUTROS slots do mesmo
    // grupo, menos (a) os do próprio target e (b) qualquer material que também pertença a um slot de fora do
    // grupo (nunca desliga outra categoria por causa de material compartilhado).
    // matsOf(const Slot&) -> std::vector<int>: materiais do slot no DFF carregado (vazio se o DFF não os tem).
    template <class MatsFn>
    inline std::vector<int> ExclusiveVictims(const Slot& target, MatsFn matsOf)
    {
        std::vector<int> victims;
        const int g = GroupOf(target.name);
        if (g < 0) return victims;

        const std::vector<int> mine = matsOf(target);
        std::vector<int> outside;                       // materiais de slots de outras categorias
        for (int k = 0; k < kCount; ++k)
        {
            const Slot& s = kSlots[k];
            if (&s == &target) continue;
            if (GroupOf(s.name) == g) { const std::vector<int> m = matsOf(s); victims.insert(victims.end(), m.begin(), m.end()); }
            else                      { const std::vector<int> m = matsOf(s); outside.insert(outside.end(), m.begin(), m.end()); }
        }
        auto in = [](const std::vector<int>& v, int x) { for (int y : v) if (y == x) return true; return false; };
        std::vector<int> r;
        for (int i : victims)
            if (!in(mine, i) && !in(outside, i) && !in(r, i)) r.push_back(i);
        return r;
    }
}
