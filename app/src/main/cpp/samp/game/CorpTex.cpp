// CorpTex.cpp - aparencia por jogador: corporacao (roupas), rosto, tom de pele e cabelo. Ver CorpTex.h
//
// POR QUE TROCAR NO RENDER (e nao no molde do modelo):
//   Os 76 (Corp) / 96 (Normal) materiais do DFF pertencem a geometria compartilhada pelo molde do modelo e por todos os
//   clones (RpClumpClone so referencia a geometria). Trocar a textura "de verdade" mudaria TODOS os peds
//   do modelo. Entao, em CEntity::Render do ped escolhido, pomos as texturas nos materiais (atribuicao
//   direta do ponteiro, sem mexer em refcount), o jogo desenha, e devolvemos a original logo depois.
//   Nossas texturas ficam vivas (1 ref nossa) enquanto houver ped usando.
//
// MAPEAMENTO MATERIAL -> NOME
//   O DFF so guarda nomes; quando a textura nao existe no texdb, o GetTexture_hook devolve "male01" (invisivel),
//   entao o nome em runtime pode ser "male01" e nao serve de chave. A chave e o nome gravado no DFF
//   (DffPreflight::Info::materialsList[i].texture), pelo INDICE do material. Rosto/cabelo/pele/ocultar
//   so funcionam nesse modo; se a contagem do motor diferir da do DFF, so a corp (por nome) e aplicada.
//
// MODELOS: 7 = FemininaCorp (corp + aparencia), 9 = FemininaNormal (so aparencia: mesmos body.*, hair e head do DFF da Corp;
//   as roupas fantasy_f_* da Normal ainda nao foram mapeadas e continuam ocultas pela male01 do texdb).
//
// O QUE CADA OPCAO FAZ (tudo e "mostrar a escolhida com o PNG certo, ocultar as outras com a male01")
//   corp  : pecas fantasy_f_cop_* com PNG em textures/272/<corp>/ recebem o PNG; as sem PNG ficam ocultas.
//           Sem corp escolhida, as pecas nao sao tocadas.
//   rosto : body.head1-5 sao a MESMA malha; mostra body.head1 com rosto/<tom>/<variante>.png, oculta head2-5.
//   pele  : o tom vale para torso/bracos/ombros/maos (pele/torso), pernas (pele/leg) e pes (pele/feet). Regiao do
//           corpo totalmente coberta por uma peca visivel e ocultada (evita z-fight); parcial fica com pele.
//   cabelo: fantasy_f_hair<estilo> com cabelo/<estilo>/<cor>.png; os outros 4 estilos ocultos.
//
// LIMITES CONHECIDOS (confirmar em jogo, ver log [CORPTEX]):
//   * Se CPed::Render nao chamar CEntity::Render, o hook nao e atingido; o Process loga um AVISO apos 5 s.
//   * Ped desenhado pela lista de alfa (fade de distancia) nao passa por CEntity::Render.
#include "CorpTex.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

constexpr int      kCorpModel      = 7;      // hospeda o DFF da FemininaCorp (so este tem as pecas fantasy_f_cop_*)
constexpr int      kNormalModel    = 9;      // hospeda o DFF da FemininaNormal (mesmo corpo: rosto, cabelo, tom de pele)
// (quando migrar o DFF da Corp para o 272, troque kCorpModel e a tabela s_skins do CustomSkinStream.cpp)
constexpr int      kMaleModel      = 271;    // hospeda o DFF masculino (male.dff): so texturas por NOME do material (sem rosto/pele/roupa)
constexpr const char* kMaleDefault = "masculino";   // pasta usada pelo 271 quando nenhuma /corp foi escolhida: textures/272/masculino/
bool IsHostModel(int m) { return m == kCorpModel || m == kNormalModel || m == kMaleModel; }
constexpr const char* kSkinFolder  = "272";  // textures/<kSkinFolder>/<corp>/  e  textures/<kSkinFolder>/aparencia/...
constexpr const char* kAppFolder   = "aparencia";
constexpr int      kMaxTexSize     = 1024;   // teto de lado no celular (os PNGs de 2048 sao reduzidos)
constexpr int      kMaxMaterials   = 512;
constexpr size_t   kMaxCachedCorps = 3;
constexpr size_t   kMaxAppTex      = 40;     // texturas de rosto/cabelo/pele em cache
constexpr uint64_t kGraveyardMs    = 5000;
constexpr const char* kHideTexture = "male01";          // textura invisivel do texdb
constexpr const char* kPiecePrefix = "fantasy_f_cop_";  // pecas de roupa da Corp
constexpr uintptr_t OFF_GEOM_MATLIST      = 0x28;   // RpGeometry::matList.materials  (igual ao CustomSkin.cpp)
constexpr uintptr_t OFF_GEOM_NUMMATERIALS = 0x30;   // RpGeometry::matList.numMaterials

// opcoes validas (conferidas contra as pastas): variantes de rosto por tom, cores de cabelo por estilo
constexpr int kMaxClothSet = 9;       // pastas de roupa: 1..7 (boot) / 1..6; o limite real e o que existe em disco
constexpr int kFaceVariants[7] = {0, 6, 8, 8, 7, 6, 12};
constexpr int kHairColors[6]   = {0, 6, 6, 7, 7, 7};
constexpr const char* kToneNames[7] = {"nenhum", "medio", "escuro", "claro", "moreno", "avermelhado", "palido"};

