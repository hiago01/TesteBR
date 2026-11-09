#pragma once
// Celular - apps LOCAIS reais: Contatos, Notas e Calendario. Funcionam sem servidor (dados em PhoneData.h); na Fase 3 o servidor
// passa a sincronizar estes mesmos dados. Cada app usa so PhoneContext + componentes de PhoneUI.h/PhoneForm.h.
#include "PhoneData.h"
#include "PhoneForm.h"

namespace Phone
{
	// ================================================================================================ Contatos
	class ContactsApp : public KeyboardApp
	{
	public:
		enum { ID_BACK = 1, ID_ACTION, ID_CALL, ID_MSG, ID_EMERG, ID_EDIT, ID_DEL, ID_F_NAME, ID_F_NUM, ID_ROW = 1000 };
		explicit ContactsApp(PhoneContext* c) : KeyboardApp(c) {}
		void onCreate() override { reload(); }
		void onResume() override { reload(); }
		void onPaused() override { m_press = 0; m_confirm = false; }
		bool onBackApp() override
		{
			if (m_screen == 2) { m_screen = (m_edit >= 0) ? 1 : 0; m_err.clear(); return true; }
			if (m_screen == 1) { m_screen = 0; m_confirm = false; return true; }
			return false;
		}

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			if (m_screen == 0) drawList(cv, m, content);
			else if (m_screen == 1) drawDetail(cv, m, content);
			else drawEdit(cv, m, content);
			m_msg.draw(cv, m, content, m_c->nowMs);
		}

		bool onTouch(const Touch& t, const Metrics& m, const Rect&) override
		{
			(void)m;
			if (kbTouch(t)) return true;
			if (m_screen == 0) m_scroll.onTouch(t);
			trackPress(t);
			if (t.kind != Touch::Tap) return true;
			const int id = hitAt(t.pos);
			if (id == ID_BACK) { if (m_kbOn) hideKeyboard(); onBackApp(); return true; }
			if (m_screen == 0)
			{
				if (id == ID_ACTION) startEdit(-1);
				else if (id >= ID_ROW && id - ID_ROW < (int)m_list.size()) { m_sel = id - ID_ROW; m_screen = 1; m_confirm = false; }
			}
			else if (m_screen == 1 && m_sel >= 0 && m_sel < (int)m_list.size())
			{
				const Data::Contact k = m_list[m_sel];
				if (id == ID_CALL) { m_c->launchArg = "dial:" + k.number; if (m_c->openApp) m_c->openApp("telefone"); }
				else if (id == ID_MSG) { m_c->launchArg = "to:" + k.number; if (m_c->openApp) m_c->openApp("mensagens"); }
				else if (id == ID_EMERG) { m_list[m_sel].emergency = !m_list[m_sel].emergency; persist(k.number); }
				else if (id == ID_EDIT) startEdit(m_sel);
				else if (id == ID_DEL)
				{
					if (!m_confirm) { m_confirm = true; m_msg.set("Toque de novo para excluir", m_c->nowMs, false); }
					else { m_list.erase(m_list.begin() + m_sel); m_sel = -1; m_screen = 0; m_confirm = false; Data::SaveContacts(*m_c, m_list); m_c->changed(); }
				}
			}
			else if (m_screen == 2)
			{
				if (id == ID_F_NAME) focus(0);
				else if (id == ID_F_NUM) focus(1);
				else if (id == ID_ACTION) save();
			}
			return true;
		}

		// ---- usado pelos testes ----
		size_t count() const { return m_list.size(); }
		int screen() const { return m_screen; }

	protected:
		void onKeyEvent(const Keyboard::Event& e) override
		{
			if (m_screen != 2 || m_focus < 0) return;
			if (m_focus == 0) { if (Keyboard::Apply(m_name, e, Data::kNameMax, false, Keyboard::Mode::Text)) focus(1); else m_kb.autoShift(m_name); }
			else { if (Keyboard::Apply(m_number, e, Data::kNumberMax, false, Keyboard::Mode::Phone)) save(); }
			m_err.clear();
		}

