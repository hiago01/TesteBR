#pragma once
// Celular - componentes visuais COMPARTILHADOS no estilo iOS (paleta, titulo grande, listas agrupadas, glifos dos apps).
// Header-only, so usa Canvas/Metrics do Maximo OS. Nenhum app desenha botao/lista propria: tudo vem daqui.
#include "PhoneMetrics.h"
#include "../maximoos/Components.h"
#include "../maximoos/StatusBar.h"
#include <cstring>

namespace Phone
{
	using namespace MaximoOS;

	namespace PC   // paleta do celular
	{
		const Color Light   (242, 242, 247);      // fundo claro (Ajustes, Contatos...)
		const Color White   (255, 255, 255);
		const Color Sep     (60, 60, 67, 45);
		const Color LabelL  (0, 0, 0);            // texto sobre fundo claro
		const Color DimL    (110, 110, 118);
		const Color Dark    (0, 0, 0);
		const Color Dark2   (28, 28, 30);
		const Color Blue    (10, 132, 255);
		const Color Green   (52, 199, 89);
		const Color Red     (255, 59, 48);
		const Color Orange  (255, 149, 0);
		const Color Purple  (175, 82, 222);
		const Color Gray    (142, 142, 147);
		const Color Wall0   (14, 60, 170);        // papel de parede: azul no topo...
		const Color Wall1   (8, 10, 40);          // ...noite no meio...
		const Color Wall2   (150, 130, 190);      // ...lavanda embaixo
	}

	// ---- glifos dos apps (primitivas do Canvas; nada de emoji/imagem externa) ----
	inline void DrawGlyph(Canvas& cv, const std::string& id, const Rect& r, Color c)
	{
		const Vec2 m = r.center();
		const float S = std::min(r.w, r.h), h = S * 0.5f, t = std::max(1.4f, S / 11.0f);
		auto P = [&](float fx, float fy) { return Vec2{ m.x + fx * h, m.y + fy * h }; };
		if (id == "telefone")
		{
			cv.line(P(-0.55f, 0.55f), P(0.55f, -0.55f), c, t * 2.6f);
			cv.fillCircle(P(-0.62f, 0.62f), h * 0.2f, c); cv.fillCircle(P(0.62f, -0.62f), h * 0.2f, c);
		}
		else if (id == "mensagens" || id == "papo") Icons::Draw(cv, Icon::Message, r, c);
		else if (id == "camera") Icons::Draw(cv, Icon::Camera, r, c);
		else if (id == "galeria") Icons::Draw(cv, Icon::Gallery, r, c);
		else if (id == "contatos") Icons::Draw(cv, Icon::People, r, c);
		else if (id == "ajustes") Icons::Draw(cv, Icon::Gear, r, c);
		else if (id == "notas") Icons::Draw(cv, Icon::Notes, r, c);
		else if (id == "calendario") Icons::Draw(cv, Icon::Calendar, r, c);
		else if (id == "relogio") Icons::Draw(cv, Icon::Clock, r, c);
		else if (id == "calculadora") Icons::Draw(cv, Icon::Calculator, r, c);
		else if (id == "mapas") Icons::Draw(cv, Icon::Map, r, c);
		else if (id == "noticias") Icons::Draw(cv, Icon::News, r, c);
		else if (id == "radio") Icons::Draw(cv, Icon::Radio, r, c);
		else if (id == "tune") Icons::Draw(cv, Icon::Music, r, c);
		else if (id == "corre") Icons::Draw(cv, Icon::Car, r, c);
		else if (id == "banco") Icons::Draw(cv, Icon::Coin, r, c);
		else if (id == "gravador") Icons::Draw(cv, Icon::Mic, r, c);
		else if (id == "clipe" || id == "live") Icons::Draw(cv, Icon::Play, r, c);
		else if (id == "gram") { cv.strokeRect({ m.x - 0.7f * h, m.y - 0.7f * h, 1.4f * h, 1.4f * h }, c, h * 0.4f, t * 1.3f); cv.strokeCircle(m, h * 0.32f, c, t * 1.3f); cv.fillCircle(P(0.42f, -0.42f), h * 0.07f, c); }
		else if (id == "loja") { cv.line(P(-0.6f, 0.65f), P(0, -0.7f), c, t * 1.6f); cv.line(P(0.6f, 0.65f), P(0, -0.7f), c, t * 1.6f); cv.line(P(-0.4f, 0.2f), P(0.4f, 0.2f), c, t * 1.4f); }
		else if (id == "qrcode")
		{
			const float q = h * 0.5f;
			for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
				if ((i != 1 || j != 1)) cv.fillRect({ m.x + (i - 1.5f) * q * 1.1f, m.y + (j - 1.5f) * q * 1.1f, q, q }, c, q * 0.15f);
		}
		else if (id == "pede") { cv.strokeCircle(m, h * 0.62f, c, t * 1.4f); cv.line(P(-0.25f, -0.2f), P(-0.25f, 0.3f), c, t); cv.line(P(0.25f, -0.2f), P(0.25f, 0.3f), c, t); cv.line(P(0, -0.25f), P(0, 0.3f), c, t); }
		else if (id == "sinuca") { cv.fillCircle(m, h * 0.62f, c); cv.fillCircle(m, h * 0.3f, Color(0, 0, 0, 120)); }
		else Icons::Draw(cv, Icon::Apps, r, c);
	}

