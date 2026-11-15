// CarryTuner.cpp - ver CarryTuner.h. Desenha com o draw list do ImGui (sem widgets do ImGui: o ImGui do projeto nao recebe toque).
// Duas abas: CARREGADO (posicao/orientacao/dobra do corpo carregado) e CARREGADOR (rotacao de cada osso dos bracos de quem carrega).
#include "../main.h"
#include "../vendor/imgui/imgui.h"
#include "CarryTuner.h"
#include "CarryArms.h"
#include "PlayerCarry.h"
#include "PlayerCarryCore.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern char* g_pszStorage;

namespace CarryTuner {
namespace {
std::atomic<bool> s_open{false};
bool s_loaded = false, s_right = false;
std::atomic<int> s_page{0};            // 0 = CARREGADO, 1 = CARREGADOR
std::atomic<int> s_bone{0};            // osso selecionado na aba CARREGADOR
char s_status[96] = "";

enum Act { A_NONE = 0, A_DEC, A_INC, A_SAVE, A_RESET, A_CLOSE, A_SIDE, A_TAB0, A_TAB1 };
struct Row { const char* label; float* v; float lo, hi, step; const char* fmt; };   // v == nullptr -> linha especial (dobra / osso)

struct Rect { float x = 0, y = 0, w = 0, h = 0; bool has(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; } };
struct Layout { float u; Rect panel, tab[2], side, rowR[7], dec[7], inc[7], val[7], bSave, bReset, bClose; };

std::atomic<int> s_finger{-1};
std::atomic<int> s_holdAct{A_NONE}, s_holdRow{-1};
std::atomic<long long> s_holdStart{0}, s_holdLast{0};
std::atomic<int> s_pending{A_NONE};       // acoes que mexem em estado do jogo rodam em Draw (thread de render)
std::atomic<bool> s_reapply{false}, s_armsDirty{false};

long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string IniPath() {
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/');
    return p + "SAMP/carry_tuning.ini";
}

void Save() {
    carrycore::Tuning& t = carrycore::Tune();
    FLog("[CARRY-TUNE] offX=%.3f offY=%.3f offZ=%.3f tilt=%.1f roll=%.1f yaw=%.1f bend=%d", t.offX, t.offY, t.offZ, t.tilt, t.roll, t.yaw, t.bend);
    for (int i = 0; i < carryarms::kBones; i++)
        FLog("[CARRY-TUNE] arm%d(%s)=%.1f,%.1f,%.1f", i, carryarms::BoneName(i), carryarms::Arms().deg[i][0], carryarms::Arms().deg[i][1], carryarms::Arms().deg[i][2]);
    std::string path = IniPath();
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "w");
    if (!f) { snprintf(s_status, sizeof s_status, "NAO salvou (sem pasta). Valores no samp.log"); return; }
    fprintf(f, "ver=2\n");
    fprintf(f, "offx=%.3f\noffy=%.3f\noffz=%.3f\ntilt=%.1f\nroll=%.1f\nyaw=%.1f\nbend=%d\n", t.offX, t.offY, t.offZ, t.tilt, t.roll, t.yaw, t.bend);
    for (int i = 0; i < carryarms::kBones; i++)
        fprintf(f, "arm%d=%.1f,%.1f,%.1f\n", i, carryarms::Arms().deg[i][0], carryarms::Arms().deg[i][1], carryarms::Arms().deg[i][2]);
    fclose(f);
    snprintf(s_status, sizeof s_status, "Salvo: SAMP/carry_tuning.ini");
}

int RowCount() { return s_page.load() == 0 ? 7 : 4; }

Layout Compute(float W, float H) {
    Layout L; const float u = H / 100.0f; L.u = u;
    const float pw = 74 * u, ph = 92 * u;
    L.panel = { s_right ? W - pw - 2 * u : 2 * u, 5 * u, pw, ph };
    const float x0 = L.panel.x + 2 * u, y0 = L.panel.y + 2 * u, iw = pw - 4 * u;
    const float tw = (iw - 14 * u) * 0.5f;
    L.tab[0] = { x0, y0, tw, 7 * u }; L.tab[1] = { x0 + tw + 1 * u, y0, tw, 7 * u };
    L.side  = { x0 + iw - 12 * u, y0, 12 * u, 7 * u };
    const float rh = 8.4f * u, gap = 1.0f * u; float y = y0 + 9 * u;
    for (int i = 0; i < 7; i++) {
        L.rowR[i] = { x0, y, iw, rh };
        const float bw = 10 * u, vw = 15 * u;
        L.inc[i] = { x0 + iw - bw, y, bw, rh };
        L.val[i] = { L.inc[i].x - vw - gap, y, vw, rh };
        L.dec[i] = { L.val[i].x - bw - gap, y, bw, rh };
        y += rh + gap;
    }
    const float bw = (iw - 2 * gap) / 3.0f, bh = 10 * u;
    L.bSave = { x0, y + gap, bw, bh }; L.bReset = { x0 + bw + gap, y + gap, bw, bh }; L.bClose = { x0 + 2 * (bw + gap), y + gap, bw, bh };
    return L;
}

Row Rows(int i) {
    if (s_page.load() == 1) {
        const int b = s_bone.load();
        float* d = carryarms::Arms().deg[b];
        switch (i) {
        case 0: return { carryarms::BoneName(b), nullptr, 0, 0, 1, "" };
        case 1: return { "  giro X", &d[0], -180.f, 180.f, 2.0f, "%.0f" };
        case 2: return { "  giro Y", &d[1], -180.f, 180.f, 2.0f, "%.0f" };
        default: return { "  giro Z", &d[2], -180.f, 180.f, 2.0f, "%.0f" };
        }
    }
    carrycore::Tuning& t = carrycore::Tune();
    switch (i) {
    case 0: return { "Lado X (+direita)", &t.offX, -0.8f, 0.8f, 0.01f, "%.2f" };
    case 1: return { "Frente/tras Y",     &t.offY, -0.8f, 0.8f, 0.01f, "%.2f" };
    case 2: return { "Altura Z",          &t.offZ, -0.2f, 1.4f, 0.01f, "%.2f" };
    case 3: return { "Inclinacao (tilt)", &t.tilt, -180.f, 180.f, 2.0f, "%.0f" };
    case 4: return { "Giro lado (roll)",  &t.roll, -180.f, 180.f, 2.0f, "%.0f" };
    case 5: return { "Virar (yaw) 180=troca", &t.yaw, -180.f, 180.f, 5.0f, "%.0f" };
    default: return { "Dobra do corpo",   nullptr, -3, 3, 1, "%+d" };
    }
}

void Nudge(int row, int dir) {
    if (row < 0 || row >= RowCount()) return;
    Row r = Rows(row);
    if (!r.v) {
        if (s_page.load() == 1) {                                          // aba CARREGADOR: trocar o osso
            int b = (s_bone.load() + dir + carryarms::kBones) % carryarms::kBones;
            s_bone.store(b);
        } else {
            carrycore::Tuning& t = carrycore::Tune();
            int b = t.bend + dir; if (b < -3) b = -3; if (b > 3) b = 3;
            if (b != t.bend) { t.bend = b; s_reapply.store(true); }
        }
        return;
    }
    float nv = *r.v + dir * r.step;
    if (nv < r.lo) nv = r.lo;
    if (nv > r.hi) nv = r.hi;
    *r.v = nv;
    if (s_page.load() == 1) s_armsDirty.store(true);
}

void Reset() {
    if (s_page.load() == 1) { carryarms::Arms() = carryarms::Deltas(); s_armsDirty.store(true); snprintf(s_status, sizeof s_status, "Bracos: padrao"); }
    else { carrycore::Tune() = carrycore::Tuning(); s_reapply.store(true); snprintf(s_status, sizeof s_status, "Corpo: padrao"); }
}
} // namespace

