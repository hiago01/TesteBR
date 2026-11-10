#pragma once
// Celular - apps. Cada app e uma classe independente (App do Maximo OS) que so usa PhoneContext + componentes de PhoneUI.h.
// Fase 1: Ajustes, Calculadora e Relogio. Fase 2: Telefone, Mensagens, Contatos, Notas e Calendario (dados locais reais; envio/chamada dependem do servidor). Os demais apps ficam em ServicoPendente: tela honesta dizendo o que falta,
// ate a implementacao do servidor (fases seguintes). Nenhum dado falso e exibido.
#include "PhoneManager.h"
#include "PhoneAppsLocal.h"
#include "PhoneAppsComms.h"
#include "PhoneAppCall.h"
#include "PhoneAppsGram.h"
#include "PhoneAppsStore.h"
#include <cstdio>

namespace Phone
{
	// ------------------------------------------------------------------------------------------------ Ajustes
	class SettingsApp : public App
	{
	public:
		explicit SettingsApp(PhoneContext* c) : m_c(c), m_wp(c) {}
		void onPause() override { m_scroll = ScrollView(); m_press = -1; m_confirmChip = false; }
		void update(float dt) override { m_scroll.update(dt); m_wp.update(dt); }
		bool onBack() override { if (m_screen != 0) { m_screen = 0; m_scroll = ScrollView(); return true; } return false; }

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			if (m_screen == 2) { m_wp.draw(cv, m, content); if (m_wp.takeClose()) { m_screen = 0; m_scroll = ScrollView(); } return; }
			cv.fillRect(content, PC::Light, 0);
			m_rows.clear();
			NavResult nv = DrawNavBar(cv, m, content, m_screen == 0 ? "Ajustes" : "Tamanho do celular", m_screen == 0 ? "" : "Ajustes", "", true, m_press == 100, false);
			m_back = nv.back;
			const Rect body = nv.body;
			float y = m.px(8);
			const float rowH = m.px(46), mx = m.px(16);
			auto row = [&](int id, const std::string& label, const std::string& value, bool first, bool last, bool toggleOn, bool isToggle, const Color& dot, float valueW = 0)
			{
				const Rect r = m_scroll.itemRect(y, rowH).inset(0, 0);
				const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
				m_rows.push_back({ id, rr });
				if (m_scroll.visible(rr))
				{
					RowStyle st; st.first = first; st.last = last; st.chevron = !isToggle && m_screen == 0; st.pressed = (m_press == id); st.valueW = valueW;
					DrawRow(cv, m, rr, label, isToggle ? std::string() : value, st, &dot);
					if (isToggle) DrawToggleIos(cv, m, { rr.right() - m.px(66), rr.y + (rr.h - m.px(30)) * 0.5f, m.px(50), m.px(30) }, toggleOn);
				}
				y += rowH;
			};
			m_scroll.setLayout(body, m.px(700));
			cv.pushClip(body);
			if (m_screen == 0)
			{
				const PhoneSettings& s = m_c->settings;
				row(1, "Modo avião", "", true, false, s.airplane, true, PC::Orange);
				row(2, "Wi-Fi", "", false, false, s.wifi, true, PC::Blue);
				row(3, "Som", "", false, false, s.sound, true, PC::Red);
				row(4, "Notificações", "", false, false, s.notif, true, PC::Red);
				row(5, "Ligação sem apertar", "", false, true, s.handsFree, true, PC::Green);
				y += m.px(22);
				char buf[16]; std::snprintf(buf, sizeof(buf), "%d%%", s.sizePct);
				row(10, "Tamanho do celular", buf, true, false, false, false, PC::Purple);
				row(12, "Papel de parede", "", false, true, false, false, PC::Blue);
				y += m.px(22);
				const StatusInfo st = m_c->status ? m_c->status() : StatusInfo();
				row(20, "Sobre", "Maximo RP", true, false, false, false, PC::Gray);
				row(22, "Meu número", m_c->myNumber.empty() ? std::string("Sem chip") : Data::FormatNumber(m_c->myNumber), false, false, false, false, PC::Gray);
				row(23, "IMEI", m_c->imei.empty() ? std::string("—") : Store::FormatImei(m_c->imei), false, false, false, false, PC::Gray, 190);
				if (!m_c->myNumber.empty())
				{
					const bool showMsg = !m_c->chipMsg.empty() && m_c->nowMs - m_c->chipMsgAt < 5000;
					row(24, m_confirmChip ? "Toque de novo: remover chip" : "Remover chip", showMsg ? m_c->chipMsg : std::string(), false, false, false, false, m_confirmChip ? PC::Red : PC::Gray, showMsg ? 170 : 0);
				}
				else if (!m_c->chipMsg.empty() && m_c->nowMs - m_c->chipMsgAt < 5000) row(24, "Chip", m_c->chipMsg, false, false, false, false, PC::Gray, 190);
				row(21, "Hora", st.clock, false, true, false, false, PC::Gray);
			}
			else
			{
				DrawTextAs(cv, FontDisplaySemibold, m_scroll.itemRect(y, m.px(22)).moved(m.px(20), 0), "ESCALA DO APARELHO", PC::DimL, m.font(12));
				y += m.px(26);
				const int opts[3] = { 75, 100, 125 };
				for (int i = 0; i < 3; ++i)
				{
					const Rect r = m_scroll.itemRect(y, rowH);
					const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
					m_rows.push_back({ 30 + i, rr });
					RowStyle st; st.first = i == 0; st.last = i == 2; st.chevron = false; st.pressed = (m_press == 30 + i);
					char b[16]; std::snprintf(b, sizeof(b), "%d%%", opts[i]);
					DrawRow(cv, m, rr, b, m_c->settings.sizePct == opts[i] ? "OK" : "", st);
					y += rowH;
				}
			}
			cv.popClip();
		}

