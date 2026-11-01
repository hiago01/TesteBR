#pragma once
// Canvas de teste: grava as operacoes (para checar limites/clip) e mede texto com largura fixa (0.55*size).
#include "../../app/src/main/cpp/samp/gui/maximoos/Canvas.h"
#include <cstdio>
#include <vector>
using namespace MaximoOS;
struct MockCanvas : Canvas
{
	struct Op { char k; Rect r; Color c; float a = 0, b = 0; std::string s; Vec2 p2, p3; Rect clip; float alpha = 1; };
	std::vector<Op> ops; std::vector<Rect> clips; float alpha = 1; int minClipDepth = 0, maxClipDepth = 0;
	Rect curClip() const { return clips.empty() ? Rect{ -1e9f, -1e9f, 2e9f, 2e9f } : clips.back(); }
	void add(Op o) { o.clip = curClip(); o.alpha = alpha; ops.push_back(o); }
	void fillRect(const Rect& r, Color c, float rad) override { add({ 'R', r, c, rad }); }
	void strokeRect(const Rect& r, Color c, float rad, float th) override { add({ 'S', r, c, rad, th }); }
	void fillGradientV(const Rect& r, Color t, Color b) override { add({ 'G', r, t, 0, 0, "", {}, {} }); (void)b; }
	void fillCircle(Vec2 c, float rad, Color col) override { add({ 'C', { c.x - rad, c.y - rad, 2 * rad, 2 * rad }, col, rad }); }
	void strokeCircle(Vec2 c, float rad, Color col, float th) override { add({ 'O', { c.x - rad, c.y - rad, 2 * rad, 2 * rad }, col, rad, th }); }
	void line(Vec2 a, Vec2 b, Color c, float th) override { add({ 'L', { a.x, a.y, b.x - a.x, b.y - a.y }, c, th }); }
	void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) override { add({ 'T', { a.x, a.y, 0, 0 }, col, 0, 0, "", b, c }); }
	void text(Vec2 p, Color c, const std::string& s, float size) override { Vec2 m = measure(s, size); add({ 'X', { p.x, p.y, m.x, m.y }, c, size, 0, s }); }
	Vec2 measure(const std::string& s, float size) override { int n = 0; for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n; return { n * size * 0.55f, size }; }
	void pushClip(const Rect& r) override
	{
		Rect c = curClip(); float x0 = std::max(c.x, r.x), y0 = std::max(c.y, r.y), x1 = std::min(c.right(), r.right()), y1 = std::min(c.bottom(), r.bottom());
		clips.push_back({ x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0) }); maxClipDepth = std::max(maxClipDepth, (int)clips.size());
	}
	void popClip() override { if (!clips.empty()) clips.pop_back(); else minClipDepth = -1; }
	void setAlpha(float a) override { alpha = a; }
	// grava como texto para o renderizador Python
	void dump(const char* path) const
	{
		FILE* f = fopen(path, "w"); if (!f) return;
		for (auto& o : ops)
		{
			fprintf(f, "%c %.2f %.2f %.2f %.2f %d %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f|%s\n", o.k, o.r.x, o.r.y, o.r.w, o.r.h, o.c.r, o.c.g, o.c.b, (int)(o.c.a * o.alpha),
				o.a, o.b, o.p2.x, o.p2.y, o.p3.x, o.p3.y, o.clip.x, o.clip.y, o.clip.w, o.clip.h, o.s.c_str());
		}
		fclose(f);
	}
};