void Toggle() { s_open.store(!s_open.load()); LoadSaved(); s_finger.store(-1); s_holdAct.store(A_NONE); }
bool IsOpen() { return s_open.load(); }

void LoadSaved() {
    if (s_loaded) return;
    s_loaded = true;
    std::string path = IniPath();
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f) return;
    carrycore::Tuning& t = carrycore::Tune();
    char line[128];
    bool armsOk = false;   // ini sem ver=2 tem bracos antigos (ajustes errados) -> ignora
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "ver=", 4)) { armsOk = atoi(line + 4) >= 2; continue; }
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0; const char* val = eq + 1; const float v = (float)atof(val);
        if (!strcmp(line, "offx")) t.offX = v; else if (!strcmp(line, "offy")) t.offY = v; else if (!strcmp(line, "offz")) t.offZ = v;
        else if (!strcmp(line, "tilt")) t.tilt = v; else if (!strcmp(line, "roll")) t.roll = v; else if (!strcmp(line, "yaw")) t.yaw = v;
        else if (!strcmp(line, "bend")) t.bend = (int)v;
        else if (!strncmp(line, "arm", 3) && line[3] >= '0' && line[3] < '0' + carryarms::kBones && !line[4] && armsOk) {
            float* d = carryarms::Arms().deg[line[3] - '0'];
            sscanf(val, "%f,%f,%f", &d[0], &d[1], &d[2]);
        }
    }
    fclose(f);
    if (t.bend < -3) t.bend = -3;
    if (t.bend > 3) t.bend = 3;
    s_armsDirty.store(true);
    FLog("[CARRY-TUNE] carregado de %s: offX=%.3f offY=%.3f offZ=%.3f tilt=%.1f roll=%.1f yaw=%.1f bend=%d", path.c_str(), t.offX, t.offY, t.offZ, t.tilt, t.roll, t.yaw, t.bend);
}