	// ---- cabecalho iOS: botao voltar azul + titulo centralizado (conteudo claro) ----
	struct NavResult { Rect back; Rect action; Rect body; };
	inline NavResult DrawNavBar(Canvas& cv, const Metrics& m, const Rect& content, const std::string& title, const std::string& backLabel,
	                            const std::string& actionLabel, bool light, bool backPressed, bool actionPressed)
	{
		const float hh = m.px(44);
		const Color fg = light ? PC::LabelL : Colors::Text;
		const Rect bar{ content.x, content.y, content.w, hh };
		NavResult r;
		if (!backLabel.empty())
		{
			r.back = { bar.x + m.px(6), bar.y, m.px(110), hh };
			const float is = m.px(16);
			cv.fillTriangle({ r.back.x + m.px(10) + is * 0.55f, bar.y + hh * 0.5f - is * 0.55f }, { r.back.x + m.px(10) + is * 0.55f, bar.y + hh * 0.5f + is * 0.55f }, { r.back.x + m.px(10), bar.y + hh * 0.5f }, backPressed ? PC::Blue.alpha(0.5f) : PC::Blue);
			DrawTextAs(cv, FontText, { r.back.x + m.px(28), bar.y, r.back.w - m.px(28), hh }, backLabel, backPressed ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(17));
		}
		DrawTextAs(cv, FontDisplaySemibold, { bar.x + m.px(100), bar.y, bar.w - m.px(200), hh }, title, fg, m.font(17), Align::Center);
		if (!actionLabel.empty())
		{
			r.action = { bar.right() - m.px(100), bar.y, m.px(92), hh };
			DrawTextAs(cv, FontTextSemibold, r.action, actionLabel, actionPressed ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(17), Align::Right);
		}
		r.body = { content.x, bar.bottom(), content.w, content.h - hh };
		return r;
	}

	// ---- linha de lista agrupada (cartao branco/escuro com separador) ----
	struct RowStyle { bool light = true; bool first = false, last = false; bool chevron = true; bool pressed = false; float valueW = 0; };   // valueW: largura (px do aparelho) da coluna do valor; 0 = padrao
	inline void DrawRow(Canvas& cv, const Metrics& m, const Rect& r, const std::string& label, const std::string& value, const RowStyle& st,
	                    const Color* dot = nullptr)
	{
		const Color bg = st.light ? (st.pressed ? Color(220, 220, 226) : PC::White) : (st.pressed ? Color(58, 58, 60) : PC::Dark2);
		const float rad = m.px(10);
		cv.fillRect(r, bg, (st.first || st.last) ? rad : 0);
		if (!st.first && !st.last) cv.fillRect(r, bg, 0);
		else if (st.first && !st.last) cv.fillRect({ r.x, r.y + r.h * 0.5f, r.w, r.h * 0.5f }, bg, 0);
		else if (st.last && !st.first) cv.fillRect({ r.x, r.y, r.w, r.h * 0.5f }, bg, 0);
		float tx = r.x + m.px(16);
		if (dot) { const float d = m.px(30); cv.fillRect({ r.x + m.px(12), r.y + (r.h - d) * 0.5f, d, d }, *dot, m.px(7)); tx = r.x + m.px(54); }
		const Color fg = st.light ? PC::LabelL : Colors::Text;
		const float vw = value.empty() ? 0 : m.px(st.valueW > 0 ? st.valueW : 110);
		DrawTextAs(cv, FontTextSemibold, { tx, r.y, r.right() - tx - m.px(st.chevron ? 34 : 16) - vw, r.h }, label, fg, m.font(16));
		if (!value.empty()) DrawTextIn(cv, { r.right() - m.px(st.chevron ? 34 : 16) - vw, r.y, vw, r.h }, value, PC::Gray, m.font(16), Align::Right);
		if (st.chevron)
		{
			const float c = m.px(6), cx = r.right() - m.px(18), cy = r.y + r.h * 0.5f;
			cv.line({ cx - c * 0.5f, cy - c }, { cx + c * 0.5f, cy }, PC::Gray.alpha(0.7f), std::max(1.5f, m.px(1.6f)));
			cv.line({ cx + c * 0.5f, cy }, { cx - c * 0.5f, cy + c }, PC::Gray.alpha(0.7f), std::max(1.5f, m.px(1.6f)));
		}
		if (!st.last) cv.fillRect({ tx, r.bottom() - std::max(1.0f, m.px(0.6f)), r.right() - tx, std::max(1.0f, m.px(0.6f)) }, st.light ? PC::Sep : Color(255, 255, 255, 30), 0);
	}

