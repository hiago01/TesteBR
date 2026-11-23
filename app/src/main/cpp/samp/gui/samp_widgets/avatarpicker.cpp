#include "../../main.h"
#include "../gui.h"
#include "avatar.h"
#include "central.h"
#include "central_logic.h"
#include "avatarpicker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

namespace
{
	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	std::string g_toast;
	int64_t     g_toastUntil = 0;
	int64_t     g_lastList = 0;
	void Toast(const std::string& s) { g_toast = s; g_toastUntil = NowMs() + 2600; }

	struct Lay
	{
		float photoA[2], photoB[2];       // botao de foto
		float t1y, t2y;                   // y dos titulos
		int   fc, lc;                     // colunas de molduras / fundos
		float fx, fy, fw, fh, fgap;       // celulas das molduras (origem, tamanho, espaco)
		float lx, ly, lw, lh, lgap;       // celulas dos fundos
		float toastY, total;
	};

	void Compute(float x0, float y0, float w, float u, Lay& L)
	{
		const bool wide = (w / u) >= 500.0f;
		L.fc = wide ? 8 : 4; L.lc = wide ? 4 : 3;
		float y = y0;
		L.photoA[0] = x0; L.photoA[1] = y; L.photoB[0] = x0 + w; L.photoB[1] = y + 30.0f * u;
		y += 40.0f * u;
		L.t1y = y; y += 17.0f * u;
		L.fgap = 6.0f * u; L.fh = 64.0f * u; L.fx = x0; L.fy = y;
		L.fw = (w - L.fgap * (float)(L.fc - 1)) / (float)L.fc;
		y += (float)((Avatar::kBorderCount + L.fc - 1) / L.fc) * (L.fh + L.fgap);
		y += 4.0f * u;
		L.t2y = y; y += 17.0f * u;
		L.lgap = 6.0f * u; L.lh = 34.0f * u; L.lx = x0; L.ly = y;
		L.lw = (w - L.lgap * (float)(L.lc - 1)) / (float)L.lc;
		y += (float)((Avatar::kLineCount + L.lc - 1) / L.lc) * (L.lh + L.lgap);
		L.toastY = y; y += 16.0f * u;
		L.total = y - y0;
	}

	void FrameCell(const Lay& L, int i, float& ax, float& ay, float& bx, float& by)
	{
		ax = L.fx + (float)(i % L.fc) * (L.fw + L.fgap); ay = L.fy + (float)(i / L.fc) * (L.fh + L.fgap);
		bx = ax + L.fw; by = ay + L.fh;
	}
	void LineCell(const Lay& L, int i, float& ax, float& ay, float& bx, float& by)
	{
		ax = L.lx + (float)(i % L.lc) * (L.lw + L.lgap); ay = L.ly + (float)(i / L.lc) * (L.lh + L.lgap);
		bx = ax + L.lw; by = ay + L.lh;
	}

	// cadeado desenhado por primitivas, centro (cx,cy), s = tamanho
	void Padlock(ImGuiRenderer* r, float cx, float cy, float s)
	{
		const ImColor c(255, 214, 96, 255), d(40, 30, 10, 255);
		ImVec2 arc[12];
		for (int k = 0; k < 12; k++) { const float a = 3.14159265f + 3.14159265f * (float)k / 11.0f; arc[k] = ImVec2(cx + std::cos(a) * s * 0.34f, cy - s * 0.02f + std::sin(a) * s * 0.40f); }
		r->drawPolyline(arc, 12, c, false, std::max(1.2f, s * 0.13f));
		r->drawRoundedRect(ImVec2(cx - s * 0.46f, cy - s * 0.04f), ImVec2(cx + s * 0.46f, cy + s * 0.48f), c, s * 0.10f, true);
		r->drawRoundedRect(ImVec2(cx - s * 0.06f, cy + s * 0.14f), ImVec2(cx + s * 0.06f, cy + s * 0.32f), d, s * 0.05f, true);
	}
	bool In(float x, float y, float ax, float ay, float bx, float by) { return x >= ax && x <= bx && y >= ay && y <= by; }
}

float AvatarPicker::Height(float w, float u) { Lay L; Compute(0.0f, 0.0f, w, u, L); return L.total; }

int AvatarPicker::HitTest(float x0, float y0, float w, float u, float px, float py)
{
	Lay L; Compute(x0, y0, w, u, L);
	if (In(px, py, L.photoA[0], L.photoA[1], L.photoB[0], L.photoB[1])) return 1;
	for (int i = 0; i < Avatar::kBorderCount; i++) { float a, b, c, d; FrameCell(L, i, a, b, c, d); if (In(px, py, a, b, c, d)) return 100 + i; }
	for (int i = 0; i < Avatar::kLineCount; i++)   { float a, b, c, d; LineCell(L, i, a, b, c, d);  if (In(px, py, a, b, c, d)) return 200 + i; }
	return 0;
}

void AvatarPicker::Activate(int code)
{
	if (code == 1) { Avatar::RequestPick(); return; }
	if (code >= 100 && code < 100 + Avatar::kBorderCount)
	{
		const int i = code - 100;
		if (!Avatar::BorderUnlocked(i)) { Toast("Moldura bloqueada: só a administração libera."); return; }
		Avatar::SetBorder(i);
		return;
	}
	if (code >= 200 && code < 200 + Avatar::kLineCount)
	{
		const int i = code - 200;
		if (!Avatar::LineUnlocked(i)) { Toast("Fundo bloqueado: só a administração libera."); return; }
		CentralUI::ChooseStyle(i);
	}
}