void ApplyPending() {                       // chamado por PlayerCarry (o arquivo carregar.ifp pode carregar depois do painel)
    if (s_armsDirty.exchange(false)) PlayerCarry::ApplyCarrierArms();
}

// resolve o que esta em (x,y): act + linha
static void HitTest(const Layout& L, float x, float y, int& act, int& row) {
    act = A_NONE; row = -1;
    if (!L.panel.has(x, y)) return;
    act = -1;                                            // dentro do painel (consome) mesmo sem botao
    if (L.tab[0].has(x, y)) { act = A_TAB0; return; }
    if (L.tab[1].has(x, y)) { act = A_TAB1; return; }
    if (L.side.has(x, y)) { act = A_SIDE; return; }
    if (L.bSave.has(x, y)) { act = A_SAVE; return; }
    if (L.bReset.has(x, y)) { act = A_RESET; return; }
    if (L.bClose.has(x, y)) { act = A_CLOSE; return; }
    const int n = RowCount();
    for (int i = 0; i < n; i++) {
        if (L.dec[i].has(x, y)) { act = A_DEC; row = i; return; }
        if (L.inc[i].has(x, y)) { act = A_INC; row = i; return; }
    }
}

bool HandleRawTouch(int type, int num, int x, int y) {
    if (!s_open.load()) return false;
    if (type == 2) {                                     // push
        if (s_finger.load() >= 0) return false;
        const ImVec2 sc = ImGui::GetIO().DisplaySize;
        const Layout L = Compute(sc.x, sc.y);
        int act, row; HitTest(L, (float)x, (float)y, act, row);
        if (act == A_NONE) return false;                 // fora do painel: joystick/camera seguem para o jogo
        s_finger.store(num);
        if (act == A_DEC || act == A_INC) {
            Nudge(row, act == A_INC ? +1 : -1);
            s_holdAct.store(act); s_holdRow.store(row); s_holdStart.store(NowMs()); s_holdLast.store(NowMs());
        } else if (act > 0) { s_pending.store(act); }
        return true;
    }
    if (s_finger.load() != num) return false;            // move/pop: so do dedo dono
    if (type == 1) { s_finger.store(-1); s_holdAct.store(A_NONE); }
    return true;
}

static void Txt(ImDrawList* dl, const Rect& r, const char* s, float fs, ImU32 col, bool center) {
    ImFont* f = ImGui::GetFont();
    const ImVec2 sz = f->CalcTextSizeA(fs, 100000.0f, 0.0f, s);
    const float px = center ? r.x + (r.w - sz.x) * 0.5f : r.x + 0.8f * (fs * 0.5f);
    dl->AddText(f, fs, ImVec2(px, r.y + (r.h - sz.y) * 0.5f), col, s);
}
static void Btn(ImDrawList* dl, const Rect& r, const char* s, float fs, bool strong = false) {
    dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), strong ? IM_COL32(40, 120, 220, 235) : IM_COL32(70, 70, 80, 235), r.h * 0.2f);
    Txt(dl, r, s, fs, IM_COL32(255, 255, 255, 255), true);
}

