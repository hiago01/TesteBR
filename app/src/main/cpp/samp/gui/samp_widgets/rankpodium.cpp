#include "../../main.h"
#include "../gui.h"
#include "avatar.h"
#include "central.h"
#include "rankpodium.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
	struct Metal { ImColor dark, mid, light, text; };
	Metal MetalOf(int place)
	{
		if (place == 0) return { ImColor(120, 82, 10, 255),  ImColor(214, 160, 34, 255),  ImColor(255, 226, 120, 255), ImColor(255, 214, 90, 255) };
		if (place == 1) return { ImColor(70, 78, 92, 255),   ImColor(150, 160, 178, 255), ImColor(226, 232, 244, 255), ImColor(214, 224, 238, 255) };
		return                 { ImColor(98, 52, 22, 255),   ImColor(188, 112, 56, 255),  ImColor(240, 178, 120, 255), ImColor(236, 160, 100, 255) };
	}

	void Ellipse(ImGuiRenderer* r, float cx, float cy, float rx, float ry, const ImColor& c)
	{
		constexpr int N = 28; ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = 6.2831853f * (float)i / (float)N; p[i] = ImVec2(cx + std::cos(a) * rx, cy + std::sin(a) * ry); }
		r->drawConvexPolyFilled(p, N, c);
	}

	// ramo de louro: 7 folhas ao longo de um arco (lado -1 = esquerdo, +1 = direito) em volta da foto
	void Laurel(ImGuiRenderer* r, float cx, float cy, float rad, int side, const ImColor& col, float u)
	{
		for (int i = 0; i < 7; i++)
		{
			const float k = (float)i / 6.0f;
			const float ang = 1.5708f + (float)side * (0.55f + k * 2.35f);            // de baixo, subindo pelos lados
			const float px = cx + std::cos(ang) * rad, py = cy + std::sin(ang) * rad;
			const float tx = -std::sin(ang) * (float)(-side), ty = std::cos(ang) * (float)(-side);   // tangente (sentido da subida)
			const float nx = std::cos(ang), ny = std::sin(ang);                        // normal (para fora)
			const float L = (6.5f - 1.8f * k) * u, W = (2.8f - 0.6f * k) * u;
			// folha = losango inclinado para fora
			const float dx = tx * L + nx * L * 0.35f, dy = ty * L + ny * L * 0.35f;
			ImVec2 p[4] = { ImVec2(px, py), ImVec2(px + dx * 0.5f + nx * W, py + dy * 0.5f + ny * W), ImVec2(px + dx, py + dy), ImVec2(px + dx * 0.5f - nx * W, py + dy * 0.5f - ny * W) };
			r->drawConvexPolyFilled(p, 4, col);
		}
	}

	void Crown(ImGuiRenderer* r, float cx, float baseY, float w, float h, const Metal& m)
	{
		const float x0 = cx - w * 0.5f;
		const ImVec2 p[9] = { ImVec2(x0, baseY), ImVec2(x0, baseY - h * 0.62f), ImVec2(x0 + w * 0.22f, baseY - h * 0.30f), ImVec2(x0 + w * 0.36f, baseY - h),
		                      ImVec2(cx, baseY - h * 0.42f), ImVec2(x0 + w * 0.64f, baseY - h), ImVec2(x0 + w * 0.78f, baseY - h * 0.30f), ImVec2(x0 + w, baseY - h * 0.62f), ImVec2(x0 + w, baseY) };
		ImVec2 q[5] = { p[0], p[2], p[4], p[6], p[8] };
		r->drawConvexPolyFilled(q, 5, m.mid);
		ImVec2 a[3] = { p[1], p[2], p[0] }, b[3] = { p[2], p[3], p[4] }, c[3] = { p[4], p[5], p[6] }, d[3] = { p[6], p[7], p[8] };
		r->drawConvexPolyFilled(a, 3, m.light); r->drawConvexPolyFilled(b, 3, m.light); r->drawConvexPolyFilled(c, 3, m.light); r->drawConvexPolyFilled(d, 3, m.light);
		r->drawPolyline(p, 9, m.dark, true, std::max(1.0f, h * 0.05f));
		r->drawRoundedRect(ImVec2(x0, baseY - h * 0.16f), ImVec2(x0 + w, baseY), m.dark, h * 0.08f, true);
	}

	std::string Fit(ImGuiRenderer* r, const std::string& s, float fs, float maxW)
	{
		if (r->calculateTextSize(s, fs).x <= maxW) return s;
		std::string t = s;
		while (t.size() > 1 && r->calculateTextSize(t + "..", fs).x > maxW) { t.pop_back(); while (t.size() > 1 && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); }
		return t + "..";
	}
}

