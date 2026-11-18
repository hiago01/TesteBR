#pragma once
// Celular - UM so lugar que desenha o icone de um app (tela inicial, dock, Loja, apps abertos, animacao de abrir, aviso/banner).
// Usa a imagem do atlas embutido (PhoneContext::appIcon); sem atlas cai no quadrado colorido com glifo. O Calendario e "vivo":
// dia da semana e dia do mes da data real do aparelho sao desenhados por cima do fundo branco.
#include "PhoneApp.h"
#include "PhoneUI.h"

namespace Phone
{
	// q = quadrado do icone; col = cor de fallback do app; shadow = sombra suave (icones sobre fundo)
	inline void DrawAppIcon(Canvas& cv, PhoneContext& ctx, const std::string& id, const Rect& q, const Color& col, bool shadow = true)
	{
		const float rad = q.w * 0.2237f;
		if (shadow) cv.fillRect(q.moved(0, q.h * 0.03f), Color(0, 0, 0, 60), rad);
		const bool isCal = id == "calendario";
		uint64_t itex = 0; Vec2 uv0{ 0, 0 }, uv1{ 1, 1 };
		if (ctx.appIcon && ctx.appIcon(id, itex, uv0, uv1) && itex)
			cv.image(q, itex, uv0, uv1);                    // icone como imagem (atlas embutido)
		else if (isCal)
			cv.fillRect(q, Color(244, 244, 244), rad);      // sem atlas: cartao branco liso (a data e desenhada abaixo)
		else
		{
			cv.fillRect(q, Color::mix(col, Color(0, 0, 0), 0.30f), rad);
			cv.fillRect({ q.x, q.y, q.w, q.h * 0.62f }, col, rad);
			cv.fillRect({ q.x, q.y + q.h * 0.30f, q.w, q.h * 0.32f }, col, 0);
			DrawGlyph(cv, id, q.inset(q.w * 0.22f), Color(255, 255, 255));
		}
		if (id == "relogio")
		{
			// mostrador sem ponteiros (atlas) + ponteiros da hora REAL do aparelho
			const StatusInfo si = ctx.status ? ctx.status() : StatusInfo();
			if (si.hour >= 0)
			{
				const Vec2 c = q.center(); const float W = q.w;
				auto Hand = [&](float deg, float len, float tail, float th, Color col)
				{
					const float t = deg * 0.0174533f, sx = std::sin(t), sy = -std::cos(t);
					const Vec2 a{ c.x - sx * tail, c.y - sy * tail }, b{ c.x + sx * len, c.y + sy * len };
					cv.line(a, b, col, th);
					cv.fillCircle(a, th * 0.5f, col); cv.fillCircle(b, th * 0.5f, col);   // pontas arredondadas
				};
				const float hDeg = ((si.hour % 12) + si.minute / 60.0f + si.second / 3600.0f) * 30.0f;
				const float mDeg = (si.minute + si.second / 60.0f) * 6.0f;
				const float sDeg = si.second * 6.0f;
				const Color ink(28, 28, 30), orange(255, 149, 0);
				Hand(hDeg, W * 0.27f, W * 0.04f, W * 0.048f, ink);
				Hand(mDeg, W * 0.40f, W * 0.04f, W * 0.040f, ink);
				cv.fillCircle(c, W * 0.036f, ink);
				Hand(sDeg, W * 0.42f, W * 0.11f, std::max(1.0f, W * 0.014f), orange);
				cv.fillCircle(c, W * 0.024f, orange);
				cv.fillCircle(c, W * 0.009f, Color(250, 250, 250));
			}
		}
		if (isCal)
		{
			const StatusInfo si = ctx.status ? ctx.status() : StatusInfo();
			static const char* kWd[7] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb" };
			if (si.weekday >= 0 && si.weekday < 7 && si.day >= 1 && si.day <= 31)
			{
				DrawTextAs(cv, FontTextSemibold, { q.x, q.y + q.h * 0.105f, q.w, q.h * 0.21f }, std::string(kWd[si.weekday]), Color(255, 55, 60), q.h * 0.19f, Align::Center);
				DrawTextAs(cv, FontText, { q.x, q.y + q.h * 0.34f, q.w, q.h * 0.54f }, std::to_string(si.day), Color(38, 38, 40), q.h * 0.58f, Align::Center);
			}
		}
	}
}
