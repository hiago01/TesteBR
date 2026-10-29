#pragma once

// ---------------------------------------------------------------------------
// Leitor/mesclador PURO (sem dependencia do jogo) de IPL BINARIO do gta3.img / gta_int.img
// + arquivos de ajuste em texto (texdb/ipl/<nome_do_ipl>.ipl).
// Usado por CustomIpl.h (jogo) e por testes/host_test_customipl.cpp (host, g++).
//
// ======================= EVIDENCIA (libGTASA.so arm64 do projeto) =======================
// CStreaming::ConvertBufferToObject (0x395114) -> bl CIplStore::LoadIpl(int slot, uchar* buf, int size)  [0x395270]
// CIplStore::LoadIpl (0x33b5f4, simbolo exportado, retorna sempre true):
//   1. IplDef = pool[slot] (stride 0x34). Se a caixa (bb) ainda e' invalida -> LoadIplBoundingBox(slot,buf,size) (0x33bb34)
//      -> o bb e' calculado UMA vez, a partir do MESMO buffer (por isso mesclar antes do LoadIpl e' consistente).
//   2. strncmp(buf,"bnry",4) (string em 0x7390c0). Se igual -> caminho binario (0x33b934); senao -> texto (linhas).
//   3. Cabecalho binario lido (confirmado nos dois desassemblies):
//        +0x04  int16   numero de instancias           (ldrsh [buf,#4]; o jogo so olha 16 bits)
//        +0x14  int16   numero de car generators       (ldrsh [hdr+0x10] da copia do cabecalho)
//        +0x1C  uint32  offset da tabela de instancias (add buf+off; passo 0x28)
//        +0x3C  uint32  offset da tabela de car gens   (passo 0x30)
//      (+0x20,+0x28,+0x30,+0x38,+0x40,+0x48 sao copiados mas nao usados no caminho de entidades)
//   4. Para cada instancia: CFileLoader::LoadObjectInstance(CFileObjectInstance*, nullptr)  -> entidade.
//      O retorno NAO e' testado (0x33b9d4 usa x23 direto): modelo sem ModelInfo => ponteiro nulo => CRASH.
//      Por isso TODA instancia que o mod acrescenta/troca tem o ID checado contra ms_modelInfoPtrs antes.
//   5. lod = int32 na posicao 0x24 da instancia: -1 = sem LOD; senao indice no IPL PRINCIPAL relacionado (staticIdx), lido num array
//      de entidades por indice (ldr [arr + idx*8]) e incrementa o contador de filhos do LOD (+0x58).
// CFileLoader::LoadObjectInstance(const char*) (0x4d206c) = sscanf("%d %s %d %f %f %f %f %f %f %f %d") direto
//   para a struct de 0x28 bytes (+0x1C id, +0x20 "interior", +0..8 pos, +0xC..0x18 quaternion, +0x24 lod) e chama
//   a versao struct SEM nenhuma conversao  =>  texto -> binario e' 1:1 (sem conjugar quaternion).
//
// ======================= ESTRATEGIA DE MISTURA =======================
// Nunca mexe na tabela original no lugar. A tabela nova (original com trocas/remocoes + acrescimos) e' escrita no
// FIM de uma COPIA do buffer e so dois campos do cabecalho mudam: +0x04 (contagem) e +0x1C (offset). Tudo que e'
// relativo ao inicio do buffer (car generators em +0x3C e demais tabelas) continua valido sem relocar.
// Remover entradas compacta a tabela. O campo lod NAO e' remapeado (indexa o IPL principal, nao esta tabela).
//
// ======================= FORMATO DO ARQUIVO texdb/ipl/<nome>.ipl =======================
// O nome do arquivo (sem .ipl, sem diferenciar maiusculas) e' o nome do IPL no IMG (ex.: countrye_stream0.ipl).
// Tres secoes (virgulas/espacos equivalem; '#' comeca comentario):
//   replace                      # troca a instancia de indice N (indice = ordem do IPL decodificado; confere o ID)
//   N, idOriginal, id, nome, interior, x, y, z, rx, ry, rz, rw, lod
//   end
//   remove                       # remove a instancia N (confere o ID)
//   N, idOriginal
//   end
//   inst                         # acrescenta no fim; mesmo formato de linha do IPL texto do jogo
//   id, nome, interior, x, y, z, rx, ry, rz, rw, lod
//   end
// - O "lod" (replace/inst) e' o indice do LOD no IPL PRINCIPAL (data/maps/..., ex. countrye.ipl); copie o de uma instancia existente (-1 = sem LOD).
// - "interior" e' o campo de 32 bits m_nInstanceType (byte baixo = area; bits altos = flags), igual ao IPL decodificado.
// - O ID do modelo e' resolvido pelo NOME (como o jogo); se o nome nao existir (ex.: "dummy" de IPL decodificado sem
//   restaurar IDs), vale o ID numerico, desde que exista ModelInfo.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <strings.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace CustomIplParse
{
    inline constexpr size_t   kHdrSize      = 0x4C;
    inline constexpr size_t   kInstSize     = 0x28;
    inline constexpr uint32_t kMaxInstances = 0x7FFF;      // o jogo le a contagem como int16
    inline constexpr size_t   kMaxIplBytes  = 32u * 1024u * 1024u;
    inline constexpr float    kMaxCoord     = 100000.0f;
    inline constexpr float    kFarWarn      = 400.0f;      // aviso: instancia longe da caixa do IPL original

    // struct binaria de 0x28 bytes (CFileObjectInstance)
    struct Inst
    {
        float    pos[3];
        float    rot[4];
        int32_t  id;
        uint32_t type;      // area (byte baixo) + flags
        int32_t  lod;
    };
    static_assert(sizeof(Inst) == kInstSize, "CFileObjectInstance deve ter 0x28 bytes");

    // ------------------------------------------------------------------ binario
    enum class BinStatus : uint8_t { Ok = 0, TooSmall, BadMagic, BadCount, BadOffset, Truncated };

    inline const char* BinStatusStr(BinStatus s)
    {
        switch (s) {
            case BinStatus::Ok:        return "ok";
            case BinStatus::TooSmall:  return "buffer menor que o cabecalho (0x4C)";
            case BinStatus::BadMagic:  return "nao e' IPL binario ('bnry' ausente)";
            case BinStatus::BadCount:  return "contagem de instancias invalida (>32767 ou bits altos)";
            case BinStatus::BadOffset: return "offset da tabela de instancias invalido";
            case BinStatus::Truncated: return "tabela de instancias passa do fim do buffer";
        }
        return "?";
    }

    struct BinView { uint32_t count = 0; uint32_t instOff = 0; };

    inline uint32_t RdU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
    inline void     WrU32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

    inline BinStatus ReadBin(const uint8_t* buf, size_t n, BinView& v)
    {
        if (!buf || n < kHdrSize) return BinStatus::TooSmall;
        if (std::memcmp(buf, "bnry", 4) != 0) return BinStatus::BadMagic;
        const uint32_t cnt = RdU32(buf + 4);
        if (cnt > kMaxInstances) return BinStatus::BadCount;
        const uint32_t off = RdU32(buf + 0x1C);
        if (cnt > 0)
        {
            if (off < kHdrSize || (off & 3u)) return BinStatus::BadOffset;
            if ((uint64_t)off + (uint64_t)cnt * kInstSize > (uint64_t)n) return BinStatus::Truncated;
        }
        v.count = cnt; v.instOff = off;
        return BinStatus::Ok;
    }

    // ------------------------------------------------------------------ texto (ajustes)
    struct InstText
    {
        int      id = -1;
        char     name[40] = {};
        uint32_t type = 0;
        float    pos[3] = {};
        float    rot[4] = {};
        int      lod = -1;
    };
    struct ReplaceOp { int index; int origId; InstText t; int line; };
    struct RemoveOp  { int index; int origId; int line; };
    struct AddOp     { InstText t; int line; };

    struct OverrideFile
    {
        std::vector<ReplaceOp> rep;
        std::vector<RemoveOp>  rem;
        std::vector<AddOp>     add;
        std::vector<std::string> errors;     // "linha N: motivo"
        int linesOk = 0, linesBad = 0;
        bool Empty() const { return rep.empty() && rem.empty() && add.empty(); }
    };

    namespace detail
    {
        inline std::string Trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (unsigned char)s[a] <= ' ') a++;
            while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
            return s.substr(a, b - a);
        }
        inline void Split(const std::string& line, std::vector<std::string>& out)
        {
            out.clear(); std::string cur;
            for (char c : line)
            {
                if (c == ',' || (unsigned char)c <= ' ') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
                else cur.push_back(c);
            }
            if (!cur.empty()) out.push_back(cur);
        }
        inline bool ToInt(const std::string& s, long long& v)
        {
            if (s.empty()) return false;
            char* e = nullptr; v = std::strtoll(s.c_str(), &e, 10);
            return e && *e == 0;
        }
        inline bool ToFloat(const std::string& s, float& v)
        {
            if (s.empty()) return false;
            char* e = nullptr; double d = std::strtod(s.c_str(), &e);
            if (!e || *e != 0 || !std::isfinite(d)) return false;
            v = (float)d; return true;
        }
        // tokens[b..b+10] = id nome interior x y z rx ry rz rw lod
        inline bool ParseInstTokens(const std::vector<std::string>& t, size_t b, InstText& o, std::string& why)
        {
            long long id, type, lod; float f[7];
            if (!ToInt(t[b], id) || id < 0 || id > 65535) { why = "id invalido"; return false; }
            if (t[b + 1].size() >= sizeof(o.name)) { why = "nome do modelo longo demais"; return false; }
            if (!ToInt(t[b + 2], type) || type < 0 || type > 0xFFFFFFFFll) { why = "campo interior invalido"; return false; }
            for (int k = 0; k < 7; k++) if (!ToFloat(t[b + 3 + k], f[k])) { why = "numero invalido (x y z rx ry rz rw)"; return false; }
            if (!ToInt(t[b + 10], lod) || lod < -1 || lod > (long long)kMaxInstances) { why = "lod invalido (-1 ou indice)"; return false; }
            for (int k = 0; k < 3; k++) if (std::fabs(f[k]) > kMaxCoord) { why = "coordenada fora de +-100000"; return false; }
            const float q = std::sqrt(f[3]*f[3] + f[4]*f[4] + f[5]*f[5] + f[6]*f[6]);
            if (!(q > 0.5f && q < 1.5f)) { why = "quaternion nao normalizado (norma fora de 0.5..1.5)"; return false; }
            o.id = (int)id; std::snprintf(o.name, sizeof(o.name), "%s", t[b + 1].c_str());
            o.type = (uint32_t)type; o.pos[0] = f[0]; o.pos[1] = f[1]; o.pos[2] = f[2];
            o.rot[0] = f[3]; o.rot[1] = f[4]; o.rot[2] = f[5]; o.rot[3] = f[6]; o.lod = (int)lod;
            return true;
        }
    }

    inline bool ParseOverride(const char* text, size_t len, OverrideFile& out)
    {
        using namespace detail;
        enum Sec { None, Replace, Remove, Inst } sec = None;
        int lineNo = 0; size_t pos = 0; std::vector<std::string> tk; std::string why;
        auto bad = [&](const char* m) { out.linesBad++; out.errors.push_back("linha " + std::to_string(lineNo) + ": " + m); };
        while (pos <= len)
        {
            size_t e = pos; while (e < len && text[e] != '\n') e++;
            std::string line(text + pos, e - pos); pos = e + 1; lineNo++;
            if (pos > len + 1) break;
            size_t h = line.find('#'); if (h != std::string::npos) line.erase(h);
            line = Trim(line); if (line.empty()) continue;
            Split(line, tk); if (tk.empty()) continue;
            std::string k0 = tk[0]; for (auto& c : k0) c = (char)std::tolower((unsigned char)c);
            if (tk.size() == 1 && (k0 == "replace" || k0 == "remove" || k0 == "inst"))
            {
                if (sec != None) { bad("secao aberta sem 'end'"); }
                sec = (k0 == "replace") ? Replace : (k0 == "remove") ? Remove : Inst; continue;
            }
            if (tk.size() == 1 && k0 == "end") { if (sec == None) bad("'end' sem secao"); sec = None; continue; }
            if (sec == None) { bad("linha fora de secao (replace/remove/inst)"); continue; }
            long long idx, oid;
            if (sec == Remove)
            {
                if (tk.size() != 2 || !ToInt(tk[0], idx) || !ToInt(tk[1], oid) || idx < 0 || idx > (long long)kMaxInstances || oid < 0 || oid > 65535)
                { bad("remove espera: indice, idOriginal"); continue; }
                out.rem.push_back({ (int)idx, (int)oid, lineNo }); out.linesOk++;
            }
            else if (sec == Replace)
            {
                if (tk.size() != 13 || !ToInt(tk[0], idx) || !ToInt(tk[1], oid) || idx < 0 || idx > (long long)kMaxInstances || oid < 0 || oid > 65535)
                { bad("replace espera: indice, idOriginal + 11 campos da instancia"); continue; }
                ReplaceOp r{}; r.index = (int)idx; r.origId = (int)oid; r.line = lineNo;
                if (!ParseInstTokens(tk, 2, r.t, why)) { bad(why.c_str()); continue; }
                out.rep.push_back(r); out.linesOk++;
            }
            else
            {
                if (tk.size() != 11) { bad("inst espera 11 campos: id, nome, interior, x, y, z, rx, ry, rz, rw, lod"); continue; }
                AddOp a{}; a.line = lineNo;
                if (!ParseInstTokens(tk, 0, a.t, why)) { bad(why.c_str()); continue; }
                out.add.push_back(a); out.linesOk++;
            }
        }
        if (sec != None) { out.linesBad++; out.errors.push_back("arquivo terminou com secao aberta (falta 'end')"); }
        return !out.Empty();
    }

    // ------------------------------------------------------------------ mistura
    // resolve(nome, idDoArquivo) -> id do modelo valido no jogo, ou -1 (modelo inexistente => entrada ignorada)
    using Resolver = std::function<int(const char*, int)>;

    struct MergeReport
    {
        size_t origCount = 0, finalCount = 0, replaced = 0, removed = 0, added = 0, skipped = 0, lodFixed = 0;
        std::vector<std::string> notes;
    };

    namespace detail
    {
        enum class Act : uint8_t { Keep, Replace, Remove };
        inline Inst ToInst(const InstText& t, int id, int lod)
        {
            Inst i{}; i.pos[0] = t.pos[0]; i.pos[1] = t.pos[1]; i.pos[2] = t.pos[2];
            i.rot[0] = t.rot[0]; i.rot[1] = t.rot[1]; i.rot[2] = t.rot[2]; i.rot[3] = t.rot[3];
            i.id = id; i.type = t.type; i.lod = lod; return i;
        }
    }

    // files: em ordem de prioridade crescente (o ULTIMO que mexe num mesmo indice vence).
    // Retorna true somente se alguma alteracao foi aplicada; out recebe o buffer novo (copia + tabela no fim).
    inline bool Merge(const uint8_t* in, size_t n, const std::vector<const OverrideFile*>& files, const Resolver& resolve,
                      std::vector<uint8_t>& out, MergeReport& rep, std::string* whyNot = nullptr)
    {
        using namespace detail;
        auto fail = [&](const std::string& m) { if (whyNot) *whyNot = m; return false; };
        BinView v; BinStatus st = ReadBin(in, n, v);
        if (st != BinStatus::Ok) return fail(BinStatusStr(st));
        if (n > kMaxIplBytes) return fail("buffer maior que o limite");
        const size_t N = v.count;
        rep = MergeReport(); rep.origCount = N;

        std::vector<Inst> orig(N);
        if (N) std::memcpy(orig.data(), in + v.instOff, N * kInstSize);

        std::vector<Act> act(N, Act::Keep);
        std::vector<Inst> repl(N);
        std::vector<int>  replLod(N, -1);            // lod do replace (indice ORIGINAL)
        std::vector<std::pair<Inst, int>> adds;       // instancia + lod (indice ORIGINAL)

        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (const Inst& i : orig) for (int k = 0; k < 3; k++) { if (i.pos[k] < mn[k]) mn[k] = i.pos[k]; if (i.pos[k] > mx[k]) mx[k] = i.pos[k]; }
        auto farFrom = [&](const float* p) {
            if (N == 0) return false;
            for (int k = 0; k < 2; k++) if (p[k] < mn[k] - kFarWarn || p[k] > mx[k] + kFarWarn) return true;
            return false;
        };
        auto note = [&](const std::string& m) { if (rep.notes.size() < 200) rep.notes.push_back(m); };

        for (const OverrideFile* f : files)
        {
            if (!f) continue;
            for (const RemoveOp& r : f->rem)
            {
                if (r.index < 0 || (size_t)r.index >= N) { rep.skipped++; note("remove linha " + std::to_string(r.line) + ": indice " + std::to_string(r.index) + " fora do IPL (" + std::to_string(N) + " instancias)"); continue; }
                if (orig[r.index].id != r.origId) { rep.skipped++; note("remove linha " + std::to_string(r.line) + ": indice " + std::to_string(r.index) + " tem id " + std::to_string(orig[r.index].id) + ", arquivo diz " + std::to_string(r.origId) + " -> ignorado"); continue; }
                act[r.index] = Act::Remove;
            }
            for (const ReplaceOp& r : f->rep)
            {
                if (r.index < 0 || (size_t)r.index >= N) { rep.skipped++; note("replace linha " + std::to_string(r.line) + ": indice " + std::to_string(r.index) + " fora do IPL (" + std::to_string(N) + " instancias)"); continue; }
                if (orig[r.index].id != r.origId) { rep.skipped++; note("replace linha " + std::to_string(r.line) + ": indice " + std::to_string(r.index) + " tem id " + std::to_string(orig[r.index].id) + ", arquivo diz " + std::to_string(r.origId) + " -> ignorado"); continue; }
                const int mid = resolve ? resolve(r.t.name, r.t.id) : r.t.id;
                if (mid < 0) { rep.skipped++; note("replace linha " + std::to_string(r.line) + ": modelo '" + r.t.name + "' (id " + std::to_string(r.t.id) + ") nao existe no jogo -> ignorado"); continue; }
                if (resolve && mid != r.t.id) note("replace linha " + std::to_string(r.line) + ": nome '" + r.t.name + "' resolve para id " + std::to_string(mid) + " (arquivo dizia " + std::to_string(r.t.id) + "); uso o do nome");
                if (farFrom(r.t.pos)) note("AVISO replace linha " + std::to_string(r.line) + ": posicao a mais de 400 unidades da area do IPL original (a caixa de streaming vai crescer)");
                repl[r.index] = ToInst(r.t, mid, -1); replLod[r.index] = r.t.lod; act[r.index] = Act::Replace;
            }
            for (const AddOp& a : f->add)
            {
                const int mid = resolve ? resolve(a.t.name, a.t.id) : a.t.id;
                if (mid < 0) { rep.skipped++; note("inst linha " + std::to_string(a.line) + ": modelo '" + a.t.name + "' (id " + std::to_string(a.t.id) + ") nao existe no jogo -> ignorado"); continue; }
                if (resolve && mid != a.t.id) note("inst linha " + std::to_string(a.line) + ": nome '" + a.t.name + "' resolve para id " + std::to_string(mid) + " (arquivo dizia " + std::to_string(a.t.id) + "); uso o do nome");
                if (farFrom(a.t.pos)) note("AVISO inst linha " + std::to_string(a.line) + ": posicao a mais de 400 unidades da area do IPL original (a caixa de streaming vai crescer)");
                adds.push_back({ ToInst(a.t, mid, -1), a.t.lod });
            }
        }

        // indices novos
        std::vector<int> oldToNew(N, -1); size_t k = 0;
        for (size_t i = 0; i < N; i++) if (act[i] != Act::Remove) oldToNew[i] = (int)k++;
        const size_t finalCount = k + adds.size();
        if (finalCount > kMaxInstances) return fail("contagem final passaria de 32767 instancias");

        for (size_t i = 0; i < N; i++) { if (act[i] == Act::Replace) rep.replaced++; else if (act[i] == Act::Remove) rep.removed++; }
        rep.added = adds.size();
        if (rep.replaced == 0 && rep.removed == 0 && rep.added == 0) return fail("nenhuma alteracao aplicavel");

        // O campo lod NAO indexa esta tabela: LoadIpl (0x33b744..0x33b80c) faz entityArrays[IplDef.staticIdx][lod], isto e,
        // o indice e' a posicao da linha 'inst' no IPL PRINCIPAL relacionado (ex.: countrye_stream4 -> countrye.ipl).
        // Por isso passa SEM remapear e SEM comparar com o tamanho desta tabela.
        auto mapLod = [&](int lod, bool&) -> int { return lod < 0 ? -1 : lod; };

        std::vector<Inst> fin; fin.reserve(finalCount);
        for (size_t i = 0; i < N; i++)
        {
            if (act[i] == Act::Remove) continue;
            Inst x; bool fx = false;
            if (act[i] == Act::Replace) { x = repl[i]; x.lod = mapLod(replLod[i], fx); }
            else { x = orig[i]; x.lod = mapLod(orig[i].lod, fx); }
            if (fx) rep.lodFixed++;
            fin.push_back(x);
        }
        for (auto& a : adds) { Inst x = a.first; bool fx = false; x.lod = mapLod(a.second, fx); if (fx) rep.lodFixed++; fin.push_back(x); }

        out.assign(in, in + n);
        while (out.size() & 3u) out.push_back(0);
        const size_t newOff = out.size();
        if (newOff + fin.size() * kInstSize > kMaxIplBytes) return fail("buffer final maior que o limite");
        out.resize(newOff + fin.size() * kInstSize);
        if (!fin.empty()) std::memcpy(out.data() + newOff, fin.data(), fin.size() * kInstSize);
        WrU32(out.data() + 4, (uint32_t)fin.size());     // contagem  (+0x04)
        WrU32(out.data() + 0x1C, (uint32_t)newOff);      // offset    (+0x1C)
        rep.finalCount = fin.size();
        return true;
    }

    // Confere um buffer binario: cabecalho ok, e (se informado) todo id de instancia existe. Retorna "" se ok.
    inline std::string Validate(const uint8_t* buf, size_t n, const std::function<bool(int)>& modelExists)
    {
        BinView v; BinStatus st = ReadBin(buf, n, v);
        if (st != BinStatus::Ok) return BinStatusStr(st);
        for (uint32_t i = 0; i < v.count; i++)
        {
            Inst x; std::memcpy(&x, buf + v.instOff + (size_t)i * kInstSize, kInstSize);
            if (modelExists && !modelExists(x.id)) return "instancia " + std::to_string(i) + " com modelo " + std::to_string(x.id) + " inexistente";
            if (x.lod < -1 || x.lod > (int32_t)kMaxInstances) return "instancia " + std::to_string(i) + " com lod invalido (" + std::to_string(x.lod) + ")";
            for (int k = 0; k < 7; k++) { float f = (k < 3) ? x.pos[k] : x.rot[k - 3]; if (!std::isfinite(f)) return "instancia " + std::to_string(i) + " com valor nao finito"; }
        }
        return {};
    }
}