void RankPodium::Draw(ImGuiRenderer* r, float x0, float y0, float w, float u, double t, const Entry e[3])
{
	if (!r) return;
	Avatar::Tick();                                  // fotos de outras contas viram textura aqui
	const float H = kHeight * u, base = y0 + H - 2.0f * u;
	// brilho do palco atras do podio
	for (int g = 3; g >= 1; g--) Ellipse(r, x0 + w * 0.5f, base - 20.0f * u, w * (0.18f + 0.09f * (float)g), (22.0f + 9.0f * (float)g) * u, ImColor(150, 110, 255, 12));

	struct Slot { int place; float cxF, pedW, pedTop, pedH, photoR; };
	const Slot slots[3] = { { 1, 0.20f, 150.0f, 144.0f, 24.0f, 22.0f }, { 2, 0.80f, 150.0f, 144.0f, 24.0f, 22.0f }, { 0, 0.50f, 196.0f, 132.0f, 36.0f, 28.0f } };   // 2o, 3o e por ultimo o 1o (fica na frente)
	for (const Slot& s : slots)
	{
		const Entry& en = e[s.place];
		const Metal m = MetalOf(s.place);
		const float cx = x0 + w * s.cxF, hw = s.pedW * 0.5f * u;
		const float top = y0 + s.pedTop * u, bot = top + s.pedH * u;

		// pedestal: corpo com brilho metalico + tampa elipse + sombra
		Ellipse(r, cx, bot + 1.0f * u, hw + 6.0f * u, 6.0f * u, ImColor(0, 0, 0, 90));
		r->drawRectFilledMulticolor(ImVec2(cx - hw, top), ImVec2(cx, bot), m.dark, m.light, m.light, m.dark);
		r->drawRectFilledMulticolor(ImVec2(cx, top), ImVec2(cx + hw, bot), m.light, m.dark, m.dark, m.light);
		Ellipse(r, cx, bot, hw, 5.0f * u, m.dark);
		Ellipse(r, cx, top, hw, 6.0f * u, m.light);
		Ellipse(r, cx, top, hw - 6.0f * u, 4.0f * u, m.mid);
		{   // numero no corpo
			const std::string n = std::to_string(s.place + 1);
			const float fs = (s.place == 0 ? 17.0f : 13.0f) * u;
			const ImVec2 ts = r->calculateTextSize(n, fs);
			const ImVec2 pos(cx - ts.x * 0.5f, top + (s.pedH * u - ts.y) * 0.5f + 2.0f * u);
			r->drawText(ImVec2(pos.x + 1.0f * u, pos.y + 1.0f * u), ImColor(0, 0, 0, 160), n, true, fs);
			r->drawText(pos, ImColor(255, 255, 255, 255), n, true, fs);
		}
		if (!en.ok) continue;

		// plaquinha do nome com o FUNDO DE LINHA do jogador (Central) + faixa escura com o valor
		const float pw = (s.pedW - 22.0f) * u, ph = 34.0f * u;
		const ImVec2 pa(cx - pw * 0.5f, top - ph + 6.0f * u), pb(cx + pw * 0.5f, top + 6.0f * u);
		CentralUI::DrawStylePreview(r, pa, pb, en.style, u, t);
		r->drawRoundedRect(ImVec2(pa.x, pb.y - 14.0f * u), pb, ImColor(0, 0, 0, 120), 6.0f * u, true);
		{
			const float fs = 10.6f * u;
			const std::string nm = Fit(r, en.name, fs, pw - 10.0f * u);
			const ImVec2 ts = r->calculateTextSize(nm, fs);
			r->drawText(ImVec2(cx - ts.x * 0.5f + 0.8f * u, pa.y + 3.0f * u + 0.8f * u), ImColor(0, 0, 0, 170), nm, true, fs);
			r->drawText(ImVec2(cx - ts.x * 0.5f, pa.y + 3.0f * u), ImColor(255, 255, 255, 255), nm, true, fs);
			const float vs = 9.6f * u;
			const std::string vv = Fit(r, en.value, vs, pw - 8.0f * u);
			const ImVec2 vz = r->calculateTextSize(vv, vs);
			r->drawText(ImVec2(cx - vz.x * 0.5f, pb.y - 13.0f * u + (13.0f * u - vz.y) * 0.5f), m.text, vv, true, vs);
		}

		// foto + moldura do avatar + louros + coroa
		const float R = s.photoR * u, pcy = pa.y - 3.0f * u - R;
		Laurel(r, cx, pcy, R + 6.5f * u, -1, m.mid, u);
		Laurel(r, cx, pcy, R + 6.5f * u, +1, m.mid, u);
		Avatar::DrawPhotoOf(r, en.acc, cx, pcy, R);
		Avatar::DrawBorder(r, en.border, cx, pcy, R, u * 0.75f, t);
		Crown(r, cx, pcy - R - 2.0f * u, R * 1.15f, R * 0.78f, m);
	}
}
