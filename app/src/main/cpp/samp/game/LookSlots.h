// LookSlots.h - visual por SLOTS (RPC 232) por jogador. Codigo PURO (sem RenderWare/RakNet): compila e e testado no host.
//
//   Rede (servidor -> cliente), igual ao maximo_look.inc:
//     uint16 playerid | uint8 version(=1) | uint8 flags (bit0 = limpar tudo) | uint8 n |
//     n x [ uint8 slotId | uint8 estado | (se estado==2) uint16 variante ]
//     estado: 0 = oculto | 1 = textura original | 2 = variante do pack (pack/<pasta>/<variante>.png)
//   slotId = indice em SkinSlots::kSlots + 1 (estavel). Slot ausente do snapshot = NAO TOCAR (padrao do molde).
//
//   Plan() transforma o snapshot em operacoes por MATERIAL (nome do DFF) e ja aplica a regra de cobertura do /skin7
//   (camisa/calca/bermuda/tenis visiveis escondem a pele/roupa intima que cobrem; chinelo nao cobre).
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "SkinSlots.h"

namespace LookSlots
{
    constexpr int kVersion  = 1;
    constexpr int kMaxSlots = 80;      // LOOK_MAX_SLOTS do servidor
    constexpr int kMaxVar   = 1023;    // LOOK_MAXVAR_DEFAULT do servidor

    enum Kind { kHide = 0, kOrig = 1, kPack = 2 };

    struct St
    {
        uint8_t  id  = 0;
        uint8_t  est = 0;
        uint16_t var = 0;
        bool operator==(const St& o) const { return id == o.id && est == o.est && var == o.var; }
    };

    struct Op { std::string mat; int slot = 0; int kind = kHide; std::string rel; };   // rel = "pasta/n.png" (kPack)

    inline std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

    inline std::vector<std::string> Split(const char* list)
    {
        std::vector<std::string> v; std::string cur;
        for (const char* p = list; ; ++p)
        {
            if (*p == ',' || *p == 0) { if (!cur.empty()) v.push_back(Lower(cur)); cur.clear(); if (!*p) break; }
            else cur.push_back(*p);
        }
        return v;
    }

    // Le o pacote ja posicionado depois do cabecalho do RPC. BS precisa de bool Read(T&) para uint8_t e uint16_t.
    // Retorna false se o pacote for invalido (nada deve ser aplicado). Ids desconhecidos sao descartados.
    template <class BS>
    bool Parse(BS& bs, uint16_t& pid, std::vector<St>& out, std::string& err)
    {
        out.clear();
        uint8_t ver = 0, flags = 0, n = 0;
        if (!bs.Read(pid))                  { err = "sem playerid"; return false; }
        if (!bs.Read(ver) || ver != kVersion) { err = "versao " + std::to_string((int)ver) + " nao suportada"; return false; }
        if (!bs.Read(flags) || !bs.Read(n)) { err = "cabecalho incompleto"; return false; }
        if (n > kMaxSlots)                  { err = "n=" + std::to_string((int)n) + " acima do limite"; return false; }
        for (int i = 0; i < n; ++i)
        {
            uint8_t id = 0, est = 0; uint16_t var = 0;
            if (!bs.Read(id) || !bs.Read(est)) { err = "slot " + std::to_string(i) + " incompleto"; out.clear(); return false; }
            if (est > 2)                       { err = "estado " + std::to_string((int)est) + " invalido"; out.clear(); return false; }
            if (est == 2 && !bs.Read(var))     { err = "variante ausente"; out.clear(); return false; }
            if (id < 1 || id > SkinSlots::kCount || var > kMaxVar) continue;          // desconhecido: ignora (compat futura)
            if (est == 2) var = (uint16_t)SkinSlots::CanonVar(SkinSlots::kSlots[id - 1], var);   // rosto: formato antigo -> tom*100+rosto
            bool dup = false;
            for (St& s : out) if (s.id == id) { s.est = est; s.var = (est == 2) ? var : 0; dup = true; break; }
            if (!dup) out.push_back(St{id, est, (uint16_t)(est == 2 ? var : 0)});
        }
        if (flags & 1) out.clear();                                                      // limpar tudo
        std::sort(out.begin(), out.end(), [](const St& a, const St& b) { return a.id < b.id; });
        return true;
    }

