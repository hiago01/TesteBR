#pragma once

// ---------------------------------------------------------------------------
// Banco de texturas dos ICONES do inventario (texdb/icones/), lido pelo MOTOR NATIVO - pasta separada do banco de skins.
//
//   <raiz>/texdb/icones/icones.txt                    lista de texturas
//   <raiz>/texdb/icones/icones.{dxt,etc}.{dat,tmb,toc} + icones.unc.tmb   dados (DXT1/DXT5, ETC1/RGBA4444)
//   <raiz>/texdb/icones/icones.idx                    "nome<TAB>nome-no-banco"
//   Gerar:  python3 tools/icon_texdb.py build <pasta_icones> <saida>      (veja tools/LEIA-ME_ICONES.md)
//
// O servidor (painel) escolhe o NOME do icone de cada item; o cliente procura a textura aqui. Banco ausente, vazio ou sem o nome:
// Texture() devolve nullptr e o inventario usa o icone padrao do atlas (nunca derruba o jogo).
// Texture() devolve o ponteiro do cache (SEM +1 de refCount): o cache guarda uma ref permanente; nao solte.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <map>
#include <string>
#include <unistd.h>

#include "../main.h"          // g_pszStorage
#include "SkinTexDbCore.h"
#include "Textures/TextureDatabaseRuntime.h"
#include "../util/CUtil.h"

namespace IconTexDb
{
    inline constexpr const char* kDbName = "icones";

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
            return p + "texdb/icones/";
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
        if (!Exists(d + "icones.txt") || !Exists(d + "icones.idx")) { Msg_() = "texdb/icones nao encontrado (icones novos desligados; segue o atlas)"; return false; }
        std::string text;
        if (FILE* f = std::fopen((d + "icones.idx").c_str(), "rb"))
        {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const size_t n = Idx().Parse(text);
        if (n == 0) { Msg_() = "icones.idx vazio ou ilegivel; banco ignorado"; return false; }
        TextureDatabaseRuntime* db = TextureDatabaseRuntime::Load(kDbName, false, TextureDatabaseFormat::DF_Default);
        if (!db) { Msg_() = "TextureDatabaseRuntime::Load(\"icones\") FALHOU (confira os arquivos .dat/.tmb/.toc)"; return false; }
        Ready_() = true;
        Msg_() = std::to_string(n) + " icones no banco";
        return true;
    }

    inline std::string Status()
    {
        using namespace detail;
        if (!Tried_()) return "texdb/icones: ainda nao carregado";
        return std::string("texdb/icones: ") + (Ready_() ? "ATIVO, " : "inativo: ") + Msg_();
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
