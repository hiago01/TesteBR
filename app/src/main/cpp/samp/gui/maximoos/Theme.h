#pragma once
// Maximo OS - tema visual CENTRALIZADO (cores, tamanhos de fonte logicos e icones vetoriais desenhados por primitivas).
// Nenhum app define cor/fonte/icone proprio: tudo vem daqui, para manter a identidade unica do Maximo RP.
#include "Canvas.h"

namespace MaximoOS
{
	namespace Colors
	{
		const Color Bezel      (22, 24, 30);
		const Color BezelEdge  (92, 98, 112);
		const Color BezelGloss (255, 255, 255, 28);
		const Color Bg         (13, 16, 24);          // fundo dos apps (grafite)
		const Color Panel      (24, 29, 41);          // paineis
		const Color PanelHi    (33, 40, 56);
		const Color Line       (46, 54, 72);
		const Color Text       (236, 240, 248);
		const Color TextDim    (150, 160, 180);
		const Color Accent     (30, 144, 255);        // azul eletrico: acoes e elementos ativos
		const Color AccentDark (11, 91, 255);
		const Color Navy       (10, 30, 74);
		const Color Danger     (232, 84, 84);
		const Color Warn       (255, 181, 71);
		const Color Ok         (68, 205, 130);
		const Color Scrim      (0, 0, 0, 150);
	}

	namespace Fonts   // tamanhos LOGICOS (lu); Metrics::font() converte para pixels respeitando o minimo legivel
	{
		constexpr float Title = 26.0f, Header = 20.0f, Body = 16.0f, Caption = 13.0f, Tiny = 11.0f;
	}

	enum class Icon
	{
		None, Home, Back, Recents, Gear, Shield, People, Car, Alert, Document, Building, Message, Folder, Calculator, Wrench,
		Bell, Signal, Battery, Close, Chevron
	};

