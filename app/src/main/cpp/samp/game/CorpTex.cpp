// CorpTex.cpp - ver CorpTex.h
//
// POR QUE TROCAR NO RENDER (e nao no molde do modelo):
//   Os 76 materiais do skin.dff pertencem a geometria compartilhada pelo molde do modelo e por todos os
//   clones (RpClumpClone so referencia a geometria). Trocar a textura "de verdade" mudaria TODOS os peds
//   do modelo. Entao, em CEntity::Render do ped escolhido, pomos a textura da corp nos materiais (atribuicao
//   direta do ponteiro, sem mexer em refcount), o jogo desenha, e devolvemos a original logo depois.
//   Nossas texturas ficam vivas (1 ref nossa) enquanto houver ped usando a corp.
//
// MAPEAMENTO MATERIAL -> NOME
//   O DFF so guarda nomes; quando a textura nao existe no texdb, o GetTexture_hook devolve "male01", entao o nome
//   da textura do material em runtime pode ser "male01" e nao serve de chave. Por isso a chave e o nome gravado
//   no DFF (DffPreflight::Info::materialsList[i].texture), pelo INDICE do material (mesma ordem que o
//   CustomSkin usa). So se a contagem do motor diferir da do DFF cai para o nome da textura em runtime.
//
// LIMITES CONHECIDOS (confirmar em jogo, ver log [CORPTEX]):
//   * Se CPed::Render nao chamar CEntity::Render, o hook nao e atingido; o Process loga um AVISO apos 5 s.
//   * Ped desenhado pela lista de alfa (fade de distancia) nao passa por CEntity::Render: nesses frames
//     aparece a textura padrao.
#include "CorpTex.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "Entity/CEntityGTA.h"
#include "SkinImageUtil.h"
#include "DffPreflight.h"
#include "CustomSkinStream.h"
#include "../vendor/stb/stb_image.h"      // a implementacao esta em sprite2d.cpp
#include "../net/netgame.h"
#include "playerped.h"
#include "Textures/TextureDatabaseRuntime.h"   // banco de texturas do jogo ("male01" = invisivel)
#include "../vendor/raknet/SAMP/SAMPRPC.h"

extern CNetGame* pNetGame;

