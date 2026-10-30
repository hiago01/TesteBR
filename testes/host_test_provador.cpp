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
bool g_hostOnline = true; bool g_hostSrvHave = false; uint32_t g_hostSrvVer = 0; ProvadorModel::SrvTable g_hostSrv;
int g_hostLocalModel = 7; std::vector<std::string> g_hostChat; int g_hostWearCalls = 0; bool g_hostSuspend = false;

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

// ---- ajudantes dos testes novos ----------------------------------------------------------------------------
static int SlotId(const char* n) { for (int k = 0; k < SkinSlots::kCount; ++k) if (!std::strcmp(SkinSlots::kSlots[k].name, n)) return k + 1; return 0; }
static void Pump() { s_lookLastSendMs = 0; CCustomSkin::Process(); }
static void ServerEcho()   // o servidor processa a fila enviada e devolve o snapshot ao dono (RPC 232)
{
    std::vector<ProvadorModel::Op> ops;
    for (const std::string& c : g_hostChat) {      // "/lookx set <id> <est> [<var>]"
        int id = 0, est = 0, var = 0;
        if (std::sscanf(c.c_str(), "/lookx set %d %d %d", &id, &est, &var) >= 2) ops.push_back({id, est, est == 2 ? var : 0});
    }
    g_hostSrv = ProvadorModel::Simulate(g_hostSrv.empty() ? ProvadorModel::NewTable() : g_hostSrv, ops);
    g_hostSrvHave = true; ++g_hostSrvVer;
}
static bool CommitOk()      // confirma e fecha o ciclo como o servidor faria (eco do snapshot)
{
    g_hostChat.clear();
    const bool ok = Ui("commit");
    if (!ok) return false;
    if (s_await.active) {
        for (int i = 0; i < 400 && !(s_lookQ.empty() && !s_lookResetQ); ++i) Pump();        // LookFlush envia 1 por vez (debounce)
        ServerEcho();
        Pump();
        CHECK(!s_await.active);
    }
    return !s_await.active && !s_bak.active;
}
static CCustomSkin::UiState Pub() { PublishUiState(); CCustomSkin::UiState st; CCustomSkin::UiGetState(st); return st; }
static const CCustomSkin::UiSlot& U(const CCustomSkin::UiState& st, const char* n) { return st.slots[(size_t)SlotId(n) - 1]; }