	// ---- Ajustes no estilo iOS: lista de LARGURA TOTAL, icone pequeno colorido (29 lu) com glifo branco, texto regular 17 ----
	namespace PC
	{
		const Color IosSep   (198, 198, 200);     // separador fino
		const Color IosChev  (196, 196, 200);     // seta
		const Color IosValue (142, 142, 147);     // valor a direita
		const Color IosBlue  (0, 122, 255);
		const Color IosGreen (52, 199, 89);
		const Color IosOrange(255, 149, 0);
		const Color IosRed   (255, 59, 48);
		const Color IosGray  (142, 142, 147);
	}

	// arco em segmentos de reta (graus; 0 = direita, -90 = cima)
	inline void DrawArcLines(Canvas& cv, Vec2 c, float rad, float a0, float a1, Color col, float th)
	{
		const int n = 12;
		Vec2 prev{ c.x + std::cos(a0 * 0.0174533f) * rad, c.y + std::sin(a0 * 0.0174533f) * rad };
		for (int i = 1; i <= n; ++i)
		{
			const float a = (a0 + (a1 - a0) * i / n) * 0.0174533f;
			const Vec2 q{ c.x + std::cos(a) * rad, c.y + std::sin(a) * rad };
			cv.line(prev, q, col, th);
			prev = q;
		}
	}

