#pragma once

// ---------------------------------------------------------------------------
// Banco de texturas "skins" (texdb/skins/) lido pelo MOTOR NATIVO (mesmo formato do banco do mapa).
//
//   <raiz>/texdb/skins/skins.txt                    lista de texturas
//   <raiz>/texdb/skins/skins.{dxt,etc}.{dat,tmb,toc} + skins.unc.tmb   dados (DXT1/DXT5, ETC1/RGBA4444)
//   <raiz>/texdb/skins/skins.idx                    pasta/arquivo -> nome da textura (so do Maximo RP)
//   Gerar:  python3 tools/skin_texdb.py build <SAMP/skins/pack> <saida/texdb>      (veja MAXIMO_OS... / LEIA-ME_SKINS_TEXDB.md)
//
// COMO FUNCIONA
//   1. CGame::InitialiseRenderWare chama SkinTexDb::Load() (logo depois do banco "touch"): se skins.txt + skins.idx existem,
//      TextureDatabaseRuntime::Load("skins", false, DF_Default) carrega so o INDICE do banco (texturas sob demanda).
//   2. Quem precisa de uma variante do pack (/skin7 slot, CorpTex, /provador) pergunta primeiro Has(rel)/Variants(pasta):
//      se o banco tem, Texture(rel) devolve o RwTexture do motor; se nao, tudo segue como antes (PNG solto em SAMP/skins/pack).
//   3. As referencias: Texture() devolve a textura com +1 de refCount (a "ref de criacao" do chamador, igual a CreateTextureRgba)
//      e o cache aqui guarda UMA ref permanente: a textura do banco nunca chega a 0 enquanto o jogo roda.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../main.h"          // g_pszStorage
#include "SkinTexDbCore.h"
#include "Textures/TextureDatabaseRuntime.h"
#include "../util/CUtil.h"

namespace SkinTexDb
{
    inline constexpr const char* kDbName = "skins";

    namespace detail
    {
        inline SkinTexDbCore::Index& Idx() { static SkinTexDbCore::Index i; return i; }
        inline bool& Ready_() { static bool r = false; return r; }
        inline bool& Tried_() { static bool t = false; return t; }
        inline std::string& Msg_() { static std::string m; return m; }
        inline std::map<std::string, RwTexture*>& Cache() { static std::map<std::string, RwTexture*> c; return c; }
        inline std::map<std::string, bool>& Failed() { static std::map<std::string, bool> f; return f; }

        inline std::string Root()
        {
            if (!g_pszStorage || !g_pszStorage[0]) return {};
            std::string p(g_pszStorage);
            if (p.back() != '/') p.push_back('/');
            return p;
        }
        inline std::string DbDir() { const std::string r = Root(); return r.empty() ? r : r + "texdb/skins/"; }
        inline bool Exists(const std::string& p) { return !p.empty() && ::access(p.c_str(), R_OK) == 0; }
    }

    // true = banco carregado e indice lido (usar Has/Variants/Texture)
    inline bool Ready() { return detail::Ready_(); }