    inline void Plan(const std::vector<St>& st, std::vector<Op>& ops)
    {
        ops.clear();
        for (const St& s : st)
        {
            if (s.id < 1 || s.id > SkinSlots::kCount) continue;
            const SkinSlots::Slot& sl = SkinSlots::kSlots[s.id - 1];
            for (const std::string& name : Split(sl.texture))
            {
                Op o; o.mat = name; o.slot = s.id;
                if      (s.est == 0) o.kind = kHide;
                else if (s.est == 1) o.kind = kOrig;
                else { o.kind = kPack; o.rel = SkinSlots::VariantRel(sl, (int)s.var) + ".png"; }
                ops.push_back(std::move(o));
            }
        }

        // cobertura: pecas visiveis (estado != 0) que cobrem por inteiro uma regiao escondem a regiao
        std::vector<const SkinSlots::Box*> vis;
        for (const St& s : st)
        {
            if (s.est == 0 || s.id < 1 || s.id > SkinSlots::kCount) continue;
            const char* tex = SkinSlots::kSlots[s.id - 1].texture;
            for (const SkinSlots::Box& p : SkinSlots::kCoverPieces)
                if (std::strlen(tex) == std::strlen(p.name) && SkinSlots::EqNoCase(tex, p.name, std::strlen(p.name))) vis.push_back(&p);
        }
        for (const SkinSlots::Box& t : SkinSlots::kCoverTargets)
        {
            const std::string tn = Lower(t.name);
            for (Op& o : ops)
            {
                if (o.mat != tn || o.kind == kHide) continue;
                for (const SkinSlots::Box* p : vis)
                    if (SkinSlots::PieceCovers(*p, t)) { o.kind = kHide; break; }
            }
        }
    }

    // Visual PADRAO de um jogador novo (= Look_ApplyDefault do maximo_look.inc): corpo + rosto/sobrancelha/cabelo/intima, variante 1.
    inline std::vector<St> DefaultSlots()
    {
        static const char* const names[] = { "skin_torso", "skin_leg", "skin_feet", "head1", "brow1", "hair1", "underwear" };
        std::vector<St> v;
        for (const char* n : names)
            for (int i = 0; i < SkinSlots::kCount; ++i)
                if (std::strcmp(SkinSlots::kSlots[i].name, n) == 0) { v.push_back(St{(uint8_t)(i + 1), (uint8_t)kPack, (uint16_t)SkinSlots::CanonVar(SkinSlots::kSlots[i], 1)}); break; }
        std::sort(v.begin(), v.end(), [](const St& a, const St& b) { return a.id < b.id; });
        return v;
    }

    // Plan() + ESTADO PADRAO EXPLICITO dos slots que o snapshot NAO cita: "slot ausente = padrao" no servidor significa
    // "invisivel" (o DFF do modelo 7 so mostra male01), mas o MOLDE compartilhado guarda a roupa do jogador LOCAL; sem isto o
    // ped remoto herdava a roupa de quem esta olhando (todos "com a mesma roupa"). So ops para materiais que nenhum slot citado
    // ja trata (slots que dividem material, ex.: sobrancelha/barba, nao se anulam).
    inline void PlanWithDefaults(const std::vector<St>& st, std::vector<Op>& ops)
    {
        Plan(st, ops);
        std::vector<std::string> done;
        for (const Op& o : ops) done.push_back(o.mat);
        for (int id = 1; id <= SkinSlots::kCount; ++id)
        {
            bool listed = false;
            for (const St& s : st) if (s.id == id) { listed = true; break; }
            if (listed) continue;
            for (const std::string& name : Split(SkinSlots::kSlots[id - 1].texture))
            {
                if (std::find(done.begin(), done.end(), name) != done.end()) continue;
                Op o; o.mat = name; o.slot = id; o.kind = kHide;
                ops.push_back(std::move(o));
                done.push_back(name);
            }
        }
    }
}
