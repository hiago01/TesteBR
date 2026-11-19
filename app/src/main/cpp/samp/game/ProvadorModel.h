#pragma once
// ---------------------------------------------------------------------------------------------------------
// ProvadorModel.h - regras PURAS (sem RenderWare/RakNet) do /provador. Compila e é testado no host
// (testes/host_test_provador.cpp). Usado por CustomSkin.cpp (PublishUiState / confirmação do servidor).
//
// POR QUE EXISTE
//   "Material visível" NÃO é "roupa equipada": depois do /skin7 apply TODOS os materiais do DFF estão visíveis
//   (camisa 1..4, cabelo 1..4, ...) e a maioria com a textura original do DFF, que no aparelho vira "male01"
//   (invisível). A interface antiga mostrava "ATIVO" para tudo. Aqui o estado de um item é derivado em 5 níveis:
//
//     kUnavailable  o DFF carregado não tem o(s) material(is) do slot
//     kDefault      visível com a textura ORIGINAL do DFF (nenhuma variante do pack aplicada) = nada equipado
//     kEquipped     visível com uma variante do pack (pack/<pasta>/<n>.png) aplicada
//     kDisabled     oculto pelo jogador (DESLIGAR)
//     kCovered      oculto porque outra peça visível o cobre por inteiro (pele sob camisa/calça/tênis)
//
//   Prévia (não confirmada) NÃO é um estado: é uma marca (`previewed`) sobre qualquer um deles, calculada
//   comparando com a foto feita no início da prévia (BeginBackup).
//
// ESTADO DO SERVIDOR (autoridade)
//   O servidor (maximo_look.inc) é quem decide o estado definitivo e o devolve por RPC 232 ao dono. Aqui está o
//   ESPELHO da regra dele (Look_PutSlot): ligar uma peça visível de um grupo exclusivo devolve ao padrão (0 = sem
//   registro) as outras visíveis do grupo. É com ele que o cliente confere se o que pediu foi o que o servidor gravou.
// ---------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "SkinSlots.h"

namespace ProvadorModel
{
    enum ItemState : int { kUnavailable = 0, kDefault = 1, kEquipped = 2, kDisabled = 3, kCovered = 4 };

    // variante do pack que o material tem aplicada: kNoPack = textura original, kMixed = materiais do slot com variantes diferentes
    constexpr int kNoPack = -1;
    constexpr int kMixed  = -2;

    struct Counts
    {
        int total       = 0;     // materiais do slot no DFF carregado
        int hidden      = 0;     // quantos estão ocultos
        int hiddenCover = 0;     // dos ocultos, quantos estão cobertos por outra peça (CoveredByGarments)
        int variant     = kNoPack;
    };

    inline ItemState Classify(const Counts& c)
    {
        if (c.total <= 0) return kUnavailable;
        if (c.hidden == 0)                         // todos visíveis
            return c.variant >= 0 ? kEquipped : kDefault;
        if (c.hidden == c.total)                   // todos ocultos
            return c.hiddenCover == c.total ? kCovered : kDisabled;
        // parcial: parte visível (ex.: pele das pernas sob uma bermuda). Conta como "visível" para o jogador.
        return c.variant >= 0 ? kEquipped : kDefault;
    }

    // "tshirts/1/34" com pasta "tshirts/1" -> 34. Qualquer outra coisa (caminho avulso, texto) -> kNoPack.
    inline int VariantOf(const char* rel, const char* folder)
    {
        if (!rel || !folder) return kNoPack;
        const size_t fl = std::strlen(folder), rl = std::strlen(rel);
        if (rl <= fl + 1 || std::strncmp(rel, folder, fl) != 0 || rel[fl] != '/') return kNoPack;
        const char* tail = rel + fl + 1;
        if (!*tail || std::strlen(tail) > 6) return kNoPack;
        int n = 0;
        for (const char* p = tail; *p; ++p)
        {
            if (*p < '0' || *p > '9') return kNoPack;
            n = n * 10 + (*p - '0');
        }
        return n;
    }

    // ------------------------------------------------------------------ espelho do servidor
    // Um item por id de slot (1..kCount). est: 0 oculto | 1 original | 2 variante. known=false = sem registro (padrão).
    struct Srv
    {
        bool     known = false;
        uint8_t  est   = 0;
        uint16_t var   = 0;
        bool operator==(const Srv& o) const
        {
            if (known != o.known) return false;
            return !known || (est == o.est && var == o.var);
        }
        bool operator!=(const Srv& o) const { return !(*this == o); }
    };
    typedef std::vector<Srv> SrvTable;     // índice = id do slot; [0] não usado

    inline SrvTable NewTable() { return SrvTable((size_t)SkinSlots::kCount + 1); }

    // = Look_PutSlot do servidor. est 255 (LOOK_UNSET_WIRE) volta ao padrão.
    inline void Put(SrvTable& t, int id, int est, int var)
    {
        if (id < 1 || id > SkinSlots::kCount) return;
        if (t.size() < (size_t)SkinSlots::kCount + 1) t.resize((size_t)SkinSlots::kCount + 1);
        if (est == 255) { t[(size_t)id] = Srv(); return; }
        const int g = SkinSlots::GroupOf(SkinSlots::kSlots[id - 1].name);
        if (est > 0 && g >= 0)
        {
            for (int o = 1; o <= SkinSlots::kCount; ++o)
            {
                if (o == id || SkinSlots::GroupOf(SkinSlots::kSlots[o - 1].name) != g) continue;
                Srv& s = t[(size_t)o];
                if (s.known && s.est != 0) s = Srv();      // só as VISÍVEIS voltam ao padrão; as ocultas ficam
            }
        }
        Srv n; n.known = true; n.est = (uint8_t)est; n.var = (uint16_t)(est == 2 ? var : 0);
        t[(size_t)id] = n;
    }

    struct Op { int slotId; int est; int var; };

    // O que o servidor terá DEPOIS de processar `ops` em ordem, partindo de `before`.
    inline SrvTable Simulate(const SrvTable& before, const std::vector<Op>& ops)
    {
        SrvTable t = before;
        for (const Op& o : ops) Put(t, o.slotId, o.est, o.var);
        return t;
    }

    // snapshot (RPC 232, ids com registro) -> tabela. `items` = (id, est, var).
    template <class St>
    inline SrvTable FromSnapshot(const std::vector<St>& items)
    {
        SrvTable t = NewTable();
        for (const St& s : items)
        {
            if (s.id < 1 || s.id > SkinSlots::kCount) continue;
            Srv n; n.known = true; n.est = (uint8_t)s.est; n.var = (uint16_t)(s.est == 2 ? s.var : 0);
            t[(size_t)s.id] = n;
        }
        return t;
    }

    inline bool SameTable(const SrvTable& a, const SrvTable& b)
    {
        for (int i = 1; i <= SkinSlots::kCount; ++i)
        {
            const Srv x = (size_t)i < a.size() ? a[(size_t)i] : Srv();
            const Srv y = (size_t)i < b.size() ? b[(size_t)i] : Srv();
            if (x != y) return false;
        }
        return true;
    }

    // a UI mostra isto quando o servidor tem registro do slot
    inline const char* SrvWord(const Srv& s)
    {
        if (!s.known) return "padrão";
        return s.est == 0 ? "desligado" : (s.est == 1 ? "original" : "variante");
    }
}