		bool onTouch(const Touch& t, const Metrics& m, const Rect&) override
		{
			(void)m;
			if (m_screen == 2) return m_wp.onTouch(t);
			m_scroll.onTouch(t);
			if (t.kind == Touch::Down) { m_press = -1; if (m_back.contains(t.pos)) m_press = 100; else for (auto& r : m_rows) if (r.rect.contains(t.pos) && m_scroll.viewport().contains(t.pos)) m_press = r.id; }
			else if (t.kind == Touch::DragBegin || t.kind == Touch::Cancel || t.kind == Touch::Up) m_press = -1;
			else if (t.kind == Touch::Tap)
			{
				if (m_back.contains(t.pos) && m_screen != 0) { m_screen = 0; m_scroll = ScrollView(); return true; }
				for (auto& r : m_rows)
				{
					if (!r.rect.contains(t.pos) || !m_scroll.viewport().contains(t.pos)) continue;
					PhoneSettings& s = m_c->settings;
					switch (r.id)
					{
					case 1: s.airplane = !s.airplane; break;
					case 2: s.wifi = !s.wifi; break;
					case 3: s.sound = !s.sound; break;
					case 4: s.notif = !s.notif; break;
					case 5: s.handsFree = !s.handsFree; break;
					case 10: m_screen = 1; m_scroll = ScrollView(); break;
					case 12: m_screen = 2; m_wp.open(); break;
					case 24:
						if (!m_confirmChip) m_confirmChip = true;
						else { m_confirmChip = false; m_c->chipMsg = m_c->removeChip ? m_c->removeChip() : std::string("Indisponível."); m_c->chipMsgAt = m_c->nowMs; if (m_c->chipMsg.empty()) m_c->chipMsg = "Removendo chip..."; }
						break;
					case 30: s.sizePct = 75; break;
					case 31: s.sizePct = 100; break;
					case 32: s.sizePct = 125; break;
					default: break;
					}
					m_c->changed();
					break;
				}
			}
			return true;
		}
	private:
		static void DrawToggleIos(Canvas& cv, const Metrics&, const Rect& r, bool on)
		{
			cv.fillRect(r, on ? PC::Green : Color(200, 200, 205), r.h * 0.5f);
			cv.fillCircle({ on ? r.right() - r.h * 0.5f : r.x + r.h * 0.5f, r.y + r.h * 0.5f }, r.h * 0.43f, Color(255, 255, 255));
		}
		struct RowRef { int id; Rect rect; };
		bool m_confirmChip = false;
		PhoneContext* m_c; WallpaperPicker m_wp; int m_screen = 0; int m_press = -1; Rect m_back; ScrollView m_scroll; std::vector<RowRef> m_rows;
	};

	// ------------------------------------------------------------------------------------------------ Calculadora
	class CalculatorApp : public App
	{
	public:
		void onPause() override { m_press = -1; }
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Dark, 0);
			const float pad = m.px(14), gap = m.px(10);
			const float bw = (content.w - 2 * pad - 3 * gap) / 4.0f;
			const float bh = std::min(bw, (content.h - m.px(150) - 5 * gap) / 5.0f);
			const float top = content.bottom() - 5 * bh - 4 * gap - m.px(6);
			DrawTextIn(cv, { content.x + pad, top - m.px(104), content.w - 2 * pad, m.px(90) }, m_disp, Color(255, 255, 255), m.font(m_disp.size() > 8 ? 44.0f : 64.0f), Align::Right);
			static const char* L[5][4] = { { "AC", "+/-", "%", "/" }, { "7", "8", "9", "x" }, { "4", "5", "6", "-" }, { "1", "2", "3", "+" }, { "0", ",", "", "=" } };
			m_keys.clear();
			for (int r = 0; r < 5; ++r) for (int c = 0; c < 4; ++c)
			{
				if (!L[r][c][0]) continue;
				Rect k{ content.x + pad + c * (bw + gap), top + r * (bh + gap), bw, bh };
				if (r == 4 && c == 0) k.w = bw * 2 + gap;
				if (r == 4 && c == 1) k.x += bw + gap;
				const std::string lab = L[r][c];
				const bool op = (c == 3), fn = (r == 0 && c < 3);
				Color bg = op ? PC::Orange : fn ? Color(165, 165, 170) : Color(51, 51, 53);
				if (m_press == r * 4 + c) bg = Color::mix(bg, Color(255, 255, 255), 0.25f);
				cv.fillRect(k, bg, std::min(k.w, k.h) * 0.5f);
				DrawTextIn(cv, k, lab, fn ? Color(0, 0, 0) : Color(255, 255, 255), m.font(26), Align::Center);
				m_keys.push_back({ r * 4 + c, k, lab });
			}
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (t.kind == Touch::Down) { m_press = -1; for (auto& k : m_keys) if (k.rect.contains(t.pos)) m_press = k.id; }
			else if (t.kind == Touch::Up || t.kind == Touch::Cancel || t.kind == Touch::DragBegin) m_press = -1;
			else if (t.kind == Touch::Tap) for (auto& k : m_keys) if (k.rect.contains(t.pos)) { press(k.label); break; }
			return true;
		}
		std::string display() const { return m_disp; }
		void press(const std::string& k) { key(k); }
	private:
		struct Key { int id; Rect rect; std::string label; };
		static std::string fmt(double v)
		{
			char b[48]; std::snprintf(b, sizeof(b), "%.8g", v);
			std::string s = b; for (auto& ch : s) if (ch == '.') ch = ',';
			return s;
		}
		double cur() const { std::string s = m_disp; for (auto& ch : s) if (ch == ',') ch = '.'; return std::atof(s.c_str()); }
		void apply()
		{
			if (!m_op) return;
			const double b = cur();
			switch (m_op) { case '+': m_acc += b; break; case '-': m_acc -= b; break; case 'x': m_acc *= b; break; case '/': m_acc = b != 0 ? m_acc / b : 0; m_err = (b == 0); break; default: break; }
			m_disp = m_err ? "Erro" : fmt(m_acc);
		}
		void key(const std::string& k)
		{
			if (m_err && k != "AC") return;
			if (k == "AC") { m_disp = "0"; m_acc = 0; m_op = 0; m_fresh = true; m_err = false; }
			else if (k == "+/-") { if (m_disp != "0") m_disp = m_disp[0] == '-' ? m_disp.substr(1) : "-" + m_disp; }
			else if (k == "%") { m_disp = fmt(cur() / 100.0); m_fresh = true; }
			else if (k == "+" || k == "-" || k == "x" || k == "/")
			{
				if (m_op && !m_fresh) apply(); else m_acc = cur();
				m_op = k[0]; m_fresh = true;
			}
			else if (k == "=") { if (m_op) { apply(); m_op = 0; m_fresh = true; } }
			else if (k == ",") { if (m_fresh) { m_disp = "0,"; m_fresh = false; } else if (m_disp.find(',') == std::string::npos) m_disp += ","; }
			else { if (m_fresh || m_disp == "0") { m_disp = k; m_fresh = false; } else if (m_disp.size() < 12) m_disp += k; }
		}
		std::string m_disp = "0"; double m_acc = 0; char m_op = 0; bool m_fresh = true, m_err = false; int m_press = -1; std::vector<Key> m_keys;
	};

	// ------------------------------------------------------------------------------------------------ Relogio
	class ClockApp : public App
	{
	public:
		explicit ClockApp(PhoneContext* c) : m_c(c) {}
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Dark, 0);
			const StatusInfo st = m_c->status ? m_c->status() : StatusInfo();
			DrawTextAs(cv, FontDisplaySemibold, { content.x, content.y + m.px(70), content.w, m.px(100) }, st.clock, Color(255, 255, 255), m.font(84), Align::Center);
			static const char* wd[7] = { "Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sábado" };
			if (st.weekday >= 0 && st.weekday < 7 && st.day > 0)
			{
				char b[64]; std::snprintf(b, sizeof(b), "%s, %02d/%02d/%04d", wd[st.weekday], st.day, st.month, st.year);
				DrawTextIn(cv, { content.x, content.y + m.px(176), content.w, m.px(30) }, b, PC::Gray, m.font(17), Align::Center);
			}
			DrawTextIn(cv, { content.x + m.px(20), content.y + m.px(250), content.w - m.px(40), m.px(24) }, "Hora do aparelho", PC::Gray, m.font(13), Align::Center);
		}
	private:
		PhoneContext* m_c;
	};

	// ------------------------------------------------------------------------------------------------ Servico pendente (honesto)
	// App cujo funcionamento depende do servidor (fases seguintes). Mostra claramente o que falta; nao simula dados.
	class PendingServiceApp : public App
	{
	public:
		PendingServiceApp(std::string id, std::string title, std::string need, bool light) : m_id(std::move(id)), m_title(std::move(title)), m_need(std::move(need)), m_light(light) {}
		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, m_light ? PC::Light : PC::Dark, 0);
			const Color fg = m_light ? PC::LabelL : Colors::Text;
			DrawGlyph(cv, m_id, Rect{ content.center().x - m.px(34), content.y + m.px(150), m.px(68), m.px(68) }, PC::Gray);
			DrawTextAs(cv, FontDisplaySemibold, { content.x, content.y + m.px(232), content.w, m.px(32) }, m_title, fg, m.font(22), Align::Center);
			DrawTextIn(cv, { content.x + m.px(24), content.y + m.px(272), content.w - m.px(48), m.px(24) }, "Ainda não disponível neste servidor", PC::Gray, m.font(14), Align::Center);
			DrawTextIn(cv, { content.x + m.px(24), content.y + m.px(298), content.w - m.px(48), m.px(40) }, m_need, PC::Gray, m.font(12), Align::Center);
		}
	private:
		std::string m_id, m_title, m_need; bool m_light;
	};

	// ------------------------------------------------------------------------------------------------ catalogo (inventario do video)
	inline void InstallDefaultApps(PhoneManager& pm)
	{
		PhoneContext* c = &pm.context();
		auto pend = [](const char* id, const char* title, const char* need, bool light) { std::string i = id, t = title, n = need; return [i, t, n, light]() { return std::unique_ptr<App>(new PendingServiceApp(i, t, n, light)); }; };
		auto add = [&](const char* id, const char* title, Color col, int page, int dock, bool light, std::function<std::unique_ptr<App>()> f, bool seeThrough = false, bool shown = true)
		{ PhoneAppDef d; d.shown = shown; d.id = id; d.title = title; d.color = col; d.page = page; d.dock = dock; d.light = light; d.factory = std::move(f); d.seeThrough = seeThrough; pm.addApp(d); };

		// dock
		add("telefone",  "Telefone",  PC::Green,  -1, 0, true,  [c]() { return std::unique_ptr<App>(new DialerApp(c)); });
		add("loja",      "Loja",      PC::Blue,   -1, 1, true,  [c]() { return std::unique_ptr<App>(new StoreApp(c)); });
		add("camera",    "Câmera",    Color(90, 90, 96), -1, 2, false, [c]() { return std::unique_ptr<App>(new CameraApp(c)); }, true);
		add("mensagens", "Mensagens", PC::Green,  -1, 3, true,  [c]() { return std::unique_ptr<App>(new MessagesApp(c)); });
		// tela de ligacao (nao aparece na grade nem no dock; aberta pelo Telefone ou por uma ligacao recebida)
		add("ligacao",   "Ligação",   PC::Green,  -1, -1, false, [c]() { return std::unique_ptr<App>(new CallApp(c)); });
		// pagina 1
		add("contatos",  "Contatos",  Color(120, 124, 132), 0, -1, true, [c]() { return std::unique_ptr<App>(new ContactsApp(c)); });
		add("galeria",   "Galeria",   Color(255, 90, 100),  0, -1, true,  [c]() { return std::unique_ptr<App>(new GalleryApp(c)); });
		add("ajustes",   "Ajustes",   Color(120, 124, 132), 0, -1, true, [c]() { return std::unique_ptr<App>(new SettingsApp(c)); });
		add("notas",     "Notas",     Color(255, 204, 0),   0, -1, true, [c]() { return std::unique_ptr<App>(new NotesApp(c)); });
		add("calendario","Calendário",Color(255, 255, 255), 0, -1, true, [c]() { return std::unique_ptr<App>(new CalendarApp(c)); });
		add("relogio",   "Relógio",   Color(30, 30, 34),    0, -1, false, [c]() { return std::unique_ptr<App>(new ClockApp(c)); });
		add("calculadora","Calculadora", Color(70, 70, 76), 0, -1, false, []() { return std::unique_ptr<App>(new CalculatorApp()); });
		add("mapas",     "Mapas",     PC::Green,   0, -1, true,  pend("mapas", "Mapas", "Requer o mapa do GTA dentro da interface.", true), false, false);
		add("noticias",  "Notícias",  PC::Orange,  0, -1, false, pend("noticias", "Notícias", "Requer servidor: matérias dos jornalistas.", false), false, false);
		add("qrcode",    "QR Code",   Color(60, 60, 220), 0, -1, false, pend("qrcode", "QR Code", "Requer servidor: identidade do jogador.", false), false, false);
		add("radio",     "Rádio",     PC::Purple,  0, -1, false, pend("radio", "Rádio", "Requer servidor: canais.", false), false, false);
		add("gravador",  "Gravador",  PC::Red,     0, -1, false, pend("gravador", "Gravador", "Requer captura de áudio e armazenamento.", false), false, false);
		// pagina 2
		add("tune",      "Tune",      Color(30, 215, 96),   1, -1, false, pend("tune", "Tune", "Requer servidor: músicas.", false), false, false);
		add("gram",      "Gram",      Color(225, 48, 108),  1, -1, true,  [c]() { return std::unique_ptr<App>(new GramApp(c)); });
		add("clipe",     "Clipe",     Color(20, 20, 24),    1, -1, false, pend("clipe", "Clipe", "Requer servidor de vídeo.", false), false, false);
		add("live",      "Live",      Color(255, 45, 85),   1, -1, false, pend("live", "Live", "Requer serviço de vídeo em tempo real.", false), false, false);
		add("pede",      "Pede",      PC::Red,     1, -1, true,  pend("pede", "Pede", "Requer servidor: restaurantes e pedidos.", true), false, false);
		add("corre",     "Corre",     Color(20, 20, 24),    1, -1, false, pend("corre", "Corre", "Requer servidor: corridas.", false), false, false);
		add("banco",     "Banco",     Color(130, 40, 200),  1, -1, true,  pend("banco", "Banco", "Requer servidor: conta bancária.", true), false, false);
		add("papo",      "Papo",      Color(88, 101, 242),  1, -1, false, pend("papo", "Papo", "Requer servidor: salas.", false), false, false);
		add("sinuca",    "Sinuca",    Color(0, 140, 90),    1, -1, false, pend("sinuca", "Sinuca", "Requer servidor: partidas entre jogadores.", false), false, false);
	}
}
