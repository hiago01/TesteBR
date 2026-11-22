#pragma once

// ---------------------------------------------------------------------------
// Banco de texturas do MENU JOGADOR (texdb/perfil/): 2 banners ("perfil_grupos" e "perfil_vip"), lidos pelo MOTOR NATIVO,
// pasta separada dos bancos de skins e icones (mesmo formato; mesma ideia do texdb/armas). Copia de IconTexDb.h so com outro nome de banco.
//   <raiz>/texdb/perfil/perfil.txt | perfil.idx | perfil.{dxt,etc}.{dat,tmb,toc} | perfil.unc.tmb
//   Gerar:  python3 tools/perfil_texdb.py build <banner_grupos.png> <banner_vip.png> <saida>
// Banco ausente: Texture() devolve nullptr e o menu desenha o banner so com cor e texto (nunca derruba o jogo).
// Texture() devolve o ponteiro do cache (SEM +1 de refCount): nao solte.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <map>
#include <string>
#include <unistd.h>

#include "../main.h"          // g_pszStorage
#include "SkinTexDbCore.h"
#include "Textures/TextureDatabaseRuntime.h"
#include "../util/CUtil.h"

namespace PerfilTexDb
{
    inline constexpr const char* kDbName = "perfil";

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
            return p + "texdb/perfil/";
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
        if (!Exists(d + "perfil.txt") || !Exists(d + "perfil.idx")) { Msg_() = "texdb/perfil nao encontrado (banners do Menu Jogador desligados)"; return false; }
        std::string text;
        if (FILE* f = std::fopen((d + "perfil.idx").c_str(), "rb"))
        {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const size_t n = Idx().Parse(text);
        if (n == 0) { Msg_() = "perfil.idx vazio ou ilegivel; banco ignorado"; return false; }
        TextureDatabaseRuntime* db = TextureDatabaseRuntime::Load(kDbName, false, TextureDatabaseFormat::DF_Default);
        if (!db) { Msg_() = "TextureDatabaseRuntime::Load(\"perfil\") FALHOU (confira os arquivos .dat/.tmb/.toc)"; return false; }
        Ready_() = true;
        Msg_() = std::to_string(n) + " banners no banco";
        return true;
    }

    inline std::string Status()
    {
        using namespace detail;
        if (!Tried_()) return "texdb/perfil: ainda nao carregado";
        return std::string("texdb/perfil: ") + (Ready_() ? "ATIVO, " : "inativo: ") + Msg_();
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
