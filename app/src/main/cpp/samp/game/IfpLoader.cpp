// IfpLoader.cpp
#include <cassert>
#include "common.h"      // int32/uint8... (precisa vir antes dos headers de Animation/*, como no AnimManager.cpp)
#include "IfpLoader.h"
#include "CustomAnim.h"

#include "game/Animation/AnimManager.h"
#include "game/Animation/AnimBlendHierarchy.h"
#include "game/Animation/AnimBlendSequence.h"
#include "game/Animation/AnimSequenceFrames.h"
#include "../main.h"          // FLog

#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <algorithm>
#include <map>

namespace CustomIfp {
namespace {

// ---------- formato intermediario (independente de ANP3/ANPK) ----------
struct Key { float q[4]; float t; float pos[3]; };            // q = x,y,z,w ; t = tempo ABSOLUTO (s)
struct Seq { char name[32] = {}; int boneId = -1; bool root = false; std::vector<Key> keys; };
struct Anim { char name[32] = {}; std::vector<Seq> seqs; };
struct Pack { char block[32] = {}; std::vector<Anim> anims; };

struct Reader {
    const uint8_t* p; size_t n; size_t o = 0; bool ok = true;
    const uint8_t* take(size_t k) { if (o + k > n) { ok = false; return nullptr; } auto r = p + o; o += k; return r; }
    template <class T> T get() { T v{}; if (auto b = take(sizeof(T))) memcpy(&v, b, sizeof(T)); return v; }
    void str(char* dst, size_t cap, size_t len) {                 // le len bytes; copia no maximo cap-1
        auto b = take(len); if (!b) return;
        size_t c = std::min(len, cap - 1); memcpy(dst, b, c); dst[c] = 0;
    }
};

static std::string Norm(const char* s) {                          // " Bip01 L UpperArm" -> "lupperarm"
    std::string r;
    for (; *s; ++s) if (*s != ' ' && *s != '_') r += (char)tolower((unsigned char)*s);
    if (r.rfind("bip01", 0) == 0) r.erase(0, 5);
    return r;
}

// ---------- ANP2 / ANP3 ----------
static bool ParseAnp23(Reader& r, bool isAnp3, Pack& out, std::string& err) {
    r.str(out.block, sizeof(out.block), 24);
    int nAnims = r.get<int32_t>();
    if (!r.ok || nAnims < 0 || nAnims > 2000) { err = "cabecalho ANP invalido"; return false; }
    out.anims.resize(nAnims);
    for (auto& a : out.anims) {
        r.str(a.name, sizeof(a.name), 24);
        int nSeq = r.get<int32_t>();
        if (isAnp3) { r.get<int32_t>(); r.get<int32_t>(); }       // dataSize, unk (usados so pelo bloco externo do jogo)
        if (!r.ok || nSeq < 0 || nSeq > 64) { err = "numero de sequencias invalido"; return false; }
        a.seqs.resize(nSeq);
        for (auto& s : a.seqs) {
            r.str(s.name, sizeof(s.name), 24);
            int type = r.get<int32_t>(), nFrames = r.get<int32_t>(), bone = r.get<int32_t>();
            s.boneId = bone;
            if (!r.ok || nFrames < 0 || nFrames > 20000) { err = "numero de frames invalido"; return false; }
            s.keys.assign(nFrames, Key{});
            for (auto& k : s.keys) {
                switch (type) {
                case 1: case 2: {                                  // float: quat(4) + time [+ trans(3)]
                    for (float& f : k.q) f = r.get<float>();
                    k.t = r.get<float>();
                    if (type == 2) { s.root = true; for (float& f : k.pos) f = r.get<float>(); }
                    break; }
                case 3: case 4: {                                  // comprimido: int16 quat/4096, time/60 [+ trans/1024]
                    for (float& f : k.q) f = r.get<int16_t>() / 4096.0f;
                    k.t = r.get<int16_t>() / 60.0f;
                    if (type == 4) { s.root = true; for (float& f : k.pos) f = r.get<int16_t>() / 1024.0f; }
                    break; }
                default: err = "tipo de sequencia desconhecido"; return false;
                }
            }
            if (type >= 3) FLog("[IFP] AVISO: %s/%s usa keyframes comprimidos (tempo lido como absoluto)", a.name, s.name);
        }
    }
    return r.ok;
}

// ---------- ANPK ----------
static void Round4(uint32_t& x) { if (x & 3) x += 4 - (x & 3); }
struct Chunk { char id[4]; uint32_t size; };

static bool ParseAnpk(Reader& r, Pack& out, std::string& err) {
    // ja consumimos 'ANPK' + size. Sequencia: INFO(numAnims, nome do bloco), depois por animacao NAME, DGAN{INFO, CPAN{ANIM, frames}...}
    auto readChunk = [&](Chunk& c) { c.size = 0; if (auto b = r.take(8)) { memcpy(c.id, b, 4); memcpy(&c.size, b + 4, 4); } };
    Chunk c; readChunk(c); Round4(c.size);
    if (!r.ok || c.size < 5 || c.size > 4096) { err = "INFO do ANPK invalido"; return false; }
    std::vector<char> buf(std::max<uint32_t>(c.size, 64) + 1, 0);
    if (auto b = r.take(c.size)) memcpy(buf.data(), b, c.size);
    int nAnims = *(int32_t*)buf.data();
    strncpy(out.block, buf.data() + 4, sizeof(out.block) - 1);
    if (!r.ok || nAnims < 0 || nAnims > 2000) { err = "numero de animacoes invalido"; return false; }
    out.anims.resize(nAnims);
    for (auto& a : out.anims) {
        readChunk(c); Round4(c.size);                              // NAME
        if (!r.ok || c.size > 256) { err = "NAME invalido"; return false; }
        r.str(a.name, sizeof(a.name), c.size);
        readChunk(c); Round4(c.size);                              // DGAN (conteudo e lido em sequencia)
        readChunk(c); Round4(c.size);                              // INFO -> nSeq
        if (!r.ok || c.size < 4 || c.size > 256) { err = "INFO da animacao invalido"; return false; }
        std::vector<char> ib(c.size + 1, 0);
        if (auto b = r.take(c.size)) memcpy(ib.data(), b, c.size);
        int nSeq = *(int32_t*)ib.data();
        if (nSeq < 0 || nSeq > 64) { err = "numero de sequencias invalido"; return false; }
        a.seqs.resize(nSeq);
        for (auto& s : a.seqs) {
            readChunk(c);                                          // CPAN
            readChunk(c); Round4(c.size);                          // ANIM
            if (!r.ok || c.size < 32 || c.size > 256) { err = "ANIM invalido"; return false; }
            std::vector<char> ab(c.size + 1, 0);
            if (auto b = r.take(c.size)) memcpy(ab.data(), b, c.size);
            strncpy(s.name, ab.data(), sizeof(s.name) - 1);
            int nFrames = *(int32_t*)(ab.data() + 28);
            if (c.size == 44) s.boneId = *(int32_t*)(ab.data() + 40);
            else s.boneId = BoneIdFromName(s.name);
            if (nFrames <= 0) continue;
            if (nFrames > 20000) { err = "numero de frames invalido"; return false; }
            Chunk fk; readChunk(fk);                               // KRTS / KRT0 / KR00
            enum { KR00, KRT0, KRTS } kind;
            if      (!memcmp(fk.id, "KRTS", 4)) kind = KRTS;
            else if (!memcmp(fk.id, "KRT0", 4)) kind = KRT0;
            else if (!memcmp(fk.id, "KR00", 4)) kind = KR00;
            else { err = "tipo de keyframe ANPK desconhecido"; return false; }
            s.root = (kind != KR00);
            s.keys.assign(nFrames, Key{});
            for (auto& k : s.keys) {
                float f[11] = {};
                size_t cnt = kind == KRTS ? 11 : kind == KRT0 ? 8 : 5;
                for (size_t i = 0; i < cnt; ++i) f[i] = r.get<float>();
                // ANPK guarda o quaternion "invertido": o loader original aplica Conjugate()
                k.q[0] = -f[0]; k.q[1] = -f[1]; k.q[2] = -f[2]; k.q[3] = f[3];
                if (kind == KRTS)      { k.pos[0] = f[4]; k.pos[1] = f[5]; k.pos[2] = f[6]; k.t = f[10]; }   // escala ignorada
                else if (kind == KRT0) { k.pos[0] = f[4]; k.pos[1] = f[5]; k.pos[2] = f[6]; k.t = f[7]; }
                else                   { k.t = f[4]; }
            }
        }
    }
    return r.ok;
}

// ---------- construcao no CAnimManager ----------
static std::string LowerStr(const char* s) { std::string r; for (; *s; ++s) r += (char)tolower((unsigned char)*s); return r; }
static std::map<std::string, std::vector<std::string>> g_animNames;   // bloco(minusculo) -> nomes

static bool BuildBlock(const Pack& pk, const char* blockOverride, LoadResult& res) {
    const char* bname = (blockOverride && *blockOverride) ? blockOverride : pk.block;
    if (!*bname || strlen(bname) > MAX_ANIM_BLOCK_NAME - 1) { res.error = "nome de bloco vazio ou > 15 chars"; return false; }
    if (CAnimManager::GetAnimationBlock(bname)) { res.error = std::string("bloco ja existe: ") + bname; return false; }
    int n = (int)pk.anims.size();
    if (CAnimManager::ms_numAnimBlocks >= NUM_ANIM_BLOCKS) { res.error = "sem espaco em ms_aAnimBlocks"; return false; }
    if (CAnimManager::ms_numAnimations + n > (int)CAnimManager::ms_aAnimations.size()) { res.error = "sem espaco em ms_aAnimations"; return false; }

    int bi = CAnimManager::RegisterAnimBlock(bname);
    CAnimBlock* blk = &CAnimManager::ms_aAnimBlocks[bi];
    blk->startAnimation = CAnimManager::ms_numAnimations;
    blk->animationCount = n;
    blk->bLoaded = true;
    blk->usRefs  = 1;                    // "pin": impede RemoveAnimBlock por contagem de refs zerada

    for (int i = 0; i < n; ++i) {
        const Anim& a = pk.anims[i];
        CAnimBlendHierarchy* h = &CAnimManager::ms_aAnimations[blk->startAnimation + i];
        h->SetName(a.name);
        h->m_nAnimBlockId = bi;          // o loader ANP23 original do projeto NAO seta isto (fica -1)
        h->m_bRunningCompressed = false;
        h->m_bKeepCompressed = false;
        h->m_nSeqCount = (uint16_t)a.seqs.size();
        h->m_pSequences = new CAnimBlendSequence[a.seqs.size() ? a.seqs.size() : 1];
        for (size_t k = 0; k < a.seqs.size(); ++k) {
            const Seq& s = a.seqs[k];
            CAnimBlendSequence* seq = &h->m_pSequences[k];
            seq->SetName(s.name);
            if (s.boneId >= 0) seq->SetBoneTag(s.boneId);
            else FLog("[IFP] %s/%s: osso desconhecido (so hash de nome)", a.name, s.name);
            if (s.keys.empty()) continue;
            seq->SetNumFrames((int)s.keys.size(), s.root, false, nullptr);
            for (size_t f = 0; f < s.keys.size(); ++f) {
                KeyFrameTrans* kf = seq->GetUncompressedFrame((int)f);   // respeita sizeof(KeyFrame) x sizeof(KeyFrameTrans)
                // ATENCAO: no seu projeto CQuaternion declara {w,x,y,z}, mas o motor le a memoria como x,y,z,w
                // (mesma ordem do arquivo). Por isso copio os 4 floats crus em vez de atribuir por nome.
                static_assert(sizeof(CQuaternion) == 4 * sizeof(float), "CQuaternion");
                memcpy(&kf->rotation, s.keys[f].q, 4 * sizeof(float));
                kf->deltaTime = s.keys[f].t;                              // absoluto; CalcTotalTime converte
                if (s.root) { kf->translation.x = s.keys[f].pos[0]; kf->translation.y = s.keys[f].pos[1]; kf->translation.z = s.keys[f].pos[2]; }
            }
        }
        h->RemoveQuaternionFlips();
        h->CalcTotalTime();              // total + converte tempos absolutos em deltas
    }
    CAnimManager::ms_numAnimations += n;
    strncpy(res.block, bname, sizeof(res.block) - 1);
    res.numAnims = n;
    CustomAnim::RegisterCustomBlock(bi);
    { auto& v = g_animNames[LowerStr(bname)]; v.clear(); for (auto& a : pk.anims) v.push_back(a.name); }
    return true;
}

} // namespace

const std::vector<std::string>* GetAnimNames(const char* block) {
    if (!block) return nullptr;
    auto it = g_animNames.find(LowerStr(block));
    return it == g_animNames.end() ? nullptr : &it->second;
}

int BoneIdFromName(const char* name) {
    struct E { const char* n; int id; };
    static const E tbl[] = {
        {"root",0},{"normal",0},{"pelvis",1},{"spine",2},{"spine1",3},{"neck",4},{"head",5},
        {"lbrow",6},{"rbrow",7},{"jaw",8},
        {"rclavicle",21},{"rupperarm",22},{"rforearm",23},{"rhand",24},{"rfinger",25},{"rfinger01",26},
        {"lclavicle",31},{"lupperarm",32},{"lforearm",33},{"lhand",34},{"lfinger",35},{"lfinger01",36},
        {"lthigh",41},{"lcalf",42},{"lfoot",43},{"ltoe0",44},
        {"rthigh",51},{"rcalf",52},{"rfoot",53},{"rtoe0",54},
        {"belly",201},{"rbreast",301},{"lbreast",302},
    };
    std::string k = Norm(name);
    for (auto& e : tbl) if (k == e.n) return e.id;
    return -1;
}

LoadResult LoadMemory(const uint8_t* data, size_t size, const char* blockOverride) {
    LoadResult res; Reader r{data, size};
    char ident[4] = {}; uint32_t fsize = 0;
    if (auto b = r.take(8)) { memcpy(ident, b, 4); memcpy(&fsize, b + 4, 4); }
    if (!r.ok) { res.error = "arquivo vazio/curto"; return res; }
    Pack pk; bool ok;
    if      (!memcmp(ident, "ANP3", 4)) ok = ParseAnp23(r, true,  pk, res.error);
    else if (!memcmp(ident, "ANP2", 4)) ok = ParseAnp23(r, false, pk, res.error);
    else if (!memcmp(ident, "ANPK", 4)) ok = ParseAnpk(r, pk, res.error);
    else { res.error = "assinatura desconhecida (esperado ANP3/ANP2/ANPK)"; return res; }
    if (!ok) { if (res.error.empty()) res.error = "arquivo truncado"; return res; }
    res.ok = BuildBlock(pk, blockOverride, res);
    return res;
}

LoadResult LoadFile(const char* path, const char* blockOverride) {
    LoadResult res;
    FILE* f = fopen(path, "rb");
    if (!f) { res.error = std::string("nao abriu: ") + path; return res; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 8 || sz > 64 * 1024 * 1024) { fclose(f); res.error = "tamanho invalido"; return res; }
    std::vector<uint8_t> buf((size_t)sz);
    size_t got = fread(buf.data(), 1, buf.size(), f); fclose(f);
    if (got != buf.size()) { res.error = "leitura incompleta"; return res; }
    return LoadMemory(buf.data(), buf.size(), blockOverride);
}

} // namespace CustomIfp
