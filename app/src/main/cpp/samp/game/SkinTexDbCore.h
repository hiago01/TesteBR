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
#include <sys/stat.h>
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

        // Substituta para um rel ausente (ex.: outro jogador usa tshirts/3/99 e este banco nao tem): a variante mais proxima
        // da MESMA pasta (numerica; empate -> a menor); sem numericas, o primeiro arquivo da pasta. "" se a pasta nao existe.
        std::string FallbackFor(const std::string& rel) const
        {
            const std::string r = LowerAscii(rel);
            const size_t sl = r.rfind('/');
            if (sl == std::string::npos) return {};
            const std::string folder = r.substr(0, sl), stem = r.substr(sl + 1);
            const std::vector<int> v = Variants(folder);
            if (!v.empty())
            {
                bool dig = !stem.empty() && stem.size() <= 6;
                for (char c : stem) if (!std::isdigit((unsigned char)c)) { dig = false; break; }
                int best = v.front();
                if (dig)
                {
                    const int want = std::atoi(stem.c_str());
                    for (int x : v) if (std::abs(x - want) < std::abs(best - want)) best = x;
                }
                return folder + "/" + std::to_string(best);
            }
            const std::string pre = folder + "/";
            for (const std::string& k : m_all)
                if (k.compare(0, pre.size(), pre) == 0 && k.find('/', pre.size()) == std::string::npos) return k;
            return {};
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
    // ------------------------------------------------------------------------------------------------------------------
    // CONFERENCIA DOS ARQUIVOS DO BANCO antes de entregar ao TextureDatabaseRuntime::Load do libGTASA.
    // O Load nativo faz SIGSEGV em SortEntries se o indice estiver truncado/inconsistente (caso real: .toc/.tmb novos com o
    // .dat antigo, depois de acrescentar texturas). Aqui so se le o .toc (4 bytes por textura) e o .tmb (~90 KB), nunca o .dat.
    //   .toc : uint32 tamanhoDoDat | uint32 offset[N]                      (tamanhoDoDat == tamanho real do .dat)
    //   .tmb : N registros  <HHHHII> hash16,fmt,w,h,tam+4,rle + dados      (tamanho do registro = 12 + campo tam+4)
    // Devolve "" se esta tudo certo; senao a descricao do problema (para o log e para o jogador).
    // ------------------------------------------------------------------------------------------------------------------
    inline long long FileSize(const std::string& p)
    {
        struct stat st {};
        return ::stat(p.c_str(), &st) == 0 ? (long long)st.st_size : -1LL;
    }
    inline bool ReadFileAll(const std::string& p, std::vector<uint8_t>& out)
    {
        out.clear();
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) return false;
        uint8_t b[8192]; size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) out.insert(out.end(), b, b + n);
        std::fclose(f);
        return true;
    }
    inline uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    // conta os registros de um .tmb; -1 se o arquivo nao fecha exatamente em registros inteiros
    inline long long CountTmbRecords(const std::vector<uint8_t>& t)
    {
        size_t pos = 0; long long n = 0;
        while (pos < t.size())
        {
            if (pos + 16 > t.size()) return -1;
            const uint32_t sz = Rd32(&t[pos + 8]);
            if (sz < 4 || sz > (64u << 20)) return -1;
            pos += 12 + (size_t)sz;
            if (pos > t.size()) return -1;
            ++n;
        }
        return n;
    }
    // dir = ".../texdb/skins/"; nTex = linhas do skins.idx. Confere cada formato que existe (dxt/etc/pvr) + skins.unc.tmb.
    inline std::string CheckBankFiles(const std::string& dir, const std::string& name, size_t nTex)
    {
        int formats = 0;
        for (const char* k : { "dxt", "etc", "pvr" })
        {
            const std::string base = dir + name + "." + k;
            const long long dsz = FileSize(base + ".dat");
            if (dsz < 0) continue;                                         // formato ausente: o motor escolhe outro
            ++formats;
            std::vector<uint8_t> toc, tmb;
            if (!ReadFileAll(base + ".toc", toc) || toc.size() < 8 || (toc.size() % 4) != 0)
                return name + "." + k + ".toc ausente ou truncado";
            const long long total = (long long)Rd32(&toc[0]);
            const size_t n = toc.size() / 4 - 1;
            if (total != dsz)
                return name + "." + k + ".dat tem " + std::to_string(dsz) + " bytes mas o .toc espera " + std::to_string(total) +
                       " (faltam " + std::to_string(total - dsz) + "): o .dat nao foi atualizado junto com o .toc/.tmb";
            if (n != nTex)
                return name + "." + k + ".toc tem " + std::to_string(n) + " texturas e o " + name + ".idx tem " + std::to_string(nTex);
            for (size_t i = 0; i < n; ++i)
            {
                const long long o = (long long)Rd32(&toc[4 + i * 4]);
                if (o + 16 > dsz) return name + "." + k + ".toc: offset " + std::to_string(o) + " fora do .dat";
            }
            if (!ReadFileAll(dir + name + "." + k + ".tmb", tmb))
                return name + "." + k + ".tmb ausente";
            const long long c = CountTmbRecords(tmb);
            if (c != (long long)n) return name + "." + k + ".tmb tem " + (c < 0 ? std::string("registros truncados") : std::to_string(c) + " registros") + " mas o .toc tem " + std::to_string(n);
        }
        if (formats == 0) return "nenhum " + name + ".{dxt,etc,pvr}.dat encontrado";
        std::vector<uint8_t> unc;
        if (ReadFileAll(dir + name + ".unc.tmb", unc))
        {
            const long long c = CountTmbRecords(unc);
            if (c != (long long)nTex) return name + ".unc.tmb tem " + (c < 0 ? std::string("registros truncados") : std::to_string(c) + " registros") + " mas o " + name + ".idx tem " + std::to_string(nTex);
        }
        return {};
    }
}