namespace CorpTex {

int RPC_CorpTex = 231;                 // TODO validar (ver .h). AnimSync usa 230, IdCard 202.
std::atomic<int> g_active{0};

namespace {

constexpr int      kHostModel      = 7;      // modelo que hospeda o skin.dff da FemininaCorp (hoje 7; quando migrar p/ 272, troque aqui)
constexpr const char* kSkinFolder  = "272";  // pasta dos PNGs: textures/<kSkinFolder>/<corp>/
constexpr int      kMaxTexSize     = 1024;   // teto de lado no celular (os PNGs de 2048 sao reduzidos)
constexpr int      kMaxMaterials   = 512;
constexpr const char* kHideTexture = "male01";          // textura invisivel do texdb: pecas que a corp NAO tem ficam ocultas
constexpr const char* kPiecePrefix = "fantasy_f_cop_";  // so as pecas de roupa da Corp sao ocultadas (corpo, cabelo e tatuagens nao)
constexpr size_t   kMaxCachedCorps = 3;
constexpr uint64_t kGraveyardMs    = 5000;
constexpr uintptr_t OFF_GEOM_MATLIST      = 0x28;   // RpGeometry::matList.materials  (igual ao CustomSkin.cpp)
constexpr uintptr_t OFF_GEOM_NUMMATERIALS = 0x30;   // RpGeometry::matList.numMaterials

// ---- fila RPC/chat -> thread do jogo -------------------------------------------------------
struct Cmd { enum Kind { Set, Clear, Release } kind; PLAYERID id; char corp[16]; };
std::mutex       s_qMutex;
std::vector<Cmd> s_q;

std::mutex  s_infoMutex;
std::string s_info = "nada aplicado (use /corp <nome>)";

// ---- so a thread do jogo daqui para baixo --------------------------------------------------
struct Swap  { RpMaterial* mat; RwTexture* repl; RwTexture* saved; };
struct Entry { PLAYERID id; void* ent; RpClump* clump; std::string corp; std::vector<Swap> swaps; bool swapped; uint32_t renders; };
struct Corp  { std::unordered_map<std::string, RwTexture*> tex; uint64_t lastUse; };

std::string                 s_want[MAX_PLAYERS];     // corp pedida por jogador ("" = nenhuma)
std::string                 s_failedFor[MAX_PLAYERS];// corp que ja falhou ao carregar (nao repete a cada frame)
std::vector<PLAYERID>       s_wantIds;
std::vector<Entry>          s_entries;
std::unordered_map<std::string, Corp> s_corps;
std::vector<std::pair<RwTexture*, uint64_t>> s_graveyard;
uint32_t                    s_totalRenders = 0;
uint64_t                    s_firstEntryMs = 0;
bool                        s_warnedNoHook = false;

uint64_t NowMs() { timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }

void SetInfo(const std::string& t) { { std::lock_guard<std::mutex> l(s_infoMutex); s_info = t; } FLog("[CORPTEX] %s", t.c_str()); }

std::string BaseDir()
{
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    return p + "SAMP/skins/textures/" + kSkinFolder + "/";
}

// nome de pasta aceito: 1..15 de [a-z0-9_-]  (impede "../" vindo do servidor)
bool ValidName(const std::string& s)
{
    if (s.empty() || s.size() > 15) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

std::vector<std::string> ListCorps()
{
    std::vector<std::string> out;
    const std::string base = BaseDir();
    if (base.empty()) return out;
    if (DIR* d = ::opendir(base.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            struct stat st{};
            if (::stat((base + e->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode) && ValidName(e->d_name)) out.push_back(e->d_name);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

CEntityGTA* GetEnt(PLAYERID id)
{
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool) return nullptr;
    CPlayerPed* ped = nullptr;
    if (id == pool->GetLocalPlayerID()) ped = pool->GetLocalPlayer() ? pool->GetLocalPlayer()->GetPlayerPed() : nullptr;
    else if (pool->GetSlotState(id)) { CRemotePlayer* rp = pool->GetAt(id); ped = rp ? rp->GetPlayerPed() : nullptr; }
    return (ped && ped->m_pPed) ? reinterpret_cast<CEntityGTA*>(ped->m_pPed) : nullptr;
}

// ---- RenderWare: materiais do clump (mesma logica do CustomSkin.cpp) -------------------------
RpAtomic* CollectGeomCB(RpAtomic* atomic, void* data)
{
    auto* geoms = static_cast<std::vector<RpGeometry*>*>(data);
    RpGeometry* g = RpAtomicGetGeometry(atomic);
    if (g && std::find(geoms->begin(), geoms->end(), g) == geoms->end()) geoms->push_back(g);
    return atomic;
}

bool CollectMaterials(RpClump* clump, std::vector<RpMaterial*>& out)
{
    out.clear();
    if (!clump) return false;
    std::vector<RpGeometry*> geoms;
    RpClumpForAllAtomics(clump, CollectGeomCB, &geoms);
    for (RpGeometry* g : geoms) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g);
        RpMaterial** arr = *reinterpret_cast<RpMaterial***>(base + OFF_GEOM_MATLIST);
        const int n = *reinterpret_cast<int32_t*>(base + OFF_GEOM_NUMMATERIALS);
        if (!arr || n < 0 || n > kMaxMaterials) return false;
        for (int i = 0; i < n; ++i) out.push_back(arr[i]);
    }
    return !out.empty();
}

// RGBA 8-bit -> RwTexture (mesmos ajustes do CustomSkin::CreateTextureRgba: pot, sem mipmap, linear, WRAP)
RwTexture* CreateTextureRgba(const uint8_t* px, int w, int h, const char* name)
{
    if (!px || w <= 0 || h <= 0) return nullptr;
    std::vector<uint8_t> scaled;
    const int tw = SkinImg::PotFor(w, kMaxTexSize), th = SkinImg::PotFor(h, kMaxTexSize);
    const uint8_t* src = px;
    if (tw != w || th != h) {
        if (!SkinImg::ResizeRgba(px, w, h, tw, th, scaled)) return nullptr;
        src = scaled.data();
    }
    RwImage* image = RwImageCreate(tw, th, 32);
    if (!image) return nullptr;
    if (!RwImageAllocatePixels(image)) { RwImageDestroy(image); return nullptr; }
    for (int y = 0; y < th; ++y) std::memcpy(image->cpPixels + (size_t)y * image->stride, src + (size_t)y * tw * 4, (size_t)tw * 4);

    int rw = tw, rh = th, rd = 32, rf = 0;
    RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
    rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
    if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4)) {
        rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
        rf |= rwRASTERFORMAT8888; rd = 32;
    }
    if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

    RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
    if (!raster) { RwImageDestroy(image); return nullptr; }
    if (!RwRasterSetFromImage(raster, image)) { if (RwRasterDestroy) RwRasterDestroy(raster); RwImageDestroy(image); return nullptr; }
    RwImageDestroy(image);

    RwTexture* tex = RwTextureCreate(raster);
    if (!tex) { if (RwRasterDestroy) RwRasterDestroy(raster); return nullptr; }
    RwTextureSetName(tex, name ? name : "corp");
    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwTextureSetAddressing(tex, rwTEXTUREADDRESSWRAP);
    return tex;
}

RwTexture* LoadPng(const std::string& path, const std::string& name)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px) { FLog("[CORPTEX] AVISO: %s: %s", path.c_str(), stbi_failure_reason()); return nullptr; }
    RwTexture* t = CreateTextureRgba(px, w, h, name.c_str());
    stbi_image_free(px);
    if (!t) FLog("[CORPTEX] AVISO: %s: falha ao criar RwTexture", path.c_str());
    return t;
}

