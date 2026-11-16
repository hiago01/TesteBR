// TargetMenu.cpp - ver TargetMenu.h.
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "net/actorpool.h"
#include "actor.h"
#include "../vendor/imgui/imgui.h"
#include "../vendor/raknet/SAMP/SAMPRPC.h"
#include "TargetMenu.h"
#include "PlayerCarry.h"
#include "PlayerCarryCore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern CNetGame* pNetGame;
extern uintptr_t g_libGTASA;

namespace {
long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr float kMaxDist = 6.0f;          // so aparece para quem esta perto
constexpr long long kAutoCloseMs = 10000;

// --- toque (thread de toque) ---
struct Tap { bool valid = false; float x = 0, y = 0; long long t = 0; };
Tap g_tap[4];
std::atomic<bool>  g_tapReq{ false };
std::atomic<float> g_tapX{ 0 }, g_tapY{ 0 };

// --- menu (so a thread de desenho mexe em g_menu; toque so le g_open/rects via atomics) ---
std::atomic<bool> g_open{ false };
std::atomic<bool> g_closing{ false };
std::atomic<int>  g_finger{ -1 };
std::atomic<int>  g_pendingAct{ -1 };      // indice de opcao tocada (thread de toque -> desenho)
std::atomic<bool> g_pendingClose{ false };
struct Box { float x = 0, y = 0, w = 0, h = 0; };
Box g_box;                                  // area total do menu (para o toque)
std::vector<Box> g_btn;                     // botoes (para o toque) -- protegido por g_boxVer (copia simples; tamanho fixo pequeno)
Box g_btnArr[8]; std::atomic<int> g_btnN{ 0 };

struct Opt { std::string label; std::string cmd; int icon = 0; };   // icon: 0 carregar, 1 soltar, 2 fechar; cmd vazio = so fecha
struct Menu {
    bool isActor = false; int id = -1; int acct = 0;
    std::vector<Opt> opts;
    long long openedMs = 0;
    float ax = 0, ay = 0;                   // ancora (toque)
} g_menu;

// --- admin / ids de conta (RPC 234) ---
std::atomic<bool> g_admin{ false };
std::atomic<int>  g_acct[1004];

// --- animacao: g_openAt = inicio da abertura; g_closeAt > 0 = fechando (fim do menu quando passar kCloseMs) ---
constexpr long long kOpenMs = 170, kCloseMs = 130;
long long g_openAt = 0, g_closeAt = 0;
std::atomic<int> g_pressed{ -1 };            // linha pressionada (destaque azul)

bool Proj(const CVector& w0, ImVec2& o) {
    CVector w = w0, sc;
    ((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + 0x6E9DF8))(&w, &sc, 0, 0, 0, 0);   // CSprite::CalcScreenCoors
    if (sc.z < 1.0f) return false;
    o = ImVec2(sc.x, sc.y);
    return true;
}

bool PedPos(CPedGTA* p, CVector& out) {
    if (!p || !IsValidGamePed(p)) return false;
    CVector bp(0.f, 0.f, 0.f);
    p->GetBonePosition(&bp, 1 /*pelve*/, false);
    if (!std::isfinite(bp.x) || !std::isfinite(bp.y) || !std::isfinite(bp.z) || (std::fabs(bp.x) + std::fabs(bp.y) + std::fabs(bp.z) < 0.01f)) return false;
    out = bp;
    return true;
}

CPedGTA* LocalGta() {
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool || !pool->GetLocalPlayer()) return nullptr;
    CPlayerPed* lp = pool->GetLocalPlayer()->GetPlayerPed();
    return (lp && lp->m_pPed) ? lp->m_pPed : nullptr;
}

struct Cand { bool isActor; int id; CVector pos; float dist; };