    // Chamar UMA vez, na thread do jogo, depois que o motor registrou os bancos nativos (InitialiseRenderWare).
    inline bool Load()
    {
        using namespace detail;
        if (Tried_()) return Ready_();
        Tried_() = true;
        const std::string d = DbDir();
        if (d.empty()) { Msg_() = "sem raiz de storage"; return false; }
        if (!Exists(d + "skins.txt"))  { Msg_() = "texdb/skins/skins.txt nao encontrado (banco ignorado; segue o pack em PNG)"; return false; }
        if (!Exists(d + "skins.idx"))  { Msg_() = "texdb/skins/skins.idx nao encontrado (gere com tools/skin_texdb.py); banco ignorado"; return false; }
        std::string text;
        if (FILE* f = std::fopen((d + "skins.idx").c_str(), "rb"))
        {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const size_t n = Idx().Parse(text);
        if (n == 0) { Msg_() = "skins.idx vazio ou ilegivel; banco ignorado"; return false; }
        TextureDatabaseRuntime* db = TextureDatabaseRuntime::Load(kDbName, false, TextureDatabaseFormat::DF_Default);
        if (!db) { Msg_() = "TextureDatabaseRuntime::Load(\"skins\") FALHOU (confira os arquivos .dat/.tmb/.toc do formato da GPU)"; return false; }
        Ready_() = true;
        Msg_() = std::to_string(n) + " texturas no banco skins";
        return true;
    }

    // GPU do aparelho (precisa de contexto GL na thread atual; sem contexto devolve o aviso). Mostra o que a GPU aceita.
    inline std::string GpuInfo()
    {
        if (eglGetCurrentContext() == EGL_NO_CONTEXT) return "GPU: sem contexto GL nesta thread (rode /skin7 texdb gpu no jogo)";
        auto S = [](GLenum n) { const GLubyte* p = glGetString(n); return std::string(p ? (const char*)p : ""); };
        const std::string ext = S(GL_EXTENSIONS);
        auto has = [&](const char* k) { return ext.find(k) != std::string::npos; };
        const bool dxt = has("GL_EXT_texture_compression_dxt1") || has("GL_EXT_texture_compression_s3tc");
        const bool etc = has("GL_OES_compressed_ETC1_RGB8_texture");
        const bool pvr = has("GL_IMG_texture_compression_pvrtc");
        const bool atc = has("GL_AMD_compressed_ATC_texture") || has("GL_ATI_texture_compression_atitc");
        return "GPU: " + S(GL_RENDERER) + " | DXT " + (dxt ? "SIM" : "nao") + " | ETC1 " + (etc ? "SIM" : "nao") +
               " | PVRTC " + (pvr ? "SIM" : "nao") + " | ATC " + (atc ? "SIM" : "nao") +
               (dxt || etc ? "" : " | ATENCAO: nem DXT nem ETC1 (o banco nao serve nesta GPU)");
    }

    // quais arquivos de dados existem (o motor escolhe um conforme a GPU)
    inline std::string Files()
    {
        const std::string d = detail::DbDir();
        auto sz = [&](const char* f) { struct stat st{}; return ::stat((d + f).c_str(), &st) == 0 ? (long long)st.st_size / 1048576 : -1LL; };
        char b[160];
        std::snprintf(b, sizeof(b), "arquivos: dxt.dat %lld MB, etc.dat %lld MB (-1 = ausente)", sz("skins.dxt.dat"), sz("skins.etc.dat"));
        return b;
    }

    inline std::string Status()
    {
        using namespace detail;
        if (!Tried_()) return "texdb/skins: ainda nao carregado";
        return std::string("texdb/skins: ") + (Ready_() ? "ATIVO, " : "inativo: ") + Msg_();
    }

    inline bool Has(const std::string& rel) { return detail::Ready_() && detail::Idx().Has(rel); }
    inline std::vector<int> Variants(const std::string& folder) { return detail::Ready_() ? detail::Idx().Variants(folder) : std::vector<int>(); }
    inline std::vector<std::string> List(const std::string& prefix) { return detail::Ready_() ? detail::Idx().WithPrefix(prefix) : std::vector<std::string>(); }
    inline std::vector<std::string> SubFolders(const std::string& prefix) { return detail::Ready_() ? detail::Idx().SubFolders(prefix) : std::vector<std::string>(); }
    inline std::string FallbackRel(const std::string& rel) { return detail::Ready_() ? detail::Idx().FallbackFor(rel) : std::string(); }
    inline size_t Count() { return detail::Ready_() ? detail::Idx().Size() : 0; }

    // Textura do banco para 'rel' (ex.: "tshirts/1/34"). nullptr se nao existe/falhou. O chamador recebe +1 de refCount
    // (solte com o mesmo TexRelease que usa para texturas criadas por PNG).
    inline RwTexture* Texture(const std::string& rel)
    {
        using namespace detail;
        if (!Ready_()) return nullptr;
        const std::string key = SkinTexDbCore::LowerAscii(rel);
        auto c = Cache().find(key);
        if (c != Cache().end()) { ++c->second->refCount; return c->second; }
        if (Failed().count(key)) return nullptr;
        const std::string nm = Idx().Name(key);
        RwTexture* t = nm.empty() ? nullptr : CUtil::LoadTextureFromDB(kDbName, nm.c_str());   // devolve com +1 (fica como a ref permanente do cache)
        if (!t) { Failed()[key] = true; return nullptr; }
        Cache()[key] = t;
        ++t->refCount;                                                                           // +1 para o chamador
        return t;
    }

    // Para aparencia de OUTROS jogadores: se 'rel' nao existe/falha neste aparelho, usa a variante mais proxima da mesma
    // pasta (nunca derruba o jogo; sem substituta -> nullptr e o item fica como esta). Nao usar em comandos do proprio jogador.
    inline RwTexture* TextureOrFallback(const std::string& rel, bool* usedFallback = nullptr)
    {
        if (usedFallback) *usedFallback = false;
        if (RwTexture* t = Has(rel) ? Texture(rel) : nullptr) return t;
        const std::string fb = FallbackRel(rel);
        if (fb.empty() || fb == SkinTexDbCore::LowerAscii(rel)) return nullptr;
        RwTexture* t = Texture(fb);
        if (t)
        {
            static int logged = 0;
            if (logged++ < 20) FLog("[SKINTEXDB] '%s' ausente: usando '%s' no lugar", rel.c_str(), fb.c_str());
            if (usedFallback) *usedFallback = true;
        }
        return t;
    }
}
