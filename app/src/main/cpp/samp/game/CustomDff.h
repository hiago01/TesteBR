#pragma once

// ---------------------------------------------------------------------------
// DFF por ENTRADA  (texdb/DFF/*.dff)  -  mesma ideia da mistura do COL v3, para modelos
//
// COMO FUNCIONA
//   O jogo carrega todo modelo (DFF) por CStreaming::ConvertBufferToObject(buf, modelId, x): le os
//   setores do IMG para o buffer de streaming e converte. O hook de CustomSkinStream.cpp (Convert_hook)
//   chama este modulo: se existir  texdb/DFF/<nome-do-modelo>.dff  o buffer entregue ao jogo passa a ser
//   o do SEU arquivo (alocado com o tamanho REAL do arquivo, nao com o tamanho da entrada do IMG).
//   Nenhuma outra entrada do IMG e' tocada: e' "troca por entrada", o resto continua vindo do IMG.
//
//   O NOME do arquivo (sem .dff) e' o nome do modelo no IDE, ex.: laehospital1.dff -> modelo 5403.
//   A ligacao e' feita pelo hash do nome (CKeyGen::GetUppercaseKey == CBaseModelInfo::m_nKey), igual
//   ao que CustomImg.h ja faz.
//
// ONDE COLOCAR (dentro de Android/data/com.maximo.roleplay/files/), ordem de precedencia crescente:
//     texdb/DFF/*.dff     <- principal
//     texdb/dff/*.dff     <- variante minuscula (Android diferencia maiusculas)
//     SAMP/dff/*.dff
//   Nome repetido: o ULTIMO vence (vai para o log).
//
// LIMITES (todos configurados aqui, nada fixo no IMG):
//     kMaxDffBytes  = 32 MB por arquivo        (o IMG v2 guarda o tamanho em 16 bits de setores = ~128 MB)
//     kMaxFiles     = 4096 arquivos
//   Alem disso o modelo ORIGINAL do IMG tem de existir (m_nCdSize != 0) e nao pode ser "big"
//   (maior que o buffer de streaming): modelos big sao lidos em 2 etapas pelo jogo e nao sao trocados.
//
// SEGURANCA: antes de entregar ao jogo o arquivo passa por uma validacao estrutural (so le bytes, nao
//   toca no engine): Clump 0x10 na raiz, todo chunk cabe dentro do pai, >=1 Atomic, >=1 Geometry.
//   Reprovou -> o jogo carrega o modelo ORIGINAL do IMG e o motivo vai para o log.
//   Geometria "native" (DFF exportado da propria versao mobile) e' ACEITA, ao contrario do preflight de skin.
//
// GEOMETRIA COM MAIS DE 65.535 VERTICES (ver DffSplit.h, auditoria da libGTASA arm64 2.10)
//   O jogo NAO le geometria desse tamanho: RpGeometryCreate devolve NULL para numVertices >= 65536, o Bin Mesh e'
//   truncado para uint16 (_rpMeshRead) e o desenho usa GL_UNSIGNED_SHORT fixo (rqDrawIndexed). Por isso, depois de
//   validado, o arquivo passa por DffSplit::Process: se alguma geometria passa de 65.535 vertices ela e' DIVIDIDA nos
//   proprios bytes (varias Geometry + um Atomic por parte, materiais/UV/normais/cores/pesos de skin preservados).
//   DFF com todas as geometrias <= 65.535 vertices (todos os antigos) sai IDENTICO, byte a byte.
//   Nao deu para dividir (native, Breakable, skin com split...) -> o modelo ORIGINAL do IMG e' usado e o motivo vai ao log.
//   So para modelos cujo SetClump guarda o clump INTEIRO: clump, veiculo e ped. Objeto/predio/time/LOD (CAtomicModelInfo) guardam
//   1 atomic so (LoadAtomicFile @0x4d046c liga cada atomic a um modelo pelo nome do frame, FindRelatedModelInfoCB @0x5546c8, e
//   CAtomicModelInfo::SetAtomic @0x45aa6c SUBSTITUI o anterior): la' a divisao e' recusada (Find(..., allowSplit=false)).
//   Marcador:  dff_split_off.on = nao divide (o arquivo > 65.535 e' recusado em vez de quebrar o RpClumpStreamRead)
//
// MARCADORES (arquivos vazios em SAMP/):  dff_off.on = desliga tudo
// LOG (samp_log.txt): prefixo [DFF]
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../main.h"   // g_pszStorage, FLog
#include "Core/KeyGen.h"
#include "DffSplit.h"

namespace CustomDff
{
    inline constexpr size_t kMaxDffBytes = 32u * 1024u * 1024u;
    inline constexpr size_t kMaxFiles    = 4096;

