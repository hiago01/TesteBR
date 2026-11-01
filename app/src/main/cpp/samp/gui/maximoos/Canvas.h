#pragma once
// Maximo OS - Canvas: UNICA superficie de desenho que os apps/componentes conhecem.
// No jogo e um adaptador fino sobre o ImGuiRenderer ja existente (gui/samp_widgets/maximoos.cpp); nos testes de PC e
// um gravador (testes/maximoos/MockCanvas.h). Nao e uma segunda engine grafica.
#include "Types.h"
#include <string>

namespace MaximoOS
{
	class Canvas
	{
	public:
		virtual ~Canvas() {}
		virtual void fillRect(const Rect& r, Color c, float radius = 0) = 0;
		virtual void strokeRect(const Rect& r, Color c, float radius, float thickness) = 0;
		virtual void fillGradientV(const Rect& r, Color top, Color bottom) = 0;      // sem cantos arredondados
		virtual void fillCircle(Vec2 center, float radius, Color c) = 0;
		virtual void strokeCircle(Vec2 center, float radius, Color c, float thickness) = 0;
		virtual void line(Vec2 a, Vec2 b, Color c, float thickness) = 0;
		virtual void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) = 0;
		virtual void text(Vec2 pos, Color c, const std::string& s, float size) = 0;  // pos = canto superior esquerdo
		virtual Vec2 measure(const std::string& s, float size) = 0;
		virtual void pushClip(const Rect& r) = 0;                                    // intersecta com o clip atual
		virtual void popClip() = 0;
		virtual void setAlpha(float a) = 0;                                          // multiplicador global (transicoes)
	};

	enum class Align { Left, Center, Right };

	// Corta o texto em UTF-8 com "..." para caber em maxW (ASCII "..." porque a fonte do jogo e Latin-1).
	inline std::string FitText(Canvas& cv, const std::string& s, float size, float maxW)
	{
		if (maxW <= 0) return std::string();
		if (cv.measure(s, size).x <= maxW) return s;
		const std::string dots = "...";
		std::string out = s;
		while (!out.empty())
		{
			size_t n = out.size() - 1;
			while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;   // volta ao inicio do caractere UTF-8
			out.resize(n);
			if (cv.measure(out + dots, size).x <= maxW) return out + dots;
		}
		return cv.measure(dots, size).x <= maxW ? dots : std::string();
	}

	// Desenha uma linha de texto dentro de 'box', alinhada e centralizada na vertical, sem passar das bordas.
	inline void DrawTextIn(Canvas& cv, const Rect& box, const std::string& s, Color c, float size, Align al = Align::Left)
	{
		std::string t = FitText(cv, s, size, box.w);
		if (t.empty()) return;
		Vec2 m = cv.measure(t, size);
		float x = box.x;
		if (al == Align::Center) x = box.x + (box.w - m.x) * 0.5f;
		else if (al == Align::Right) x = box.right() - m.x;
		cv.text({ x, box.y + (box.h - m.y) * 0.5f }, c, t, size);
	}
}