// caixas (x absoluto maximo, z min, z max) tiradas da geometria do skin.dff; usadas para saber se uma peca cobre uma regiao
struct Box { const char* name; float xabs, zmin, zmax; };
constexpr Box kBodyRegions[] = {
    {"body.feets2", 0.19f, -0.99f, -0.96f},      // so no DFF da FemininaNormal
    {"body.armleft", 0.51f, 0.01f, 0.40f},
    {"body.armright", 0.51f, 0.01f, 0.40f},
    {"body.feets", 0.21f, -1.00f, -0.90f},
    {"body.legleft1", 0.20f, -0.19f, 0.06f},
    {"body.legleft2", 0.20f, -0.25f, -0.14f},
    {"body.legleft3", 0.21f, -0.81f, -0.21f},
    {"body.legleft4", 0.19f, -0.88f, -0.80f},
    {"body.legleft5", 0.19f, -0.96f, -0.88f},
    {"body.legleft6", 0.19f, -0.61f, -0.21f},
    {"body.legright1", 0.20f, -0.19f, 0.06f},
    {"body.legright2", 0.20f, -0.25f, -0.14f},
    {"body.legright3", 0.21f, -0.81f, -0.21f},
    {"body.legright4", 0.19f, -0.88f, -0.80f},
    {"body.legright5", 0.19f, -0.96f, -0.88f},
    {"body.legright6", 0.19f, -0.61f, -0.20f},
    {"body.shoulderleft", 0.28f, 0.33f, 0.53f},
    {"body.shoulderright", 0.28f, 0.33f, 0.53f},
    {"body.torso1", 0.19f, 0.24f, 0.57f},
    {"body.torso3", 0.15f, -0.00f, 0.40f},
    {"body.torso4", 0.13f, 0.08f, 0.24f}
};
constexpr Box kPieces[] = {
    {"fantasy_f_cop_boot1", 0.22f, -1.00f, -0.79f},
    {"fantasy_f_cop_boot2", 0.22f, -0.99f, -0.74f},
    {"fantasy_f_cop_hat2", 0.07f, 0.74f, 0.90f},
    {"fantasy_f_cop_hat3", 0.10f, 0.73f, 0.83f},
    {"fantasy_f_cop_pants1", 0.25f, -0.84f, 0.11f},
    {"fantasy_f_cop_short1", 0.22f, -0.43f, 0.10f},
    {"fantasy_f_cop_torso1", 0.16f, 0.02f, 0.57f},
    {"fantasy_f_cop_torso2", 0.30f, 0.00f, 0.58f},
    {"fantasy_f_cop_torso3", 0.29f, 0.03f, 0.61f},
    {"fantasy_f_cop_torso4", 0.55f, -0.02f, 0.62f},
    {"fantasy_f_cop_vest1", 0.18f, 0.09f, 0.59f},
    {"fantasy_f_cop_vest2", 0.20f, 0.13f, 0.58f},
    {"fantasy_f_cop_vest3", 0.20f, 0.05f, 0.60f}
};

constexpr Box kPieces9[] = {   // roupas da FemininaNormal (modelo 9)
    {"fantasy_f_bikinibotton", 0.16f, -0.13f, 0.10f},
    {"fantasy_f_bikinitop", 0.15f, 0.26f, 0.57f},
    {"fantasy_f_boot1", 0.21f, -1.00f, -0.86f},
    {"fantasy_f_boot2", 0.22f, -0.99f, -0.88f},
    {"fantasy_f_boot3", 0.21f, -1.01f, -0.85f},
    {"fantasy_f_boot5", 0.21f, -1.01f, -0.82f},
    {"fantasy_f_pants1", 0.21f, -0.91f, 0.11f},
    {"fantasy_f_pants2", 0.26f, -0.96f, 0.11f},
    {"fantasy_f_pants4", 0.23f, -0.82f, 0.11f},
    {"fantasy_f_pants5", 0.21f, -0.90f, 0.11f},
    {"fantasy_f_short1", 0.20f, -0.19f, 0.11f},
    {"fantasy_f_slipper1", 0.21f, -1.00f, -0.94f},
    {"fantasy_f_slipper2", 0.22f, -0.99f, -0.92f},
    {"fantasy_f_slipper3", 0.21f, -1.00f, -0.79f},
    {"fantasy_f_torso1", 0.16f, 0.19f, 0.56f},
    {"fantasy_f_torso2", 0.31f, 0.10f, 0.59f},
    {"fantasy_f_torso3", 0.15f, 0.03f, 0.38f},
    {"fantasy_f_torso4", 0.30f, 0.00f, 0.58f},
    {"fantasy_f_torso5", 0.56f, -0.07f, 0.66f},
    {"fantasy_f_torso6", 0.21f, -0.25f, 0.56f}
};

// pecas da Corp visiveis por padrao (as que existem no texdb), usadas quando nenhuma corp foi escolhida
constexpr const char* kDefaultVisible[] = {"fantasy_f_cop_boot2", "fantasy_f_cop_hat3", "fantasy_f_cop_pants1",
                                           "fantasy_f_cop_torso3", "fantasy_f_cop_torso4", "fantasy_f_cop_vest3"};

struct Look {
    std::string corp;
    uint8_t tone = 0, face = 0, hairStyle = 0, hairColor = 0;
    uint8_t bootSet = 0, bootVar = 0, pantsSet = 0, pantsVar = 0, shirtSet = 0, shirtVar = 0;   // roupas da Normal (pasta + variante)
    uint8_t under = 0;                                                                           // roupa intima: 0 padrao (preto), 1..6 cor, 9 nenhuma
    bool Active() const { return !corp.empty() || tone || hairStyle || bootSet || pantsSet || shirtSet; }
    bool operator==(const Look& o) const {
        return corp == o.corp && tone == o.tone && face == o.face && hairStyle == o.hairStyle && hairColor == o.hairColor &&
               bootSet == o.bootSet && bootVar == o.bootVar && pantsSet == o.pantsSet && pantsVar == o.pantsVar &&
               shirtSet == o.shirtSet && shirtVar == o.shirtVar && under == o.under;
    }
};

// ---- fila RPC/chat -> thread do jogo -------------------------------------------------------
struct Cmd { enum Kind { Set, Clear, Release, Reload } kind; PLAYERID id; Look look; };
std::mutex       s_qMutex;
std::vector<Cmd> s_q;
Look             s_localLook;          // espelho do seu proprio visual (protegido por s_qMutex): base dos comandos /rosto, /cabelo...

std::mutex  s_infoMutex;
std::string s_info = "nada aplicado (use /corp, /rosto, /cabelo, /pele)";

// ---- so a thread do jogo daqui para baixo --------------------------------------------------
struct Swap  { RpMaterial* mat; RwTexture* repl; RwTexture* saved; };
struct Entry { PLAYERID id; void* ent; RpClump* clump; Look look; std::vector<Swap> swaps; bool swapped; uint32_t renders; };
struct Corp  { std::unordered_map<std::string, RwTexture*> tex; uint64_t lastUse; };
struct Tried { void* ent = nullptr; RpClump* clump = nullptr; Look look; bool valid = false; };
struct AppTex { RwTexture* tex; uint64_t lastUse; };

