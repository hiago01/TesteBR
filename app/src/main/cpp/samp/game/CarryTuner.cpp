// CarryTuner.cpp - ver CarryTuner.h
#include "../main.h"
#include "../vendor/imgui/imgui.h"
#include "CarryTuner.h"
#include "PlayerCarry.h"
#include "PlayerCarryCore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern char* g_pszStorage;

namespace CarryTuner {
namespace {
bool s_open = false, s_loaded = false, s_right = false;
char s_status[96] = "";

std::string IniPath() {
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/');
    return p + "SAMP/carry_tuning.ini";
}

void Save() {
    carrycore::Tuning& t = carrycore::Tune();
    FLog("[CARRY-TUNE] offX=%.3f offY=%.3f offZ=%.3f tilt=%.1f roll=%.1f bend=%d", t.offX, t.offY, t.offZ, t.tilt, t.roll, t.bend);
    std::string path = IniPath();
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "w");
    if (!f) { snprintf(s_status, sizeof s_status, "NAO salvou (sem pasta). Valores no samp.log"); return; }
    fprintf(f, "offx=%.3f\noffy=%.3f\noffz=%.3f\ntilt=%.1f\nroll=%.1f\nbend=%d\n", t.offX, t.offY, t.offZ, t.tilt, t.roll, t.bend);
    fclose(f);
    snprintf(s_status, sizeof s_status, "Salvo em SAMP/carry_tuning.ini");
}

void Reset() { carrycore::Tune() = carrycore::Tuning(); PlayerCarry::ReapplyAnims(); snprintf(s_status, sizeof s_status, "Valores padrao"); }

// linha:  rotulo  [-]  slider  [+]
bool Row(const char* label, float* v, float lo, float hi, float step, const char* fmt, float w) {
    bool ch = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const float bw = ImGui::GetFrameHeight() * 1.5f;
    if (ImGui::Button("-", ImVec2(bw, 0))) { *v -= step; ch = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w - 2 * bw - 2 * ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::SliderFloat("##s", v, lo, hi, fmt)) ch = true;
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(bw, 0))) { *v += step; ch = true; }
    ImGui::PopID();
    if (*v < lo) *v = lo; if (*v > hi) *v = hi;
    return ch;
}
} // namespace

void Toggle() { s_open = !s_open; LoadSaved(); }
bool IsOpen() { return s_open; }

void LoadSaved() {
    if (s_loaded) return;
    s_loaded = true;
    std::string path = IniPath();
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f) return;
    carrycore::Tuning& t = carrycore::Tune();
    char line[96];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0; const float v = (float)atof(eq + 1);
        if (!strcmp(line, "offx")) t.offX = v; else if (!strcmp(line, "offy")) t.offY = v; else if (!strcmp(line, "offz")) t.offZ = v;
        else if (!strcmp(line, "tilt")) t.tilt = v; else if (!strcmp(line, "roll")) t.roll = v; else if (!strcmp(line, "bend")) t.bend = (int)v;
    }
    fclose(f);
    if (t.bend < -3) t.bend = -3; if (t.bend > 3) t.bend = 3;
    FLog("[CARRY-TUNE] carregado de %s: offX=%.3f offY=%.3f offZ=%.3f tilt=%.1f roll=%.1f bend=%d", path.c_str(), t.offX, t.offY, t.offZ, t.tilt, t.roll, t.bend);
}

void Draw() {
    if (!s_open) return;
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float pw = W * 0.40f, ph = H * 0.80f;
    ImGui::SetNextWindowPos(ImVec2(s_right ? W - pw - W * 0.02f : W * 0.02f, H * 0.10f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 oldPad = st.FramePadding, oldGrab = ImVec2(st.GrabMinSize, 0);
    st.FramePadding = ImVec2(10.0f, H * 0.012f + 6.0f);
    st.GrabMinSize = H * 0.045f;
    ImGui::Begin("Ajuste do carregar", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(H / 720.0f * 1.15f);

    carrycore::Tuning& t = carrycore::Tune();
    const float w = ImGui::GetContentRegionAvail().x;
    Row("Lado (X)  + = direita do carregador", &t.offX, -0.8f, 0.8f, 0.01f, "%.2f m", w);
    Row("Frente/tras (Y)  - = para tras", &t.offY, -0.8f, 0.8f, 0.01f, "%.2f m", w);
    Row("Altura (Z)", &t.offZ, -0.2f, 1.4f, 0.01f, "%.2f m", w);
    Row("Inclinacao (pes p/ frente)", &t.tilt, -90.0f, 90.0f, 1.0f, "%.0f graus", w);
    Row("Giro para o lado (roll)", &t.roll, -90.0f, 90.0f, 1.0f, "%.0f graus", w);

    ImGui::TextUnformatted("Dobra do corpo (coluna)");
    const float bw = ImGui::GetFrameHeight() * 1.5f;
    int b = t.bend;
    if (ImGui::Button("<", ImVec2(bw, 0))) b--;
    ImGui::SameLine();
    char buf[32]; snprintf(buf, sizeof buf, "%+d graus x2", b * 20);
    ImGui::Button(buf, ImVec2(w - 2 * bw - 2 * st.ItemSpacing.x, 0));
    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(bw, 0))) b++;
    if (b < -3) b = -3; if (b > 3) b = 3;
    if (b != t.bend) { t.bend = b; PlayerCarry::ReapplyAnims(); }

    const float bh = ImGui::GetFrameHeight() * 1.2f, hw = (w - 2 * st.ItemSpacing.x) / 3.0f;
    ImGui::Spacing();
    if (ImGui::Button("SALVAR", ImVec2(hw, bh))) Save();
    ImGui::SameLine();
    if (ImGui::Button("PADRAO", ImVec2(hw, bh))) Reset();
    ImGui::SameLine();
    if (ImGui::Button("FECHAR", ImVec2(hw, bh))) s_open = false;
    if (ImGui::Button(s_right ? "Mover painel para a ESQUERDA" : "Mover painel para a DIREITA", ImVec2(w, 0))) s_right = !s_right;
    if (s_status[0]) ImGui::TextUnformatted(s_status);
    ImGui::TextWrapped("Carregue o boneco (/npccarregar e /carregar npc) e mexa nos controles olhando o carregado.");

    ImGui::End();
    st.FramePadding = oldPad; st.GrabMinSize = oldGrab.x;
}

} // namespace CarryTuner
