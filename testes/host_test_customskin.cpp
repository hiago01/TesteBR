#define CUSTOMSKIN_HOST_STUBS
#include "game/CustomSkin.cpp"
#include <cassert>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

uintptr_t g_libGTASA = 0x1000;
char* g_pszStorage = nullptr;
void* g_hostModelInfo = nullptr; bool g_hostModelLoaded = true;
RpClump* (*g_hostRead)() = nullptr; void (*g_hostInstall)(RpClump*) = nullptr;
RpClump* (*g_hostTemplate)() = nullptr; bool (*g_hostCollect)(RpClump*, std::vector<RpMaterial*>&) = nullptr;
int g_hostDestroyedTextures = 0;

static RpClump g_clump;
static std::vector<RpMaterial*> g_mats;
static std::vector<RwTexture*> g_origTex;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ ++fails; std::printf("FALHOU linha %d: %s\n", __LINE__, #c);} }while(0)

static RpClump* Tmpl() { return &g_clump; }
static bool Collect(RpClump*, std::vector<RpMaterial*>& out) { out = g_mats; return true; }

static void Touch(const std::string& p) { fs::create_directories(fs::path(p).parent_path()); std::ofstream(p).put('x'); }

// estado REAL de um slot (como a UI veria)
static bool Vis(const char* n) { auto idx = SlotMaterials(*SkinSlots::Find(n)); if (idx.empty()) return false; for (int i : idx) if (s_hidden[i]) return false; return true; }
static RwTexture* MatTex(const char* n) { auto idx = SlotMaterials(*SkinSlots::Find(n)); return g_mats[idx[0]]->texture; }
static uint8_t MatA(const char* n) { auto idx = SlotMaterials(*SkinSlots::Find(n)); return g_mats[idx[0]]->color.alpha; }
static void Run(const std::string& c) { Execute(c); }
static uint32_t nextId = 100;
static bool Ui(const std::string& op)   // executa como o Process() faria e devolve reqOk do estado publicado
{
    const uint32_t id = ++nextId;
    Execute("ui " + std::to_string(id) + " " + op);
    PublishUiState();
    CCustomSkin::UiState st; CCustomSkin::UiGetState(st);
    CHECK(st.reqId == id);
    return st.reqOk;
}
static int Refs(RwTexture* t) { return t->refCount; }

int main()
{
    // pack falso: pastas reais dos slots, variantes 1..3 (hair: 1..4)
    const std::string root = "/tmp/skin7host/";
    fs::remove_all(root);
    g_pszStorage = strdup(root.c_str());
    for (int k = 0; k < SkinSlots::kCount; ++k)
        for (int v = 1; v <= 3; ++v) Touch(root + "SAMP/skins/pack/" + SkinSlots::kSlots[k].folder + "/" + std::to_string(v) + ".png");
    Touch(root + "SAMP/skins/pack/hair/2/4.png");

    // "DFF" carregado: 1 material por textura citada nos slots
    std::set<std::string> seen;
    for (int k = 0; k < SkinSlots::kCount; ++k)
    {
        std::string t = SkinSlots::kSlots[k].texture; size_t p = 0;
        while (p <= t.size()) { size_t e = t.find(',', p); if (e == std::string::npos) e = t.size(); std::string x = t.substr(p, e - p); if (!x.empty() && seen.insert(x).second) { DffPreflight::MaterialInfo mi; mi.texture = x; mi.textured = true; s_parsed.materialsList.push_back(mi); } p = e + 1; }
    }
    const size_t n = s_parsed.materialsList.size();
    for (size_t i = 0; i < n; ++i)
    {
        RwTexture* t = new RwTexture{}; t->refCount = 2;          // 1 do material + 1 de s_enabledTex (como no TexturePass)
        std::snprintf(t->name, sizeof(t->name), "%s", s_parsed.materialsList[i].texture.c_str());
        RpMaterial* m = new RpMaterial{}; m->texture = t;
        g_mats.push_back(m); g_origTex.push_back(t);
        s_enabledTex.push_back(t); s_origAlpha.push_back(255);
    }
    s_hidden.assign(n, 0);
    s_loadedOk = true; s_installed = &g_clump; g_hostTemplate = Tmpl; g_hostCollect = Collect;
    std::printf("materiais simulados: %zu\n", n);

    // ---- 0) estado inicial publicado ----
    PublishUiState();
    { CCustomSkin::UiState st; CHECK(CCustomSkin::UiGetState(st)); CHECK(st.skinReady); CHECK((int)st.slots.size() == SkinSlots::kCount); CHECK(st.slots[0].hasMaterial); CHECK(!st.pending); }

    // ---- 1) previa de cabelo: hair1 -> hair2 (exclusividade) ----
    RwTexture* hair1Orig = MatTex("hair1");
    CHECK(Ui("prev hair1 1")); CHECK(Vis("hair1"));
    CHECK(Ui("prev hair2 2")); CHECK(Vis("hair2") && !Vis("hair1"));
    CHECK(Ui("prev hair2 4")); CHECK(Vis("hair2") && !Vis("hair3") && !Vis("hair4"));
    // outras categorias independentes
    CHECK(Ui("prev backpack1 1")); CHECK(Vis("backpack1") && Vis("hair2"));
    { CCustomSkin::UiState st; CCustomSkin::UiGetState(st); CHECK(st.pending); auto& hs = st.slots[0]; (void)hs; }
    // variante e visibilidade vindas do estado real
    { CCustomSkin::UiState st; CCustomSkin::UiGetState(st);
      int ih2 = -1; for (int k = 0; k < SkinSlots::kCount; ++k) if (!std::strcmp(SkinSlots::kSlots[k].name, "hair2")) ih2 = k;
      CHECK(st.slots[ih2].visible && st.slots[ih2].variant == 4); }

    // ---- 2) variante inexistente: falha verificada no estado, nada muda ----
    const bool okBad = Ui("prev hair3 99");
    CHECK(!okBad); CHECK(Vis("hair2") && !Vis("hair3"));      // hair2 continua ativo
    { CCustomSkin::UiState st; CCustomSkin::UiGetState(st); CHECK(!st.reqOk && !st.msg.empty()); }
    CHECK(!Ui("prev naoexiste 1")); CHECK(!Ui("prev hair2 abc"));

    // ---- 3) CANCELAR restaura tudo (ocultos, textura original, alfa, refs) ----
    CHECK(Ui("cancel"));
    CHECK(Vis("hair1") && Vis("hair2") && Vis("backpack1"));   // estado original: tudo visivel de novo
    // originalmente todos os slots estavam visiveis (s_hidden = 0): apos cancelar, deve voltar a 0 em todos
    bool allVis = true; for (size_t i = 0; i < n; ++i) if (s_hidden[i]) allVis = false;
    CHECK(allVis);
    for (size_t i = 0; i < n; ++i) { CHECK(s_enabledTex[i] == g_origTex[i]); CHECK(g_mats[i]->texture == g_origTex[i]); CHECK(g_mats[i]->color.alpha == 255); }
    CHECK(s_matPack.empty()); CHECK(s_slotVar.empty());
    { CCustomSkin::UiState st; CCustomSkin::UiGetState(st); CHECK(!st.pending); }
    CHECK(!s_bak.active);
    // deixa o cemiterio expirar: as refs das texturas da previa sao soltas; as originais continuam vivas
    for (auto& g : s_graveyard) g.second = 0;
    { const int before = g_hostDestroyedTextures; CCustomSkin::Process(); CHECK(g_hostDestroyedTextures > before); }
    CHECK(s_graveyard.empty());
    for (size_t i = 0; i < n; ++i) CHECK(Refs(g_origTex[i]) == 2);          // material + s_enabledTex, nem mais nem menos
    (void)hair1Orig;

    // ---- 4) COMMIT: confirma e solta o backup sem destruir a textura em uso ----
    CHECK(Ui("prev hair1 1")); CHECK(Ui("prev hair3 2")); CHECK(Vis("hair3") && !Vis("hair1"));
    RwTexture* cur = MatTex("hair3");
    CHECK(Ui("commit")); CHECK(!s_bak.active);
    CHECK(MatTex("hair3") == cur && Refs(cur) == 2);
    { CCustomSkin::UiState st; CCustomSkin::UiGetState(st); CHECK(!st.pending); }
    for (auto& g : s_graveyard) g.second = 0; CCustomSkin::Process();
    for (size_t i = 0; i < n; ++i) { if (s_enabledTex[i] != g_origTex[i]) continue; }
    // depois do commit um cancel nao desfaz nada
    CHECK(Ui("cancel")); CHECK(Vis("hair3") && !Vis("hair1"));

    // ---- 5) sessao nova: off manual + ativar outro; toggle on/off ----
    CHECK(Ui("prev hair3 off")); CHECK(!Vis("hair3"));
    CHECK(Ui("prev hair4 1")); CHECK(Vis("hair4") && !Vis("hair3"));
    CHECK(Ui("prev hair3 on")); CHECK(Vis("hair3") && !Vis("hair4"));    // 'on' tambem respeita a exclusividade
    CHECK(Ui("cancel")); CHECK(Vis("hair3") && !Vis("hair4"));            // volta ao estado do commit anterior

    // ---- 6) varias trocas seguidas + cancelar: sem vazamento nem estado quebrado ----
    for (int rep = 0; rep < 30; ++rep) {
        const char* hs[] = {"hair1","hair2","hair3","hair4"}; const char* bs[] = {"boot1","boot2","boot3","boot4"};
        CHECK(Ui(std::string("prev ") + hs[rep % 4] + " " + std::to_string(1 + rep % 3)));
        CHECK(Ui(std::string("prev ") + bs[rep % 4] + " 2"));
        int vh = 0; for (auto h : hs) vh += Vis(h); CHECK(vh == 1);
        int vb = 0; for (auto b : bs) vb += Vis(b); CHECK(vb == 1);
    }
    CHECK(Ui("cancel"));
    for (auto& g : s_graveyard) g.second = 0; CCustomSkin::Process();
    CHECK(s_graveyard.empty());
    CHECK(Vis("hair3") && !Vis("hair1"));
    // nenhuma textura vazou: contagem de refs de cada material = material + s_enabledTex
    for (size_t i = 0; i < n; ++i) { RwTexture* t = s_enabledTex[i]; CHECK(g_mats[i]->texture == t || s_hidden[i]); }
    {   // soma de refs vivas == refs esperadas (cada material visivel + cada s_enabledTex)
        std::map<RwTexture*, int> expect;
        for (size_t i = 0; i < n; ++i) { expect[s_enabledTex[i]]++; if (g_mats[i]->texture) expect[g_mats[i]->texture]++; }
        for (auto& kv : expect) if (kv.first != s_hiddenTex) CHECK(kv.first->refCount == kv.second);
    }

    // ---- 7) apply durante a previa invalida o backup sem vazar ----
    CHECK(Ui("prev hair2 1")); CHECK(s_bak.active);
    DiscardBackup(); CHECK(!s_bak.active);

    // ---- 8) skin nao instalada: prev falha de forma honesta ----
    s_installed = nullptr;
    CHECK(!Ui("prev hair1 1")); { CCustomSkin::UiState st; CCustomSkin::UiGetState(st); CHECK(!st.skinReady); }
    s_installed = &g_clump;

    // ---- 9) o /skin7 slot normal continua igual (sem UI) ----
    Run("slot backpack2 2"); CHECK(Vis("backpack2") && !Vis("backpack1"));


    // ---- 10) COBERTURA: pele das pernas / roupa intima sob calca e bermuda ----
    {
        auto H = [&](const char* tex) { auto v = MaterialsByTexture(tex); CHECK(!v.empty()); return s_hidden[(size_t)v[0]] != 0; };
        s_hidden.assign(n, 0); DiscardBackup();
        const char* gar[] = {"torso1","torso2","torso3","torso4","boot1","boot2","boot3","boot4","slipper1","slipper2","slipper3",
                             "pants1","pants2","pants3","pants4","short1","short2","short3"};
        for (auto g : gar) Run(std::string("slot ") + g + " off");
        // sem nenhuma peca: nada coberto, tudo visivel
        CHECK(!H("body.legleft1") && !H("body.legright5") && !H("fantasy_m_underwear"));

        Run("slot short3 2");                                   // bermuda: cobre so a parte de cima da perna
        CHECK(Vis("short3"));
        CHECK(H("body.legleft1") && H("body.legright1") && H("fantasy_m_underwear"));
        CHECK(!H("body.legleft2") && !H("body.legright2"));      // cobertura parcial: fica com pele
        CHECK(!H("body.legleft3") && !H("body.legleft4") && !H("body.legleft5"));
        { CCustomSkin::UiState st; PublishUiState(); CCustomSkin::UiGetState(st); int il = -1;
          for (int k = 0; k < SkinSlots::kCount; ++k) if (!std::strcmp(SkinSlots::kSlots[k].name, "skin_leg")) il = k;
          CHECK(st.slots[il].partial && !st.slots[il].visible); }

        Run("slot pants2 1");                                   // calca longa: cobre as 5 pernas; bermuda sai (exclusividade)
        CHECK(Vis("pants2") && !Vis("short3"));
        for (const char* t : {"body.legleft1","body.legleft2","body.legleft3","body.legleft4","body.legleft5",
                              "body.legright1","body.legright5","fantasy_m_underwear"}) CHECK(H(t));

        Run("slot pants3 1");                                   // calca curta: cobre pernas 1-2; religa 3-5
        CHECK(Vis("pants3") && !Vis("pants2"));
        CHECK(H("body.legleft1") && H("body.legleft2") && H("body.legright2"));
        CHECK(!H("body.legleft3") && !H("body.legleft4") && !H("body.legleft5") && !H("body.legright3"));
        CHECK(H("fantasy_m_underwear"));

        Run("slot underwear on");                               // ligar a roupa intima por baixo de uma calca: continua coberta
        CHECK(H("fantasy_m_underwear"));

        Run("slot pants3 off");                                 // tirou a calca: tudo volta
        CHECK(!H("body.legleft1") && !H("body.legleft2") && !H("body.legright2") && !H("fantasy_m_underwear"));

        // previa + cancelar restaura tambem o que a cobertura escondeu
        CHECK(Ui("prev short1 1"));
        CHECK(H("body.legleft1") && H("fantasy_m_underwear"));
        CHECK(Ui("cancel"));
        CHECK(!H("body.legleft1") && !H("body.legright1") && !H("fantasy_m_underwear") && !Vis("short1"));

        // variante invalida nao muda a cobertura
        CHECK(Ui("prev short2 1")); CHECK(!Ui("prev short1 99"));
        CHECK(Vis("short2") && H("body.legleft1"));
        CHECK(Ui("commit"));
        // slots de outras categorias nao sao tocados
        const bool hairBefore = Vis("hair3");
        Run("slot pants4 1"); CHECK(Vis("hair3") == hairBefore);
    }

    // ---- 11) COBERTURA: camisas, tenis (boot) e chinelo ----
    {
        auto H = [&](const char* tex) { auto v = MaterialsByTexture(tex); CHECK(!v.empty()); return s_hidden[(size_t)v[0]] != 0; };
        auto HR = [&](const char* tex) { auto v = MaterialsByTexture(tex); CHECK(!v.empty()); return s_hidden[(size_t)v.back()] != 0; };
        const char* all[] = {"torso1","torso2","torso3","torso4","boot1","boot2","boot3","boot4","slipper1","slipper2","slipper3",
                             "pants1","pants2","pants3","pants4","short1","short2","short3"};
        s_hidden.assign(n, 0); DiscardBackup();
        for (auto g : all) Run(std::string("slot ") + g + " off");
        CHECK(!H("body.torso1") && !H("body.armleft") && !H("body.shoulderleft") && !H("body.feetleft") && !H("body.legleft5"));

        // regata (torso1): cobre tronco; bracos e ombros ficam com pele
        Run("slot torso1 3");
        CHECK(Vis("torso1"));
        CHECK(H("body.torso1") && H("body.torso2") && H("body.torso3"));
        CHECK(!H("body.armleft") && !H("body.armright") && !H("body.shoulderleft") && !H("body.shoulderright"));
        // camiseta (torso2): tambem cobre os ombros
        Run("slot torso2 1");
        CHECK(Vis("torso2") && !Vis("torso1"));
        CHECK(H("body.torso1") && H("body.shoulderleft") && H("body.shoulderright") && !H("body.armleft"));
        // jaqueta (torso4): cobre bracos
        Run("slot torso4 1");
        CHECK(Vis("torso4") && !Vis("torso2"));
        CHECK(H("body.torso1") && H("body.shoulderleft") && H("body.armleft") && H("body.armright"));
        // volta para a regata: bracos e ombros reaparecem
        Run("slot torso1 3");
        CHECK(H("body.torso1") && !H("body.shoulderleft") && !H("body.armleft") && !H("body.armright"));
        Run("slot torso1 off");
        CHECK(!H("body.torso1") && !H("body.torso3") && !H("body.armleft"));

        // tenis (boot2): esconde a pele dos pes e do tornozelo; panturrilha (leg4) fica
        Run("slot boot2 1");
        CHECK(Vis("boot2"));
        CHECK(H("body.feetleft") && HR("body.feetright") && H("body.legleft5") && H("body.legright5"));
        CHECK(!H("body.legleft3") && !H("body.legleft4"));
        // chinelo: com tenis nao usa chinelo (grupo unico); ao ativar o chinelo o tenis sai e o pe volta
        Run("slot slipper1 1");
        CHECK(Vis("slipper1") && !Vis("boot2"));
        CHECK(!H("body.feetleft") && !H("body.feetright") && !H("body.legleft5"));
        // e o contrario: ativar tenis desliga o chinelo
        Run("slot boot3 2");
        CHECK(Vis("boot3") && !Vis("slipper1") && !Vis("boot2"));
        CHECK(H("body.feetleft") && H("body.legleft5") && H("body.legleft4"));      // boot3 sobe ate -0.81: cobre leg4 tambem
        { int vis = 0; for (auto g : {"boot1","boot2","boot3","boot4","slipper1","slipper2","slipper3"}) vis += Vis(g); CHECK(vis == 1); }

        // bermuda + tenis: cada um esconde o seu; tirar o tenis nao religa o que a bermuda cobre
        Run("slot short3 2");
        CHECK(H("body.legleft1") && H("body.legleft5") && H("body.feetleft"));
        Run("slot boot3 off");
        CHECK(!H("body.feetleft") && !H("body.legleft5") && !H("body.legleft4") && H("body.legleft1") && H("fantasy_m_underwear"));

        // previa de camisa + cancelar restaura o que a cobertura escondeu
        Run("slot short3 off");
        CHECK(Ui("prev torso4 1")); CHECK(H("body.armleft") && H("body.shoulderleft"));
        CHECK(Ui("cancel")); CHECK(!H("body.armleft") && !H("body.shoulderleft") && !H("body.torso1"));
        // mao nunca e ocultada
        CHECK(Ui("prev torso3 1")); CHECK(H("body.armleft"));
        { auto hv = MaterialsByTexture("body.hand"); if (!hv.empty()) CHECK(!s_hidden[(size_t)hv[0]]); }
        CHECK(Ui("cancel"));
    }
    if (fails) { std::printf("RESULTADO: %d FALHA(S)\n", fails); return 1; }
    std::printf("RESULTADO: todos os testes de host passaram\n");
    return 0;
}