Look                        s_look[MAX_PLAYERS];     // visual pedido por jogador
Tried                       s_tried[MAX_PLAYERS];    // ultima construcao tentada (nao repete a cada frame)
std::vector<PLAYERID>       s_wantIds;
std::vector<Entry>          s_entries;
std::unordered_map<std::string, Corp>   s_corps;
std::unordered_map<std::string, AppTex> s_appTex;    // chave = caminho relativo a aparencia/ (ex.: rosto/3/5.png)
std::vector<std::pair<RwTexture*, uint64_t>> s_graveyard;
uint32_t                    s_totalRenders = 0;
uint64_t                    s_firstEntryMs = 0;
bool                        s_warnedNoHook = false;

uint64_t NowMs() { timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }

void SetInfo(const std::string& t) { { std::lock_guard<std::mutex> l(s_infoMutex); s_info = t; } FLog("[CORPTEX] %s", t.c_str()); }

std::string TexRoot()      // .../SAMP/skins/textures/
{
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    return p + "SAMP/skins/textures/";
}

std::string BaseDir()
{
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    return p + "SAMP/skins/textures/" + kSkinFolder + "/";
}

// ---- roupas da FemininaNormal: onde ficam os PNGs ------------------------------------------------
// Local novo (MESMA arvore que ja funciona para rosto/pele/cabelo/corp):  textures/272/aparencia/roupa/<tipo>/<pasta>/<variante>.png
// Local antigo (ainda aceito):                                          textures/9/roupa/...
// Escolhe o primeiro que ABRE de verdade (fopen), nao so o que existe (access): no Android um arquivo copiado por
// outro app pode existir e mesmo assim dar "Permission denied" para o jogo.
bool CanOpen(const std::string& p) { FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false; std::fclose(f); return true; }

std::string ClothPath(const std::string& sub)      // sub = "boot/1/1.png" | "mapa.txt"
{
    const std::string a = BaseDir() + kAppFolder + "/roupa/" + sub;
    const std::string b = TexRoot() + "9/roupa/" + sub;
    if (CanOpen(a)) return a;
    if (CanOpen(b)) return b;
    if (::access(a.c_str(), F_OK) == 0) return a;   // existe mas nao abre: o chamador mostra o erro
    if (::access(b.c_str(), F_OK) == 0) return b;
    return a;
}

