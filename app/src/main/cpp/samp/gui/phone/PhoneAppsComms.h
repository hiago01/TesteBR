#pragma once
// Celular - apps de COMUNICACAO: Telefone (discador, recentes, contatos) e Mensagens (conversas). A interface e os dados locais sao
// reais; o ENVIO/CHAMADA depende do servidor (PhoneContext::dial / sendMessage, Fase 3). Sem o gancho, o app avisa "servico indisponivel"
// e NAO finge que enviou/ligou.
#include "PhoneData.h"
#include "PhoneForm.h"
#include <map>

namespace Phone
{
	inline const char* kNoCallService() { return "Serviço de chamadas indisponível neste servidor."; }
	inline const char* kNoMsgService()  { return "Serviço de mensagens indisponível neste servidor."; }

	// ================================================================================================ Telefone
	class DialerApp : public KeyboardApp
	{
	public:
		enum { ID_CALL = 1, ID_BACKSP, ID_TAB = 10, ID_KEY = 20, ID_ROW = 1000 };
		explicit DialerApp(PhoneContext* c) : KeyboardApp(c) {}
		void onCreate() override { reload(); }
		void onResume() override
		{
			reload();
			if (m_c->launchArg.compare(0, 5, "dial:") == 0) { m_number = Data::CleanNumber(m_c->launchArg.substr(5)); m_tab = 0; m_c->launchArg.clear(); }
		}
		void onPaused() override { m_press = 0; }

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			const float tabH = m.px(56);
			const Rect tabs{ content.x, content.bottom() - tabH, content.w, tabH };
			Rect body{ content.x, content.y, content.w, content.h - tabH };
			beginFrame(body);
			if (m_tab == 0) drawKeypad(cv, m, body);
			else drawLists(cv, m, body);
			cv.fillRect(tabs, Color(249, 249, 251), 0);
			cv.fillRect({ tabs.x, tabs.y, tabs.w, std::max(1.0f, m.px(0.6f)) }, PC::Sep, 0);
			static const char* L[3] = { "Teclado", "Recentes", "Contatos" };
			for (int i = 0; i < 3; ++i)
			{
				const Rect r{ tabs.x + i * tabs.w / 3.0f, tabs.y, tabs.w / 3.0f, tabs.h };
				DrawTextIn(cv, r, L[i], m_tab == i ? PC::Blue : PC::Gray, m.font(14), Align::Center);
				m_nav.add(ID_TAB + i, r);
			}
			m_msg.draw(cv, m, body, m_c->nowMs);
		}

		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (m_tab != 0) m_scroll.onTouch(t);
			trackPress(t);
			if (t.kind == Touch::LongPress && hitAt(t.pos) == ID_BACKSP) { m_number.clear(); return true; }
			if (t.kind != Touch::Tap) return true;
			const int id = hitAt(t.pos);
			if (id >= ID_TAB && id < ID_TAB + 3) { m_tab = id - ID_TAB; m_scroll = ScrollView(); reload(); }
			else if (id == ID_CALL) place();
			else if (id == ID_BACKSP) Text::PopBack(m_number);
			else if (id >= ID_KEY && id < ID_KEY + 12) { static const char* k = "123456789*0#"; if (m_number.size() < Data::kNumberMax) m_number += k[id - ID_KEY]; }
			else if (id >= ID_ROW) pick(id - ID_ROW);
			return true;
		}
		const std::string& number() const { return m_number; }
		int tab() const { return m_tab; }
		void place()
		{
			const std::string n = Data::CleanNumber(m_number);
			if (!Data::HasDigit(n)) { m_msg.set("Digite um número.", m_c->nowMs); return; }
			const std::string err = m_c->dial ? m_c->dial(n) : std::string(kNoCallService());
			if (!err.empty()) { m_msg.set(err, m_c->nowMs); return; }
			Data::AddCall(*m_c, n, 0); m_c->changed(); reload();
			m_msg.set("Chamando " + n + "...", m_c->nowMs, false);
		}

	private:
		void reload() { m_contacts = Data::LoadContacts(*m_c); Data::SortContacts(m_contacts); m_calls = Data::LoadCalls(*m_c); }
		void pick(int i)
		{
			if (m_tab == 1 && i >= 0 && i < (int)m_calls.size()) { m_number = m_calls[i].number; m_tab = 0; }
			else if (m_tab == 2 && i >= 0 && i < (int)m_contacts.size()) { m_number = m_contacts[i].number; m_tab = 0; }
		}
		void drawKeypad(Canvas& cv, const Metrics& m, const Rect& body)
		{
			const float fs = m_number.size() > 11 ? 26.0f : 34.0f;
			DrawTextIn(cv, { body.x + m.px(20), body.y + m.px(36), body.w - m.px(40), m.px(46) }, m_number, PC::LabelL, m.font(fs), Align::Center);
			const std::string nm = Data::NameFor(m_contacts, Data::CleanNumber(m_number));
			DrawTextIn(cv, { body.x + m.px(20), body.y + m.px(84), body.w - m.px(40), m.px(22) }, nm, PC::Blue, m.font(15), Align::Center);
			const float gap = m.px(14), top = body.y + m.px(118);
			const float avail = body.bottom() - top - m.px(8);
			const float d = std::min((body.w - m.px(56) - 2 * gap) / 3.0f, (avail - 4 * gap) / 5.0f);
			const float gx = body.center().x - (3 * d + 2 * gap) * 0.5f;
			static const char* K = "123456789*0#";
			for (int i = 0; i < 12; ++i)
			{
				const Rect r{ gx + (i % 3) * (d + gap), top + (i / 3) * (d + gap), d, d };
				cv.fillCircle(r.center(), d * 0.5f, m_press == ID_KEY + i ? Color(190, 190, 198) : Color(228, 228, 234));
				DrawTextIn(cv, r, std::string(1, K[i]), PC::LabelL, m.font(30), Align::Center);
				m_hits.add(ID_KEY + i, r);
			}
			const float cy = top + 4 * (d + gap);
			const Rect call{ body.center().x - d * 0.5f, cy, d, d };
			cv.fillCircle(call.center(), d * 0.5f, m_press == ID_CALL ? Color(40, 160, 70) : PC::Green);
			DrawGlyph(cv, "telefone", call.inset(d * 0.26f), Color(255, 255, 255)); m_hits.add(ID_CALL, call);
			if (!m_number.empty())
			{
				const Rect bk{ gx + 2 * (d + gap), cy, d, d };
				DrawTextIn(cv, bk, "<", m_press == ID_BACKSP ? PC::Gray : PC::DimL, m.font(30), Align::Center); m_hits.add(ID_BACKSP, bk);
			}
		}
		void drawLists(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, m_tab == 1 ? "Recentes" : "Contatos", "", "", true, false, false);
			beginFrame(nv.body);
			const Rect body = nv.body; const float rowH = m.px(52), mx = m.px(16);
			const size_t n = m_tab == 1 ? m_calls.size() : m_contacts.size();
			if (n == 0)
			{
				DrawTextIn(cv, { body.x, body.center().y - m.px(14), body.w, m.px(28) }, m_tab == 1 ? "Nenhuma chamada" : "Nenhum contato", PC::Gray, m.font(18), Align::Center);
				return;
			}
			m_scroll.setLayout(body, m.px(16) + n * rowH);
			cv.pushClip(body);
			for (size_t i = 0; i < n; ++i)
			{
				const Rect r = m_scroll.itemRect(m.px(8) + i * rowH, rowH); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
				if (!m_scroll.visible(rr)) continue;
				RowStyle st; st.first = i == 0; st.last = i + 1 == n; st.chevron = false; st.pressed = m_press == ID_ROW + (int)i;
				if (m_tab == 1)
				{
					const Data::Call& c = m_calls[i]; const std::string nm = Data::NameFor(m_contacts, c.number);
					static const char* dir[3] = { "Feita", "Recebida", "Perdida" };
					DrawRow(cv, m, rr, nm.empty() ? c.number : nm, dir[c.dir], st);
				}
				else DrawRow(cv, m, rr, m_contacts[i].name, m_contacts[i].number, st);
				m_hits.add(ID_ROW + (int)i, rr);
			}
			cv.popClip();
		}

		std::vector<Data::Contact> m_contacts; std::vector<Data::Call> m_calls;
		std::string m_number; int m_tab = 0; ScrollView m_scroll; InlineMsg m_msg;
	};

	// ================================================================================================ Mensagens
	class MessagesApp : public KeyboardApp
	{
	public:
		enum { ID_BACK = 1, ID_ACTION, ID_SEND, ID_COMPOSER, ID_F_NUM, ID_ROW = 1000, ID_CT = 3000 };
		explicit MessagesApp(PhoneContext* c) : KeyboardApp(c) {}
		void onCreate() override { reload(); }
		void onResume() override
		{
			reload();
			if (m_c->launchArg.compare(0, 3, "to:") == 0) { openThread(Data::CleanNumber(m_c->launchArg.substr(3))); m_c->launchArg.clear(); }
			if (m_screen == 1) Data::MarkThreadRead(*m_c, m_num);
		}
		void onPaused() override { m_press = 0; m_confirm = false; }
		bool onBackApp() override
		{
			if (m_screen != 0) { m_screen = 0; m_confirm = false; reload(); return true; }
			return false;
		}
		void onUpdate(float dt) override
		{
			m_reloadMs += dt;
			if (m_reloadMs > 700.0f && m_screen != 2) { m_reloadMs = 0; const size_t before = m_msgs.size(); reload(); if (m_screen == 1) { if (m_msgs.size() != before) m_scroll.jumpToEnd(); Data::MarkThreadRead(*m_c, m_num); } }
		}

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			if (m_screen == 0) drawThreads(cv, m, content);
			else if (m_screen == 1) drawThread(cv, m, content);
			else drawNew(cv, m, content);
			m_msg.draw(cv, m, content, m_c->nowMs);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (kbTouch(t)) return true;
			m_scroll.onTouch(t);
			trackPress(t);
			if (t.kind != Touch::Tap) return true;
			const int id = hitAt(t.pos);
			if (id == ID_BACK) { if (m_kbOn) hideKeyboard(); onBackApp(); return true; }
			if (m_screen == 0)
			{
				if (id == ID_ACTION) { m_screen = 2; m_numIn.clear(); m_err.clear(); m_scroll = ScrollView(); showKeyboard(Keyboard::Mode::Phone, "Próximo", m_numIn); }
				else if (id >= ID_ROW && id - ID_ROW < (int)m_threads.size()) openThread(m_threads[id - ID_ROW].number);
			}
			else if (m_screen == 1)
			{
				if (id == ID_SEND) send();
				else if (id == ID_COMPOSER) showKeyboard(Keyboard::Mode::Text, "Enviar", m_text);
				else if (id == ID_ACTION)
				{
					if (!m_confirm) { m_confirm = true; m_msg.set("Toque de novo para apagar a conversa", m_c->nowMs, false); }
					else { Data::DeleteThread(*m_c, m_num); m_c->changed(); m_confirm = false; hideKeyboard(); m_screen = 0; reload(); }
				}
			}
			else
			{
				if (id == ID_ACTION || id == ID_F_NUM) { if (id == ID_ACTION) startNew(); else showKeyboard(Keyboard::Mode::Phone, "Próximo", m_numIn); }
				else if (id >= ID_CT && id - ID_CT < (int)m_contacts.size()) openThread(m_contacts[id - ID_CT].number);
			}
			return true;
		}
		int screen() const { return m_screen; } const std::string& thread() const { return m_num; } const std::string& compose() const { return m_text; }
		void setCompose(const std::string& s) { m_text = s; }
		void send()
		{
			const std::string txt = Text::Trim(m_text);
			if (txt.empty()) return;
			const std::string err = m_c->sendMessage ? m_c->sendMessage(m_num, txt) : std::string(kNoMsgService());
			if (!err.empty()) { m_msg.set(err, m_c->nowMs); return; }
			Data::AddMessage(*m_c, m_num, txt, true); m_c->changed(); m_text.clear(); reload(); m_scroll.jumpToEnd(); m_kb.autoShift(m_text);
		}

	protected:
		void onKeyEvent(const Keyboard::Event& e) override
		{
			if (m_screen == 1) { if (Keyboard::Apply(m_text, e, Data::kMsgMax, false, Keyboard::Mode::Text)) send(); else m_kb.autoShift(m_text); }
			else if (m_screen == 2) { if (Keyboard::Apply(m_numIn, e, Data::kNumberMax, false, Keyboard::Mode::Phone)) startNew(); m_err.clear(); }
		}

	private:
		void reload()
		{
			m_msgs = Data::LoadMsgs(*m_c); m_threads = Data::Threads(*m_c); m_contacts = Data::LoadContacts(*m_c); Data::SortContacts(m_contacts);
			m_wrap.clear(); m_wrapW = -1;
		}
		void openThread(const std::string& n)
		{
			if (!Data::HasDigit(n)) return;
			m_num = n; m_screen = 1; m_text.clear(); m_confirm = false; hideKeyboard(); m_scroll = ScrollView(); m_scroll.jumpToEnd();
			Data::MarkThreadRead(*m_c, m_num); reload();
		}
		void startNew()
		{
			const std::string n = Data::CleanNumber(m_numIn);
			if (!Data::HasDigit(n)) { m_err = "Digite um número válido."; return; }
			openThread(n);
		}
		std::string title(const std::string& number) const { const std::string nm = Data::NameFor(m_contacts, number); return nm.empty() ? number : nm; }

		void drawThreads(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Mensagens", "", "Nova", true, false, m_press == ID_ACTION);
			beginFrame(nv.body); m_nav.add(ID_ACTION, nv.action);
			const Rect body = nv.body;
			if (m_threads.empty())
			{
				DrawTextIn(cv, { body.x, body.center().y - m.px(30), body.w, m.px(28) }, "Nenhuma conversa", PC::Gray, m.font(19), Align::Center);
				DrawTextIn(cv, { body.x + m.px(20), body.center().y, body.w - m.px(40), m.px(24) }, "Toque em Nova para escrever.", PC::Gray, m.font(14), Align::Center);
				return;
			}
			const float rowH = m.px(64), mx = m.px(16);
			m_scroll.setLayout(body, m.px(16) + m_threads.size() * rowH);
			cv.pushClip(body);
			for (size_t i = 0; i < m_threads.size(); ++i)
			{
				const Rect r = m_scroll.itemRect(m.px(8) + i * rowH, rowH); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
				if (!m_scroll.visible(rr)) continue;
				RowStyle st; st.first = i == 0; st.last = i + 1 == m_threads.size(); st.chevron = false; st.pressed = m_press == ID_ROW + (int)i;
				DrawRow(cv, m, rr, std::string(), std::string(), st);
				const Data::Thread& th = m_threads[i];
				if (th.unread > 0) cv.fillCircle({ rr.x + m.px(14), rr.y + rowH * 0.5f }, m.px(5), PC::Blue);
				DrawTextIn(cv, { rr.x + m.px(28), rr.y + m.px(9), rr.w - m.px(44), m.px(22) }, title(th.number), PC::LabelL, m.font(16));
				DrawTextIn(cv, { rr.x + m.px(28), rr.y + m.px(33), rr.w - m.px(44), m.px(20) }, th.last, PC::Gray, m.font(13));
				m_hits.add(ID_ROW + (int)i, rr);
			}
			cv.popClip();
		}

		void drawThread(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, title(m_num), "Mensagens", "Apagar", true, m_press == ID_BACK, m_press == ID_ACTION);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_ACTION, nv.action);
			const float barH = m.px(56);
			const Rect view{ body.x, body.y, body.w, body.h - barH }, bar{ body.x, view.bottom(), body.w, barH };
			// bolhas
			const float fs = m.font(15), lineH = m.px(21), padX = m.px(12), padY = m.px(8), maxBw = body.w * 0.74f, gap = m.px(6), edge = m.px(12);
			if (m_wrapW != view.w) { m_wrap.clear(); m_wrapW = view.w; }
			struct Item { const Data::Msg* msg; const std::vector<std::string>* ln; float h; };
			std::vector<Item> items; float total = m.px(10);
			for (auto& mg : m_msgs)
			{
				if (mg.number != m_num) continue;
				auto it = m_wrap.find(mg.seq);
				if (it == m_wrap.end()) it = m_wrap.emplace(mg.seq, Text::Wrap(cv, mg.text, fs, maxBw - 2 * padX)).first;
				const float h = it->second.size() * lineH + 2 * padY;
				items.push_back({ &mg, &it->second, h }); total += h + gap;
			}
			m_scroll.setLayout(view, total + m.px(10));
			cv.pushClip(view);
			if (items.empty()) DrawTextIn(cv, { view.x, view.center().y - m.px(14), view.w, m.px(28) }, "Nenhuma mensagem ainda", PC::Gray, m.font(16), Align::Center);
			float y = m.px(10);
			for (auto& it : items)
			{
				const Rect r = m_scroll.itemRect(y, it.h);
				y += it.h + gap;
				if (!m_scroll.visible(r)) continue;
				float tw = 0; for (auto& l : *it.ln) tw = std::max(tw, cv.measure(l, fs).x);
				const float bw = std::min(maxBw, tw + 2 * padX);
				const Rect b{ it.msg->out ? view.right() - edge - bw : view.x + edge, r.y, bw, it.h };
				cv.fillRect(b, it.msg->out ? PC::Blue : Color(229, 229, 234), m.px(16));
				for (size_t k = 0; k < it.ln->size(); ++k)
					DrawTextIn(cv, { b.x + padX, b.y + padY + k * lineH, bw - 2 * padX, lineH }, (*it.ln)[k], it.msg->out ? Color(255, 255, 255) : PC::LabelL, fs);
			}
			cv.popClip();
			// barra de composicao
			cv.fillRect(bar, Color(249, 249, 251), 0);
			cv.fillRect({ bar.x, bar.y, bar.w, std::max(1.0f, m.px(0.6f)) }, PC::Sep, 0);
			const float sb = m.px(34);
			const Rect field{ bar.x + m.px(12), bar.y + m.px(9), bar.w - m.px(24) - sb - m.px(8), bar.h - m.px(18) };
			cv.fillRect(field, Color(255, 255, 255), field.h * 0.5f);
			cv.strokeRect(field, Color(60, 60, 67, 60), field.h * 0.5f, std::max(1.0f, m.px(1)));
			const float tfs = m.font(15);
			if (m_text.empty() && !m_kbOn) DrawTextIn(cv, field.inset(m.px(14), 0), "Mensagem", PC::Gray, tfs);
			else
			{
				const std::string shown = TailFit(cv, m_text, tfs, field.w - m.px(30));
				DrawTextIn(cv, field.inset(m.px(14), 0), shown, PC::LabelL, tfs);
				if (m_kbOn && ((m_c->nowMs / 500) % 2 == 0)) cv.fillRect({ field.x + m.px(14) + cv.measure(shown, tfs).x + m.px(1), field.center().y - tfs * 0.6f, std::max(1.5f, m.px(1.5f)), tfs * 1.2f }, PC::Blue, 0);
			}
			m_hits.add(ID_COMPOSER, field);
			const Rect sr{ field.right() + m.px(8), bar.y + (bar.h - sb) * 0.5f, sb, sb };
			const bool can = !Text::Trim(m_text).empty();
			cv.fillCircle(sr.center(), sb * 0.5f, can ? PC::Blue : Color(190, 190, 198));
			cv.fillTriangle({ sr.center().x, sr.center().y - sb * 0.24f }, { sr.center().x - sb * 0.22f, sr.center().y + sb * 0.1f }, { sr.center().x + sb * 0.22f, sr.center().y + sb * 0.1f }, Color(255, 255, 255));
			cv.fillRect({ sr.center().x - sb * 0.05f, sr.center().y, sb * 0.1f, sb * 0.22f }, Color(255, 255, 255), 0);
			m_hits.add(ID_SEND, sr);
			drawKeyboard(cv, m);
		}

		void drawNew(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Nova mensagem", "Cancelar", "Próximo", true, m_press == ID_BACK, m_press == ID_ACTION);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_ACTION, nv.action);
			const float rowH = m.px(50), mx = m.px(16); float y = body.y + m.px(12);
			const Rect r1{ body.x + mx, y, body.w - 2 * mx, rowH };
			RowStyle a; a.first = true; a.last = true;
			DrawField(cv, m, r1, "Para", m_numIn, "Número", true, m_c->nowMs, a); m_hits.add(ID_F_NUM, r1);
			y = r1.bottom() + m.px(8);
			if (!m_err.empty()) { DrawTextIn(cv, { body.x + mx + m.px(8), y, body.w - 2 * mx, m.px(22) }, m_err, PC::Red, m.font(14)); }
			y += m.px(26);
			if (!m_contacts.empty() && !m_kbOn)
			{
				DrawTextIn(cv, { body.x + mx + m.px(8), y, body.w, m.px(22) }, "CONTATOS", PC::DimL, m.font(12)); y += m.px(24);
				const Rect view{ body.x, y, body.w, body.bottom() - y };
				m_scroll.setLayout(view, m_contacts.size() * m.px(48) + m.px(8));
				cv.pushClip(view);
				for (size_t i = 0; i < m_contacts.size(); ++i)
				{
					const Rect r = m_scroll.itemRect(i * m.px(48), m.px(48)); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, m.px(48) };
					if (!m_scroll.visible(rr)) continue;
					RowStyle st; st.first = i == 0; st.last = i + 1 == m_contacts.size(); st.chevron = false; st.pressed = m_press == ID_CT + (int)i;
					DrawRow(cv, m, rr, m_contacts[i].name, m_contacts[i].number, st);
					m_hits.add(ID_CT + (int)i, rr);
				}
				cv.popClip();
			}
			drawKeyboard(cv, m);
		}

		std::vector<Data::Msg> m_msgs; std::vector<Data::Thread> m_threads; std::vector<Data::Contact> m_contacts;
		std::map<int, std::vector<std::string>> m_wrap; float m_wrapW = -1, m_reloadMs = 0;
		std::string m_num, m_text, m_numIn, m_err; int m_screen = 0; bool m_confirm = false;
		ScrollView m_scroll; InlineMsg m_msg;
	};
}
