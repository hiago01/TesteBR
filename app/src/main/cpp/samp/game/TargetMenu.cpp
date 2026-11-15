// TargetMenu.cpp - ver TargetMenu.h.
#include "../main.h"
#include "../vendor/imgui/imgui.h"
#include "TargetMenu.h"
#include "PlayerCarry.h"
#include "PlayerCarryCore.h"
#include "../net/netgame.h"
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
std::atomic<int>  g_finger{ -1 };
std::atomic<int>  g_pendingAct{ -1 };      // indice de opcao tocada (thread de toque -> desenho)
std::atomic<bool> g_pendingClose{ false };
struct Box { float x = 0, y = 0, w = 0, h = 0; };
Box g_box;                                  // area total do menu (para o toque)
std::vector<Box> g_btn;                     // botoes (para o toque) -- protegido por g_boxVer (copia simples; tamanho fixo pequeno)
Box g_btnArr[8]; std::atomic<int> g_btnN{ 0 };

struct Opt { std::string label; std::string cmd; };
struct Menu {
    bool isActor = false; int id = -1; std::string name;
    std::vector<Opt> opts;
    long long openedMs = 0;
    float ax = 0, ay = 0;                   // ancora (toque)
} g_menu;

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
    if (PlayerCarry::LocalIsCarrying()) { m.opts.push_back({ "Soltar", "/soltar" }); return; }
    snprintf(cmd, sizeof cmd, "/carregar %d", m.isActor ? (int)(carrycore::kActorBase + m.id) : m.id);
    m.opts.push_back({ "Carregar", cmd });                 // 1a opcao
    // (novas opcoes entram aqui, em ordem)
}

struct Lay { Box box; Box btn[8]; int n = 0; float fs = 0; Box title; };
Lay Layout(const Menu& m, float sw, float sh) {
    Lay L; L.fs = std::max(18.0f, sh * 0.034f);
    const float bh = L.fs * 2.3f, bw = L.fs * 9.0f, gap = L.fs * 0.35f, pad = L.fs * 0.6f, th = L.fs * 1.6f;
    L.n = (int)std::min<size_t>(m.opts.size(), 8);
    const float w = bw + pad * 2, h = pad * 2 + th + L.n * bh + (L.n > 0 ? (L.n - 1) * gap : 0.f);
    float x = m.ax + L.fs, y = m.ay - h * 0.5f;                       // a direita do toque
    if (x + w > sw - 8) x = m.ax - L.fs - w;                          // sem espaco: a esquerda
    x = std::max(8.0f, std::min(x, sw - w - 8)); y = std::max(8.0f, std::min(y, sh - h - 8));
    L.box = { x, y, w, h }; L.title = { x + pad, y + pad, bw, th };
    for (int i = 0; i < L.n; i++) L.btn[i] = { x + pad, y + pad + th + i * (bh + gap), bw, bh };
    return L;
}
bool In(const Box& b, float x, float y) { return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h; }
}

