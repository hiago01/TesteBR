#pragma once
// Maximo OS - apps embutidos da ETAPA 1: TestApp (valida ciclo de vida, componentes, rolagem e navegacao interna)
// e PlaceholderApp (tela "Em breve" dos apps previstos). NENHUM deles le/escreve dados do servidor.
#include "AppModel.h"

namespace MaximoOS
{
	class PlaceholderApp : public App
	{
	public:
		PlaceholderApp(std::string title, Icon icon, Color accent) : m_title(std::move(title)), m_icon(icon), m_accent(accent) {}
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, Colors::Bg, 0);
			HeaderResult h = DrawScreenHeader(cv, m, content, m_title, true, m_backPressed);
			m_back = h.backBtn;
			const Rect card = { h.body.center().x - m.px(250), h.body.center().y - m.px(130), m.px(500), m.px(260) };
			DrawPanel(cv, m, card);
			const float is = m.px(72);
			Icons::Draw(cv, m_icon, { card.center().x - is * 0.5f, card.y + m.px(26), is, is }, m_accent);
			DrawTextIn(cv, { card.x, card.y + m.px(112), card.w, m.px(34) }, "Módulo não implementado", Colors::Text, m.font(Fonts::Header), Align::Center);
			DrawTextIn(cv, { card.x + m.px(20), card.y + m.px(150), card.w - m.px(40), m.px(26) }, "Apenas a estrutura visual existe nesta versão.", Colors::TextDim, m.font(Fonts::Caption), Align::Center);
			DrawTextIn(cv, { card.x + m.px(20), card.y + m.px(180), card.w - m.px(40), m.px(26) }, "Nenhum dado do servidor é exibido.", Colors::TextDim, m.font(Fonts::Caption), Align::Center);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (m_backTap.handle(t, m_back)) finish();
			m_backPressed = m_backTap.pressed;
			return true;
		}
		bool onBack() override { return false; }            // placeholder nao tem navegacao interna: o sistema volta para a tela inicial
	private:
		std::string m_title; Icon m_icon; Color m_accent; Rect m_back; Pressable m_backTap; bool m_backPressed = false;
	};

	// App de teste: tela 1 (contador, chave, botao que abre a tela 2) e tela 2 (lista rolavel de 40 linhas).
	class TestApp : public App
	{
	public:
		int created = 0, resumed = 0, paused = 0, destroyed = 0;          // observados pelos testes
		int counter = 0; bool toggle = false; int screen = 0;

		void onCreate() override { ++created; }
		void onResume() override { ++resumed; }
		void onPause() override { ++paused; m_scroll = ScrollView(); m_p1.pressed = m_p2.pressed = m_p3.pressed = false; }
		void onDestroy() override { ++destroyed; }
		void update(float dtMs) override { if (screen == 1) m_scroll.update(dtMs); }
		bool onBack() override { if (screen == 1) { screen = 0; return true; } return false; }

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, Colors::Bg, 0);
			HeaderResult h = DrawScreenHeader(cv, m, content, screen == 0 ? "App de teste" : "Lista rolável", screen == 1, m_back_p.pressed);
			m_backBtn = h.backBtn;
			if (screen == 0)
			{
				const Rect p{ h.body.x + m.px(40), h.body.y + m.px(30), h.body.w - m.px(80), m.px(130) };
				DrawPanel(cv, m, p);
				DrawTextIn(cv, { p.x + m.px(24), p.y + m.px(12), p.w - m.px(48), m.px(36) }, "Contador: " + std::to_string(counter), Colors::Text, m.font(Fonts::Header));
				m_btnInc = { p.x + m.px(24), p.y + m.px(62), m.px(200), m.px(52) };
				DrawButton(cv, m, m_btnInc, "Somar +1", true, m_p1.pressed);
				m_toggleR = ToggleRect(m, p.right() - m.px(24), p.y + m.px(88));
				DrawToggle(cv, m, m_toggleR, toggle);
				DrawTextIn(cv, { m_toggleR.x - m.px(240), m_toggleR.y, m.px(228), m_toggleR.h }, "Chave de teste", Colors::TextDim, m.font(Fonts::Body), Align::Right);
				m_btnList = { p.x, p.bottom() + m.px(24), p.w, m.px(56) };
				DrawButton(cv, m, m_btnList, "Abrir lista rolável", false, m_p2.pressed, true, Icon::Document);
				DrawTextIn(cv, { p.x, m_btnList.bottom() + m.px(18), p.w, m.px(24) }, "Ciclo de vida: criado " + std::to_string(created) + "  retomado " + std::to_string(resumed) + "  pausado " + std::to_string(paused),
					Colors::TextDim, m.font(Fonts::Caption));
			}
			else
			{
				const float rowH = m.px(60), n = 40;
				m_scroll.setLayout(h.body, rowH * n);
				cv.pushClip(h.body);
				for (int i = 0; i < (int)n; ++i)
				{
					const Rect r = m_scroll.itemRect(i * rowH, rowH);
					if (!m_scroll.visible(r)) continue;
					cv.fillRect(r.inset(m.px(16), m.px(3)), (i & 1) ? Colors::Panel : Colors::PanelHi, m.px(10));
					DrawTextIn(cv, { r.x + m.px(36), r.y, r.w - m.px(72), r.h }, "Linha de teste " + std::to_string(i + 1), Colors::Text, m.font(Fonts::Body));
				}
				cv.popClip();
			}
		}

		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (screen == 0)
			{
				if (m_p1.handle(t, m_btnInc)) ++counter;
				if (m_p3.handle(t, m_toggleR)) toggle = !toggle;
				if (m_p2.handle(t, m_btnList)) { screen = 1; m_scroll = ScrollView(); }
			}
			else
			{
				if (m_back_p.handle(t, m_backBtn)) screen = 0;
				m_scroll.onTouch(t);
			}
			return true;
		}
	private:
		Rect m_backBtn, m_btnInc, m_btnList, m_toggleR;
		Pressable m_p1, m_p2, m_p3, m_back_p; ScrollView m_scroll;
	};
}