// ---- cache de corps --------------------------------------------------------------------------
Corp* EnsureCorp(const std::string& corp)
{
    auto it = s_corps.find(corp);
    if (it != s_corps.end()) { it->second.lastUse = NowMs(); return &it->second; }

    const std::string dir = BaseDir() + corp + "/";
    const auto idx = SkinImg::IndexPngDir(dir);
    if (idx.empty()) { SetInfo("corp '" + corp + "': nenhum PNG em " + dir); return nullptr; }

    Corp c; c.lastUse = NowMs();
    for (const auto& kv : idx) {                       // kv.first = nome em minusculas, kv.second = caminho
        if (RwTexture* t = LoadPng(kv.second, kv.first)) c.tex[kv.first] = t;
    }
    if (c.tex.empty()) { SetInfo("corp '" + corp + "': nenhum PNG pode ser carregado"); return nullptr; }
    FLog("[CORPTEX] corp '%s' carregada: %zu/%zu PNG(s) (teto %d)", corp.c_str(), c.tex.size(), idx.size(), kMaxTexSize);
    return &(s_corps[corp] = std::move(c));
}

bool CorpInUse(const std::string& corp)
{
    for (const Entry& e : s_entries) if (e.corp == corp) return true;
    for (PLAYERID id : s_wantIds) if (s_want[id] == corp) return true;
    return false;
}

void EvictCorps()
{
    while (s_corps.size() > kMaxCachedCorps) {
        auto victim = s_corps.end();
        for (auto it = s_corps.begin(); it != s_corps.end(); ++it)
            if (!CorpInUse(it->first) && (victim == s_corps.end() || it->second.lastUse < victim->second.lastUse)) victim = it;
        if (victim == s_corps.end()) return;
        for (auto& kv : victim->second.tex) s_graveyard.emplace_back(kv.second, NowMs());   // o render pode ainda usar o raster
        FLog("[CORPTEX] corp '%s' descarregada do cache", victim->first.c_str());
        s_corps.erase(victim);
    }
}