// nome de pasta aceito: 1..15 de [a-z0-9_-]  (impede "../" vindo do servidor)
bool ValidName(const std::string& s)
{
    if (s.empty() || s.size() > 15) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

// ajusta o visual a valores validos (vale para o RPC e para os comandos)
void Sanitize(Look& l)
{
    l.corp = SkinImg::Lower(l.corp);
    if (!ValidName(l.corp) || l.corp == kAppFolder) l.corp.clear();
    if (l.tone > 6) l.tone = 0;
    if (l.tone) { if (l.face < 1) l.face = 1; if (l.face > kFaceVariants[l.tone]) l.face = (uint8_t)kFaceVariants[l.tone]; }
    else l.face = 0;
    auto cloth = [](uint8_t& set, uint8_t& var) { if (set > kMaxClothSet) set = 0; if (!set) var = 0; else if (var < 1) var = 1; };
    cloth(l.bootSet, l.bootVar); cloth(l.pantsSet, l.pantsVar); cloth(l.shirtSet, l.shirtVar);
    if (l.under > 6 && l.under != 9) l.under = 0;
    if (l.hairStyle > 5) l.hairStyle = 0;
    if (l.hairStyle) { if (l.hairColor < 1) l.hairColor = 1; if (l.hairColor > kHairColors[l.hairStyle]) l.hairColor = (uint8_t)kHairColors[l.hairStyle]; }
    else l.hairColor = 0;
}

std::vector<std::string> ListCorps()
{
    std::vector<std::string> out;
    const std::string base = BaseDir();
    if (base.empty()) return out;
    if (DIR* d = ::opendir(base.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.' || std::strcmp(e->d_name, kAppFolder) == 0) continue;
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

// ---- cache de corps (carrega a pasta inteira: sao poucas pecas) --------------------------------
Corp* EnsureCorp(const std::string& corp)
{
    auto it = s_corps.find(corp);
    if (it != s_corps.end()) { it->second.lastUse = NowMs(); return &it->second; }

    const std::string dir = BaseDir() + corp + "/";
    const auto idx = SkinImg::IndexPngDir(dir);
    if (idx.empty()) { SetInfo("corp '" + corp + "': nenhum PNG em " + dir); return nullptr; }

    Corp c; c.lastUse = NowMs();
    for (const auto& kv : idx)                         // kv.first = nome em minusculas, kv.second = caminho
        if (RwTexture* t = LoadPng(kv.second, kv.first)) c.tex[kv.first] = t;
    if (c.tex.empty()) { SetInfo("corp '" + corp + "': nenhum PNG pode ser carregado"); return nullptr; }
    FLog("[CORPTEX] corp '%s' carregada: %zu/%zu PNG(s) (teto %d)", corp.c_str(), c.tex.size(), idx.size(), kMaxTexSize);
    return &(s_corps[corp] = std::move(c));
}

// ---- cache de texturas de aparencia (1 PNG por vez, sob demanda) -------------------------------
RwTexture* GetAppTex(const std::string& rel)
{
    auto it = s_appTex.find(rel);
    if (it != s_appTex.end()) { it->second.lastUse = NowMs(); return it->second.tex; }
    // "roupa/..." : ver ClothPath (272/aparencia/roupa ou 9/roupa); o resto em textures/272/aparencia/
    const std::string path = rel.compare(0, 6, "roupa/") == 0 ? ClothPath(rel.substr(6)) : BaseDir() + kAppFolder + "/" + rel;
    RwTexture* t = ::access(path.c_str(), F_OK) == 0 ? LoadPng(path, rel) : nullptr;
    if (!t) FLog("[CORPTEX] AVISO: textura de aparencia ausente: %s", path.c_str());
    s_appTex[rel] = AppTex{t, NowMs()};                // guarda tambem a falha (nao tenta de novo a cada frame)
    return t;
}

// cor lisa para a roupa intima (bikinitop/bikinibotton nao tem PNG no pacote)
RwTexture* GetSolidTex(int idx)
{
    static const uint8_t kCol[7][3] = {{20, 20, 20}, {20, 20, 20}, {235, 235, 235}, {190, 30, 40}, {30, 60, 170}, {230, 120, 170}, {120, 120, 120}};
    if (idx < 1 || idx > 6) idx = 1;
    const std::string key = "solid/" + std::to_string(idx);
    auto it = s_appTex.find(key);
    if (it != s_appTex.end()) { it->second.lastUse = NowMs(); return it->second.tex; }
    std::vector<uint8_t> px(16 * 16 * 4);
    for (size_t i = 0; i < 16 * 16; ++i) { px[i * 4] = kCol[idx][0]; px[i * 4 + 1] = kCol[idx][1]; px[i * 4 + 2] = kCol[idx][2]; px[i * 4 + 3] = 255; }
    RwTexture* t = CreateTextureRgba(px.data(), 16, 16, key.c_str());
    s_appTex[key] = AppTex{t, NowMs()};
    return t;
}

// ---- mapa pasta -> material(is) das roupas da Normal (editavel em textures/9/roupa/mapa.txt) ---------------
// formato do arquivo, uma linha por pasta:   tipo pasta material[,material...]     (# comentario)
//   ex.:  tshirts 2 fantasy_f_torso1       boot 4 fantasy_f_boot5
struct ClothMap { std::string tipo; int pasta; std::vector<std::string> mats; };
std::vector<ClothMap> s_clothMap;
bool s_clothMapLoaded = false;

constexpr const char* kDefaultClothMap[][3] = {      // PALPITE a confirmar em jogo (ver mapa_roupas.json)
    {"boot", "1", "fantasy_f_boot1"},   {"boot", "2", "fantasy_f_boot2"},   {"boot", "3", "fantasy_f_boot3"},
    {"boot", "4", "fantasy_f_boot5"},   {"boot", "5", "fantasy_f_slipper1"}, {"boot", "6", "fantasy_f_slipper2"}, {"boot", "7", "fantasy_f_slipper3"},
    {"pants", "1", "fantasy_f_pants1"}, {"pants", "2", "fantasy_f_pants2"}, {"pants", "3", "fantasy_f_pants4"},
    {"pants", "4", "fantasy_f_pants5"}, {"pants", "5", "fantasy_f_short1"}, {"pants", "6", "fantasy_f_torso6"},
    {"tshirts", "1", "fantasy_f_torso1"}, {"tshirts", "2", "fantasy_f_torso2"}, {"tshirts", "3", "fantasy_f_torso3"},
    {"tshirts", "4", "fantasy_f_torso4"}, {"tshirts", "5", "fantasy_f_torso5"}, {"tshirts", "6", "fantasy_f_torso6"},
};

void SetClothMap(const std::string& tipo, int pasta, const std::vector<std::string>& mats)
{
    for (ClothMap& c : s_clothMap) if (c.tipo == tipo && c.pasta == pasta) { c.mats = mats; return; }
    s_clothMap.push_back({tipo, pasta, mats});
}

void LoadClothMap()
{
    s_clothMap.clear();
    for (const auto& d : kDefaultClothMap) SetClothMap(d[0], std::atoi(d[1]), {d[2]});
    int overridden = 0;
    const std::string path = ClothPath("mapa.txt");
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            std::string l = line;
            const size_t hash = l.find('#'); if (hash != std::string::npos) l.erase(hash);
            char tipo[16] = {}, mats[200] = {}; int pasta = 0;
            if (std::sscanf(l.c_str(), "%15s %d %199s", tipo, &pasta, mats) != 3) continue;
            std::vector<std::string> v; std::string cur;
            for (char* c = mats;; ++c) { if (*c == ',' || *c == 0) { if (!cur.empty()) v.push_back(SkinImg::Lower(cur)); cur.clear(); if (!*c) break; } else cur.push_back(*c); }
            const std::string t = SkinImg::Lower(tipo);
            if ((t == "boot" || t == "pants" || t == "tshirts") && pasta > 0 && !v.empty()) { SetClothMap(t, pasta, v); ++overridden; }
        }
        std::fclose(f);
    }
    s_clothMapLoaded = true;
    FLog("[CORPTEX] mapa de roupas: %zu pasta(s) (%d vindas de %s)", s_clothMap.size(), overridden, path.c_str());
}

const std::vector<std::string>* ClothMats(const char* tipo, int pasta)
{
    if (!s_clothMapLoaded) LoadClothMap();
    for (const ClothMap& c : s_clothMap) if (c.tipo == tipo && c.pasta == pasta) return &c.mats;
    return nullptr;
}

bool CorpInUse(const std::string& corp)
{
    for (const Entry& e : s_entries) if (e.look.corp == corp) return true;
    for (PLAYERID id : s_wantIds) if (s_look[id].corp == corp) return true;
    return false;
}

void EvictCaches()
{
    while (s_corps.size() > kMaxCachedCorps) {
        auto victim = s_corps.end();
        for (auto it = s_corps.begin(); it != s_corps.end(); ++it)
            if (!CorpInUse(it->first) && (victim == s_corps.end() || it->second.lastUse < victim->second.lastUse)) victim = it;
        if (victim == s_corps.end()) break;
        for (auto& kv : victim->second.tex) s_graveyard.emplace_back(kv.second, NowMs());   // o render pode ainda usar o raster
        FLog("[CORPTEX] corp '%s' descarregada do cache", victim->first.c_str());
        s_corps.erase(victim);
    }
    if (s_appTex.size() > kMaxAppTex) {
        std::unordered_set<RwTexture*> inUse;
        for (const Entry& e : s_entries) for (const Swap& s : e.swaps) inUse.insert(s.repl);
        std::vector<std::pair<uint64_t, std::string>> order;
        for (auto& kv : s_appTex) if (!kv.second.tex || !inUse.count(kv.second.tex)) order.emplace_back(kv.second.lastUse, kv.first);
        std::sort(order.begin(), order.end());
        for (size_t i = 0; i < order.size() && s_appTex.size() > kMaxAppTex; ++i) {
            auto it = s_appTex.find(order[i].second);
            if (it->second.tex) s_graveyard.emplace_back(it->second.tex, NowMs());
            s_appTex.erase(it);
        }
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

bool PieceCovers(const Box& p, const Box& r)
{
    constexpr float tol = 0.03f;
    return p.zmin <= r.zmin + tol && p.zmax >= r.zmax - tol && p.xabs >= r.xabs - tol;
}

const Box* FindPiece(const std::string& key)
{
    for (const Box& b : kPieces) if (key == b.name) return &b;
    for (const Box& b : kPieces9) if (key == b.name) return &b;
    return nullptr;
}

void BuildEntry(PLAYERID id, CEntityGTA* e, const Look& look)
{
    std::vector<RpMaterial*> mats;
    if (!CollectMaterials(e->m_pRwClump, mats)) { SetInfo("jogador " + std::to_string(id) + ": clump sem materiais legiveis"); return; }

    const int model = (int)e->GetModelId();
    const DffPreflight::Info* info = CustomSkinStream::GetInfo(model);
    const bool byIndex = info && info->materialsList.size() == mats.size();
    if (!byIndex) FLog("[CORPTEX] AVISO: modelo %d: motor tem %zu materiais, DFF %zu: modo nome (so a corp, sem ocultar/rosto/cabelo/pele)",
                       model, mats.size(), info ? info->materialsList.size() : (size_t)0);

    std::vector<std::string> keys(mats.size());
    for (size_t i = 0; i < mats.size(); ++i) {
        if (byIndex) keys[i] = SkinImg::Lower(info->materialsList[i].texture);
        else if (mats[i] && mats[i]->texture) keys[i] = SkinImg::Lower(std::string(mats[i]->texture->name, strnlen(mats[i]->texture->name, 32)));
    }

    RwTexture* hideTex = byIndex ? TextureDatabaseRuntime::GetTexture(kHideTexture) : nullptr;
    if (byIndex && !hideTex) FLog("[CORPTEX] AVISO: textura '%s' nao achada no texdb: nada sera ocultado", kHideTexture);

    std::unordered_map<std::string, RwTexture*> over;   // nome do DFF (minusculo) -> textura a usar
    std::vector<const Box*> visible;                    // pecas da Corp visiveis (para a regra de cobertura)
    int nCorp = 0, nHidden = 0, nApp = 0;

    // 1) corporacao
    Corp* c = nullptr;
    // Vale para os dois modelos: no 7 (Corp) as pecas fantasy_f_cop_*; no 9 (Normal) qualquer peca pelo NOME do material
    // (fantasy_f_torso2.png, fantasy_f_pants1.png, fantasy_f_boot1.png ...) dentro de textures/272/<pasta>/  ->  /corp <pasta>
    // Modelo 271 (masculino): sem /corp usa a pasta padrao, senao o corpo fica invisivel (todo material sem PNG = male01 = oculto)
    const std::string corpName = (look.corp.empty() && model == kMaleModel) ? std::string(kMaleDefault) : look.corp;
    if (!corpName.empty()) { c = EnsureCorp(corpName); if (!c) FLog("[CORPTEX] corp '%s' indisponivel: ignorada", corpName.c_str()); }
    for (size_t i = 0; i < mats.size(); ++i) {
        const std::string& k = keys[i];
        if (k.empty() || !mats[i]) continue;
        if (c) {
            auto it = c->tex.find(k);
            if (it != c->tex.end()) { over[k] = it->second; ++nCorp; if (const Box* b = FindPiece(k)) visible.push_back(b); continue; }
            if (hideTex && byIndex && k.compare(0, std::strlen(kPiecePrefix), kPiecePrefix) == 0) { over[k] = hideTex; ++nHidden; }
        } else if (byIndex && model == kCorpModel) {   // sem corp (ou corp indisponivel): vale o que o texdb mostra por padrao
            for (const char* d : kDefaultVisible) if (k == d) if (const Box* b = FindPiece(k)) visible.push_back(b);
        }
    }

    // 1b) roupas da FemininaNormal (modelo 9): pasta + variante -> material(is) do DFF, por mapa.txt
    if (model == kNormalModel && byIndex && (look.bootSet || look.pantsSet || look.shirtSet)) {
        auto wear = [&](const char* tipo, int set, int var) {
            if (!set) return;
            const std::vector<std::string>* mm = ClothMats(tipo, set);
            if (!mm) { FLog("[CORPTEX] AVISO: sem mapeamento para %s/%d (mapa.txt)", tipo, set); return; }
            RwTexture* t = GetAppTex(std::string("roupa/") + tipo + "/" + std::to_string(set) + "/" + std::to_string(var) + ".png");
            if (!t) return;
            for (const std::string& m : *mm) { over[m] = t; ++nApp; if (const Box* b = FindPiece(m)) visible.push_back(b); }
        };
        wear("boot", look.bootSet, look.bootVar);
        wear("pants", look.pantsSet, look.pantsVar);
        wear("tshirts", look.shirtSet, look.shirtVar);
    }
    // 1c) roupa intima (corpo a mostra): so quando ha tom de pele; some onde uma roupa cobre
    if (model == kNormalModel && byIndex && look.tone && look.under != 9) {
        RwTexture* ut = GetSolidTex(look.under ? look.under : 1);
        for (const char* nm : {"fantasy_f_bikinitop", "fantasy_f_bikinibotton"}) {
            const Box* ub = FindPiece(nm);
            bool covered = false;
            for (const Box* p : visible) if (ub && p != ub && PieceCovers(*p, *ub)) { covered = true; break; }
            if (covered) { if (hideTex) { over[nm] = hideTex; ++nHidden; } }
            else if (ut) { over[nm] = ut; ++nApp; }
        }
    }

    // 2) rosto, pele e cabelo (so no modo indice)
    if (byIndex && (look.tone || look.hairStyle)) {
        if (look.tone) {
            const std::string t = std::to_string(look.tone);
            if (RwTexture* f = GetAppTex("rosto/" + t + "/" + std::to_string(look.face) + ".png")) {
                over["body.head1"] = f; ++nApp;
                if (hideTex) for (int h = 2; h <= 5; ++h) { over["body.head" + std::to_string(h)] = hideTex; ++nHidden; }
            }
            RwTexture* tTorso = GetAppTex("pele/torso/" + t + ".png");
            RwTexture* tLeg   = GetAppTex("pele/leg/" + t + ".png");
            RwTexture* tFeet  = GetAppTex("pele/feet/" + t + ".png");
            for (const Box& r : kBodyRegions) {
                const std::string name = r.name;
                RwTexture* tex = (name == "body.feets" || name == "body.feets2") ? tFeet : name.compare(0, 8, "body.leg") == 0 ? tLeg : tTorso;
                if (!tex) continue;
                bool covered = false;
                for (const Box* p : visible) if (PieceCovers(*p, r)) { covered = true; break; }
                if (covered && hideTex) { over[name] = hideTex; ++nHidden; }
                else { over[name] = tex; ++nApp; }
            }
            if (tTorso) { over["body.hand"] = tTorso; ++nApp; }   // maos usam o mesmo PNG do torso (6 tons), nao sao ocultadas por roupa
        }
        if (look.hairStyle) {
            for (int s = 1; s <= 5; ++s) {
                const std::string key = "fantasy_f_hair" + std::to_string(s);
                if (s == look.hairStyle) {
                    if (RwTexture* h = GetAppTex("cabelo/" + std::to_string(s) + "/" + std::to_string(look.hairColor) + ".png")) { over[key] = h; ++nApp; }
                } else if (hideTex) { over[key] = hideTex; ++nHidden; }
            }
        }
    } else if (!byIndex && (look.tone || look.hairStyle)) {
        FLog("[CORPTEX] AVISO: rosto/pele/cabelo exigem o modo indice (contagem de materiais igual ao DFF)");
    }

    Entry en{id, e, e->m_pRwClump, look, {}, false, 0};
    for (size_t i = 0; i < mats.size(); ++i) {
        if (keys[i].empty() || !mats[i]) continue;
        auto it = over.find(keys[i]);
        if (it != over.end()) en.swaps.push_back({mats[i], it->second, nullptr});
    }

    FLog("[CORPTEX] jogador %d modelo=%d corp='%s' tom=%d rosto=%d cabelo=%d/%d bota=%d/%d calca=%d/%d camisa=%d/%d intima=%d: %zu material(is) trocado(s) de %zu (modo %s) | corp=%d aparencia=%d ocultos=%d",
         (int)id, model, look.corp.c_str(), (int)look.tone, (int)look.face, (int)look.hairStyle, (int)look.hairColor,
         (int)look.bootSet, (int)look.bootVar, (int)look.pantsSet, (int)look.pantsVar, (int)look.shirtSet, (int)look.shirtVar, (int)look.under, en.swaps.size(), mats.size(),
         byIndex ? "indice" : "nome", nCorp, nApp, nHidden);
    if (en.swaps.empty()) { SetInfo("nenhum material do ped casou com o visual pedido"); return; }

    if (s_entries.empty()) { s_firstEntryMs = NowMs(); s_totalRenders = 0; s_warnedNoHook = false; }
    s_entries.push_back(std::move(en));
    g_active.store((int)s_entries.size());
    SetInfo("jogador " + std::to_string(id) + ": visual aplicado (" + std::to_string(s_entries.back().swaps.size()) + " material(is))");
}

void Reconcile(PLAYERID id)
{
    const Look& look = s_look[id];
    CEntityGTA* e = GetEnt(id);
    Entry* en = FindEntry(id);

    if (!e || !IsHostModel((int)e->GetModelId()) || !e->m_pRwClump) { if (en) DropEntry(id); s_tried[id].valid = false; return; }   // ped ainda nao existe / outra skin
    Tried& t = s_tried[id];
    if (t.valid && t.ent == (void*)e && t.clump == e->m_pRwClump && t.look == look) return;                                         // em dia (ou ja tentado)
    if (en) DropEntry(id);                                                                                                           // ped/clump recriado ou visual mudou
    t.ent = e; t.clump = e->m_pRwClump; t.look = look; t.valid = true;
    BuildEntry(id, e, look);
}

void ForgetWant(PLAYERID id)
{
    DropEntry(id);
    s_look[id] = Look{}; s_tried[id] = Tried{};
    s_wantIds.erase(std::remove(s_wantIds.begin(), s_wantIds.end(), id), s_wantIds.end());
}

void Push(Cmd::Kind k, PLAYERID id, const Look& look)
{
    Cmd c{k, id, look};
    std::lock_guard<std::mutex> l(s_qMutex);
    s_q.push_back(std::move(c));
}

void PushLook(PLAYERID id, const Look& l) { Push(l.Active() ? Cmd::Set : Cmd::Clear, id, l); }

// ---- RPC -------------------------------------------------------------------------------------
// formato: uint16 playerid | uint8 len | char corp[len] | uint8 tom | uint8 rosto | uint8 estiloCabelo | uint8 corCabelo |
//          uint8 botaPasta | botaVar | calcaPasta | calcaVar | camisaPasta | camisaVar | intima
void ScrCorpTex(RPCParameters* rpcParams)
{
    RakNet::BitStream bs((unsigned char*)rpcParams->input, (rpcParams->numberOfBitsOfData / 8) + 1, false);
    PLAYERID id = 0; uint8_t len = 0; char corp[16] = {};
    Look l;
    if (!bs.Read(id) || id >= MAX_PLAYERS) return;
    if (!bs.Read(len) || len >= sizeof(corp)) return;
    if (len && !bs.Read(corp, len)) return;
    if (!bs.Read(l.tone) || !bs.Read(l.face) || !bs.Read(l.hairStyle) || !bs.Read(l.hairColor)) return;
    if (!bs.Read(l.bootSet) || !bs.Read(l.bootVar) || !bs.Read(l.pantsSet) || !bs.Read(l.pantsVar) ||
        !bs.Read(l.shirtSet) || !bs.Read(l.shirtVar) || !bs.Read(l.under)) return;
    corp[len] = 0; l.corp = corp;
    Sanitize(l);

    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (pool && id == pool->GetLocalPlayerID()) { std::lock_guard<std::mutex> g(s_qMutex); s_localLook = l; }   // mantem o espelho dos comandos
    PushLook(id, l);
}

} // namespace

void Register(RakClientInterface* rak)   { rak->RegisterAsRemoteProcedureCall(&RPC_CorpTex, ScrCorpTex); }
void Unregister(RakClientInterface* rak) { rak->UnregisterAsRemoteProcedureCall(&RPC_CorpTex); }

void ClearAll()
{
    { std::lock_guard<std::mutex> l(s_qMutex); s_q.clear(); s_localLook = Look{}; }
    Push(Cmd::Release, 0xFFFF, Look{});       // 0xFFFF = todos (executado no Process, na thread do jogo)
}

void OnPlayerStreamIn(PLAYERID) { /* o Process reaplica sozinho assim que o ped existir com o modelo certo */ }

void OnPlayerStreamOut(PLAYERID id)
{
    if (id < MAX_PLAYERS) Push(Cmd::Release, id, Look{});      // solta ANTES do ped morrer (servidor reenvia no stream-in)
}

void Process()
{
    std::vector<Cmd> cmds;
    { std::lock_guard<std::mutex> l(s_qMutex); cmds.swap(s_q); }

    for (const Cmd& c : cmds) {
        if (c.kind == Cmd::Release && c.id == 0xFFFF) { while (!s_wantIds.empty()) ForgetWant(s_wantIds.back()); continue; }
        if (c.kind == Cmd::Reload) {            // /roupa recarregar: le mapa.txt de novo e refaz todo mundo
            s_clothMapLoaded = false;
            for (PLAYERID id : s_wantIds) s_tried[id].valid = false;
            continue;
        }
        if (c.id >= MAX_PLAYERS) continue;
        if (c.kind == Cmd::Set) {
            if (std::find(s_wantIds.begin(), s_wantIds.end(), c.id) == s_wantIds.end()) s_wantIds.push_back(c.id);
            s_look[c.id] = c.look; s_tried[c.id].valid = false;
        } else {                                            // Clear ou Release de um jogador
            ForgetWant(c.id);
        }
    }

    // Modelo 271: o servidor nao precisa mandar nada. A cada 0,5 s inclui quem esta com a skin 271 para receber a pasta padrao.
    {
        static uint64_t s_lastScanMs = 0;
        const uint64_t nowScan = NowMs();
        if (nowScan - s_lastScanMs >= 500) {
            s_lastScanMs = nowScan;
            for (PLAYERID id = 0; id < MAX_PLAYERS; ++id) {
                CEntityGTA* e = GetEnt(id);
                if (e && (int)e->GetModelId() == kMaleModel && std::find(s_wantIds.begin(), s_wantIds.end(), id) == s_wantIds.end())
                    s_wantIds.push_back(id);
            }
        }
    }

    for (size_t i = 0; i < s_wantIds.size(); ++i) Reconcile(s_wantIds[i]);   // ped novo, modelo trocado, clump recriado...

    const uint64_t now = NowMs();
    for (size_t i = 0; i < s_graveyard.size();) {
        if (now - s_graveyard[i].second >= kGraveyardMs) { RwTextureDestroy(s_graveyard[i].first); s_graveyard.erase(s_graveyard.begin() + (long)i); }
        else ++i;
    }
    EvictCaches();

    if (!s_entries.empty() && !s_warnedNoHook && s_totalRenders == 0 && now - s_firstEntryMs > 5000) {
        s_warnedNoHook = true;
        FLog("[CORPTEX] AVISO: ha %zu ped(s) com visual ativo mas CEntity::Render nunca foi chamado para eles em 5 s. "
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

// ---- comandos de chat (so o seu proprio ator) ---------------------------------------------------
namespace {

std::string Trim(std::string a)
{
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    while (!a.empty() && a.back() == ' ') a.pop_back();
    return a;
}

std::vector<int> Ints(const std::string& a)
{
    std::vector<int> v; size_t i = 0;
    while (i < a.size()) {
        while (i < a.size() && (a[i] == ' ')) ++i;
        if (i >= a.size()) break;
        size_t j = i; bool neg = false;
        if (a[j] == '-') { neg = true; ++j; }
        size_t k = j; int n = 0;
        while (k < a.size() && a[k] >= '0' && a[k] <= '9' && k - j < 4) { n = n * 10 + (a[k] - '0'); ++k; }
        if (k == j) return {-1000};                          // lixo
        v.push_back(neg ? -n : n); i = k;
        if (i < a.size() && a[i] != ' ') return {-1000};
    }
    return v;
}

std::string Describe(const Look& l)
{
    std::string s = "corp=" + (l.corp.empty() ? std::string("-") : l.corp);
    s += " | tom=" + (l.tone ? std::to_string(l.tone) + " (" + kToneNames[l.tone] + ")" : std::string("-"));
    s += " | rosto=" + (l.face ? std::to_string(l.face) : std::string("-"));
    s += " | cabelo=" + (l.hairStyle ? std::to_string(l.hairStyle) + "/cor " + std::to_string(l.hairColor) : std::string("-"));
    auto pv = [](uint8_t set, uint8_t var) { return set ? std::to_string(set) + "/" + std::to_string(var) : std::string("-"); };
    s += " | bota=" + pv(l.bootSet, l.bootVar) + " calca=" + pv(l.pantsSet, l.pantsVar) + " camisa=" + pv(l.shirtSet, l.shirtVar);
    s += " | intima=" + (l.under == 9 ? std::string("nenhuma") : std::to_string(l.under ? l.under : 1));
    return s;
}

// aplica uma alteracao no espelho local e enfileira o visual completo
template <class F> std::string ApplyLocal(PLAYERID me, F&& edit)
{
    Look l;
    { std::lock_guard<std::mutex> g(s_qMutex); l = s_localLook; }
    edit(l);
    Sanitize(l);
    { std::lock_guard<std::mutex> g(s_qMutex); s_localLook = l; }
    PushLook(me, l);
    return "[CORPTEX] " + Describe(l);
}

// quantas variantes a pasta tem em disco (1.png, 2.png, ... sem buraco)
int CountVariants(const char* tipo, int set)
{
    int n = 0;
    while (n < 200) {
        const std::string p = ClothPath(std::string(tipo) + "/" + std::to_string(set) + "/" + std::to_string(n + 1) + ".png");
        if (::access(p.c_str(), F_OK) != 0) break;
        ++n;
    }
    return n;
}

} // namespace

std::string DebugCommand(const std::string& argsIn)     // /corp
{
    const std::string a = SkinImg::Lower(Trim(argsIn));
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
    if (a == "limpar" || a == "clear" || a == "off" || a == "reset") return ApplyLocal(me, [](Look& l) { l.corp.clear(); });

    if (!ValidName(a)) return "[CORPTEX] nome invalido (use so a-z 0-9 _ -, ate 15)";
    const auto corps = ListCorps();
    if (std::find(corps.begin(), corps.end(), a) == corps.end()) return "[CORPTEX] corp '" + a + "' nao existe (use /corp lista)";
    return ApplyLocal(me, [&](Look& l) { l.corp = a; });
}

std::string VisualCommand(const std::string& cmdIn, const std::string& argsIn)   // /rosto /cabelo /pele /visual
{
    const std::string cmd = SkinImg::Lower(cmdIn), args = Trim(SkinImg::Lower(argsIn));
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool) return "[CORPTEX] conecte-se a um servidor primeiro";
    const PLAYERID me = pool->GetLocalPlayerID();

    if (cmd == "visual") {
        if (args == "limpar" || args == "clear" || args == "reset") return ApplyLocal(me, [](Look& l) { l = Look{}; });
        Look l; { std::lock_guard<std::mutex> g(s_qMutex); l = s_localLook; }
        return "[CORPTEX] " + Describe(l) + " | /rosto <tom> <var> | /pele <tom> | /cabelo <estilo> <cor> | /bota /calca /camisa <pasta> <var> | /intima <0-6> | /roupa lista | /corp <nome> | /visual limpar";
    }

    if (cmd == "roupa") {
        if (args == "limpar" || args == "clear" || args == "reset")
            return ApplyLocal(me, [](Look& l) { l.bootSet = l.bootVar = l.pantsSet = l.pantsVar = l.shirtSet = l.shirtVar = 0; });
        if (args == "recarregar" || args == "reload") {
            Push(Cmd::Reload, me, Look{});
            return "[CORPTEX] mapa de roupas recarregado (textures/9/roupa/mapa.txt)";
        }
        if (args == "lista" || args == "list") {
            std::string s;
            for (const char* t : {"boot", "pants", "tshirts"}) {
                s += std::string(s.empty() ? "" : " | ") + (std::strcmp(t, "boot") == 0 ? "bota" : std::strcmp(t, "pants") == 0 ? "calca" : "camisa") + ":";
                for (int p = 1; p <= kMaxClothSet; ++p) { const int n = CountVariants(t, p); if (n) s += " " + std::to_string(p) + "(" + std::to_string(n) + ")"; }
            }
            return "[CORPTEX] pasta(variantes) - " + s;
        }
        return "[CORPTEX] uso: /roupa limpar | lista | recarregar  -  /bota /calca /camisa <pasta> <variante>  -  /intima <0-6>";
    }

    const std::vector<int> v = Ints(args);
    if (v.size() == 1 && v[0] == -1000) return "[CORPTEX] use so numeros";

    if (cmd == "bota" || cmd == "calca" || cmd == "camisa") {
        const char* tipo = cmd == "bota" ? "boot" : cmd == "calca" ? "pants" : "tshirts";
        if (v.empty() || v.size() > 2) return "[CORPTEX] uso: /" + cmd + " <pasta> <variante> (0 remove; /roupa lista mostra as pastas)";
        if (v[0] < 0 || v[0] > kMaxClothSet) return "[CORPTEX] pasta invalida";
        auto field = [&](Look& l) -> std::pair<uint8_t*, uint8_t*> {
            return cmd == "bota" ? std::make_pair(&l.bootSet, &l.bootVar) : cmd == "calca" ? std::make_pair(&l.pantsSet, &l.pantsVar) : std::make_pair(&l.shirtSet, &l.shirtVar);
        };
        if (v[0] == 0) return ApplyLocal(me, [&](Look& l) { auto f = field(l); *f.first = 0; *f.second = 0; });
        const int n = CountVariants(tipo, v[0]);
        if (n == 0) return "[CORPTEX] a pasta " + std::to_string(v[0]) + " nao existe em textures/9/roupa/" + tipo;
        if (v.size() == 2 && (v[1] < 1 || v[1] > n)) return "[CORPTEX] a pasta " + std::to_string(v[0]) + " tem variantes de 1 a " + std::to_string(n);
        const int varWanted = v.size() == 2 ? v[1] : 1;
        std::string warn;
        {   // diagnostico no chat: o arquivo existe mas o jogo consegue abrir?
            const std::string pf = ClothPath(std::string(tipo) + "/" + std::to_string(v[0]) + "/" + std::to_string(varWanted) + ".png");
            FILE* tf = std::fopen(pf.c_str(), "rb");
            if (!tf) warn = std::string(" | ERRO: nao consigo abrir o PNG (") + std::strerror(errno) + "): " + pf;
            else std::fclose(tf);
        }
        std::string r = ApplyLocal(me, [&](Look& l) { auto f = field(l); *f.first = (uint8_t)v[0]; if (v.size() == 2) *f.second = (uint8_t)v[1]; else if (!*f.second) *f.second = 1; });
        return r + warn;
    }
    if (cmd == "intima") {
        if (v.size() != 1 || v[0] < 0 || v[0] > 6) return "[CORPTEX] uso: /intima <cor 1-6> (0 = nenhuma): 1 preto, 2 branco, 3 vermelho, 4 azul, 5 rosa, 6 cinza";
        return ApplyLocal(me, [&](Look& l) { l.under = v[0] == 0 ? 9 : (uint8_t)v[0]; });
    }

    if (cmd == "rosto" || cmd == "pele") {
        if (v.empty() || v.size() > (cmd == "rosto" ? 2u : 1u)) return cmd == "rosto" ? "[CORPTEX] uso: /rosto <tom 1-6> <variante> (0 remove)" : "[CORPTEX] uso: /pele <tom 1-6> (0 remove)";
        if (v[0] < 0 || v[0] > 6) return "[CORPTEX] tom de 1 a 6 (1 medio, 2 escuro, 3 claro, 4 moreno, 5 avermelhado, 6 palido)";
        if (v[0] == 0) return ApplyLocal(me, [](Look& l) { l.tone = 0; l.face = 0; });
        if (v.size() == 2 && (v[1] < 1 || v[1] > kFaceVariants[v[0]]))
            return "[CORPTEX] tom " + std::to_string(v[0]) + " tem variantes de 1 a " + std::to_string(kFaceVariants[v[0]]);
        return ApplyLocal(me, [&](Look& l) { l.tone = (uint8_t)v[0]; if (v.size() == 2) l.face = (uint8_t)v[1]; });
    }
    if (cmd == "cabelo") {
        if (v.empty() || v.size() > 2) return "[CORPTEX] uso: /cabelo <estilo 1-5> <cor> (0 remove)";
        if (v[0] < 0 || v[0] > 5) return "[CORPTEX] estilo de 1 a 5";
        if (v[0] == 0) return ApplyLocal(me, [](Look& l) { l.hairStyle = 0; l.hairColor = 0; });
        if (v.size() == 2 && (v[1] < 1 || v[1] > kHairColors[v[0]]))
            return "[CORPTEX] estilo " + std::to_string(v[0]) + " tem cores de 1 a " + std::to_string(kHairColors[v[0]]);
        return ApplyLocal(me, [&](Look& l) { l.hairStyle = (uint8_t)v[0]; if (v.size() == 2) l.hairColor = (uint8_t)v[1]; });
    }
    return "[CORPTEX] comando desconhecido";
}

} // namespace CorpTex