	private:
		void reload() { m_list = Data::LoadContacts(*m_c); Data::SortContacts(m_list); if (m_sel >= (int)m_list.size()) m_sel = -1; }
		// grava e reposiciona a selecao pelo numero (a ordenacao pode mudar)
		void persist(const std::string& number)
		{
			Data::SortContacts(m_list); Data::SaveContacts(*m_c, m_list); m_c->changed();
			m_sel = -1; for (size_t i = 0; i < m_list.size(); ++i) if (m_list[i].number == number) m_sel = (int)i;
		}
		void startEdit(int idx)
		{
			m_edit = idx; m_screen = 2; m_err.clear();
			m_name = idx >= 0 ? m_list[idx].name : std::string(); m_number = idx >= 0 ? m_list[idx].number : std::string();
			focus(idx >= 0 ? -1 : 0);
			if (idx >= 0) hideKeyboard();
		}
		void focus(int f)
		{
			m_focus = f;
			if (f == 0) showKeyboard(Keyboard::Mode::Text, "Próximo", m_name);
			else if (f == 1) showKeyboard(Keyboard::Mode::Phone, "Salvar", m_number);
			else hideKeyboard();
		}
		void save()
		{
			const std::string name = Text::Trim(m_name);
			const std::string err = Data::ValidateContact(m_list, name, m_number, m_edit);
			if (!err.empty()) { m_err = err; return; }
			Data::Contact k; k.name = Data::Clip(name, Data::kNameMax); k.number = Data::CleanNumber(m_number);
			if (m_edit >= 0) { k.emergency = m_list[m_edit].emergency; m_list[m_edit] = k; }
			else m_list.push_back(k);
			persist(k.number);
			hideKeyboard(); m_screen = 1; m_edit = m_sel;
		}

		void drawList(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Contatos", "", "Novo", true, false, m_press == ID_ACTION);
			beginFrame(nv.body); m_nav.add(ID_ACTION, nv.action);
			const Rect body = nv.body;
			if (m_list.empty())
			{
				DrawTextIn(cv, { body.x, body.center().y - m.px(30), body.w, m.px(28) }, "Nenhum contato", PC::Gray, m.font(19), Align::Center);
				DrawTextIn(cv, { body.x + m.px(20), body.center().y, body.w - m.px(40), m.px(24) }, "Toque em Novo para adicionar.", PC::Gray, m.font(14), Align::Center);
				return;
			}
			std::vector<int> em, all;
			for (size_t i = 0; i < m_list.size(); ++i) { all.push_back((int)i); if (m_list[i].emergency) em.push_back((int)i); }
			const float rowH = m.px(50), mx = m.px(16), hdr = m.px(28), gap = m.px(14);
			float total = m.px(8) + hdr + all.size() * rowH + gap + m.px(12);
			if (!em.empty()) total += hdr + em.size() * rowH + gap;
			m_scroll.setLayout(body, total);
			cv.pushClip(body);
			float y = m.px(8);
			auto section = [&](const char* title, const std::vector<int>& idx)
			{
				DrawTextIn(cv, m_scroll.itemRect(y, hdr).moved(mx + m.px(8), 0), title, PC::DimL, m.font(12));
				y += hdr;
				for (size_t k = 0; k < idx.size(); ++k)
				{
					const Rect r = m_scroll.itemRect(y, rowH); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
					if (m_scroll.visible(rr))
					{
						RowStyle st; st.first = k == 0; st.last = k + 1 == idx.size(); st.pressed = m_press == ID_ROW + idx[k];
						DrawRow(cv, m, rr, m_list[idx[k]].name, m_list[idx[k]].number, st);
						m_hits.add(ID_ROW + idx[k], rr);
					}
					y += rowH;
				}
				y += gap;
			};
			if (!em.empty()) section("EMERGÊNCIA", em);
			section("TODOS OS CONTATOS", all);
			cv.popClip();
		}