// ---- estado por jogador ----------------------------------------------------------------------
Entry* FindEntry(PLAYERID id) { for (Entry& e : s_entries) if (e.id == id) return &e; return nullptr; }

void DropEntry(PLAYERID id)
{
    for (size_t i = 0; i < s_entries.size(); ++i)
        if (s_entries[i].id == id) { s_entries.erase(s_entries.begin() + (long)i); break; }
    g_active.store((int)s_entries.size());
}

void BuildEntry(PLAYERID id, CEntityGTA* e, Corp* c, const std::string& corp)
{
    std::vector<RpMaterial*> mats;
    if (!CollectMaterials(e->m_pRwClump, mats)) { SetInfo("jogador " + std::to_string(id) + ": clump sem materiais legiveis"); return; }

    const DffPreflight::Info* info = CustomSkinStream::GetInfo();
    const bool byIndex = info && info->materialsList.size() == mats.size();
    if (!byIndex) FLog("[CORPTEX] AVISO: motor tem %zu materiais, DFF %zu: usando o nome da textura em runtime (pecas fora do texdb viram 'male01' e nao trocam)",
                       mats.size(), info ? info->materialsList.size() : (size_t)0);

    Entry en{id, e, e->m_pRwClump, corp, {}, false, 0};
    std::vector<std::string> used, hidden;

    // textura invisivel (male01) do banco do jogo; sem ela as pecas ausentes ficam com a textura padrao
    RwTexture* hideTex = byIndex ? TextureDatabaseRuntime::GetTexture(kHideTexture) : nullptr;
    if (byIndex && !hideTex) FLog("[CORPTEX] AVISO: textura '%s' nao achada no texdb: pecas que a corp nao tem ficam com a textura padrao", kHideTexture);

    for (size_t i = 0; i < mats.size(); ++i) {
        std::string key;
        if (byIndex) key = SkinImg::Lower(info->materialsList[i].texture);
        else if (mats[i] && mats[i]->texture) key = SkinImg::Lower(std::string(mats[i]->texture->name, strnlen(mats[i]->texture->name, 32)));
        if (key.empty() || !mats[i]) continue;
        auto it = c->tex.find(key);
        if (it == c->tex.end()) {
            // peca de roupa da Corp que esta corp nao tem -> oculta (o nome vem do DFF, so no modo indice)
            if (hideTex && byIndex && key.compare(0, std::strlen(kPiecePrefix), kPiecePrefix) == 0) {
                en.swaps.push_back({mats[i], hideTex, nullptr});
                hidden.push_back(key);
            }
            continue;
        }
        en.swaps.push_back({mats[i], it->second, nullptr});
        used.push_back(key);
    }
    std::string unused;
    for (const auto& kv : c->tex) if (std::find(used.begin(), used.end(), kv.first) == used.end()) unused += " " + kv.first;

    std::string hid;
    for (const std::string& h : hidden) hid += " " + h;
    FLog("[CORPTEX] jogador %d corp=%s: %zu material(is) trocado(s) de %zu (modo %s), %zu oculto(s)%s%s%s", (int)id, corp.c_str(), en.swaps.size(), mats.size(),
         byIndex ? "indice" : "nome", hidden.size(), hid.empty() ? "" : " [ocultos:", hid.c_str(), hid.empty() ? "" : "]");
    if (!unused.empty()) FLog("[CORPTEX]   PNG sem material:%s", unused.c_str());
    if (en.swaps.empty()) { SetInfo("corp '" + corp + "': nenhum material do ped casou com os PNGs"); return; }

    if (s_entries.empty()) { s_firstEntryMs = NowMs(); s_totalRenders = 0; s_warnedNoHook = false; }
    s_entries.push_back(std::move(en));
    g_active.store((int)s_entries.size());
    SetInfo("jogador " + std::to_string(id) + ": corp '" + corp + "' aplicada (" + std::to_string(s_entries.back().swaps.size()) + " textura(s))");
}