void Draw() {
    if (!s_open.load()) return;

    // acoes pendentes (thread de render)
    const int pend = s_pending.exchange(A_NONE);
    if (pend == A_SAVE) Save(); else if (pend == A_RESET) Reset(); else if (pend == A_CLOSE) { s_open.store(false); return; }
    else if (pend == A_SIDE) s_right = !s_right;
    else if (pend == A_TAB0) s_page.store(0); else if (pend == A_TAB1) s_page.store(1);
    if (s_reapply.exchange(false)) PlayerCarry::ReapplyAnims();
    ApplyPending();
    // segurar - / +: repete
    if (s_holdAct.load() != A_NONE) {
        const long long now = NowMs();
        if (now - s_holdStart.load() > 450 && now - s_holdLast.load() > 60) {
            s_holdLast.store(now);
            Nudge(s_holdRow.load(), s_holdAct.load() == A_INC ? +1 : -1);
        }
    }

    const ImVec2 sc = ImGui::GetIO().DisplaySize;
    const Layout L = Compute(sc.x, sc.y);
    const float fs = L.u * 3.6f;
    const int page = s_page.load();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(L.panel.x, L.panel.y), ImVec2(L.panel.x + L.panel.w, L.panel.y + L.panel.h), IM_COL32(0, 0, 0, 205), 1.5f * L.u);
    dl->AddRect(ImVec2(L.panel.x, L.panel.y), ImVec2(L.panel.x + L.panel.w, L.panel.y + L.panel.h), IM_COL32(255, 255, 255, 90), 1.5f * L.u, 15, 1.5f);
    Btn(dl, L.tab[0], "CARREGADO", fs * 0.9f, page == 0);
    Btn(dl, L.tab[1], "CARREGADOR", fs * 0.9f, page == 1);
    Btn(dl, L.side, "<>", fs);

    const int n = RowCount();
    for (int i = 0; i < n; i++) {
        const Row r = Rows(i);
        char val[24];
        if (r.v) snprintf(val, sizeof val, r.fmt, *r.v);
        else if (page == 1) snprintf(val, sizeof val, "%d/%d", s_bone.load() + 1, carryarms::kBones);
        else snprintf(val, sizeof val, r.fmt, carrycore::Tune().bend * 20);
        Rect lab = L.rowR[i]; lab.w = L.dec[i].x - lab.x;
        Txt(dl, lab, r.label, fs * 0.9f, IM_COL32(230, 230, 230, 255), false);
        Btn(dl, L.dec[i], page == 1 && i == 0 ? "<" : "-", fs * 1.3f);
        dl->AddRectFilled(ImVec2(L.val[i].x, L.val[i].y), ImVec2(L.val[i].x + L.val[i].w, L.val[i].y + L.val[i].h), IM_COL32(20, 20, 24, 235), L.val[i].h * 0.2f);
        Txt(dl, L.val[i], val, fs, IM_COL32(120, 220, 255, 255), true);
        Btn(dl, L.inc[i], page == 1 && i == 0 ? ">" : "+", fs * 1.3f);
    }
    if (page == 1) {
        Rect hint = { L.panel.x + 2 * L.u, L.rowR[4].y, L.panel.w - 4 * L.u, 30 * L.u };
        dl->AddText(ImGui::GetFont(), fs * 0.8f, ImVec2(hint.x, hint.y), IM_COL32(200, 200, 200, 255),
                    "Escolha o osso em < >, depois gire X/Y/Z.\nValem em tempo real no carregador.\nBraco D/E = ombro; cotovelo; mao.\nSALVAR guarda tudo (corpo + bracos).");
    }
    Btn(dl, L.bSave, "SALVAR", fs, true); Btn(dl, L.bReset, "PADRAO", fs); Btn(dl, L.bClose, "FECHAR", fs);
    if (s_status[0]) {
        Rect st = { L.panel.x + 2 * L.u, L.bSave.y + L.bSave.h + 0.5f * L.u, L.panel.w - 4 * L.u, 5 * L.u };
        Txt(dl, st, s_status, fs * 0.8f, IM_COL32(255, 220, 120, 255), false);
    }
}

} // namespace CarryTuner
