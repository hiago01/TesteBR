#pragma once

// ---------------------------------------------------------------------------
// Banco "armas" (texdb/armas/): os icones NATIVOS das armas do GTA ("<modelo>icon": ak47icon, colt45icon, m4icon...) SEM a moldura
// preta arredondada do original (so a arma, com o proprio contorno). O inventario / slots rapidos procuram aqui primeiro
// (GetGtaWeaponIcon em inventory.cpp); banco ausente ou arma sem textura aqui = usa o icone original do jogo (nada quebra).
//   <raiz>/texdb/armas/armas.{txt,idx,dxt.*,etc.*,unc.tmb}      (gerado por tools/weapon_icons_noframe.py)
// Texture() devolve o ponteiro do cache (SEM +1 de refCount); chamar so na thread de desenho/jogo.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <map>
#include <string>
#include <unistd.h>

#include "../main.h"          // g_pszStorage
#include "SkinTexDbCore.h"
#include "Textures/TextureDatabaseRuntime.h"
#include "../util/CUtil.h"

namespace WeaponIconDb
{
    inline constexpr const char* kDbName = "armas";

    namespace detail
    {
        inline SkinTexDbCore::Index& Idx() { static SkinTexDbCore::Index i; return i; }
        inline bool& Ready_() { static bool r = false; return r; }
        inline bool& Tried_() { static bool t = false; return t; }
        inline std::string& Msg_() { static std::string m; return m; }
        inline std::map<std::string, RwTexture*>& Cache() { static std::map<std::string, RwTexture*> c; return c; }
        inline std::map<std::string, bool>& Failed() { static std::map<std::string, bool> f; return f; }
        inline std::string DbDir()
        {
            if (!g_pszStorage || !g_pszStorage[0]) return {};
            std::string p(g_pszStorage);
            if (p.back() != '/') p.push_back('/');
            return p + "texdb/armas/";
        }
        inline bool Exists(const std::string& p) { return !p.empty() && ::access(p.c_str(), R_OK) == 0; }
    }

    inline bool Ready() { return detail::Ready_(); }

    // Chamar UMA vez na thread do jogo, logo depois de SkinTexDb::Load() (InitialiseRenderWare).
    inline bool Load()
    {
        using namespace detail;
        if (Tried_()) return Ready_();
        Tried_() = true;
        const std::string d = DbDir();
        if (d.empty()) { Msg_() = "sem raiz de storage"; return false; }
        if (!Exists(d + "armas.txt") || !Exists(d + "armas.idx")) { Msg_() = "texdb/armas nao encontrado (segue com os icones originais do jogo)"; return false; }
        std::string text;
        if (FILE* f = std::fopen((d + "armas.idx").c_str(), "rb"))
        {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const size_t n = Idx().Parse(text);
        if (n == 0) { Msg_() = "armas.idx vazio ou ilegivel; banco ignorado"; return false; }
        TextureDatabaseRuntime* db = TextureDatabaseRuntime::Load(kDbName, false, TextureDatabaseFormat::DF_Default);
        if (!db) { Msg_() = "TextureDatabaseRuntime::Load(\"armas\") FALHOU (confira os arquivos .dat/.tmb/.toc)"; return false; }
        Ready_() = true;
        Msg_() = std::to_string(n) + " icones de arma no banco";
        return true;
    }

    inline std::string Status()
    {
        using namespace detail;
        if (!Tried_()) return "texdb/armas: ainda nao carregado";
        return std::string("texdb/armas: ") + (Ready_() ? "ATIVO, " : "inativo: ") + Msg_();
    }

    inline bool Has(const std::string& name) { return detail::Ready_() && detail::Idx().Has(name); }

    // Textura do icone 'name' (minusculo, ex.: "agua_1"). nullptr se nao existe/falhou. Chamar so na thread de desenho/jogo.
    inline RwTexture* Texture(const std::string& name)
    {
        using namespace detail;
        if (!Ready_()) return nullptr;
        const std::string key = SkinTexDbCore::LowerAscii(name);
        auto c = Cache().find(key);
        if (c != Cache().end()) return c->second;
        if (Failed().count(key)) return nullptr;
        const std::string nm = Idx().Name(key);
        RwTexture* t = nm.empty() ? nullptr : CUtil::LoadTextureFromDB(kDbName, nm.c_str());   // vem com +1: fica como a ref permanente do cache
        if (!t || !t->raster) { Failed()[key] = true; return nullptr; }
        Cache()[key] = t;
        return t;
    }
}
