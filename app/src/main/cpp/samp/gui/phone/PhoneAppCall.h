#pragma once
// Celular - tela de LIGACAO (app oculto "ligacao"): chamando, tocando, em ligacao e "encerrada".
// O estado vem de PhoneContext::call (escrito pelo PhoneNet); aqui so desenha e trata os botoes. Sem audio de voz nesta versao.
#include "PhoneForm.h"
#include "PhoneData.h"

namespace Phone
{
	class CallApp : public App
	{
	public:
		enum { ID_ANSWER = 1, ID_HANGUP = 2, ID_TALK = 3 };
		explicit CallApp(PhoneContext* c) : m_c(c) {}
		void onResume() override { m_press = 0; }
		void onPause() override { m_press = 0; m_c->call.held = false; }
		void update(float) override
		{
			if (m_c->call.state == 0 && m_c->call.endText.empty()) finish();
		}
		bool onBack() override { return m_c->call.state != 0; }      // em ligacao o Voltar nao sai: use Desligar (ou o gesto de Inicio)

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			const PhoneCall& c = m_c->call;
			cv.fillGradientV(content, Color(40, 44, 58), Color(12, 12, 16));
			m_hits.clear();
			const std::string nm = Data::NameFor(Data::LoadContacts(*m_c), c.number);
			const std::string shown = nm.empty() ? Data::FormatNumber(c.number) : nm;
			const float cx = content.center().x, top = content.y + m.px(96);
			cv.fillCircle({ cx, top + m.px(44) }, m.px(44), Color(255, 255, 255, 40));
			const std::string ini = nm.empty() ? std::string("#") : Text::FirstChar(nm);
			DrawTextIn(cv, { cx - m.px(44), top, m.px(88), m.px(88) }, ini, Color(255, 255, 255), m.font(36), Align::Center);
			DrawTextIn(cv, { content.x + m.px(16), top + m.px(104), content.w - m.px(32), m.px(34) }, shown, Color(255, 255, 255), m.font(26), Align::Center);
			if (!nm.empty()) DrawTextIn(cv, { content.x + m.px(16), top + m.px(138), content.w - m.px(32), m.px(22) }, Data::FormatNumber(c.number), Color(255, 255, 255, 150), m.font(14), Align::Center);
			DrawTextIn(cv, { content.x + m.px(16), top + m.px(166), content.w - m.px(32), m.px(26) }, statusLine(c), Color(255, 255, 255, 200), m.font(17), Align::Center);

			const float d = m.px(70), by = content.bottom() - m.px(110) - d;
			if (c.state == 2)
			{
				button(cv, m, { cx - m.px(40) - d, by, d, d }, ID_HANGUP, PC::Red, "Recusar");
				button(cv, m, { cx + m.px(40), by, d, d }, ID_ANSWER, PC::Green, "Atender");
			}
			else if (c.state == 1 || c.state == 3) button(cv, m, { cx - d * 0.5f, by, d, d }, ID_HANGUP, PC::Red, "Desligar");
			if (c.state == 3)
			{
				if (c.voice && m_c->talk)
				{
					const Rect tb{ cx - m.px(120), by - m.px(92), m.px(240), m.px(58) };
					const bool hf = m_c->settings.handsFree;
					const bool on = hf ? !c.muted : c.held;
					Color col = hf ? (c.muted ? PC::Red : PC::Green) : (c.held ? PC::Green : PC::Blue);
					if (m_press == ID_TALK && !(hf ? false : c.held)) col = Color::mix(col, Color(0, 0, 0), 0.25f);
					cv.fillRect(tb, col, m.px(29));
					DrawTextIn(cv, tb, hf ? (on ? "Microfone ligado" : "Mudo") : (on ? "Falando..." : "Segure para falar"), Color(255, 255, 255), m.font(17), Align::Center);
					m_hits.add(ID_TALK, tb); m_tb = tb;
				}
				else DrawTextIn(cv, { content.x + m.px(16), by - m.px(70), content.w - m.px(32), m.px(24) }, "Sem áudio de voz nesta ligação", Color(255, 255, 255, 140), m.font(13), Align::Center);
			}
		}

		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (t.kind == Touch::Down)
			{
				m_press = m_hits.at(t.pos);
				if (m_press == ID_TALK && m_c->call.state == 3 && !m_c->settings.handsFree) m_c->call.held = true;   // segurar = falar
			}
			else if (t.kind == Touch::Move) { if (m_press && m_press != ID_TALK && m_hits.at(t.pos) != m_press) m_press = 0; }
			else if (t.kind == Touch::Cancel || t.kind == Touch::Up || t.kind == Touch::DragEnd) { m_press = 0; m_c->call.held = false; }
			else if (t.kind == Touch::DragBegin && m_press != ID_TALK) m_press = 0;
			if (t.kind == Touch::Tap)
			{
				const int id = m_hits.at(t.pos);
				if (id == ID_ANSWER && m_c->answer) m_c->answer();
				else if (id == ID_HANGUP && m_c->hangup) m_c->hangup();
				else if (id == ID_TALK && m_c->settings.handsFree && m_c->call.state == 3) m_c->call.muted = !m_c->call.muted;   // viva-voz: liga/desliga o mudo
			}
			return true;
		}
		bool talking() const { return m_c->call.state == 3 && m_c->call.voice && (m_c->settings.handsFree ? !m_c->call.muted : m_c->call.held); }
		Vec2 talkCenter() const { return m_tb.center(); }

		static std::string Clock(int64_t ms)
		{
			if (ms < 0) ms = 0;
			const int s = (int)(ms / 1000), h = s / 3600, mi = (s / 60) % 60, se = s % 60;
			char b[16];
			if (h > 0) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, mi, se); else std::snprintf(b, sizeof(b), "%02d:%02d", mi, se);
			return b;
		}
		std::string statusLine(const PhoneCall& c) const
		{
			switch (c.state)
			{
			case 1: return c.confirmed ? "Chamando..." : "Conectando...";
			case 2: return "Chamada recebida";
			case 3: return Clock(m_c->nowMs - c.startMs);
			default: return c.endText;
			}
		}

	private:
		void button(Canvas& cv, const Metrics& m, const Rect& r, int id, Color col, const char* label)
		{
			cv.fillCircle(r.center(), r.w * 0.5f, m_press == id ? Color::mix(col, Color(0, 0, 0), 0.25f) : col);
			DrawGlyph(cv, "telefone", r.inset(r.w * 0.27f), Color(255, 255, 255));
			DrawTextIn(cv, { r.x - m.px(20), r.bottom() + m.px(8), r.w + m.px(40), m.px(22) }, label, Color(255, 255, 255, 200), m.font(13), Align::Center);
			m_hits.add(id, r);
		}
		PhoneContext* m_c; HitList m_hits; int m_press = 0; Rect m_tb;
	};
}
