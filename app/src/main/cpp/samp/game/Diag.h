#pragma once
// ---------------------------------------------------------------------------
// DIAG: liga/desliga modulos customizados para ISOLAR a causa de um bug (bissecao).
// Arquivo:  <files>/SAMP/diag.ini   (se ainda nao ha g_pszStorage usa o caminho padrao do app)
//     [diag]
//     all=0          ; 0 = tudo desligado por padrao (vale para as chaves nao citadas) ; 1 = tudo ligado (padrao sem arquivo)
//     touch=1        ; touch.img (carros/skins customizados) + bypass do MINFO.BIN
//     ipl=1          ; IPL por instancia (texdb/ipl)
//     col=1          ; mistura de COL + guarda do CINFO.BIN
//     dff=1          ; DFF por entrada (texdb/DFF) + hook de GetTexture
//     skin=1         ; /skin7 (CCustomSkin)
//     clothes=1      ; CJ real (roupas/tatuagens, RPC)
//     anim=1         ; animacoes customizadas (RPC 230 + poses.ifp)
//     corptex=1      ; textura por corporacao (RPC 231)
//     radarskin=1    ; radar/minimapa Google (banco mapa / PNG)
// SEM arquivo = tudo ligado (comportamento de sempre). Mudancas valem ao REINICIAR o jogo (os ganchos sao instalados na abertura).
// ---------------------------------------------------------------------------
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <map>
#include "../main.h"
#include "../log.h"

namespace Diag
{
    inline constexpr const char* kKeys[] = { "touch", "ipl", "col", "dff", "skin", "clothes", "anim", "corptex", "radarskin" };

    struct State { bool loaded = false, fileFound = false; bool on[9]; std::string path; };
    inline State& S() { static State s; return s; }

    inline std::string IniPath()
    {
        if (g_pszStorage && g_pszStorage[0]) { std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/'); return p + "SAMP/diag.ini"; }
        return "/storage/emulated/0/Android/data/com.samp.mobile/files/SAMP/diag.ini";
    }

    inline void Load()
    {
        State& s = S();
        if (s.loaded) return;
        s.loaded = true;
        for (bool& b : s.on) b = true;
        s.path = IniPath();
        FILE* f = std::fopen(s.path.c_str(), "r");
        if (!f) { FLog("[DIAG] sem %s: tudo ligado", s.path.c_str()); return; }
        s.fileFound = true;
        char line[160]; bool allSet = false, inDiag = true;
        std::map<std::string, int> v;
        while (std::fgets(line, sizeof(line), f))
        {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
            size_t c = l.find(';'); if (c != std::string::npos) l.erase(c);
            while (!l.empty() && l.back() == ' ') l.pop_back();
            if (l.empty()) continue;
            if (l[0] == '[') { inDiag = (l == "[diag]"); continue; }
            if (!inDiag) continue;
            const size_t eq = l.find('='); if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), val = l.substr(eq + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            for (char& ch : k) ch = (char)std::tolower((unsigned char)ch);
            const int n = std::atoi(val.c_str());
            if (k == "all") { allSet = true; for (bool& b : s.on) b = n != 0; }
            else v[k] = n;
        }
        std::fclose(f);
        (void)allSet;
        for (size_t i = 0; i < 9; i++) { auto it = v.find(kKeys[i]); if (it != v.end()) s.on[i] = it->second != 0; }
        std::string off, on;
        for (size_t i = 0; i < 9; i++) (s.on[i] ? on : off) += std::string(kKeys[i]) + " ";
        FLog("[DIAG] %s | LIGADOS: %s| DESLIGADOS: %s", s.path.c_str(), on.c_str(), off.c_str());
    }

    inline bool On(const char* key)
    {
        Load();
        for (size_t i = 0; i < 9; i++) if (std::strcmp(kKeys[i], key) == 0) return S().on[i];
        return true;
    }

    inline std::string Summary()
    {
        Load();
        std::string on, off;
        for (size_t i = 0; i < 9; i++) (S().on[i] ? on : off) += std::string(kKeys[i]) + " ";
        return std::string("[DIAG] ") + (S().fileFound ? "diag.ini lido" : "sem diag.ini (tudo ligado)") + " | LIGADOS: " + (on.empty() ? "-" : on) + "| DESLIGADOS: " + (off.empty() ? "-" : off) + "| (mudou o arquivo? reinicie o jogo)";
    }
}