int main()
{
    // pack falso: pastas reais dos slots, variantes 1..3 (hair: 1..4)
    const std::string root = "/tmp/skin7host/";
    fs::remove_all(root);
    g_pszStorage = strdup(root.c_str());
    for (int k = 0; k < SkinSlots::kCount; ++k)
        for (int v = 1; v <= 3; ++v) Touch(root + "SAMP/skins/pack/" + SkinSlots::kSlots[k].folder + "/" + std::to_string(v) + ".png");
    Touch(root + "SAMP/skins/pack/hair/2/4.png");
    for (int t = 1; t <= 6; ++t) for (int f = 1; f <= 13; ++f) Touch(root + "SAMP/skins/pack/heads/" + std::to_string(t) + "/" + std::to_string(f) + ".png");   // rostos por tom (+ transparente)
    for (const char* part : {"torso", "leg", "feet"}) for (int t = 1; t <= 7; ++t) Touch(root + "SAMP/skins/pack/" + part + "/1/" + std::to_string(t) + ".png");   // 7 = transparente

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
    CHECK(CommitOk()); CHECK(!s_bak.active);
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
        CHECK(CommitOk());
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
        CHECK(!H("body.feetleft") && !H("body.legleft5") && H("body.legleft4"));     // boot3 (cano baixo): pe e tornozelo ficam a mostra; cobre leg4
        { int vis = 0; for (auto g : {"boot1","boot2","boot3","boot4","slipper1","slipper2","slipper3"}) vis += Vis(g); CHECK(vis == 1); }

        // bermuda + tenis: cada um esconde o seu; tirar o tenis nao religa o que a bermuda cobre
        Run("slot short3 2");
        CHECK(H("body.legleft1") && !H("body.legleft5") && !H("body.feetleft"));
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

    // =====================================================================================================
    // NOVOS: estados, confirmação pelo servidor, rede, preparação automática, fila
    // =====================================================================================================
    g_hostOnline = true; g_hostSrvHave = true; g_hostSrv = ProvadorModel::NewTable(); g_hostSrvVer = 50; s_srvSeenVer = 50;
    s_await = Awaiting(); DiscardBackup(); s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden();
    s_lookQ.clear(); s_lookPrevQ.clear(); s_lookResetQ = false; g_hostChat.clear();   // sobras dos /skin7 slot digitados nos testes acima

    // ---- 12) ESTADOS: visível != equipado ----
    {
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden(); DiscardBackup();
        auto st = Pub();
        int equipped = 0, def = 0;
        for (auto& u : st.slots) { if (u.state == ProvadorModel::kEquipped) ++equipped; if (u.state == ProvadorModel::kDefault) ++def; CHECK(!u.previewed); }
        CHECK(equipped == 0); CHECK(def == SkinSlots::kCount);                  // todos visíveis e NENHUM equipado
        CHECK(U(st, "hair1").visible && U(st, "hair1").state == ProvadorModel::kDefault);
        CHECK(!st.pending && !st.awaiting && st.online && st.serverSynced && st.dffOk);

        CHECK(Ui("prev hair2 4"));
        st = Pub();
        CHECK(U(st, "hair2").state == ProvadorModel::kEquipped && U(st, "hair2").variant == 4 && U(st, "hair2").previewed);
        CHECK(U(st, "hair1").state == ProvadorModel::kDisabled && U(st, "hair1").previewed);
        CHECK(!U(st, "boot1").previewed && U(st, "boot1").state == ProvadorModel::kDefault);
        CHECK(st.pending && g_hostSuspend);
        CHECK(Ui("cancel"));
        st = Pub();
        CHECK(U(st, "hair2").state == ProvadorModel::kDefault && U(st, "hair1").state == ProvadorModel::kDefault && !U(st, "hair2").previewed);
        CHECK(!st.pending && !g_hostSuspend);

        Run("slot hair1 off");  st = Pub(); CHECK(U(st, "hair1").state == ProvadorModel::kDisabled);
        Run("slot hair1 on");   st = Pub(); CHECK(U(st, "hair1").state == ProvadorModel::kDefault);
        Run("slot pants2 1");   st = Pub();
        CHECK(U(st, "pants2").state == ProvadorModel::kEquipped && U(st, "underwear").state == ProvadorModel::kCovered);
        Run("slot pants2 off"); st = Pub(); CHECK(U(st, "underwear").state == ProvadorModel::kDefault);
        Run("slot short3 2");   st = Pub(); CHECK(U(st, "skin_leg").partial && U(st, "skin_leg").state != ProvadorModel::kCovered);
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden();
        { auto idx = SlotMaterials(*SkinSlots::Find("skin_leg")); CHECK(idx.size() > 1); s_matPack[idx[0]] = "leg/1/2"; s_matPack[idx[1]] = "leg/1/3";
          st = Pub(); CHECK(U(st, "skin_leg").variant == -1); s_matPack.clear(); }
    }

    // ---- 12b) ROSTO = FORMATO x TOM; PELE troca corpo inteiro + rosto juntos ----
    {
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden(); DiscardBackup(); s_await = Awaiting(); s_moldStale = false; s_lookQ.clear(); s_lookPrevQ.clear();
        g_hostSrv = ProvadorModel::NewTable(); ++g_hostSrvVer; s_srvSeenVer = g_hostSrvVer; g_hostChat.clear();
        auto Mat = [&](const char* nm) -> std::string { auto idx = SlotMaterials(*SkinSlots::Find(nm)); auto it = s_matPack.find(idx[0]); return it == s_matPack.end() ? std::string() : it->second; };
        auto PrevHas = [&](const char* nm, int est, int var) { for (const LookOp& q : s_lookPrevQ) if (q.slotId == SlotId(nm) && q.est == est && q.var == var) return true; return false; };

        // formato 3 com o rosto 5 do tom 2: qualquer formato aceita qualquer tom
        CHECK(Ui("prev head3 205"));
        CHECK(Mat("head3") == "heads/2/5" && Vis("head3"));
        CHECK(PrevHas("head3", 2, 205));
        auto st = Pub();
        CHECK(U(st, "head3").variant == 205 && U(st, "head3").state == ProvadorModel::kEquipped && U(st, "head3").varMod == 100);
        // com o corpo ainda sem tom aplicado, o tom atual e 1 e a lista de rostos e a do tom 1 (6 rostos, sem a transparente)
        CHECK((int)U(st, "head1").variants.size() == SkinSlots::kToneFaces[1] && U(st, "head1").variants.front() == 101 && U(st, "head1").variants.back() == 106);
        // a pele oferece so os tons 1..6 (a 7 e transparente)
        CHECK(U(st, "skin_torso").variants == std::vector<int>({1, 2, 3, 4, 5, 6}));

        // trocar o TOM do corpo para 4: pernas e pes acompanham e o rosto visivel fica no mesmo rosto, tom 4
        CHECK(Ui("prev skin_torso 4"));
        CHECK(Mat("skin_torso") == "torso/1/4" && Mat("skin_leg") == "leg/1/4" && Mat("skin_feet") == "feet/1/4");
        CHECK(Mat("head3") == "heads/4/5");                                   // rosto 5 mantido, tom 4
        CHECK(PrevHas("skin_torso", 2, 4) && PrevHas("skin_leg", 2, 4) && PrevHas("skin_feet", 2, 4) && PrevHas("head3", 2, 405));
        st = Pub();
        CHECK(U(st, "head1").variants.front() == 401 && (int)U(st, "head1").variants.size() == SkinSlots::kToneFaces[4]);   // lista passa a ser do tom 4
        CHECK(U(st, "skin_torso").variant == 4);

        // trocar o ROSTO nao muda a cor do corpo: o novo rosto sai do tom atual
        CHECK(Ui("prev head3 407")); CHECK(Mat("head3") == "heads/4/7"); CHECK(Mat("skin_torso") == "torso/1/4");

        // tom com menos rostos: o rosto e limitado (7 -> 5 no tom 5)
        CHECK(Ui("prev skin_torso 5")); CHECK(Mat("head3") == "heads/5/5" && Mat("skin_leg") == "leg/1/5");
        // tom 6 (12 rostos) alcanca rostos que antes nao existiam na interface
        CHECK(Ui("prev head3 612")); CHECK(Mat("head3") == "heads/6/12");

        // so a cabeca VISIVEL acompanha: head1 desligada nao ganha textura nova
        CHECK(Ui("prev head1 off")); CHECK(!Vis("head1"));
        CHECK(Ui("prev skin_torso 2"));
        CHECK(Mat("head3") == "heads/2/12" || Mat("head3") == "heads/2/8");   // 12 -> limite do tom 2 (8 rostos)
        CHECK(Mat("head1").find("heads/2/") == std::string::npos);

        // commit: o servidor recebe corpo (tom) + rosto (tom*100+rosto) e o eco confirma
        g_hostChat.clear(); CHECK(Ui("commit")); CHECK(s_await.active);
        for (int i = 0; i < 12; ++i) Pump();
        bool sTor = false, sHead = false;
        for (auto& c : g_hostChat) { if (c == "/lookx set " + std::to_string(SlotId("skin_torso")) + " 2 2") sTor = true; if (c == "/lookx set " + std::to_string(SlotId("head3")) + " 2 208") sHead = true; }
        CHECK(sTor && sHead);
        ServerEcho(); Pump();
        CHECK(!s_await.active && !s_bak.active);
        st = Pub(); CHECK(U(st, "head3").srvKnown && U(st, "head3").srvVar == 208 && U(st, "skin_leg").srvVar == 2);

        // cancelar volta TUDO (corpo + rosto) ao que estava
        s_lookQ.clear(); s_lookPrevQ.clear(); g_hostChat.clear();
        CHECK(Ui("prev skin_torso 6")); CHECK(Mat("head3") == "heads/6/8" && Mat("skin_feet") == "feet/1/6");
        CHECK(Ui("cancel"));
        CHECK(Mat("skin_torso") == "torso/1/2" && Mat("skin_leg") == "leg/1/2" && Mat("skin_feet") == "feet/1/2" && Mat("head3") == "heads/2/8");
        CHECK(s_lookPrevQ.empty() && !s_bak.active);
    }

    // ---- 13) COMMIT só vale com a confirmação do servidor ----
    {
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden(); DiscardBackup(); s_await = Awaiting(); s_moldStale = false; s_lookQ.clear(); s_lookPrevQ.clear();
        g_hostSrv = ProvadorModel::NewTable(); ++g_hostSrvVer; s_srvSeenVer = g_hostSrvVer; g_hostChat.clear();
        CHECK(Ui("prev hair2 3")); CHECK(Ui("prev backpack1 2"));
        CHECK(s_lookQ.empty());                                      // prévia NÃO foi ao servidor
        for (int i = 0; i < 5; ++i) Pump(); CHECK(g_hostChat.empty());
        CHECK(Ui("commit"));
        auto st = Pub();
        CHECK(s_await.active && st.awaiting && !st.pending);
        CHECK(s_bak.active && g_hostSuspend);
        CHECK(!Ui("prev hair3 1")); CHECK(!Ui("cancel")); CHECK(!Ui("commit"));
        for (int i = 0; i < 6; ++i) Pump();
        bool sawHair2 = false, sawBack = false;
        for (auto& c : g_hostChat) { if (c == "/lookx set " + std::to_string(SlotId("hair2")) + " 2 3") sawHair2 = true; if (c == "/lookx set " + std::to_string(SlotId("backpack1")) + " 2 2") sawBack = true; }
        CHECK(sawHair2 && sawBack);
        ServerEcho(); Pump();
        CHECK(!s_await.active && !s_bak.active && !g_hostSuspend);
        CHECK(Vis("hair2") && Vis("backpack1"));
        st = Pub(); CHECK(!st.awaiting && !st.pending && U(st, "hair2").state == ProvadorModel::kEquipped);
        CHECK(st.notice.find("salva") != std::string::npos);
        CHECK(U(st, "hair2").srvKnown && U(st, "hair2").srvEst == 2 && U(st, "hair2").srvVar == 3);

        // exclusividade no servidor: hair1 e depois hair2 no mesmo commit => hair1 volta ao padrão no servidor
        g_hostSrv = ProvadorModel::NewTable(); ++g_hostSrvVer; s_srvSeenVer = g_hostSrvVer;
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden(); g_hostChat.clear();
        CHECK(Ui("prev hair1 1")); CHECK(Ui("prev hair2 2"));
        CHECK(CommitOk());
        CHECK(!g_hostSrv[(size_t)SlotId("hair1")].known && g_hostSrv[(size_t)SlotId("hair2")].known);

        // servidor RECUSA (eco sem a alteração): volta EXATAMENTE ao estado anterior e pede sync
        s_hidden.assign(n, 0); s_matPack.clear(); s_slotVar.clear(); RecountHidden(); DiscardBackup();
        RwTexture* beforeTex = MatTex("boot2"); const auto hiddenBefore = s_hidden; const auto packBefore = s_matPack;
        g_hostChat.clear(); CHECK(Ui("prev boot2 3")); CHECK(Ui("commit")); CHECK(s_await.active);
        for (int i = 0; i < 6; ++i) Pump();
        ++g_hostSrvVer; g_hostSrv = ProvadorModel::NewTable();
        Pump(); CHECK(s_await.active);
        s_await.startMs = 1; s_await.lastEchoMs = 1; s_lookLastSendMs = 1; Pump();
        CHECK(!s_await.active && !s_bak.active && !g_hostSuspend);
        CHECK(s_hidden == hiddenBefore && s_matPack == packBefore && MatTex("boot2") == beforeTex);
        CHECK(s_moldStale);
        bool sync = false; for (auto& c : g_hostChat) if (c == "/lookx sync") sync = true; CHECK(sync);
        st = Pub(); CHECK(st.notice.find("não confirmou") != std::string::npos);
        s_moldStale = false;

        // OFFLINE: não deixa confirmar
        g_hostOnline = false; CHECK(Ui("prev hair3 1")); CHECK(!Ui("commit")); { auto e = Pub(); CHECK(!e.reqOk && e.msg.find("conex") != std::string::npos); }
        CHECK(s_bak.active); CHECK(Ui("cancel")); CHECK(!s_bak.active);
        g_hostOnline = true;

        // caiu a conexão com prévia ativa
        Pump(); CHECK(Ui("prev hair3 2")); CHECK(Vis("hair3"));
        g_hostOnline = false; Pump();
        CHECK(!s_bak.active && s_lookPrevQ.empty() && s_lookQ.empty() && !g_hostSuspend);
        st = Pub(); CHECK(U(st, "hair3").state == ProvadorModel::kDefault && !U(st, "hair3").previewed);   // voltou ao padrão
         CHECK(st.notice.find("Conexão perdida") != std::string::npos && !st.online);
        g_hostOnline = true; Pump();

        // caiu durante a espera do servidor
        CHECK(Ui("prev hair4 1")); CHECK(Ui("commit")); CHECK(s_await.active);
        g_hostOnline = false; Pump();
        CHECK(!s_await.active && !s_bak.active && s_moldStale && U(Pub(), "hair4").state == ProvadorModel::kDefault);
        g_hostOnline = true; s_moldStale = false; Pump();
    }

    // ---- 14) molde trocado por fora durante a prévia ----
    {
        s_hidden.assign(n, 0); s_matPack.clear(); RecountHidden(); DiscardBackup(); g_hostSrv = ProvadorModel::NewTable();
        CHECK(Ui("prev hair2 1")); CHECK(s_bak.active);
        g_hostTemplate = [] () -> RpClump* { static RpClump o; return &o; };
        Pump(); CHECK(!s_bak.active && s_moldStale && !g_hostSuspend);
        g_hostTemplate = Tmpl; s_moldStale = false;
        for (auto& g : s_graveyard) g.second = 0; Pump();
    }

    // ---- 15) fila cheia: o pedido não some em silêncio ----
    {
        { std::lock_guard<std::mutex> l(s_cmdMutex); s_cmds.assign(64, "x"); }
        CHECK(CCustomSkin::UiRequest("refresh") == 0);
        { std::lock_guard<std::mutex> l(s_cmdMutex); s_cmds.clear(); }
        CHECK(CCustomSkin::UiRequest("refresh") != 0);
        { std::lock_guard<std::mutex> l(s_cmdMutex); s_cmds.clear(); }
    }

    // ---- 16) PREPARAÇÃO AUTOMÁTICA (molde fake com instalação) ----
    {
        static std::map<RpClump*, std::vector<RpMaterial*>> engine;
        static RpClump* installed = nullptr;
        static int reads = 0, failReads = 0;
        static uintptr_t vt = 0x1000 + 0x82F310;
        g_hostModelInfo = &vt; g_hostModelLoaded = true;
        g_hostRead = [] () -> RpClump* {
            ++reads; if (failReads > 0) { --failReads; return nullptr; }
            RpClump* c = new RpClump{};
            std::vector<RpMaterial*> v;
            for (size_t i = 0; i < s_parsed.materialsList.size(); ++i) {
                RwTexture* t = new RwTexture{}; t->refCount = 1; std::snprintf(t->name, sizeof(t->name), "%s", s_parsed.materialsList[i].texture.c_str());
                RpMaterial* m = new RpMaterial{}; m->texture = t; v.push_back(m);
            }
            engine[c] = v; return c; };
        g_hostInstall = [] (RpClump* c) { installed = c; };
        g_hostTemplate = [] () -> RpClump* { return installed; };
        g_hostCollect = [] (RpClump* c, std::vector<RpMaterial*>& out) { auto it = engine.find(c); if (it == engine.end()) return false; out = it->second; return true; };
        const std::string marker = std::string("/tmp/skin7host/SAMP/skins/") + ".applying";
        std::remove(marker.c_str());
        s_installed = nullptr; installed = nullptr; s_enabledTex.clear(); s_origAlpha.clear();
        s_prepGiveUp = false; s_prepAutoFail = 0; s_prepLastMs = 0; s_prepWhy.clear(); s_moldStale = false; s_await = Awaiting(); DiscardBackup();
        s_hidden.assign(n, 1);                                          // lixo de uma sessão anterior: a preparação NÃO pode herdar
        s_matPack.clear();
        g_hostSrv = ProvadorModel::NewTable();
        g_hostSrv[(size_t)SlotId("hair2")] = ProvadorModel::Srv{true, 2, 3};
        g_hostSrv[(size_t)SlotId("torso1")] = ProvadorModel::Srv{true, 0, 0};
        g_hostSrv[(size_t)SlotId("backpack2")] = ProvadorModel::Srv{true, 2, 2};
        g_hostSrvHave = true; ++g_hostSrvVer; g_hostChat.clear(); g_hostWearCalls = 0; g_hostLocalModel = 7;
        CHook::calls.clear();

        CHECK(Ui("refresh"));
        CHECK(InstalledOurs()); CHECK(reads == 1);
        CHECK(CHook::calls.size() == 2);
        CHECK(g_hostWearCalls == 1);
        auto st = Pub();
        CHECK(st.skinReady && st.prepMsg.empty());
        CHECK(U(st, "hair2").state == ProvadorModel::kEquipped && U(st, "hair2").variant == 3);
        CHECK(U(st, "backpack2").state == ProvadorModel::kEquipped && U(st, "backpack2").variant == 2);
        CHECK(U(st, "torso1").state == ProvadorModel::kDisabled);
        CHECK(U(st, "hair1").state == ProvadorModel::kDisabled);
        CHECK(U(st, "boot1").state == ProvadorModel::kDefault && U(st, "head3").state == ProvadorModel::kDefault);
        for (auto& c : g_hostChat) CHECK(c == "/lookx sync");           // restaurar NÃO escreve no servidor
        CHECK(s_lookQ.empty() && s_lookPrevQ.empty());
        CHECK(FileExists(marker));

        CHECK(Ui("refresh")); CHECK(Ui("refresh"));
        CHECK(reads == 1 && CHook::calls.size() == 2 && g_hostWearCalls == 1);

        installed = nullptr; s_installed = nullptr; g_hostLocalModel = 0; g_hostWearCalls = 0; s_prepLastMs = 0; std::remove(marker.c_str());
        CHECK(Ui("refresh")); CHECK(InstalledOurs()); CHECK(g_hostWearCalls == 0); CHECK(reads == 2);
        g_hostLocalModel = 7;

        installed = nullptr; s_installed = nullptr; s_prepLastMs = 0; std::remove(marker.c_str());
        CHECK(Ui("refresh")); CHECK(InstalledOurs() && reads == 3);

        installed = nullptr; s_installed = nullptr; s_prepLastMs = 0; g_hostModelLoaded = false; std::remove(marker.c_str());
        CHECK(Ui("refresh")); CHECK(!InstalledOurs() && reads == 3 && !s_prepGiveUp);
        st = Pub(); CHECK(!st.skinReady && st.prepMsg.find("modelo 7") != std::string::npos);
        g_hostModelLoaded = true;

        s_prepLastMs = NowMs(); CHECK(Ui("refresh")); CHECK(reads == 3);
        s_prepLastMs = 0; CHECK(Ui("refresh")); CHECK(InstalledOurs() && reads == 4);

        installed = nullptr; s_installed = nullptr; failReads = 100; std::remove(marker.c_str());
        for (int i = 0; i < 6; ++i) { s_prepLastMs = 0; Ui("refresh"); std::remove(marker.c_str()); }
        CHECK(!InstalledOurs() && s_prepGiveUp && reads == 4 + kPrepMaxAuto);
        st = Pub(); CHECK(st.prepMsg.find("PREPARAR") != std::string::npos);
        CHECK(!Ui("prev hair1 1"));
        failReads = 0;
        CHECK(Ui("prepare")); CHECK(InstalledOurs() && !s_prepGiveUp);

        installed = nullptr; s_installed = nullptr; s_prepLastMs = 0; s_prepGiveUp = false; s_prepAutoFail = 0;
        { FILE* f = std::fopen(marker.c_str(), "wb"); std::fputs("x", f); std::fclose(f); }
        const int r0 = reads; CHECK(Ui("refresh")); CHECK(!InstalledOurs() && reads == r0 && s_prepGiveUp);
        st = Pub(); CHECK(st.prepMsg.find(".applying") != std::string::npos);
        CHECK(Ui("prepare")); CHECK(InstalledOurs());
        std::remove(marker.c_str());

        // ---- CONTA NOVA (contanova.ini): servidor tem so os 7 slots padrao (estado 2, variante 1; head1=101), molde reinstalado ao abrir ----
        {
            installed = nullptr; s_installed = nullptr; s_prepLastMs = 0; s_prepGiveUp = false; s_prepAutoFail = 0; g_hostModelLoaded = true;
            std::remove(marker.c_str());
            s_hidden.assign(n, 1); s_matPack.clear(); s_slotVar.clear();
            g_hostSrv = ProvadorModel::NewTable();
            const char* defs[] = { "skin_torso", "skin_leg", "skin_feet", "head1", "brow1", "hair1", "underwear" };
            for (const char* d : defs) g_hostSrv[(size_t)SlotId(d)] = ProvadorModel::Srv{true, 2, (uint16_t)(std::string(d) == "head1" ? 101 : 1)};
            g_hostSrvHave = true; ++g_hostSrvVer; g_hostChat.clear(); g_hostLocalModel = 7;
            CHECK(Ui("refresh")); CHECK(InstalledOurs());
            for (const char* d : defs) {
                const SkinSlots::Slot& sl = SkinSlots::kSlots[(size_t)SlotId(d) - 1];
                const std::vector<int> idx = SlotMaterials(sl);
                bool allVis = !idx.empty();
                for (int i : idx) if (s_hidden[(size_t)i]) allVis = false;
                if (!allVis) { std::printf("CONTA NOVA: slot %s com material oculto/sem material (%zu mats)\n", d, idx.size());
                    for (int i : idx) std::printf("   mat %d hidden=%d tex=%s\n", i, (int)s_hidden[(size_t)i], s_parsed.materialsList[(size_t)i].texture.c_str()); }
                CHECK(allVis);
            }
            auto st2 = Pub();
            for (const char* d : defs) { if (U(st2, d).state != ProvadorModel::kEquipped) std::printf("CONTA NOVA: %s estado=%d\n", d, (int)U(st2, d).state); CHECK(U(st2, d).state == ProvadorModel::kEquipped); }
            CHECK(U(st2, "torso1").state == ProvadorModel::kDefault && U(st2, "pants1").state == ProvadorModel::kDefault && U(st2, "boot1").state == ProvadorModel::kDefault);   // "padrao, nada equipado"
            CHECK(!U(st2, "torso1").previewed && !st2.pending);
            std::remove(marker.c_str());
        }

        installed = nullptr; s_installed = nullptr; s_prepLastMs = 0; s_prepGiveUp = false; g_hostSrvHave = false;
        CHECK(Ui("refresh")); CHECK(InstalledOurs());
        st = Pub(); CHECK(!st.serverSynced && U(st, "hair2").state == ProvadorModel::kDefault);
        std::remove(marker.c_str());
    }

    // ---- 17) ABRIR SEM SKIN: mensagem clara, sem tentar instalar ----
    {
        s_loadedOk = false; s_installed = nullptr; s_prepGiveUp = false; s_prepLastMs = 0; s_parsed.materialsList.clear();
        const std::string dff = "/tmp/skin7host/SAMP/skins/skin.dff"; std::remove(dff.c_str());
        CHECK(Ui("refresh"));
        auto st = Pub();
        CHECK(!st.dffOk && !st.skinReady && !st.prepMsg.empty());
        CHECK(!Ui("prev hair1 1"));
        for (auto& u : st.slots) CHECK(!u.hasMaterial && u.state == ProvadorModel::kUnavailable);
    }

    if (fails) { std::printf("RESULTADO: %d FALHA(S)\n", fails); return 1; }
    std::printf("RESULTADO: todos os testes de host passaram\n");
    return 0;
}