    struct Entry
    {
        std::string path;
        std::string name;   // sem extensao, como veio no disco
        uint32_t    key = 0;
    };

    // ----------------------------------------------------------------- caminhos
    inline std::string Root()
    {
        if (!g_pszStorage || !g_pszStorage[0]) return {};
        std::string p(g_pszStorage);
        if (p.back() != '/') p.push_back('/');
        return p;
    }
    inline bool Disabled()
    {
        const std::string r = Root();
        return !r.empty() && ::access((r + "SAMP/dff_off.on").c_str(), F_OK) == 0;
    }

    inline bool SplitDisabled()
    {
        const std::string r = Root();
        return !r.empty() && ::access((r + "SAMP/dff_split_off.on").c_str(), F_OK) == 0;
    }

    // ----------------------------------------------------------------- validacao estrutural
    namespace detail
    {
        inline uint32_t U32(const uint8_t* d, size_t o) { uint32_t v; std::memcpy(&v, d + o, 4); return v; }

        inline bool IsContainer(uint32_t t)
        {
            // Clump, FrameList, GeometryList, Geometry, MaterialList, Material, Texture, Extension, Atomic
            return t == 0x10 || t == 0x0E || t == 0x1A || t == 0x0F || t == 0x08 || t == 0x07 || t == 0x06 || t == 0x03 || t == 0x14;
        }

        // percorre [pos,end); todo chunk precisa caber no pai. Recursao so em containers conhecidos;
        // os filhos de Extension (0x03) sao plugins opacos: so confere que cabem, nao entra neles.
        inline bool Walk(const uint8_t* d, size_t pos, size_t end, int depth, bool opaque, int& atomics, int& geoms, std::string& err)
        {
            if (depth > 12) { err = "aninhamento de chunks excessivo"; return false; }
            while (pos < end)
            {
                if (end - pos < 12) { err = "sobra de " + std::to_string(end - pos) + " byte(s), menor que um cabecalho de chunk"; return false; }
                const uint32_t type = U32(d, pos), size = U32(d, pos + 4);
                const size_t body = pos + 12;
                if (size > end - body)
                {
                    char b[160];
                    std::snprintf(b, sizeof(b), "chunk 0x%x em %zu declara %u bytes mas so restam %zu no pai", type, pos, size, end - body);
                    err = b; return false;
                }
                if (!opaque)
                {
                    if (type == 0x14) ++atomics;
                    if (type == 0x0F) ++geoms;
                    if (IsContainer(type) && !Walk(d, body, body + size, depth + 1, type == 0x03, atomics, geoms, err))
                        return false;
                }
                pos = body + size;
            }
            return true;
        }
    }

    struct Info { uint32_t stamp = 0; int atomics = 0; int geoms = 0; };

    inline bool Validate(const uint8_t* d, size_t n, Info& out, std::string& err)
    {
        using namespace detail;
        if (n < 24) { err = "arquivo menor que um clump minimo"; return false; }
        if (U32(d, 0) != 0x10) { err = "nao comeca com chunk Clump (0x10)"; return false; }
        const uint32_t csize = U32(d, 4);
        out.stamp = U32(d, 8);
        if (csize != n - 12) { char b[96]; std::snprintf(b, sizeof(b), "tamanho do Clump (%u) difere do arquivo (%zu - 12)", csize, n); err = b; return false; }
        if (!Walk(d, 12, n, 1, false, out.atomics, out.geoms, err)) return false;
        if (out.atomics <= 0) { err = "DFF sem Atomic"; return false; }
        if (out.geoms   <= 0) { err = "DFF sem Geometry"; return false; }
        return true;
    }

    // ----------------------------------------------------------------- indice (so nomes/caminhos)
    inline bool EndsWithDff(const char* s)
    {
        const size_t n = std::strlen(s);
        return n > 4 && ::strcasecmp(s + n - 4, ".dff") == 0;
    }