// Procura o jogador/actor tocado: retangulo de tela do corpo (cabeca..pes) com folga. Mais perto vence.
bool FindTarget(float tx, float ty, Cand& best) {
    if (!pNetGame) return false;
    CPedGTA* me = LocalGta(); CVector mp;
    if (!me || !PedPos(me, mp)) return false;
    float bestD = 1e9f; bool found = false;
    auto test = [&](bool isActor, int id, CPedGTA* ped) {
        CVector p; if (!PedPos(ped, p)) return;
        const float dx = p.x - mp.x, dy = p.y - mp.y, dz = p.z - mp.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > kMaxDist) return;
        ImVec2 top, mid, bot;
        if (!Proj(CVector(p.x, p.y, p.z + 0.85f), top) || !Proj(p, mid) || !Proj(CVector(p.x, p.y, p.z - 0.95f), bot)) return;
        const float h = bot.y - top.y; if (h < 8.0f) return;
        const float halfW = std::max(h * 0.28f, 30.0f), pad = 18.0f;
        if (tx < mid.x - halfW - pad || tx > mid.x + halfW + pad || ty < top.y - pad || ty > bot.y + pad) return;
        if (d < bestD) { bestD = d; best = Cand{ isActor, id, p, d }; found = true; }
    };
    CPlayerPool* pool = pNetGame->GetPlayerPool();
    if (pool) {
        const PLAYERID me_id = pool->GetLocalPlayerID();
        for (PLAYERID i = 0; i < MAX_PLAYERS; i++) {
            if (i == me_id || !pool->GetSlotState(i)) continue;
            CRemotePlayer* rp = pool->GetAt(i); CPlayerPed* pp = rp ? rp->GetPlayerPed() : nullptr;
            if (pp && pp->m_pPed) test(false, i, pp->m_pPed);
        }
    }
    CActorPool* ap = pNetGame->GetActorPool();
    if (ap) for (PLAYERID i = 0; i < MAX_ACTORS; i++) { CActor* a = ap->GetAt(i); if (a && a->m_pPed) test(true, i, a->m_pPed); }
    return found;
}

void BuildOptions(Menu& m) {
    m.opts.clear();
    char cmd[48];
    if (PlayerCarry::LocalIsCarrying()) m.opts.push_back({ "Soltar", "/soltar", 1 });
    else {
        snprintf(cmd, sizeof cmd, "/carregar %d", m.isActor ? (int)(carrycore::kActorBase + m.id) : m.id);
        m.opts.push_back({ "Carregar", cmd, 0 });          // 1a opcao
    }
    // (novas opcoes entram aqui, em ordem)
    m.opts.push_back({ "Fechar", "", 2 });
}

struct MenuLay { Box box; Box row[8]; int n = 0; float fs = 0; Box head; };
MenuLay MakeLay(const Menu& m, float sw, float sh) {
    MenuLay L; L.fs = std::max(14.0f, sh * 0.027f);
    const float rh = L.fs * 1.9f, w = L.fs * 8.6f, pad = L.fs * 0.35f, hh = L.fs * 2.7f;
    L.n = (int)std::min<size_t>(m.opts.size(), 8);
    const float h = pad + hh + L.n * rh + pad;
    float x = m.ax + L.fs * 1.2f, y = m.ay - h * 0.5f;                  // a direita do toque
    if (x + w > sw - 8) x = m.ax - L.fs * 1.2f - w;                     // sem espaco: a esquerda
    x = std::max(8.0f, std::min(x, sw - w - 8)); y = std::max(8.0f, std::min(y, sh - h - 8));
    L.box = { x, y, w, h }; L.head = { x, y + pad, w, hh };
    for (int i = 0; i < L.n; i++) L.row[i] = { x + pad, y + pad + hh + i * rh, w - pad * 2, rh };
    return L;
}
bool In(const Box& b, float x, float y) { return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h; }

// icones simples desenhados no draw list
void Icon(ImDrawList* dl, int kind, float cx, float cy, float r, ImU32 col) {
    const float t = std::max(1.5f, r * 0.18f);
    if (kind == 2) {                                   // X
        dl->AddLine(ImVec2(cx - r * .6f, cy - r * .6f), ImVec2(cx + r * .6f, cy + r * .6f), col, t);
        dl->AddLine(ImVec2(cx - r * .6f, cy + r * .6f), ImVec2(cx + r * .6f, cy - r * .6f), col, t);
    } else if (kind == 0) {                            // carregar: seta para cima sobre uma base
        dl->AddLine(ImVec2(cx, cy + r * .45f), ImVec2(cx, cy - r * .6f), col, t);
        dl->AddLine(ImVec2(cx - r * .45f, cy - r * .15f), ImVec2(cx, cy - r * .6f), col, t);
        dl->AddLine(ImVec2(cx + r * .45f, cy - r * .15f), ImVec2(cx, cy - r * .6f), col, t);
        dl->AddLine(ImVec2(cx - r * .6f, cy + r * .7f), ImVec2(cx + r * .6f, cy + r * .7f), col, t);
    } else {                                           // soltar: seta para baixo
        dl->AddLine(ImVec2(cx, cy - r * .6f), ImVec2(cx, cy + r * .45f), col, t);
        dl->AddLine(ImVec2(cx - r * .45f, cy + r * .15f), ImVec2(cx, cy + r * .6f), col, t);
        dl->AddLine(ImVec2(cx + r * .45f, cy + r * .15f), ImVec2(cx, cy + r * .6f), col, t);
    }
}