void Reconcile(PLAYERID id)
{
    const std::string& corp = s_want[id];
    CEntityGTA* e = GetEnt(id);
    Entry* en = FindEntry(id);

    if (!e || (int)e->GetModelId() != kHostModel || !e->m_pRwClump) { if (en) DropEntry(id); return; }   // ped ainda nao existe / outra skin
    if (en && en->ent == (void*)e && en->clump == e->m_pRwClump && en->corp == corp) return;             // em dia
    if (en) DropEntry(id);                                                                                // ped/clump recriado ou corp mudou
    if (s_failedFor[id] == corp) return;

    Corp* c = EnsureCorp(corp);
    if (!c) { s_failedFor[id] = corp; return; }
    BuildEntry(id, e, c, corp);
}

void ForgetWant(PLAYERID id)
{
    DropEntry(id);
    s_want[id].clear(); s_failedFor[id].clear();
    s_wantIds.erase(std::remove(s_wantIds.begin(), s_wantIds.end(), id), s_wantIds.end());
}

void Push(Cmd::Kind k, PLAYERID id, const std::string& corp)
{
    Cmd c{}; c.kind = k; c.id = id;
    std::strncpy(c.corp, corp.c_str(), sizeof(c.corp) - 1);
    std::lock_guard<std::mutex> l(s_qMutex);
    s_q.push_back(c);
}

// ---- RPC -------------------------------------------------------------------------------------
void ScrCorpTex(RPCParameters* rpcParams)
{
    RakNet::BitStream bs((unsigned char*)rpcParams->input, (rpcParams->numberOfBitsOfData / 8) + 1, false);
    PLAYERID id = 0; uint8_t len = 0; char corp[16] = {};
    if (!bs.Read(id) || id >= MAX_PLAYERS) return;
    if (!bs.Read(len) || len >= sizeof(corp)) return;
    if (len && !bs.Read(corp, len)) return;
    corp[len] = 0;
    if (len == 0) { Push(Cmd::Clear, id, ""); return; }
    std::string name = SkinImg::Lower(corp);
    if (!ValidName(name)) return;
    Push(Cmd::Set, id, name);
}

} // namespace

void Register(RakClientInterface* rak)   { rak->RegisterAsRemoteProcedureCall(&RPC_CorpTex, ScrCorpTex); }
void Unregister(RakClientInterface* rak) { rak->UnregisterAsRemoteProcedureCall(&RPC_CorpTex); }

void ClearAll()
{
    { std::lock_guard<std::mutex> l(s_qMutex); s_q.clear(); }
    Push(Cmd::Release, 0xFFFF, "");       // 0xFFFF = todos (executado no Process, na thread do jogo)
}

void OnPlayerStreamIn(PLAYERID) { /* o Process reaplica sozinho assim que o ped existir com o modelo certo */ }

void OnPlayerStreamOut(PLAYERID id)
{
    if (id < MAX_PLAYERS) Push(Cmd::Release, id, "");      // solta ANTES do ped morrer (servidor reenvia no stream-in)
}

