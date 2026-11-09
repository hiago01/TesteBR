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
			DrawTextIn(cv, { r.back.x + m.px(28), bar.y, r.back.w - m.px(28), hh }, backLabel, backPressed ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(17));
		}
		DrawTextIn(cv, { bar.x + m.px(100), bar.y, bar.w - m.px(200), hh }, title, fg, m.font(17), Align::Center);
		if (!actionLabel.empty())
		{
			r.action = { bar.right() - m.px(100), bar.y, m.px(92), hh };
			DrawTextIn(cv, r.action, actionLabel, actionPressed ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(17), Align::Right);
		}
		r.body = { content.x, bar.bottom(), content.w, content.h - hh };
		return r;
	}

	// ---- linha de lista agrupada (cartao branco/escuro com separador) ----
	struct RowStyle { bool light = true; bool first = false, last = false; bool chevron = true; bool pressed = false; };
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
		const float vw = value.empty() ? 0 : m.px(110);
		DrawTextIn(cv, { tx, r.y, r.right() - tx - m.px(st.chevron ? 34 : 16) - vw, r.h }, label, fg, m.font(16));
		if (!value.empty()) DrawTextIn(cv, { r.right() - m.px(st.chevron ? 34 : 16) - vw, r.y, vw, r.h }, value, PC::Gray, m.font(16), Align::Right);
		if (st.chevron)
		{
			const float c = m.px(6), cx = r.right() - m.px(18), cy = r.y + r.h * 0.5f;
			cv.line({ cx - c * 0.5f, cy - c }, { cx + c * 0.5f, cy }, PC::Gray.alpha(0.7f), std::max(1.5f, m.px(1.6f)));
			cv.line({ cx + c * 0.5f, cy }, { cx - c * 0.5f, cy + c }, PC::Gray.alpha(0.7f), std::max(1.5f, m.px(1.6f)));
		}
		if (!st.last) cv.fillRect({ tx, r.bottom() - std::max(1.0f, m.px(0.6f)), r.right() - tx, std::max(1.0f, m.px(0.6f)) }, st.light ? PC::Sep : Color(255, 255, 255, 30), 0);
	}
}