float Ease(float t) { t = std::max(0.f, std::min(1.f, t)); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }   // ease-out cubico

void BeginClose() { if (g_closeAt == 0) g_closeAt = NowMs(); }
}

namespace TargetMenu {

bool IsOpen() { return g_open.load(); }
void Close() { g_pendingClose.store(true); }

static void ScrTarget(RPCParameters* rpcParams) {
    RakNet::BitStream bs((unsigned char*)rpcParams->input, (rpcParams->numberOfBitsOfData / 8) + 1, false);
    uint8_t ver = 0, op = 0;
    if (!bs.Read(ver) || !bs.Read(op) || ver != 1) return;
    if (op == 1) { uint8_t a = 0; if (bs.Read(a)) { g_admin.store(a != 0); if (!a) g_pendingClose.store(true); FLog("[TARGETMENU] admin=%d", (int)a); } }
    else if (op == 2) { uint16_t pid = 0; int32_t acct = 0; if (bs.Read(pid) && bs.Read(acct) && pid < 1004) g_acct[pid].store(acct); }
}
static int s_rpc = 234;
void Register(RakClientInterface* rak) {
    for (auto& a : g_acct) a.store(0);
    g_admin.store(false);
    rak->RegisterAsRemoteProcedureCall(&s_rpc, ScrTarget);
}
void Unregister(RakClientInterface* rak) { rak->UnregisterAsRemoteProcedureCall(&s_rpc); g_admin.store(false); g_pendingClose.store(true); }

void ObserveTouch(int type, int num, int x, int y) {
    if (g_open.load() || !g_admin.load()) return;
    Tap& t = g_tap[num & 3]; const long long now = NowMs();
    if (type == 2) { t.valid = true; t.x = (float)x; t.y = (float)y; t.t = now; }
    else if (type == 3) { if (t.valid && (std::fabs((float)x - t.x) > 28.0f || std::fabs((float)y - t.y) > 28.0f)) t.valid = false; }
    else if (type == 1) {
        if (t.valid && now - t.t < 450) { g_tapX.store(t.x); g_tapY.store(t.y); g_tapReq.store(true); }
        t.valid = false;
    }
}

bool HandleRawTouch(int type, int num, int x, int y) {
    if (!g_open.load() || g_closing.load()) return false;
    if (type == 2) {
        if (g_finger.load() >= 0) return false;
        const float fx = (float)x, fy = (float)y;
        if (!In(g_box, fx, fy)) { g_pendingClose.store(true); return false; }      // toque fora: fecha e o toque segue para o jogo
        g_finger.store(num);
        const int n = g_btnN.load();
        for (int i = 0; i < n; i++) if (In(g_btnArr[i], fx, fy)) { g_pressed.store(i); break; }
        return true;
    }
    if (g_finger.load() != num) return false;
    if (type == 1) {                                     // soltou: so vale se ainda esta na linha em que apertou
        const int pr = g_pressed.load();
        if (pr >= 0 && pr < g_btnN.load() && In(g_btnArr[pr], (float)x, (float)y)) g_pendingAct.store(pr);
        g_pressed.store(-1); g_finger.store(-1);
    }
    return true;
}

void Draw() {
    const long long now = NowMs();
    if (g_pendingClose.exchange(false) && g_open.load()) BeginClose();
    if (g_open.load() && g_closeAt == 0 && (now - g_menu.openedMs > kAutoCloseMs)) BeginClose();
    if (g_closeAt > 0 && now - g_closeAt >= kCloseMs) { g_open.store(false); g_closing.store(false); g_closeAt = 0; g_btnN.store(0); g_finger.store(-1); g_pressed.store(-1); }
    g_closing.store(g_closeAt > 0);

    if (g_tapReq.exchange(false) && !g_open.load() && g_admin.load()) {
        CPlayerPed* lp = pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer() ? pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed() : nullptr;
        Cand c;
        if (lp && lp->m_pPed && !lp->IsInVehicle() && FindTarget(g_tapX.load(), g_tapY.load(), c)) {
            Menu m; m.isActor = c.isActor; m.id = c.id; m.openedMs = now; m.ax = g_tapX.load(); m.ay = g_tapY.load();
            m.acct = c.isActor ? 0 : (c.id >= 0 && c.id < 1004 ? g_acct[c.id].load() : 0);
            BuildOptions(m);
            g_menu = m; g_open.store(true); g_openAt = now; g_closeAt = 0; g_pressed.store(-1);
            FLog("[TARGETMENU] aberto: %s %d conta=%d (%.1f m)", c.isActor ? "actor" : "jogador", c.id, m.acct, c.dist);
        }
    }
    if (!g_open.load()) return;

    const ImVec2 sc = ImGui::GetIO().DisplaySize;
    const MenuLay L = MakeLay(g_menu, sc.x, sc.y);
    g_box = L.box; g_btnN.store(L.n);
    for (int i = 0; i < L.n; i++) g_btnArr[i] = L.row[i];

    // opcao tocada: executa e fecha com animacao
    const int act = g_pendingAct.exchange(-1);
    if (act >= 0 && act < (int)g_menu.opts.size() && g_closeAt == 0) {
        if (!g_menu.opts[act].cmd.empty() && pNetGame) {
            pNetGame->SendChatCommand(g_menu.opts[act].cmd.c_str());
            FLog("[TARGETMENU] opcao '%s' -> %s", g_menu.opts[act].label.c_str(), g_menu.opts[act].cmd.c_str());
        }
        BeginClose();
    }

    // animacao: abrir = cresce de 0.82 a 1 + fade; fechar = o inverso. Ancora = ponto do toque.
    float k = (g_closeAt > 0) ? 1.0f - Ease((float)(now - g_closeAt) / (float)kCloseMs) : Ease((float)(now - g_openAt) / (float)kOpenMs);
    const float sca = 0.82f + 0.18f * k, alpha = k;
    const float ox = (g_menu.ax < L.box.x + L.box.w * 0.5f) ? L.box.x : L.box.x + L.box.w, oy = L.box.y + L.box.h * 0.5f;   // origem da escala: borda mais perto do toque
    auto TX = [&](float x) { return ox + (x - ox) * sca; };
    auto TY = [&](float y) { return oy + (y - oy) * sca; };
    auto A = [&](int a) { return (int)(a * alpha); };

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float rad = L.fs * 0.35f * sca;
    dl->AddRectFilled(ImVec2(TX(L.box.x), TY(L.box.y)), ImVec2(TX(L.box.x + L.box.w), TY(L.box.y + L.box.h)), IM_COL32(28, 28, 30, A(235)), rad);
    dl->AddRect(ImVec2(TX(L.box.x), TY(L.box.y)), ImVec2(TX(L.box.x + L.box.w), TY(L.box.y + L.box.h)), IM_COL32(255, 255, 255, A(40)), rad, 15, 1.5f);
    ImFont* f = ImGui::GetFont();
    auto text = [&](const char* s, float fs, float cx, float cy, ImU32 col) {
        const float z = fs * sca; const ImVec2 sz = f->CalcTextSizeA(z, 100000.0f, 0.0f, s);
        dl->AddText(f, z, ImVec2(TX(cx) - sz.x * 0.5f, TY(cy) - sz.y * 0.5f), col, s);
    };
    // cabecalho: "<conta> [<playerid>]" e "(jogador)" / "NPC [id]" e "(npc)"
    char l1[48], l2[16];
    if (g_menu.isActor) { snprintf(l1, sizeof l1, "NPC [%d]", g_menu.id); snprintf(l2, sizeof l2, "(npc)"); }
    else if (g_menu.acct > 0) { snprintf(l1, sizeof l1, "%d [%d]", g_menu.acct, g_menu.id); snprintf(l2, sizeof l2, "(jogador)"); }
    else { snprintf(l1, sizeof l1, "[%d]", g_menu.id); snprintf(l2, sizeof l2, "(jogador)"); }
    const float hcx = L.head.x + L.head.w * 0.5f;
    text(l1, L.fs * 1.0f, hcx, L.head.y + L.head.h * 0.33f, IM_COL32(255, 255, 255, A(255)));
    text(l2, L.fs * 0.8f, hcx, L.head.y + L.head.h * 0.72f, IM_COL32(170, 175, 185, A(255)));
    const int pr = g_pressed.load();
    for (int i = 0; i < L.n; i++) {
        const Box& b = L.row[i];
        if (i == pr) dl->AddRectFilled(ImVec2(TX(b.x), TY(b.y)), ImVec2(TX(b.x + b.w), TY(b.y + b.h)), IM_COL32(52, 152, 255, A(235)), rad * 0.6f);
        const float cy = b.y + b.h * 0.5f;
        const ImU32 fg = IM_COL32(235, 237, 242, A(255));
        const float ix = b.x + L.fs * 0.9f;
        const float z = L.fs * sca;
        Icon(dl, g_menu.opts[i].icon, TX(ix), TY(cy), z * 0.55f, fg);
        const std::string& lb = g_menu.opts[i].label; const float fz = z * 0.95f;
        dl->AddText(f, fz, ImVec2(TX(b.x + L.fs * 1.8f), TY(cy) - fz * 0.5f), fg, lb.c_str());
    }
}

} // namespace TargetMenu