void Process()
{
    std::vector<Cmd> cmds;
    { std::lock_guard<std::mutex> l(s_qMutex); cmds.swap(s_q); }

    for (const Cmd& c : cmds) {
        if (c.kind == Cmd::Release && c.id == 0xFFFF) { while (!s_wantIds.empty()) ForgetWant(s_wantIds.back()); continue; }
        if (c.id >= MAX_PLAYERS) continue;
        if (c.kind == Cmd::Set) {
            if (std::find(s_wantIds.begin(), s_wantIds.end(), c.id) == s_wantIds.end()) s_wantIds.push_back(c.id);
            s_want[c.id] = c.corp; s_failedFor[c.id].clear();
        } else {                                            // Clear ou Release de um jogador
            ForgetWant(c.id);
        }
    }

    for (size_t i = 0; i < s_wantIds.size(); ++i) Reconcile(s_wantIds[i]);   // ped novo, modelo trocado, clump recriado...

    const uint64_t now = NowMs();
    for (size_t i = 0; i < s_graveyard.size();) {
        if (now - s_graveyard[i].second >= kGraveyardMs) { RwTextureDestroy(s_graveyard[i].first); s_graveyard.erase(s_graveyard.begin() + (long)i); }
        else ++i;
    }
    EvictCorps();

    if (!s_entries.empty() && !s_warnedNoHook && s_totalRenders == 0 && now - s_firstEntryMs > 5000) {
        s_warnedNoHook = true;
        FLog("[CORPTEX] AVISO: ha %zu ped(s) com corp ativa mas CEntity::Render nunca foi chamado para eles em 5 s. "
             "CPed::Render nao passa por CEntity::Render neste build: e preciso um hook em CPed::Render (offset a localizar no Ghidra).", s_entries.size());
    }
}

bool BeginRender(void* entity)
{
    CEntityGTA* e = static_cast<CEntityGTA*>(entity);
    if (!e || !e->IsPed()) return false;
    for (Entry& en : s_entries) {
        if (en.ent != entity) continue;
        if (e->m_pRwClump != en.clump) return false;        // clump trocado desde o ultimo Process: nao toca em ponteiros velhos
        for (Swap& s : en.swaps) { s.saved = s.mat->texture; s.mat->texture = s.repl; }
        en.swapped = true;
        ++s_totalRenders;
        if (en.renders++ == 0) FLog("[CORPTEX] render do jogador %d interceptado (%zu troca(s))", (int)en.id, en.swaps.size());
        return true;
    }
    return false;
}

void EndRender(void* entity)
{
    for (Entry& en : s_entries) {
        if (en.ent != entity || !en.swapped) continue;
        for (Swap& s : en.swaps) s.mat->texture = s.saved;
        en.swapped = false;
        return;
    }
}

std::string DebugCommand(const std::string& argsIn)
{
    std::string a = argsIn;
    while (!a.empty() && (a.front() == ' ')) a.erase(a.begin());
    while (!a.empty() && (a.back() == ' ')) a.pop_back();
    a = SkinImg::Lower(a);

    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool) return "[CORPTEX] conecte-se a um servidor primeiro";
    const PLAYERID me = pool->GetLocalPlayerID();

    if (a.empty() || a == "info") {
        std::string s; { std::lock_guard<std::mutex> l(s_infoMutex); s = s_info; }
        return "[CORPTEX] " + s + " | ativos=" + std::to_string(g_active.load()) + " | uso: /corp <nome> | limpar | lista";
    }
    if (a == "lista" || a == "list") {
        std::string s;
        for (const std::string& c : ListCorps()) s += (s.empty() ? "" : ", ") + c;
        return s.empty() ? "[CORPTEX] nenhuma pasta em SAMP/skins/textures/" + std::string(kSkinFolder) + "/" : "[CORPTEX] corps: " + s;
    }
    if (a == "limpar" || a == "clear" || a == "off" || a == "reset") { Push(Cmd::Clear, me, ""); return "[CORPTEX] limpando a sua corp"; }

    if (!ValidName(a)) return "[CORPTEX] nome invalido (use so a-z 0-9 _ -, ate 15)";
    const auto corps = ListCorps();
    if (std::find(corps.begin(), corps.end(), a) == corps.end()) return "[CORPTEX] corp '" + a + "' nao existe (use /corp lista)";
    Push(Cmd::Set, me, a);
//    return "[CORPTEX] corp '" + a + "' pedida; veja /corp info e o log [CORPTEX]";
    return;
}

} // namespace CorpTex