		void drawDetail(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "", "Contatos", "Editar", true, m_press == ID_BACK, m_press == ID_EDIT);
			beginFrame(nv.body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_EDIT, nv.action);
			if (m_sel < 0 || m_sel >= (int)m_list.size()) { m_screen = 0; return; }
			const Data::Contact& k = m_list[m_sel];
			const Rect body = nv.body; const float cx = body.center().x;
			const float av = m.px(84);
			cv.fillCircle({ cx, body.y + m.px(60) }, av * 0.5f, Color(165, 170, 180));
			std::string ini = k.name.empty() ? "?" : k.name.substr(0, Text::CpLen(k.name, 0));
			DrawTextIn(cv, { cx - av * 0.5f, body.y + m.px(60) - av * 0.5f, av, av }, Text::Upper(ini), Color(255, 255, 255), m.font(36), Align::Center);
			DrawTextIn(cv, { body.x + m.px(16), body.y + m.px(112), body.w - m.px(32), m.px(34) }, k.name, PC::LabelL, m.font(26), Align::Center);
			DrawTextIn(cv, { body.x + m.px(16), body.y + m.px(146), body.w - m.px(32), m.px(24) }, k.number, PC::DimL, m.font(16), Align::Center);
			const float bw = (body.w - m.px(32) - m.px(12)) * 0.5f, bh = m.px(46), by = body.y + m.px(186);
			const Rect b1{ body.x + m.px(16), by, bw, bh }, b2{ b1.right() + m.px(12), by, bw, bh };
			DrawFilledButton(cv, m, b1, "Ligar", PC::Green, Color(255, 255, 255), m_press == ID_CALL); m_hits.add(ID_CALL, b1);
			DrawFilledButton(cv, m, b2, "Mensagem", PC::Blue, Color(255, 255, 255), m_press == ID_MSG); m_hits.add(ID_MSG, b2);
			const float rowH = m.px(48), mx = m.px(16); float y = by + bh + m.px(22);
			const Rect r1{ body.x + mx, y, body.w - 2 * mx, rowH };
			RowStyle s1; s1.first = true; s1.last = true; s1.chevron = false; s1.pressed = m_press == ID_EMERG;
			DrawRow(cv, m, r1, "Contato de emergência", std::string(), s1); m_hits.add(ID_EMERG, r1);
			const Rect tg{ r1.right() - m.px(66), r1.y + (rowH - m.px(30)) * 0.5f, m.px(50), m.px(30) };
			cv.fillRect(tg, k.emergency ? PC::Green : Color(200, 200, 205), tg.h * 0.5f);
			cv.fillCircle({ k.emergency ? tg.right() - tg.h * 0.5f : tg.x + tg.h * 0.5f, tg.y + tg.h * 0.5f }, tg.h * 0.43f, Color(255, 255, 255));
			y += rowH + m.px(22);
			const Rect r2{ body.x + mx, y, body.w - 2 * mx, rowH };
			RowStyle s2; s2.first = true; s2.last = true; s2.chevron = false; s2.pressed = m_press == ID_DEL;
			DrawRow(cv, m, r2, std::string(), std::string(), s2);
			DrawTextIn(cv, r2, m_confirm ? "Confirmar exclusão" : "Excluir contato", PC::Red, m.font(16), Align::Center); m_hits.add(ID_DEL, r2);
		}

		void drawEdit(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, m_edit >= 0 ? "Editar" : "Novo contato", "Cancelar", "Salvar", true, m_press == ID_BACK, m_press == ID_ACTION);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_ACTION, nv.action);
			const float rowH = m.px(50), mx = m.px(16); float y = body.y + m.px(14);
			const Rect r1{ body.x + mx, y, body.w - 2 * mx, rowH }, r2{ body.x + mx, y + rowH, body.w - 2 * mx, rowH };
			RowStyle a; a.first = true; RowStyle b; b.last = true;
			DrawField(cv, m, r1, "Nome", m_name, "Nome do contato", m_focus == 0, m_c->nowMs, a); m_hits.add(ID_F_NAME, r1);
			DrawField(cv, m, r2, "Número", m_number, "Telefone", m_focus == 1, m_c->nowMs, b); m_hits.add(ID_F_NUM, r2);
			if (!m_err.empty()) DrawTextIn(cv, { body.x + mx + m.px(8), r2.bottom() + m.px(10), body.w - 2 * mx, m.px(24) }, m_err, PC::Red, m.font(14));
			drawKeyboard(cv, m);
		}

		std::vector<Data::Contact> m_list;
		int m_screen = 0, m_sel = -1, m_edit = -1, m_focus = -1; bool m_confirm = false;
		std::string m_name, m_number, m_err; ScrollView m_scroll; InlineMsg m_msg;
	};

	// ================================================================================================ Notas
	class NotesApp : public KeyboardApp
	{
	public:
		enum { ID_BACK = 1, ID_ACTION, ID_DEL, ID_AREA, ID_ROW = 1000 };
		explicit NotesApp(PhoneContext* c) : KeyboardApp(c) {}
		void onCreate() override { reload(); }
		void onResume() override { reload(); }
		void onPaused() override { m_press = 0; m_confirm = false; leaveEditor(); }
		bool onBackApp() override { if (m_screen == 1) { leaveEditor(); return true; } return false; }

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			if (m_screen == 0) drawList(cv, m, content); else drawEditor(cv, m, content);
			m_msg.draw(cv, m, content, m_c->nowMs);
		}
		bool onTouch(const Touch& t, const Metrics&, const Rect&) override
		{
			if (kbTouch(t)) return true;
			m_scroll.onTouch(t);
			trackPress(t);
			if (t.kind != Touch::Tap) return true;
			const int id = hitAt(t.pos);
			if (m_screen == 0)
			{
				if (id == ID_ACTION) newNote();
				else if (id >= ID_ROW && id - ID_ROW < (int)m_notes.size()) { m_cur = id - ID_ROW; m_screen = 1; m_confirm = false; m_cacheW = -1; m_scroll = ScrollView(); }
			}
			else
			{
				if (id == ID_BACK) leaveEditor();
				else if (id == ID_ACTION) { if (m_kbOn) hideKeyboard(); else leaveEditor(); }
				else if (id == ID_DEL)
				{
					if (!m_confirm) { m_confirm = true; m_msg.set("Toque de novo para apagar", m_c->nowMs, false); }
					else { if (m_cur >= 0 && m_cur < (int)m_notes.size()) m_notes.erase(m_notes.begin() + m_cur); m_cur = -1; m_screen = 0; m_confirm = false; hideKeyboard(); save(); }
				}
				else if (id == ID_AREA) openKeyboard();
			}
			return true;
		}
		size_t count() const { return m_notes.size(); }
		int screen() const { return m_screen; }
		std::string currentText() const { return (m_cur >= 0 && m_cur < (int)m_notes.size()) ? m_notes[m_cur].text : std::string(); }

	protected:
		void onKeyEvent(const Keyboard::Event& e) override
		{
			if (m_screen != 1 || m_cur < 0 || m_cur >= (int)m_notes.size()) return;
			std::string& s = m_notes[m_cur].text;
			Keyboard::Apply(s, e, Data::kNoteMax, true, Keyboard::Mode::Text);
			m_kb.autoShift(s); m_cacheW = -1; m_scroll.jumpToEnd(); save();
		}

	private:
		void reload() { m_notes = Data::LoadNotes(*m_c); if (m_cur >= (int)m_notes.size()) { m_cur = -1; m_screen = 0; } }
		void save() { Data::SaveNotes(*m_c, m_notes); m_c->changed(); }
		void newNote()
		{
			if (m_notes.size() >= Data::kMaxNotes) { m_msg.set("Limite de notas atingido.", m_c->nowMs); return; }
			Data::Note n; n.id = Data::NextNoteId(m_notes);
			m_notes.insert(m_notes.begin(), n); m_cur = 0; m_screen = 1; m_confirm = false; m_cacheW = -1; m_scroll = ScrollView();
			openKeyboard();
		}
		void openKeyboard() { showKeyboard(Keyboard::Mode::Text, "retorno", currentText()); m_scroll.jumpToEnd(); }
		void leaveEditor()
		{
			hideKeyboard();
			if (m_screen == 1 && m_cur >= 0 && m_cur < (int)m_notes.size() && Text::Trim(m_notes[m_cur].text).empty()) { m_notes.erase(m_notes.begin() + m_cur); save(); }   // nota vazia nao fica
			else if (m_screen == 1 && m_cur > 0 && m_cur < (int)m_notes.size())
			{   // a nota editada sobe para o topo
				const Data::Note n = m_notes[m_cur]; m_notes.erase(m_notes.begin() + m_cur); m_notes.insert(m_notes.begin(), n); save();
			}
			m_cur = -1; m_screen = 0; m_confirm = false;
		}

		void drawList(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "Notas", "", "Nova", true, false, m_press == ID_ACTION);
			beginFrame(nv.body); m_nav.add(ID_ACTION, nv.action);
			const Rect body = nv.body;
			if (m_notes.empty())
			{
				DrawTextIn(cv, { body.x, body.center().y - m.px(30), body.w, m.px(28) }, "Nenhuma nota", PC::Gray, m.font(19), Align::Center);
				DrawTextIn(cv, { body.x + m.px(20), body.center().y, body.w - m.px(40), m.px(24) }, "Toque em Nova para escrever.", PC::Gray, m.font(14), Align::Center);
				return;
			}
			const float rowH = m.px(62), mx = m.px(16);
			m_scroll.setLayout(body, m.px(16) + m_notes.size() * rowH);
			cv.pushClip(body);
			for (size_t i = 0; i < m_notes.size(); ++i)
			{
				const Rect r = m_scroll.itemRect(m.px(8) + i * rowH, rowH); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
				if (!m_scroll.visible(rr)) continue;
				RowStyle st; st.first = i == 0; st.last = i + 1 == m_notes.size(); st.chevron = false; st.pressed = m_press == ID_ROW + (int)i;
				DrawRow(cv, m, rr, std::string(), std::string(), st);
				const std::string t1 = Data::FirstLine(m_notes[i].text), t2 = Data::SecondLine(m_notes[i].text);
				DrawTextIn(cv, { rr.x + m.px(16), rr.y + m.px(8), rr.w - m.px(32), m.px(22) }, t1.empty() ? "Nova nota" : t1, PC::LabelL, m.font(16));
				DrawTextIn(cv, { rr.x + m.px(16), rr.y + m.px(31), rr.w - m.px(32), m.px(20) }, t2.empty() ? "Sem texto adicional" : t2, PC::Gray, m.font(13));
				m_hits.add(ID_ROW + (int)i, rr);
			}
			cv.popClip();
		}

		void drawEditor(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, "", "Notas", m_kbOn ? "Concluir" : "", true, m_press == ID_BACK, m_press == ID_ACTION);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); if (m_kbOn) m_nav.add(ID_ACTION, nv.action);
			if (m_cur < 0 || m_cur >= (int)m_notes.size()) { m_screen = 0; return; }
			const float fs = m.font(17), lineH = m.px(25), pad = m.px(18);
			const Rect area = body;
			const float btnH = m_kbOn ? 0.0f : m.px(56);
			const Rect view{ area.x, area.y, area.w, area.h - btnH };
			if (m_cacheW != view.w) { m_lines = Text::Wrap(cv, m_notes[m_cur].text, fs, view.w - 2 * pad); m_cacheW = view.w; }
			m_scroll.setLayout(view, m_lines.size() * lineH + pad * 2 + m.px(24));
			m_hits.add(ID_AREA, view);
			cv.pushClip(view);
			float y = pad;
			for (size_t i = 0; i < m_lines.size(); ++i, y += lineH)
			{
				const Rect r = m_scroll.itemRect(y, lineH);
				if (!m_scroll.visible(r)) continue;
				DrawTextIn(cv, { view.x + pad, r.y, view.w - 2 * pad, lineH }, m_lines[i], PC::LabelL, fs);
			}
			if (m_kbOn && ((m_c->nowMs / 500) % 2 == 0) && !m_lines.empty())
			{
				const std::string& last = m_lines.back();
				const Rect r = m_scroll.itemRect(pad + (m_lines.size() - 1) * lineH, lineH);
				cv.fillRect({ view.x + pad + cv.measure(last, fs).x + m.px(1), r.y + lineH * 0.5f - fs * 0.6f, std::max(1.5f, m.px(1.5f)), fs * 1.2f }, PC::Blue, 0);
			}
			if (m_lines.size() == 1 && m_lines[0].empty() && !m_kbOn) DrawTextIn(cv, { view.x + pad, view.y + pad, view.w - 2 * pad, lineH }, "Toque para escrever", PC::Gray, fs);
			cv.popClip();
			if (!m_kbOn)
			{
				const Rect b{ area.x + m.px(16), area.bottom() - btnH + m.px(6), area.w - m.px(32), m.px(44) };
				DrawFilledButton(cv, m, b, m_confirm ? "Confirmar" : "Apagar nota", PC::Red, Color(255, 255, 255), m_press == ID_DEL); m_hits.add(ID_DEL, b);
			}
			drawKeyboard(cv, m);
		}

		std::vector<Data::Note> m_notes; int m_screen = 0, m_cur = -1; bool m_confirm = false;
		ScrollView m_scroll; InlineMsg m_msg; std::vector<std::string> m_lines; float m_cacheW = -1;
	};

	// ================================================================================================ Calendario
	class CalendarApp : public KeyboardApp
	{
	public:
		enum { ID_BACK = 1, ID_ACTION, ID_PREV, ID_NEXT, ID_DEL, ID_F_TITLE, ID_F_TIME, ID_TODAY, ID_DAY = 100, ID_EV = 200 };
		explicit CalendarApp(PhoneContext* c) : KeyboardApp(c) {}
		void onCreate() override { reload(); goToday(); }
		void onResume() override { reload(); }
		void onPaused() override { m_press = 0; m_confirm = false; }
		bool onBackApp() override
		{
			if (m_screen == 2) { m_screen = 1; m_err.clear(); return true; }
			if (m_screen == 1) { m_screen = 0; return true; }
			return false;
		}

		void draw(Canvas& cv, const Metrics& m, const Rect& content) override
		{
			cv.fillRect(content, PC::Light, 0);
			if (m_screen == 0) drawMonth(cv, m, content); else if (m_screen == 1) drawDay(cv, m, content); else drawEdit(cv, m, content);
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
				if (id == ID_PREV) shiftMonth(-1);
				else if (id == ID_NEXT) shiftMonth(1);
				else if (id == ID_TODAY) goToday();
				else if (id >= ID_DAY && id < ID_DAY + 32) { m_day = id - ID_DAY; m_screen = 1; m_scroll = ScrollView(); }
			}
			else if (m_screen == 1)
			{
				if (id == ID_ACTION) startEdit(-1);
				else if (id >= ID_EV && id - ID_EV < (int)m_dayIdx.size()) startEdit(m_dayIdx[id - ID_EV]);
			}
			else
			{
				if (id == ID_F_TITLE) focus(0);
				else if (id == ID_F_TIME) focus(1);
				else if (id == ID_ACTION) save();
				else if (id == ID_DEL)
				{
					if (!m_confirm) { m_confirm = true; m_msg.set("Toque de novo para excluir", m_c->nowMs, false); }
					else if (m_edit >= 0 && m_edit < (int)m_events.size()) { m_events.erase(m_events.begin() + m_edit); commit(); hideKeyboard(); m_screen = 1; m_confirm = false; }
				}
			}
			return true;
		}
		int year() const { return m_y; } int month() const { return m_mo; } size_t count() const { return m_events.size(); }
		int screen() const { return m_screen; }

	protected:
		void onKeyEvent(const Keyboard::Event& e) override
		{
			if (m_screen != 2) return;
			if (m_focus == 0) { if (Keyboard::Apply(m_title, e, Data::kEventTitleMax, false, Keyboard::Mode::Text)) focus(1); else m_kb.autoShift(m_title); }
			else if (m_focus == 1) { if (Keyboard::Apply(m_time, e, 5, false, Keyboard::Mode::Time)) save(); }
			m_err.clear();
		}

	private:
		void reload() { m_events = Data::LoadEvents(*m_c); }
		void commit() { Data::SortEvents(m_events); Data::SaveEvents(*m_c, m_events); m_c->changed(); }
		StatusInfo now() const { return m_c->status ? m_c->status() : StatusInfo(); }
		void goToday()
		{
			const StatusInfo s = now();
			if (Data::ValidDate(s.year, s.month, std::max(1, s.day))) { m_y = s.year; m_mo = s.month; } else if (m_y == 0) { m_y = 2024; m_mo = 1; }
			m_screen = 0;
		}
		void shiftMonth(int d) { m_mo += d; if (m_mo < 1) { m_mo = 12; --m_y; } else if (m_mo > 12) { m_mo = 1; ++m_y; } if (m_y < 1900) { m_y = 1900; m_mo = 1; } if (m_y > 9999) { m_y = 9999; m_mo = 12; } }
		bool hasEvents(int day) const { const int k = Data::DateKey(m_y, m_mo, day); for (auto& e : m_events) if (e.date == k) return true; return false; }
		void startEdit(int idx)
		{
			m_edit = idx; m_screen = 2; m_err.clear(); m_confirm = false;
			m_title = idx >= 0 ? m_events[idx].title : std::string(); m_time = idx >= 0 ? m_events[idx].time : std::string();
			if (idx >= 0) { m_focus = -1; hideKeyboard(); } else focus(0);
		}
		void focus(int f)
		{
			m_focus = f;
			if (f == 0) showKeyboard(Keyboard::Mode::Text, "Próximo", m_title);
			else if (f == 1) showKeyboard(Keyboard::Mode::Time, "Salvar", m_time);
		}
		void save()
		{
			const std::string title = Text::Trim(m_title);
			if (title.empty()) { m_err = "Digite o título."; return; }
			if (!Data::ValidTime(m_time)) { m_err = "Hora inválida. Use HH:MM (ex.: 18:30)."; return; }
			if (m_edit < 0 && m_events.size() >= Data::kMaxEvents) { m_err = "Limite de eventos atingido."; return; }
			Data::Event e; e.date = Data::DateKey(m_y, m_mo, m_day); e.title = Data::Clip(title, Data::kEventTitleMax); e.time = m_time;
			if (m_edit >= 0) m_events[m_edit] = e; else m_events.push_back(e);
			commit(); hideKeyboard(); m_screen = 1;
		}

		void drawMonth(Canvas& cv, const Metrics& m, const Rect& content)
		{
			static const char* names[12] = { "Janeiro","Fevereiro","Março","Abril","Maio","Junho","Julho","Agosto","Setembro","Outubro","Novembro","Dezembro" };
			NavResult nv = DrawNavBar(cv, m, content, "Calendário", "", "Hoje", true, false, m_press == ID_TODAY);
			beginFrame(nv.body); m_nav.add(ID_TODAY, nv.action);
			const Rect body = nv.body; const float mx = m.px(14), cw = (body.w - 2 * mx) / 7.0f;
			const float hy = body.y + m.px(8);
			const Rect pv{ body.x + mx, hy, m.px(52), m.px(40) }, nx{ body.right() - mx - m.px(52), hy, m.px(52), m.px(40) };
			DrawTextIn(cv, { body.x + m.px(70), hy, body.w - m.px(140), m.px(40) }, std::string(names[m_mo - 1]) + " " + std::to_string(m_y), PC::LabelL, m.font(20), Align::Center);
			DrawTextIn(cv, pv, "<", m_press == ID_PREV ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(24), Align::Center); m_hits.add(ID_PREV, pv);
			DrawTextIn(cv, nx, ">", m_press == ID_NEXT ? PC::Blue.alpha(0.5f) : PC::Blue, m.font(24), Align::Center); m_hits.add(ID_NEXT, nx);
			static const char* wd[7] = { "D","S","T","Q","Q","S","S" };
			for (int i = 0; i < 7; ++i) DrawTextIn(cv, { body.x + mx + i * cw, hy + m.px(46), cw, m.px(22) }, wd[i], PC::Gray, m.font(13), Align::Center);
			const int first = Data::Weekday(m_y, m_mo, 1), dim = Data::DaysInMonth(m_y, m_mo);
			const StatusInfo td = now();
			const float ch = m.px(48), gy = hy + m.px(72);
			for (int d = 1; d <= dim; ++d)
			{
				const int idx = first + d - 1, col = idx % 7, row = idx / 7;
				const Rect r{ body.x + mx + col * cw, gy + row * ch, cw, ch };
				const bool today = td.day == d && td.month == m_mo && td.year == m_y;
				const float cr = std::min(cw, ch) * 0.38f;
				if (today) cv.fillCircle(r.center(), cr, PC::Red);
				else if (m_press == ID_DAY + d) cv.fillCircle(r.center(), cr, Color(210, 210, 216));
				DrawTextIn(cv, r, std::to_string(d), today ? Color(255, 255, 255) : PC::LabelL, m.font(17), Align::Center);
				if (hasEvents(d)) cv.fillCircle({ r.center().x, r.bottom() - m.px(5) }, m.px(2.6f), today ? Color(255, 255, 255) : PC::Gray);
				m_hits.add(ID_DAY + d, r);
			}
			// proximos eventos do mes
			float y = gy + 6 * ch + m.px(14);
			int n = 0;
			DrawTextIn(cv, { body.x + mx + m.px(4), y, body.w - 2 * mx, m.px(22) }, "EVENTOS DO MÊS", PC::DimL, m.font(12)); y += m.px(24);
			for (auto& e : m_events)
			{
				if (e.date / 100 != m_y * 100 + m_mo) continue;
				if (y + m.px(22) > body.bottom() || n >= 4) break;
				char b[24]; std::snprintf(b, sizeof(b), "%02d/%02d %s", e.date % 100, m_mo, e.time.c_str());
				DrawTextIn(cv, { body.x + mx + m.px(4), y, m.px(100), m.px(22) }, b, PC::Gray, m.font(13));
				DrawTextIn(cv, { body.x + mx + m.px(108), y, body.w - 2 * mx - m.px(112), m.px(22) }, e.title, PC::LabelL, m.font(14));
				y += m.px(24); ++n;
			}
			if (n == 0) DrawTextIn(cv, { body.x + mx + m.px(4), y, body.w - 2 * mx, m.px(22) }, "Nenhum evento. Toque em um dia.", PC::Gray, m.font(13));
		}

		void drawDay(Canvas& cv, const Metrics& m, const Rect& content)
		{
			char title[24]; std::snprintf(title, sizeof(title), "%02d/%02d/%04d", m_day, m_mo, m_y);
			NavResult nv = DrawNavBar(cv, m, content, title, "Calendário", "Novo", true, m_press == ID_BACK, m_press == ID_ACTION);
			beginFrame(nv.body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_ACTION, nv.action);
			const Rect body = nv.body;
			m_dayIdx.clear();
			const int key = Data::DateKey(m_y, m_mo, m_day);
			for (size_t i = 0; i < m_events.size(); ++i) if (m_events[i].date == key) m_dayIdx.push_back((int)i);
			if (m_dayIdx.empty()) { DrawTextIn(cv, { body.x, body.center().y - m.px(14), body.w, m.px(28) }, "Nenhum evento neste dia", PC::Gray, m.font(17), Align::Center); return; }
			const float rowH = m.px(54), mx = m.px(16);
			m_scroll.setLayout(body, m.px(16) + m_dayIdx.size() * rowH);
			cv.pushClip(body);
			for (size_t k = 0; k < m_dayIdx.size(); ++k)
			{
				const Data::Event& e = m_events[m_dayIdx[k]];
				const Rect r = m_scroll.itemRect(m.px(8) + k * rowH, rowH); const Rect rr{ body.x + mx, r.y, body.w - 2 * mx, rowH };
				if (!m_scroll.visible(rr)) continue;
				RowStyle st; st.first = k == 0; st.last = k + 1 == m_dayIdx.size(); st.pressed = m_press == ID_EV + (int)k;
				DrawRow(cv, m, rr, e.title, e.time.empty() ? "dia todo" : e.time, st);
				m_hits.add(ID_EV + (int)k, rr);
			}
			cv.popClip();
		}

		void drawEdit(Canvas& cv, const Metrics& m, const Rect& content)
		{
			NavResult nv = DrawNavBar(cv, m, content, m_edit >= 0 ? "Evento" : "Novo evento", "Cancelar", "Salvar", true, m_press == ID_BACK, m_press == ID_ACTION);
			Rect body = layoutKeyboard(m, nv.body);
			beginFrame(body); m_nav.add(ID_BACK, nv.back); m_nav.add(ID_ACTION, nv.action);
			const float rowH = m.px(50), mx = m.px(16); const float y = body.y + m.px(14);
			const Rect r1{ body.x + mx, y, body.w - 2 * mx, rowH }, r2{ body.x + mx, y + rowH, body.w - 2 * mx, rowH };
			RowStyle a; a.first = true; RowStyle b; b.last = true;
			DrawField(cv, m, r1, "Título", m_title, "Nome do evento", m_focus == 0, m_c->nowMs, a); m_hits.add(ID_F_TITLE, r1);
			DrawField(cv, m, r2, "Hora", m_time, "HH:MM (opcional)", m_focus == 1, m_c->nowMs, b); m_hits.add(ID_F_TIME, r2);
			char dt[24]; std::snprintf(dt, sizeof(dt), "Data: %02d/%02d/%04d", m_day, m_mo, m_y);
			DrawTextIn(cv, { body.x + mx + m.px(8), r2.bottom() + m.px(10), body.w - 2 * mx, m.px(22) }, dt, PC::Gray, m.font(14));
			if (!m_err.empty()) DrawTextIn(cv, { body.x + mx + m.px(8), r2.bottom() + m.px(34), body.w - 2 * mx, m.px(24) }, m_err, PC::Red, m.font(14));
			if (m_edit >= 0 && !m_kbOn)
			{
				const Rect bt{ body.x + mx, r2.bottom() + m.px(70), body.w - 2 * mx, m.px(44) };
				DrawFilledButton(cv, m, bt, m_confirm ? "Confirmar" : "Excluir evento", PC::Red, Color(255, 255, 255), m_press == ID_DEL); m_hits.add(ID_DEL, bt);
			}
			drawKeyboard(cv, m);
		}

		std::vector<Data::Event> m_events; std::vector<int> m_dayIdx;
		int m_screen = 0, m_y = 0, m_mo = 1, m_day = 1, m_edit = -1, m_focus = -1; bool m_confirm = false;
		std::string m_title, m_time, m_err; ScrollView m_scroll; InlineMsg m_msg;
	};
}