	// glifo do icone de Ajustes (c = cor do glifo, normalmente branco; bg = cor do quadrado, usada nos "furos")
	inline void DrawSettingsGlyph(Canvas& cv, const std::string& key, const Rect& box, Color c, Color bg)
	{
		const Vec2 m = box.center();
		const float S = std::min(box.w, box.h), h = S * 0.5f, t = std::max(1.2f, S / 11.0f);
		auto P = [&](float fx, float fy) { return Vec2{ m.x + fx * h, m.y + fy * h }; };
		if (key == "aviao")
		{
			// aviao apontando para cima, girado 45 graus (nariz para a direita/cima)
			auto R = [&](float x, float y) { const float k = 0.70711f; return P(0.92f * (x * k - y * k), 0.92f * (x * k + y * k)); };
			cv.line(R(0, -0.95f), R(0, 0.85f), c, t * 1.8f);
			cv.fillCircle(R(0, -0.93f), t * 0.9f, c);
			cv.fillTriangle(R(0.06f, -0.22f), R(0.98f, 0.46f), R(0.98f, 0.7f), c); cv.fillTriangle(R(0.06f, -0.22f), R(0.98f, 0.7f), R(0.06f, 0.3f), c);
			cv.fillTriangle(R(-0.06f, -0.22f), R(-0.98f, 0.46f), R(-0.98f, 0.7f), c); cv.fillTriangle(R(-0.06f, -0.22f), R(-0.98f, 0.7f), R(-0.06f, 0.3f), c);
			cv.fillTriangle(R(0.05f, 0.55f), R(0.44f, 0.9f), R(0.44f, 1.02f), c); cv.fillTriangle(R(0.05f, 0.55f), R(0.44f, 1.02f), R(0.05f, 0.86f), c);
			cv.fillTriangle(R(-0.05f, 0.55f), R(-0.44f, 0.9f), R(-0.44f, 1.02f), c); cv.fillTriangle(R(-0.05f, 0.55f), R(-0.44f, 1.02f), R(-0.05f, 0.86f), c);
		}
		else if (key == "wifi")
		{
			const Vec2 o = P(0, 0.72f);
			cv.fillCircle(o, h * 0.15f, c);
			DrawArcLines(cv, o, h * 0.52f, -135.0f, -45.0f, c, t * 1.5f);
			DrawArcLines(cv, o, h * 0.98f, -135.0f, -45.0f, c, t * 1.5f);
			DrawArcLines(cv, o, h * 1.44f, -135.0f, -45.0f, c, t * 1.5f);
		}
		else if (key == "som")
		{
			cv.fillRect({ m.x - 0.88f * h, m.y - 0.3f * h, 0.5f * h, 0.6f * h }, c, h * 0.06f);
			cv.fillTriangle(P(-0.42f, -0.3f), P(0.1f, -0.78f), P(0.1f, 0.78f), c);
			cv.fillTriangle(P(-0.42f, -0.3f), P(-0.42f, 0.3f), P(0.1f, 0.78f), c);
			DrawArcLines(cv, P(0.1f, 0), h * 0.5f, -42.0f, 42.0f, c, t * 1.4f);
			DrawArcLines(cv, P(0.1f, 0), h * 0.88f, -42.0f, 42.0f, c, t * 1.4f);
		}
		else if (key == "notif")
		{
			cv.fillCircle(P(0, -0.12f), h * 0.58f, c);
			cv.fillTriangle(P(-0.58f, -0.12f), P(0.58f, -0.12f), P(0.82f, 0.5f), c); cv.fillTriangle(P(-0.58f, -0.12f), P(0.82f, 0.5f), P(-0.82f, 0.5f), c);
			cv.fillRect({ m.x - 0.9f * h, m.y + 0.42f * h, 1.8f * h, 0.2f * h }, c, h * 0.1f);
			cv.fillCircle(P(0, -0.74f), h * 0.12f, c);
			cv.fillTriangle(P(-0.2f, 0.7f), P(0.2f, 0.7f), P(0, 0.92f), c); cv.fillCircle(P(0, 0.78f), h * 0.2f, c);
		}
		else if (key == "numero" || key == "maos")
		{
			// fone: arco grosso (inferior esquerdo) com as duas pontas mais largas
			const Vec2 o = P(0.34f, -0.34f);
			DrawArcLines(cv, o, h * 0.98f, 98.0f, 172.0f, c, h * 0.5f);
			for (int i = 0; i < 2; ++i)
			{
				const float a = (i == 0 ? 96.0f : 174.0f) * 0.0174533f;
				cv.fillCircle(Vec2{ o.x + std::cos(a) * h * 0.86f, o.y + std::sin(a) * h * 0.86f }, h * 0.27f, c);
			}
		}
		else if (key == "tamanho") DrawTextAs(cv, FontDisplayBold, box, "AA", c, S * 0.78f, Align::Center);
		else if (key == "papel")
		{
			cv.fillRect({ m.x - 0.95f * h, m.y - 0.75f * h, 1.9f * h, 1.5f * h }, c, h * 0.16f);
			cv.fillTriangle(P(-0.82f, 0.6f), P(-0.28f, -0.12f), P(0.26f, 0.6f), bg);
			cv.fillTriangle(P(-0.06f, 0.6f), P(0.4f, 0.0f), P(0.82f, 0.6f), bg);
			cv.fillCircle(P(0.45f, -0.4f), h * 0.15f, bg);
		}
		else if (key == "sobre")
		{
			for (int i = 0; i < 8; ++i)
			{
				const float a = i * 0.7853982f;
				cv.line(P(std::cos(a) * 0.6f, std::sin(a) * 0.6f), P(std::cos(a) * 0.95f, std::sin(a) * 0.95f), c, h * 0.4f);
			}
			cv.fillCircle(m, h * 0.76f, c);
			cv.fillCircle(m, h * 0.32f, bg);
		}
		else if (key == "imei")
		{
			const float w[9] = { 0.14f, 0.07f, 0.2f, 0.07f, 0.12f, 0.2f, 0.07f, 0.14f, 0.09f };
			float x = -0.78f;
			for (int i = 0; i < 9; ++i) { if (i % 2 == 0) cv.fillRect({ m.x + x * h, m.y - 0.7f * h, w[i] * h, 1.4f * h }, c, 0); x += w[i] + 0.07f; }
		}
		else if (key == "chip")
		{
			cv.fillRect({ m.x - 0.62f * h, m.y - 0.86f * h, 1.24f * h, 1.72f * h }, c, h * 0.16f);
			cv.fillTriangle(P(0.1f, -0.9f), P(0.7f, -0.9f), P(0.7f, -0.3f), bg);              // canto cortado
			cv.fillRect({ m.x - 0.3f * h, m.y - 0.1f * h, 0.6f * h, 0.62f * h }, bg, h * 0.08f);
			cv.fillRect({ m.x - 0.3f * h, m.y + 0.16f * h, 0.6f * h, t * 0.6f }, c, 0);
		}
		else if (key == "hora")
		{
			cv.fillCircle(m, h * 0.92f, c);
			cv.line(m, P(0, -0.58f), bg, t * 1.2f); cv.line(m, P(0.42f, 0.22f), bg, t * 1.2f);
		}
		else DrawGlyph(cv, key, box, c);
	}