void AvatarPicker::Draw(ImGuiRenderer* r, float x0, float y0, float w, float u, double t, int pressed)
{
	if (!r) return;
	Avatar::Tick();                                                      // textura, envio pendente e pedido de INFO ao servidor
	{ std::string m; if (Avatar::PopMessage(m)) Toast(m); }              // resultado do envio da foto etc.
	const int64_t now = NowMs();
	if (!CentralUI::HaveList() && now - g_lastList > 4000) { g_lastList = now; CentralUI::RequestList(); }   // pede o modelo de fundo atual da conta

	Lay L; Compute(x0, y0, w, u, L);
	const ImColor cText(240, 246, 255, 255), cMute(150, 176, 214, 230), cAcc(98, 232, 250, 255), cCell(20, 26, 40, 215), cLn(86, 104, 140, 200);

	// foto
	{
		const bool pr = pressed == 1;
		r->drawRoundedRect(ImVec2(L.photoA[0], L.photoA[1]), ImVec2(L.photoB[0], L.photoB[1]), pr ? ImColor(60, 120, 160, 235) : cCell, 6.0f * u, true);
		r->drawRoundedRect(ImVec2(L.photoA[0], L.photoA[1]), ImVec2(L.photoB[0], L.photoB[1]), cLn, 6.0f * u, false, 1.0f);
		const char* lab = "ESCOLHER FOTO DA GALERIA";
		const ImVec2 ts = r->calculateTextSize(std::string(lab), 11.0f * u);
		r->drawText(ImVec2((L.photoA[0] + L.photoB[0] - ts.x) * 0.5f, (L.photoA[1] + L.photoB[1] - ts.y) * 0.5f), cText, std::string(lab), false, 11.0f * u);
	}

	// molduras do avatar
	r->drawText(ImVec2(x0, L.t1y), cMute, std::string("Moldura do avatar"), false, 10.5f * u);
	for (int i = 0; i < Avatar::kBorderCount; i++)
	{
		float ax, ay, bx, by; FrameCell(L, i, ax, ay, bx, by);
		const bool sel = Avatar::Border() == i, open = Avatar::BorderUnlocked(i), pr = pressed == 100 + i;
		r->drawRoundedRect(ImVec2(ax, ay), ImVec2(bx, by), pr ? ImColor(60, 120, 160, 235) : cCell, 6.0f * u, true);
		r->drawRoundedRect(ImVec2(ax, ay), ImVec2(bx, by), sel ? cAcc : cLn, 6.0f * u, false, sel ? 2.0f * u : 1.0f);
		const float cx = (ax + bx) * 0.5f, cy = ay + 28.0f * u, ar = std::min(13.5f * u, (bx - ax) * 0.32f);
		{ constexpr int N = 24; ImVec2 p[N]; for (int k = 0; k < N; k++) { const float a = 6.2831853f * (float)k / (float)N; p[k] = ImVec2(cx + std::cos(a) * ar, cy + std::sin(a) * ar); } r->drawConvexPolyFilled(p, N, ImColor(190, 190, 200, 255)); }
		Avatar::DrawBorder(r, i, cx, cy, ar, u * 0.7f, t);
		const char* nm = Avatar::BorderName(i);
		const ImVec2 ts = r->calculateTextSize(std::string(nm), 8.6f * u);
		r->drawText(ImVec2((ax + bx - ts.x) * 0.5f, by - ts.y - 4.0f * u), open ? cText : cMute, std::string(nm), false, 8.6f * u);
		if (!open)
		{
			r->drawRoundedRect(ImVec2(ax, ay), ImVec2(bx, by), ImColor(8, 10, 18, 150), 6.0f * u, true);
			Padlock(r, cx, cy, 20.0f * u);
		}
	}

	// modelos de fundo da linha
	r->drawText(ImVec2(x0, L.t2y), cMute, std::string("Fundo da sua linha na Central"), false, 10.5f * u);
	for (int i = 0; i < Avatar::kLineCount; i++)
	{
		float ax, ay, bx, by; LineCell(L, i, ax, ay, bx, by);
		const bool sel = CentralUI::MyStyle() == i, open = Avatar::LineUnlocked(i), pr = pressed == 200 + i;
		CentralUI::DrawStylePreview(r, ImVec2(ax, ay), ImVec2(bx, by), i, u, t);
		const char* nm = CenLogic::Style(i).name;
		const ImVec2 ts = r->calculateTextSize(std::string(nm), 10.0f * u);
		r->drawText(ImVec2((ax + bx - ts.x) * 0.5f, (ay + by - ts.y) * 0.5f), ImColor(255, 255, 255, 255), std::string(nm), true, 10.0f * u);
		if (!open)
		{
			r->drawRect(ImVec2(ax, ay), ImVec2(bx, by), ImColor(8, 10, 18, 165), true);
			Padlock(r, bx - 12.0f * u, (ay + by) * 0.5f, 14.0f * u);
		}
		if (pr) r->drawRect(ImVec2(ax, ay), ImVec2(bx, by), ImColor(255, 255, 255, 50), true);
		if (sel) r->drawRect(ImVec2(ax - 2.0f * u, ay - 2.0f * u), ImVec2(bx + 2.0f * u, by + 2.0f * u), cAcc, false, 2.0f * u);
	}

	if (now < g_toastUntil) r->drawText(ImVec2(x0, L.toastY), ImColor(255, 190, 110, 255), g_toast, false, 10.0f * u);
}
