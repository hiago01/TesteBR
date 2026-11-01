#pragma once

// ---------------------------------------------------------------------------
// Banco de texturas "skins" (texdb/skins/) - parte PURA (sem RenderWare/engine): compila e e testada no host.
//   Regra de nomes e indice de variantes do pack. Gerado por tools/skin_texdb.py (mesma regra de nomes: testes/skintexdb).
//
//   pack/<pasta>/<n>.png  <->  textura "<pasta>_<n>" no banco texdb/skins   (ex.: tshirts/1/34 -> "tshirts_1_34")
//   texdb/skins/skins.idx  "pasta/arquivo<TAB>nome-no-banco" (uma linha por textura; o nome pode ter sufixo "~k" se o hash
//                           de 16 bits colidiu: por isso o cliente SEMPRE usa o indice, nunca recalcula o nome sozinho)
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace SkinTexDbCore
{
    constexpr size_t kMaxName = 31;     // RwTexture.name tem 32 bytes

    inline std::string LowerAscii(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return s;
    }

    inline uint32_t Crc32(const std::string& s)
    {
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned char ch : s)
        {
            c ^= ch;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }

    // Mesma regra de skin_texdb.py::db_name: minusculo; '/', '\\', '.', '-', ' ' viram '_'; > 31: 22 + '_' + crc32 (8 hex)
    inline std::string DbName(const std::string& rel)
    {
        std::string s = LowerAscii(rel);
        for (char& c : s) if (c == '/' || c == '\\' || c == '.' || c == '-' || c == ' ') c = '_';
        if (s.size() > kMaxName)
        {
            char hx[16];
            std::snprintf(hx, sizeof(hx), "%08x", Crc32(LowerAscii(rel)));
            s = s.substr(0, 22) + "_" + hx;
        }
        return s;
    }

    // rel valido: relativo, sem "..", sem barra invertida
    inline bool ValidRel(const std::string& rel)
    {
        return !rel.empty() && rel[0] != '/' && rel.find("..") == std::string::npos && rel.find('\\') == std::string::npos;
    }

    class Index
    {
    public:
        // Le o texto do skins.idx. Retorna o numero de entradas validas. Linhas ruins sao ignoradas.
        size_t Parse(const std::string& text)
        {
            m_map.clear(); m_all.clear();
            size_t pos = 0;
            while (pos <= text.size())
            {
                size_t e = text.find('\n', pos);
                if (e == std::string::npos) e = text.size();
                std::string ln = text.substr(pos, e - pos);
                pos = e + 1;
                while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
                const size_t tab = ln.find('\t');
                if (tab == std::string::npos || tab == 0 || tab + 1 >= ln.size()) continue;
                const std::string rel = LowerAscii(ln.substr(0, tab)), nm = ln.substr(tab + 1);
                if (!ValidRel(rel) || nm.size() > kMaxName) continue;
                if (m_map.emplace(rel, nm).second) m_all.push_back(rel);
                if (pos > text.size()) break;
            }
            std::sort(m_all.begin(), m_all.end());
            return m_map.size();
        }
        bool Has(const std::string& rel) const { return m_map.count(LowerAscii(rel)) != 0; }
        std::string Name(const std::string& rel) const { auto it = m_map.find(LowerAscii(rel)); return it == m_map.end() ? std::string() : it->second; }
        size_t Size() const { return m_map.size(); }

        // Variantes numericas "<n>" diretamente em <folder> (ex.: folder "tshirts/1" -> {1,2,34}); ordem crescente, sem repetir.
        std::vector<int> Variants(const std::string& folder) const
        {
            const std::string pre = LowerAscii(folder) + "/";
            std::set<int> out;
            for (const std::string& rel : m_all)
            {
                if (rel.compare(0, pre.size(), pre) != 0) continue;
                const std::string rest = rel.substr(pre.size());
                if (rest.empty() || rest.size() > 6 || rest.find('/') != std::string::npos) continue;
                bool dig = true;
                for (char c : rest) if (!std::isdigit((unsigned char)c)) { dig = false; break; }
                if (dig) out.insert(std::atoi(rest.c_str()));
            }
            return std::vector<int>(out.begin(), out.end());
        }

        // Todos os rel que comecam com 'prefix' (ex.: "corp/policia/"), em ordem.
        std::vector<std::string> WithPrefix(const std::string& prefix) const
        {
            const std::string pre = LowerAscii(prefix);
            std::vector<std::string> out;
            for (const std::string& rel : m_all) if (rel.compare(0, pre.size(), pre) == 0) out.push_back(rel);
            return out;
        }
        // Subpastas diretas de 'prefix' (ex.: "corp/" -> {"policia", ...}).
        std::vector<std::string> SubFolders(const std::string& prefix) const
        {
            const std::string pre = LowerAscii(prefix);
            std::set<std::string> out;
            for (const std::string& rel : m_all)
            {
                if (rel.compare(0, pre.size(), pre) != 0) continue;
                const size_t s = rel.find('/', pre.size());
                if (s != std::string::npos) out.insert(rel.substr(pre.size(), s - pre.size()));
            }
            return std::vector<std::string>(out.begin(), out.end());
        }
    private:
        std::map<std::string, std::string> m_map;   // rel minusculo -> nome no banco
        std::vector<std::string> m_all;             // rels em ordem
    };
}