	struct IosRowStyle
	{
		bool first = false, last = false;    // primeira/ultima linha do grupo (linhas de borda de largura total)
		bool chevron = true, pressed = false, check = false;
		float valueW = 0;                    // largura (px do aparelho) da coluna do valor; 0 = padrao
		const char* icon = nullptr;          // chave de DrawSettingsGlyph; nullptr = sem icone (texto comeca na margem)
		Color iconBg = PC::IosBlue;
	};

	inline void DrawIosRow(Canvas& cv, const Metrics& m, const Rect& r, const std::string& label, const std::string& value, const IosRowStyle& st)
	{
		cv.fillRect(r, st.pressed ? Color(209, 209, 214) : PC::White, 0);
		const float hair = std::max(1.0f, m.px(0.6f));
		float tx = r.x + m.px(16);
		if (st.icon)
		{
			const float d = m.px(29);
			const Rect ib{ r.x + m.px(16), r.y + (r.h - d) * 0.5f, d, d };
			cv.fillRect(ib, st.iconBg, m.px(6.5f));
			DrawSettingsGlyph(cv, st.icon, ib.inset(d * 0.17f), Color(255, 255, 255), st.iconBg);
			tx = r.x + m.px(61);
		}
		const float rightPad = m.px(16);
		const float vw = value.empty() ? 0 : m.px(st.valueW > 0 ? st.valueW : 130);
		const float chevW = st.chevron ? m.px(18) : 0;
		DrawTextAs(cv, FontText, { tx, r.y, r.right() - tx - rightPad - chevW - vw - (vw > 0 ? m.px(6) : 0), r.h }, label, PC::LabelL, m.font(17));
		if (!value.empty()) DrawTextIn(cv, { r.right() - rightPad - chevW - vw, r.y, vw, r.h }, value, PC::IosValue, m.font(17), Align::Right);
		if (st.chevron)
		{
			const float c = m.px(5.5f), cx = r.right() - m.px(20), cy = r.y + r.h * 0.5f, th = std::max(1.6f, m.px(2.0f));
			cv.line({ cx - c * 0.5f, cy - c }, { cx + c * 0.5f, cy }, PC::IosChev, th);
			cv.line({ cx + c * 0.5f, cy }, { cx - c * 0.5f, cy + c }, PC::IosChev, th);
		}
		if (st.check)
		{
			const float cx = r.right() - m.px(30), cy = r.y + r.h * 0.5f, th = std::max(1.8f, m.px(2.2f));
			cv.line({ cx - m.px(7), cy + m.px(0.5f) }, { cx - m.px(2.5f), cy + m.px(5) }, PC::IosBlue, th);
			cv.line({ cx - m.px(2.5f), cy + m.px(5) }, { cx + m.px(7), cy - m.px(5) }, PC::IosBlue, th);
		}
		if (st.first) cv.fillRect({ r.x, r.y, r.w, hair }, PC::IosSep, 0);
		if (st.last) cv.fillRect({ r.x, r.bottom() - hair, r.w, hair }, PC::IosSep, 0);
		else cv.fillRect({ tx, r.bottom() - hair, r.right() - tx, hair }, PC::IosSep, 0);
	}
}