	namespace Icons
	{
		// Desenha o icone centralizado em 'r' (usa o menor lado), cor c. Traco fino = tamanho/14.
		inline void Draw(Canvas& cv, Icon ic, const Rect& r, Color c)
		{
			const Vec2 m = r.center();
			const float S = std::min(r.w, r.h), h = S * 0.5f, t = std::max(1.2f, S / 13.0f);
			auto P = [&](float fx, float fy) { return Vec2{ m.x + fx * h, m.y + fy * h }; };      // fx,fy em -1..1
			switch (ic)
			{
			case Icon::Home:
				cv.fillTriangle(P(-0.9f, -0.05f), P(0.9f, -0.05f), P(0, -0.85f), c);
				cv.fillRect({ m.x - 0.62f * h, m.y - 0.05f * h, 1.24f * h, 0.85f * h }, c, h * 0.08f);
				cv.fillRect({ m.x - 0.17f * h, m.y + 0.28f * h, 0.34f * h, 0.52f * h }, Colors::Bg, 0);
				break;
			case Icon::Back:
				cv.fillTriangle(P(0.55f, -0.75f), P(0.55f, 0.75f), P(-0.7f, 0), c);
				break;
			case Icon::Recents:
				cv.strokeRect({ m.x - 0.62f * h, m.y - 0.62f * h, 1.24f * h, 1.24f * h }, c, h * 0.18f, t * 1.4f);
				break;
			case Icon::Gear:
				for (int i = 0; i < 8; i++)
				{
					const float a = i * 0.7853982f;
					cv.line(P(std::cos(a) * 0.62f, std::sin(a) * 0.62f), P(std::cos(a) * 0.92f, std::sin(a) * 0.92f), c, t * 2.2f);
				}
				cv.strokeCircle(m, h * 0.55f, c, t * 2.0f);
				cv.fillCircle(m, h * 0.2f, c);
				break;
			case Icon::Shield:
				cv.fillRect({ m.x - 0.7f * h, m.y - 0.85f * h, 1.4f * h, 1.1f * h }, c, h * 0.12f);
				cv.fillTriangle(P(-0.7f, 0.2f), P(0.7f, 0.2f), P(0, 0.95f), c);
				cv.fillCircle(P(0, -0.12f), h * 0.2f, Colors::Navy);
				break;
			case Icon::People:
				cv.fillCircle(P(-0.35f, -0.35f), h * 0.3f, c);
				cv.fillRect({ m.x - 0.85f * h, m.y + 0.05f * h, 1.0f * h, 0.7f * h }, c, h * 0.3f);
				cv.fillCircle(P(0.42f, -0.2f), h * 0.25f, c.alpha(0.75f));
				cv.fillRect({ m.x + 0.05f * h, m.y + 0.2f * h, 0.85f * h, 0.55f * h }, c.alpha(0.75f), h * 0.25f);
				break;
			case Icon::Car:
				cv.fillRect({ m.x - 0.95f * h, m.y - 0.05f * h, 1.9f * h, 0.6f * h }, c, h * 0.2f);
				cv.fillRect({ m.x - 0.55f * h, m.y - 0.6f * h, 1.1f * h, 0.6f * h }, c, h * 0.22f);
				cv.fillCircle(P(-0.5f, 0.6f), h * 0.22f, Colors::Bg); cv.fillCircle(P(0.5f, 0.6f), h * 0.22f, Colors::Bg);
				cv.strokeCircle(P(-0.5f, 0.6f), h * 0.22f, c, t * 1.5f); cv.strokeCircle(P(0.5f, 0.6f), h * 0.22f, c, t * 1.5f);
				break;
			case Icon::Alert:
				cv.fillTriangle(P(0, -0.9f), P(0.95f, 0.8f), P(-0.95f, 0.8f), c);
				cv.fillRect({ m.x - 0.07f * h, m.y - 0.35f * h, 0.14f * h, 0.62f * h }, Colors::Navy, 0);
				cv.fillCircle(P(0, 0.55f), h * 0.09f, Colors::Navy);
				break;
			case Icon::Document:
				cv.fillRect({ m.x - 0.62f * h, m.y - 0.85f * h, 1.24f * h, 1.7f * h }, c, h * 0.12f);
				for (int i = 0; i < 3; i++) cv.fillRect({ m.x - 0.38f * h, m.y - 0.45f * h + i * 0.42f * h, 0.76f * h, 0.12f * h }, Colors::Navy, 0);
				break;
			case Icon::Building:
				cv.fillRect({ m.x - 0.7f * h, m.y - 0.85f * h, 1.4f * h, 1.7f * h }, c, h * 0.08f);
				for (int i = 0; i < 6; i++) cv.fillRect({ m.x - 0.45f * h + (i % 2) * 0.55f * h, m.y - 0.6f * h + (i / 2) * 0.45f * h, 0.3f * h, 0.26f * h }, Colors::Navy, 0);
				break;
			case Icon::Message:
				cv.fillRect({ m.x - 0.9f * h, m.y - 0.7f * h, 1.8f * h, 1.25f * h }, c, h * 0.25f);
				cv.fillTriangle(P(-0.5f, 0.5f), P(-0.1f, 0.5f), P(-0.55f, 0.95f), c);
				break;
			case Icon::Folder:
				cv.fillRect({ m.x - 0.9f * h, m.y - 0.65f * h, 0.9f * h, 0.35f * h }, c, h * 0.1f);
				cv.fillRect({ m.x - 0.9f * h, m.y - 0.45f * h, 1.8f * h, 1.2f * h }, c, h * 0.12f);
				break;
			case Icon::Calculator:
				cv.fillRect({ m.x - 0.65f * h, m.y - 0.9f * h, 1.3f * h, 1.8f * h }, c, h * 0.14f);
				cv.fillRect({ m.x - 0.48f * h, m.y - 0.72f * h, 0.96f * h, 0.42f * h }, Colors::Navy, h * 0.06f);
				for (int i = 0; i < 6; i++) cv.fillCircle(P(-0.3f + (i % 3) * 0.3f, -0.05f + (i / 3) * 0.42f), h * 0.1f, Colors::Navy);
				break;
			case Icon::Wrench:
				cv.line(P(-0.65f, 0.65f), P(0.35f, -0.35f), c, t * 3.0f);
				cv.fillCircle(P(0.5f, -0.5f), h * 0.4f, c);
				cv.fillTriangle(P(0.5f, -0.5f), P(0.95f, -0.15f), P(0.9f, -0.75f), Colors::Navy);
				break;
			case Icon::Bell:
				cv.fillRect({ m.x - 0.5f * h, m.y - 0.65f * h, 1.0f * h, 1.0f * h }, c, h * 0.45f);
				cv.fillRect({ m.x - 0.75f * h, m.y + 0.2f * h, 1.5f * h, 0.22f * h }, c, h * 0.1f);
				cv.fillCircle(P(0, 0.72f), h * 0.16f, c);
				break;
			case Icon::Signal:
				for (int i = 0; i < 4; i++) cv.fillRect({ m.x - 0.85f * h + i * 0.45f * h, m.y + 0.8f * h - (0.4f + 0.4f * i) * h, 0.32f * h, (0.4f + 0.4f * i) * h }, c, 1);
				break;
			case Icon::Battery:
				cv.strokeRect({ m.x - 0.95f * h, m.y - 0.5f * h, 1.75f * h, 1.0f * h }, c, h * 0.15f, t * 1.3f);
				cv.fillRect({ m.x + 0.84f * h, m.y - 0.2f * h, 0.14f * h, 0.4f * h }, c, 0);
				break;
			case Icon::Close:
				cv.line(P(-0.6f, -0.6f), P(0.6f, 0.6f), c, t * 2.0f); cv.line(P(0.6f, -0.6f), P(-0.6f, 0.6f), c, t * 2.0f);
				break;
			case Icon::Chevron:
				cv.line(P(-0.3f, -0.65f), P(0.35f, 0), c, t * 2.0f); cv.line(P(0.35f, 0), P(-0.3f, 0.65f), c, t * 2.0f);
				break;
			default: break;
			}
		}
	}
}