namespace TargetMenu {

bool IsOpen() { return g_open.load(); }
void Close() { g_pendingClose.store(true); }

void ObserveTouch(int type, int num, int x, int y) {
    if (g_open.load()) return;
    Tap& t = g_tap[num & 3]; const long long now = NowMs();
    if (type == 2) { t.valid = true; t.x = (float)x; t.y = (float)y; t.t = now; }
    else if (type == 3) { if (t.valid && (std::fabs((float)x - t.x) > 28.0f || std::fabs((float)y - t.y) > 28.0f)) t.valid = false; }
    else if (type == 1) {
        if (t.valid && now - t.t < 450) { g_tapX.store(t.x); g_tapY.store(t.y); g_tapReq.store(true); }
        t.valid = false;
    }
}

bool HandleRawTouch(int type, int num, int x, int y) {
    if (!g_open.load()) return false;
    if (type == 2) {
        if (g_finger.load() >= 0) return false;
        const float fx = (float)x, fy = (float)y;
        if (!In(g_box, fx, fy)) { g_pendingClose.store(true); return false; }      // toque fora: fecha e o toque segue para o jogo
        g_finger.store(num);
        const int n = g_btnN.load();
        for (int i = 0; i < n; i++) if (In(g_btnArr[i], fx, fy)) { g_pendingAct.store(i); break; }
        return true;
    }
    if (g_finger.load() != num) return false;
    if (type == 1) g_finger.store(-1);
    return true;
}

void Draw() {
    // fecha / toque pendente
    if (g_pendingClose.exchange(false)) { g_open.store(false); g_finger.store(-1); g_btnN.store(0); }
    if (g_open.load() && (NowMs() - g_menu.openedMs > kAutoCloseMs)) { g_open.store(false); g_btnN.store(0); }

    if (g_tapReq.exchange(false) && !g_open.load()) {
        CPedGTA* me = LocalGta(); CPlayerPed* lp = pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer() ? pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed() : nullptr;
        Cand c;
        if (me && lp && !lp->IsInVehicle() && FindTarget(g_tapX.load(), g_tapY.load(), c)) {
            Menu m; m.isActor = c.isActor; m.id = c.id; m.openedMs = NowMs(); m.ax = g_tapX.load(); m.ay = g_tapY.load();
            char nm[40];
            if (c.isActor) snprintf(nm, sizeof nm, "NPC %d", c.id);
            else { const char* pn = pNetGame->GetPlayerPool()->GetPlayerName((PLAYERID)c.id); snprintf(nm, sizeof nm, "%s (%d)", pn ? pn : "Jogador", c.id); }
            m.name = nm; BuildOptions(m);
            g_menu = m; g_open.store(true);
            FLog("[TARGETMENU] aberto: %s %d (%.1f m)", c.isActor ? "actor" : "jogador", c.id, c.dist);
        }
    }
    if (!g_open.load()) return;

    const ImVec2 sc = ImGui::GetIO().DisplaySize;
    const Lay L = Layout(g_menu, sc.x, sc.y);
    g_box = L.box; g_btnN.store(L.n);
    for (int i = 0; i < L.n; i++) g_btnArr[i] = L.btn[i];

    // opcao tocada
    const int act = g_pendingAct.exchange(-1);
    if (act >= 0 && act < (int)g_menu.opts.size()) {
        if (pNetGame) pNetGame->SendChatCommand(g_menu.opts[act].cmd.c_str());
        FLog("[TARGETMENU] opcao '%s' -> %s", g_menu.opts[act].label.c_str(), g_menu.opts[act].cmd.c_str());
        g_open.store(false); g_btnN.store(0); g_finger.store(-1);
        return;
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(L.box.x, L.box.y), ImVec2(L.box.x + L.box.w, L.box.y + L.box.h), IM_COL32(25, 27, 35, 235), L.fs * 0.6f);
    dl->AddRect(ImVec2(L.box.x, L.box.y), ImVec2(L.box.x + L.box.w, L.box.y + L.box.h), IM_COL32(255, 255, 255, 70), L.fs * 0.6f, 15, 2.0f);
    ImFont* f = ImGui::GetFont();
    auto text = [&](const Box& b, const char* s, float fs, ImU32 col) {
        const ImVec2 sz = f->CalcTextSizeA(fs, 100000.0f, 0.0f, s);
        dl->AddText(f, fs, ImVec2(b.x + (b.w - sz.x) * 0.5f, b.y + (b.h - sz.y) * 0.5f), col, s);
    };
    text(L.title, g_menu.name.c_str(), L.fs * 0.9f, IM_COL32(200, 205, 220, 255));
    for (int i = 0; i < L.n; i++) {
        const Box& b = L.btn[i];
        dl->AddRectFilled(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), i == 0 ? IM_COL32(40, 120, 220, 240) : IM_COL32(70, 70, 82, 240), b.h * 0.22f);
        text(b, g_menu.opts[i].label.c_str(), L.fs, IM_COL32(255, 255, 255, 255));
    }
}

} // namespace TargetMenu
