#pragma once
// Painel de ajuste do celular na mao (/celofs, so cliente). Logica pura (sem ImGui/jogo), testada no PC.
//
// O painel fica na lateral ESQUERDA, acima do joystick, e so existe enquanto estiver aberto (comando /celofs sem parametros).
// Cada toque em + / - vira UM comando de chat para o servidor ("/celofs <modo> <eixo> <valor>"), que reposiciona o objeto na mao
// do personagem ao vivo. O servidor e' a fonte dos valores: o painel pede "/celofs sync" ao abrir e le as linhas "[CELOFS] ...".
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace CelOfs
{
	constexpr int kModes = 3;                                   // 1 = celular na mao, 2 = foto, 3 = selfie
	constexpr int kAxes  = 6;                                   // x y z rx ry rz
	inline const char* ModeName(int i)  { static const char* n[kModes] = { "Mao", "Foto", "Selfie" }; return n[std::clamp(i, 0, kModes - 1)]; }
	inline const char* AxisName(int a)  { static const char* n[kAxes] = { "X (frente)", "Y (lado)", "Z (altura)", "Giro X", "Giro Y", "Giro Z" }; return n[std::clamp(a, 0, kAxes - 1)]; }
	constexpr float kPosSteps[3] = { 0.005f, 0.01f, 0.05f };    // metros
	constexpr float kRotSteps[3] = { 1.0f, 5.0f, 15.0f };       // graus
	constexpr int   kModels[2]   = { 330, 367 };                // cellphone (iPhone 17 Pro Max) / camera (iPhone 16)

	struct ModeVals { float v[kAxes] = { 0.075f, 0.035f, 0.025f, 0.0f, 90.0f, 0.0f }; int model = 330; bool known = false; };

	struct State
	{
		ModeVals m[kModes];
		int sel = 0;                                            // aba (0..2)
		int posStep = 1, rotStep = 1;                           // indices em kPosSteps/kRotSteps
		State() { m[1].model = 367; m[2].model = 367; }
	};

	enum class Act { None, Tab, Dec, Inc, PosStep, RotStep, Model, Save, Close, Panel };
	struct Hit { Act act = Act::None; int arg = 0; };

	struct R { float x = 0, y = 0, w = 0, h = 0; bool has(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; } };

	struct PanelLayout
	{
		float s = 1.0f;                                         // escala (altura da tela / 720)
		R panel, title, tab[kModes], row[kAxes], dec[kAxes], inc[kAxes], posStep, rotStep, model[2], save, close;
		float rowPad = 0;
	};

	inline PanelLayout Compute(float sw, float sh)
	{
		PanelLayout L;
		if (!(sw > 50.0f) || !(sh > 50.0f)) return L;
		const float s = std::clamp(sh / 720.0f, 0.5f, 2.0f); L.s = s;
		const float pad = 10 * s, w = 326 * s, x0 = 12 * s, y0 = 22 * s;
		float y = y0 + pad;
		L.title = { x0 + pad, y, w - 2 * pad, 24 * s }; y += 28 * s;
		const float tw = (w - 2 * pad - 2 * 6 * s) / 3.0f;
		for (int i = 0; i < kModes; ++i) L.tab[i] = { x0 + pad + i * (tw + 6 * s), y, tw, 30 * s };
		y += 36 * s;
		const float bw = 44 * s, rh = 30 * s;
		for (int a = 0; a < kAxes; ++a)
		{
			L.row[a] = { x0 + pad, y, w - 2 * pad, rh };
			L.inc[a] = { x0 + w - pad - bw, y, bw, rh };
			L.dec[a] = { x0 + w - pad - 2 * bw - 70 * s - 8 * s, y, bw, rh };       // [ - ] valor [ + ]  (valor no meio, 70*s)
			y += rh + 3 * s;
		}
		y += 4 * s;
		const float hw = (w - 2 * pad - 6 * s) / 2.0f;
		L.posStep = { x0 + pad, y, hw, 28 * s }; L.rotStep = { x0 + pad + hw + 6 * s, y, hw, 28 * s }; y += 32 * s;
		L.model[0] = { x0 + pad, y, hw, 28 * s }; L.model[1] = { x0 + pad + hw + 6 * s, y, hw, 28 * s }; y += 32 * s;
		L.save = { x0 + pad, y, hw, 32 * s }; L.close = { x0 + pad + hw + 6 * s, y, hw, 32 * s }; y += 32 * s;
		L.panel = { x0, y0, w, y + pad - y0 };
		return L;
	}

	inline Hit HitTest(const PanelLayout& L, float x, float y)
	{
		if (!(L.panel.w > 0) || !L.panel.has(x, y)) return {};
		for (int i = 0; i < kModes; ++i) if (L.tab[i].has(x, y)) return { Act::Tab, i };
		for (int a = 0; a < kAxes; ++a)
		{
			if (L.dec[a].has(x, y)) return { Act::Dec, a };
			if (L.inc[a].has(x, y)) return { Act::Inc, a };
		}
		if (L.posStep.has(x, y)) return { Act::PosStep, 0 };
		if (L.rotStep.has(x, y)) return { Act::RotStep, 0 };
		for (int i = 0; i < 2; ++i) if (L.model[i].has(x, y)) return { Act::Model, i };
		if (L.save.has(x, y))  return { Act::Save, 0 };
		if (L.close.has(x, y)) return { Act::Close, 0 };
		return { Act::Panel, 0 };                                // dentro do painel mas fora de botao: engole o toque (nao gira a camera)
	}

	inline float Step(const State& st, int axis) { return axis < 3 ? kPosSteps[std::clamp(st.posStep, 0, 2)] : kRotSteps[std::clamp(st.rotStep, 0, 2)]; }

	inline float Round3(float v) { return std::round(v * 1000.0f) / 1000.0f; }

	// aplica +/-: atualiza o estado e devolve o comando de chat para o servidor
	inline std::string Nudge(State& st, int axis, int dir)
	{
		if (axis < 0 || axis >= kAxes) return {};
		ModeVals& mv = st.m[std::clamp(st.sel, 0, kModes - 1)];
		const float lim = axis < 3 ? 1.5f : 360.0f;
		mv.v[axis] = std::clamp(Round3(mv.v[axis] + (dir < 0 ? -1.0f : 1.0f) * Step(st, axis)), -lim, lim);
		char b[96]; std::snprintf(b, sizeof(b), "/celofs %d %d %.4f", st.sel + 1, axis, (double)mv.v[axis]);
		return b;
	}
	inline std::string SelectTab(State& st, int i) { st.sel = std::clamp(i, 0, kModes - 1); char b[48]; std::snprintf(b, sizeof(b), "/celofs %d -1 0", st.sel + 1); return b; }
	inline std::string SetModel(State& st, int which)
	{
		const int id = kModels[std::clamp(which, 0, 1)];
		st.m[std::clamp(st.sel, 0, kModes - 1)].model = id;
		char b[48]; std::snprintf(b, sizeof(b), "/celmodelo %d %d", st.sel + 1, id); return b;
	}
	inline const char* kSyncCmd = "/celofs sync";
	inline const char* kSaveCmd = "/celsalvar";

	// "[CELOFS] modo x y z rx ry rz modelo" (linha do servidor). true = era uma linha do painel (nao mostrar no chat).
	inline bool IsLine(const std::string& s) { return s.rfind("[CELOFS]", 0) == 0; }
	inline bool ParseLine(const std::string& s, State& st)
	{
		if (!IsLine(s)) return false;
		int mode = 0, model = 0; float v[kAxes];
		if (std::sscanf(s.c_str() + 8, " %d %f %f %f %f %f %f %d", &mode, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &model) == 8
			&& mode >= 1 && mode <= kModes)
		{
			ModeVals& mv = st.m[mode - 1];
			for (int i = 0; i < kAxes; ++i) if (std::isfinite(v[i])) mv.v[i] = v[i];
			mv.model = model; mv.known = true;
		}
		return true;                                              // linha do painel mesmo se invalida: nunca vai para o chat
	}

	inline std::string Fmt(int axis, float v) { char b[24]; std::snprintf(b, sizeof(b), axis < 3 ? "%.3f" : "%.1f", (double)v); return b; }

	// segurar + / -: depois de kHoldDelay repete a cada kHoldEvery
	constexpr long kHoldDelayMs = 380, kHoldEveryMs = 90;
	struct Hold
	{
		bool on = false; Act act = Act::None; int arg = 0; long start = 0, last = 0;
		void begin(Act a, int g, long now) { on = (a == Act::Dec || a == Act::Inc); act = a; arg = g; start = last = now; }
		void end() { on = false; }
		bool due(long now) { if (!on || now - start < kHoldDelayMs || now - last < kHoldEveryMs) return false; last = now; return true; }
	};
}