    inline std::unordered_map<uint32_t, Entry>& Index()
    {
        static std::unordered_map<uint32_t, Entry> idx;
        static bool built = false;
        static std::mutex mu;
        std::lock_guard<std::mutex> lk(mu);
        if (built) return idx;
        built = true;

        const std::string r = Root();
        if (r.empty()) return idx;
        if (Disabled()) { FLog("[DFF] SAMP/dff_off.on existe: mistura de DFF desligada"); return idx; }

        int files = 0;
        for (const char* dir : { "texdb/DFF/", "texdb/dff/", "SAMP/dff/" })
        {
            DIR* d = ::opendir((r + dir).c_str());
            if (!d) continue;
            std::vector<std::string> names;
            while (auto* e = ::readdir(d))
                if (EndsWithDff(e->d_name)) names.push_back(e->d_name);
            ::closedir(d);
            std::sort(names.begin(), names.end());
            for (const auto& nm : names)
            {
                if ((size_t)files >= kMaxFiles) { FLog("[DFF] limite de %zu arquivos atingido: o resto foi ignorado", kMaxFiles); return idx; }
                Entry e;
                e.path = r + dir + nm;
                e.name = nm.substr(0, nm.size() - 4);
                e.key  = CKeyGen::GetUppercaseKey(e.name.c_str());
                struct stat st {};
                if (::stat(e.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                ++files;
                auto it = idx.find(e.key);
                if (it != idx.end())
                    FLog("[DFF] '%s' repetido: %s substitui %s (o ultimo vence)", e.name.c_str(), e.path.c_str(), it->second.path.c_str());
                idx[e.key] = e;
                FLog("[DFF] encontrado: %s (%lld bytes)", e.path.c_str(), (long long)st.st_size);
            }
        }
        FLog("[DFF] indice: %d arquivo(s), %zu modelo(s) distintos", files, idx.size());
        return idx;
    }

    inline bool HasAny() { return !Index().empty(); }

    // le o arquivo inteiro com o tamanho REAL (stat), sem buffer fixo
    inline bool ReadAll(const std::string& path, std::vector<uint8_t>& out, std::string& why)
    {
        out.clear();
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) { why = std::string("stat falhou: ") + std::strerror(errno); return false; }
        const size_t size = (size_t)st.st_size;
        if (size == 0)           { why = "arquivo vazio"; return false; }
        if (size > kMaxDffBytes) { why = "maior que o limite de " + std::to_string(kMaxDffBytes) + " bytes"; return false; }
        FILE* f = ::fopen(path.c_str(), "rb");
        if (!f) { why = std::string("fopen falhou: ") + std::strerror(errno); return false; }
        out.resize(size);
        const size_t got = ::fread(out.data(), 1, size, f);
        ::fclose(f);
        if (got != size) { out.clear(); why = "leitura parcial"; return false; }
        return true;
    }

    // Procura o DFF externo do modelo (por hash do nome). true = 'data' tem um DFF validado.
    // Em reprovacao devolve false e 'why' (vazio quando simplesmente nao ha arquivo para o modelo).
    inline bool Find(uint32_t modelKey, std::vector<uint8_t>& data, std::string& path, std::string& why, bool allowSplit = true)
    {
        why.clear();
        auto& idx = Index();
        auto it = idx.find(modelKey);
        if (it == idx.end()) return false;
        path = it->second.path;
        if (!ReadAll(path, data, why)) return false;
        Info info; std::string err;
        if (!Validate(data.data(), data.size(), info, err))
        {
            why = "validacao reprovou: " + err;
            data.clear();
            return false;
        }

        // geometria > 65.535 vertices: o jogo a recusa/corrompe; divide nos bytes (DFF antigo sai identico)
        DffSplit::Report rep; std::string serr; std::vector<uint8_t> split;
        const DffSplit::Result sr = DffSplit::Process(data.data(), data.size(), split, rep, serr);
        if (sr == DffSplit::Result::Failed)
        {
            // Inspect() so falha em DFF malformado; Validate ja passou, entao aqui e' limite de formato (native, Breakable...)
            why = "DffSplit reprovou: " + serr;
            data.clear();
            return false;
        }
        if (sr == DffSplit::Result::Split)
        {
            if (!allowSplit)
            {
                // objeto/predio/time/LOD (CAtomicModelInfo): so 1 atomic e' guardado, dividir mostraria apenas uma parte
                why = "geometria com " + std::to_string(rep.in.maxGeomVerts) + " vertices (>65535) em modelo que guarda 1 atomic so (objeto/predio): "
                      "nao da para dividir dentro do mesmo modelo";
                data.clear();
                return false;
            }
            if (SplitDisabled())
            {
                why = "geometria com " + std::to_string(rep.in.maxGeomVerts) + " vertices (>65535) e SAMP/dff_split_off.on existe";
                data.clear();
                return false;
            }
            FLog("[DFF] %s: DIVIDIDO (%s) | partes novas=%d vertices descartados sem uso=%llu tri-strips convertidos=%llu | engine: mesh+indices %.2f MB",
                 path.c_str(), DffSplit::Summary(rep).c_str(), rep.partsCreated, (unsigned long long)rep.droppedUnused,
                 (unsigned long long)rep.stripsConverted, rep.out.engineMeshBytes / 1048576.0);
            for (const std::string& nt : rep.notes) FLog("[DFF] %s: aviso: %s", path.c_str(), nt.c_str());
            data.swap(split);
        }
        return true;
    }
}
