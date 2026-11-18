#pragma once
// =====================================================================================
//  GERENCIADOR DE GRUPO (Maximo RP) - tela (abas Inicio / Membros / Farm / Cofre).
//  Logica pura: desenha so no Canvas (maximoos/Canvas.h) e conversa com o jogo por COMANDOS (Cmd) que o hospedeiro executa
//  (gui/samp_widgets/orgpanel.cpp): GET/POST no servico /o/* e RPC 246. Testada e visualizada no PC com o gravador de Canvas.
//  Layout de projeto 1366 x 768 (unidades "lu"); escala u = min(largura/1366, altura/768).
//  Quem decide o que pode e o SERVIDOR (permissoes por cargo no PostgreSQL): os botoes escurecidos so avisam; o servidor recusa de qualquer jeito.
// =====================================================================================
#include "OrgPanelModel.h"
#include "../maximoos/Canvas.h"
#include "../maximoos/Components.h"
#include "../phone/PhoneKeyboard.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace OrgPanel
{
	using namespace MaximoOS;

	// ---- comandos para o hospedeiro ----
	enum Tag { T_HOME = 1, T_MEMBERS, T_FARM, T_BANK, T_RANKS, T_NOTICE, T_RANKNAME, T_RANKPERM };
	struct Cmd
	{
		enum Kind { Get, Post, RpcMember, RpcDeposit, RpcWithdraw, RpcFarm, RpcGps, RpcClose, Copy, Close } kind = Get;
		int tag = 0; std::string path, body; int a = 0, b = 0, c = 0;
	};
	enum Tab { TAB_HOME = 0, TAB_MEMBERS, TAB_FARM, TAB_BANK };

	namespace Pal
	{
		const Color BgTop(22, 42, 78), BgBot(11, 22, 46);
		const Color Card(150, 185, 235, 34), CardLine(195, 218, 255, 78), Inner(10, 22, 50, 70), Row(160, 190, 235, 34), RowHi(200, 225, 255, 62);
		const Color Text(240, 246, 255), Dim(168, 188, 220), Faint(120, 144, 184);
		const Color Cyan(64, 232, 216), Pink(255, 96, 172), Green(76, 226, 150), Beige(214, 186, 172), Blue(120, 170, 255), Danger(255, 92, 110);
		const Color Dark(20, 34, 62);
	}

	class Panel
	{
	public:
		// ================= estado publico simples =================
		bool isOpen() const { return m_open; }
		std::vector<Cmd>& cmds() { return m_cmds; }
		const Home& home() const { return m_home; }

		void open(const std::string& token, int64_t now)
		{
			*this = Panel();
			m_open = true; m_token = token; m_t0 = now; m_tabFade = 0.0f;
			get(T_HOME, "/o/home"); m_homeLoad = Load{ false, true };
		}
		void close(bool notifyServer = true)
		{
			if (!m_open) return;
			m_open = false;
			if (notifyServer) { Cmd c; c.kind = Cmd::RpcClose; m_cmds.push_back(c); }
		}
		// Voltar do Android: fecha modal -> fecha teclado -> fecha o painel. true = tratado
		bool back()
		{
			if (!m_open) return false;
			if (m_modal != M_NONE) { closeModal(); return true; }
			if (m_focus != F_NONE) { m_focus = F_NONE; return true; }
			close();
			return true;
		}
		// servidor -> cliente (RPC 245 op 17): uma acao terminou; recarrega
		void onServerDone(int what, bool ok)
		{
			(void)what; (void)ok;
			if (!m_open) return;
			refresh(true);
		}

		// resposta HTTP (status 0 = falha de rede)
		void onHttp(int tag, int status, const std::string& body)
		{
			if (!m_open) return;
			std::vector<std::string> L; std::string code; const bool ok = status == 200 && Split(body, L, code);
			if (status != 200 && code.empty()) code = "net";
			switch (tag)
			{
			case T_HOME:
				m_homeLoad.loading = false;
				if (ok && ParseHome(L, m_home)) { m_homeLoad.loaded = true; m_homeLoad.failed = false; syncNotice(); }
				else { m_homeLoad.failed = !m_homeLoad.loaded; fail(code); }
				break;
			case T_MEMBERS: m_memLoad.loading = false; if (ok) { ParseMembers(L, m_members); m_memLoad.loaded = true; m_memLoad.failed = false; } else { m_memLoad.failed = !m_memLoad.loaded; fail(code); } break;
			case T_FARM:    m_farmLoad.loading = false; if (ok) { ParseFarm(L, m_farm, m_hist); m_farmLoad.loaded = true; m_farmLoad.failed = false; } else { m_farmLoad.failed = !m_farmLoad.loaded; fail(code); } break;
			case T_BANK:    m_bankLoad.loading = false; if (ok) { ParseBank(L, m_balance, m_canWithdraw, m_tx); m_bankLoad.loaded = true; m_bankLoad.failed = false; } else { m_bankLoad.failed = !m_bankLoad.loaded; fail(code); } break;
			case T_RANKS:   if (ok) { ParseRanks(L, m_ranks); if (m_roleSel < 0 && !m_ranks.empty()) selectRole(std::min<int>(m_home.myRank, (int)m_ranks.size() - 1)); } else fail(code); break;
			case T_NOTICE:  if (ok) { toast("Mural atualizado.", false); m_noticeDirty = false; get(T_HOME, "/o/home"); } else fail(code); break;
			case T_RANKNAME: if (ok) { toast("Nome do cargo salvo.", false); refreshRanks(); get(T_HOME, "/o/home"); get(T_MEMBERS, "/o/members"); } else fail(code); break;
			case T_RANKPERM: if (ok) { toast("Permissões do cargo salvas.", false); refreshRanks(); get(T_HOME, "/o/home"); } else fail(code); break;
			default: break;
			}
		}

		void onServerToast(const std::string& s, bool bad) { if (m_open) toast(s, bad); }     // RPC 245 op 18: a mesma mensagem que o servidor mostraria no canto
		void toast(const std::string& s, bool bad) { m_toast = s; m_toastBad = bad; m_toastAt = m_now; m_toastA = std::max(m_toastA, 0.001f); }

		// ================= quadro =================
		// touches: toques ja reconhecidos (GestureTracker) neste quadro. Desenha e processa.
		void frame(Canvas& cv, float sw, float sh, const std::vector<Touch>& touches, int64_t now, float dtMs)
		{
			if (!m_open) return;
			m_now = now; m_dt = std::clamp(dtMs, 1.0f, 100.0f);
			m_sw = sw; m_sh = sh;
			m_u = std::min(sw / 1366.0f, sh / 768.0f);
			m_ox = (sw - 1366.0f * m_u) * 0.5f; m_oy = (sh - 768.0f * m_u) * 0.5f;
			animate();
			for (const Touch& t : touches) onTouch(t);
			// teclado
			if (m_kbShow) { const int n = m_kb.update(m_dt); for (int i = 0; i < n; ++i) typeEvent(Phone::Keyboard::Event{ Phone::Keyboard::Event::Back, "" }); }
			draw(cv);
		}

	private:
		// ---------- dados ----------
		struct Load { bool loaded = false, loading = false, failed = false; };
		bool m_open = false; std::string m_token; int64_t m_t0 = 0, m_now = 0; float m_dt = 16;
		Home m_home; std::vector<Member> m_members; std::vector<FarmRow> m_farm; std::vector<FarmHist> m_hist; std::vector<Tx> m_tx; std::vector<Rank> m_ranks;
		long long m_balance = 0; bool m_canWithdraw = false;
		Load m_homeLoad, m_memLoad, m_farmLoad, m_bankLoad;
		std::vector<Cmd> m_cmds;
		int m_tab = TAB_HOME;
		// ---------- campos de texto ----------
		enum Fld { F_NONE = -1, F_SEARCH_M = 0, F_SEARCH_F, F_SEARCH_B, F_RG, F_AMOUNT, F_NOTICE, F_ROLE, F_COUNT };
		struct Field { std::string s; size_t max = 24; bool multi = false, digits = false; };
		Field m_f[F_COUNT] = { { "", 24, false, false }, { "", 24, false, false }, { "", 24, false, false }, { "", 10, false, true }, { "", 10, false, true }, { "", 400, true, false }, { "", 24, false, false } };
		int m_focus = F_NONE; Phone::Keyboard m_kb; bool m_kbShow = false; Rect m_kbRect; float m_kbShift = 0, m_kbShiftTarget = 0; Metrics m_km;
		bool m_noticeDirty = false;
		// ---------- modais ----------
		enum Modal { M_NONE = 0, M_ROLE, M_KICK, M_ROLES };
		int m_modal = M_NONE, m_modalArg = -1; float m_modalA = 0; int m_roleSel = -1; int m_rolePerms = 0;
		// ---------- animacao ----------
		float m_tabPos = 0, m_tabFade = 1, m_toastA = 0; std::string m_toast; bool m_toastBad = false; int64_t m_toastAt = 0;
		float m_press = 0; int m_pressId = -1, m_pressArg = -1;
		int64_t m_lastScroll = 0; float m_barA = 0;
		// ---------- layout / toques ----------
		float m_sw = 1366, m_sh = 768, m_u = 1, m_ox = 0, m_oy = 0;
		struct Hit { Rect r; int id = 0, arg = 0; bool enabled = true; };
		std::vector<Hit> m_hits; int m_down = -1;
		ScrollView m_svMem, m_svFarm, m_svHist, m_svBank, m_svRoles, m_svNotice; ScrollView* m_activeSv = nullptr; std::vector<ScrollView*> m_svs;
		enum Id {
			H_NONE = 0, H_TAB, H_CLOSE, H_COPY, H_RADIO, H_GPS, H_DELIVER, H_SEEALL, H_NOTICE, H_NOTICE_SAVE, H_FIELD, H_RECRUIT, H_ROLES, H_CREATE, H_MEM_ROLE, H_MEM_KICK,
			H_DEPOSIT, H_WITHDRAW, H_RETRY, H_MODAL_BG, H_M_PROMOTE, H_M_DEMOTE, H_M_CANCEL, H_M_KICK_YES, H_R_SEL, H_R_PERM, H_R_SAVENAME, H_R_CLOSE
		};

		// ================= helpers de rede =================
		void get(int tag, const char* path)
		{
			Cmd c; c.kind = Cmd::Get; c.tag = tag; c.path = std::string(path) + "?t=" + m_token; m_cmds.push_back(c);
		}
		void post(int tag, const std::string& pathAndQuery, const std::string& body)
		{
			Cmd c; c.kind = Cmd::Post; c.tag = tag; c.path = pathAndQuery + (pathAndQuery.find('?') == std::string::npos ? "?t=" : "&t=") + m_token; c.body = body; m_cmds.push_back(c);
		}
		void refreshRanks() { get(T_RANKS, "/o/ranks"); }
		void loadTab(int tab, bool force)
		{
			Load* l = tab == TAB_HOME ? &m_homeLoad : tab == TAB_MEMBERS ? &m_memLoad : tab == TAB_FARM ? &m_farmLoad : &m_bankLoad;
			if (l->loading && !force) return;
			l->loading = true;
			get(tab == TAB_HOME ? T_HOME : tab == TAB_MEMBERS ? T_MEMBERS : tab == TAB_FARM ? T_FARM : T_BANK, tab == TAB_HOME ? "/o/home" : tab == TAB_MEMBERS ? "/o/members" : tab == TAB_FARM ? "/o/farm" : "/o/bank");
		}
		void refresh(bool all)
		{
			loadTab(m_tab, true);
			if (all && m_tab != TAB_HOME) { get(T_HOME, "/o/home"); }
			if (m_modal == M_ROLES) refreshRanks();
		}
		void fail(const std::string& code)
		{
			std::string m;
			if (code == "noperm") m = "Seu cargo não tem permissão para isso.";
			else if (code == "higher") m = "Você só pode mexer em quem tem cargo abaixo do seu.";
			else if (code == "self") m = "Você não pode fazer isso consigo mesmo.";
			else if (code == "isleader") m = "Não é possível mexer em um Líder.";
			else if (code == "notmember") m = "Esse jogador não é do seu grupo.";
			else if (code == "limit") m = "Limite de cargo atingido.";
			else if (code == "nofunds") m = "O cofre não tem esse valor.";
			else if (code == "args") m = "Valor inválido.";
			else if (code == "auth") { m = "Sessão expirada. Abra o painel de novo (/grupo)."; close(); }
			else if (code == "nomember") { m = "Você não faz parte de nenhum grupo."; close(); }
			else m = "Sem conexão com o servidor. Tente de novo.";
			toast(m, true);
		}
		bool has(int perm) const { return (m_home.perms & perm) != 0; }
		void syncNotice() { if (!m_noticeDirty && m_focus != F_NOTICE) m_f[F_NOTICE].s = m_home.notice; }
		static std::string Lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
		static bool Match(const std::string& name, int acct, const std::string& q)
		{
			if (q.empty()) return true;
			const std::string n = Lower(name), l = Lower(q);
			return n.find(l) != std::string::npos || std::to_string(acct).find(l) != std::string::npos;
		}
		void selectRole(int idx)
		{
			if (idx < 0 || idx >= (int)m_ranks.size()) return;
			m_roleSel = idx; m_rolePerms = m_ranks[idx].perms; m_f[F_ROLE].s = m_ranks[idx].name;
		}
		void closeModal() { m_modal = M_NONE; if (m_focus == F_ROLE) m_focus = F_NONE; }

		// ================= animacao =================
		static float Ease(float cur, float target, float dtMs, float tau) { return cur + (target - cur) * (1.0f - std::exp(-dtMs / tau)); }
		void animate()
		{
			m_tabPos = Ease(m_tabPos, (float)m_tab, m_dt, 70.0f);
			m_tabFade = Ease(m_tabFade, 1.0f, m_dt, 90.0f);
			m_modalA = Ease(m_modalA, m_modal != M_NONE ? 1.0f : 0.0f, m_dt, 70.0f);
			m_press = Ease(m_press, m_down >= 0 ? 1.0f : 0.0f, m_dt, 45.0f);
			m_kbShift = Ease(m_kbShift, m_kbShiftTarget, m_dt, 80.0f);
			const bool recent = m_now - m_lastScroll < 900;
			m_barA = Ease(m_barA, recent ? 1.0f : 0.0f, m_dt, recent ? 60.0f : 220.0f);
			if (!m_toast.empty()) { const bool on = m_now - m_toastAt < 2800; m_toastA = Ease(m_toastA, on ? 1.0f : 0.0f, m_dt, 90.0f); if (!on && m_toastA < 0.02f) { m_toast.clear(); m_toastA = 0; } }
			for (ScrollView* s : { &m_svMem, &m_svFarm, &m_svHist, &m_svBank, &m_svRoles, &m_svNotice }) s->update(m_dt);
		}

		// ================= toques =================
		int hitAt(Vec2 p) const { for (int i = (int)m_hits.size() - 1; i >= 0; --i) if (m_hits[i].r.contains(p)) return i; return -1; }
		void onTouch(const Touch& t)
		{
			// teclado: o toque que comecou nele fica com ele
			if (m_kbShow && (m_kbOwn || (t.kind == Touch::Down && m_kbRect.contains(t.pos))))
			{
				if (t.kind == Touch::Down) m_kbOwn = true;
				const Phone::Keyboard::Event ev = m_kb.onTouch(t);
				if (ev.kind != Phone::Keyboard::Event::None) typeEvent(ev);
				if (t.kind == Touch::Up || t.kind == Touch::Cancel) m_kbOwn = false;
				return;
			}
			if (t.kind == Touch::Down)
			{
				m_down = hitAt(t.pos);
				m_activeSv = nullptr;
				for (ScrollView* s : m_svs) if (s->viewport().contains(t.pos)) { s->onTouch(t); m_activeSv = s; break; }
				return;
			}
			if (t.kind == Touch::DragBegin || t.kind == Touch::Drag || t.kind == Touch::DragEnd)
			{
				if (t.kind == Touch::DragBegin) m_down = -1;
				if (m_activeSv) { m_activeSv->onTouch(t); m_lastScroll = m_now; }
				return;
			}
			if (t.kind == Touch::Cancel || t.kind == Touch::Up) { if (m_activeSv) m_activeSv->onTouch(t); if (t.kind == Touch::Cancel) m_down = -1; return; }
			if (t.kind == Touch::Tap)
			{
				const int h = hitAt(t.pos);
				const int d = m_down; m_down = -1;
				if (h >= 0 && h == d)
				{
					Hit hit = m_hits[h]; m_pressId = hit.id; m_pressArg = hit.arg; m_press = 1.0f;
					activate(hit);
				}
				else if (h < 0 && m_focus != F_NONE && !m_kbRect.contains(t.pos)) { m_focus = F_NONE; }
			}
		}
		bool m_kbOwn = false;

		void typeEvent(const Phone::Keyboard::Event& ev)
		{
			if (m_focus == F_NONE) return;
			Field& f = m_f[m_focus];
			const bool enter = Phone::Keyboard::Apply(f.s, ev, f.max, f.multi);
			if (f.digits) { std::string d; for (char c : f.s) if (c >= '0' && c <= '9') d += c; f.s = d; }
			if (m_focus == F_NOTICE) m_noticeDirty = true;
			if (enter) m_focus = F_NONE;
			else if (!f.multi && f.s.size() == 0 && ev.kind == Phone::Keyboard::Event::Char) { /* tecla ignorada (filtro de digitos) */ }
			if (m_focus != F_NONE && !f.digits) m_kb.autoShift(f.s);
		}
		void focus(int f)
		{
			m_focus = f;
			if (f == F_NONE) return;
			m_kb.reset(); m_kb.setMode(m_f[f].digits ? Phone::Keyboard::Mode::Phone : Phone::Keyboard::Mode::Text);
			m_kb.setReturnLabel(m_f[f].multi ? "Quebra" : "OK");
			if (!m_f[f].digits) m_kb.autoShift(m_f[f].s);
		}

		// ================= acoes (toque confirmado) =================
		void activate(const Hit& h)
		{
			if (!h.enabled && h.id != H_FIELD) { denied(h.id); return; }
			switch (h.id)
			{
			case H_TAB: if (h.arg != m_tab) { m_tab = h.arg; m_tabFade = 0.0f; m_focus = F_NONE; loadTab(m_tab, true); } break;
			case H_CLOSE: close(); break;
			case H_COPY:
				if (m_home.discord.empty()) toast("O Discord deste grupo ainda não foi configurado.", true);
				else { Cmd c; c.kind = Cmd::Copy; c.body = m_home.discord; m_cmds.push_back(c); toast("Link do Discord copiado.", false); }
				break;
			case H_RADIO:
				if (m_home.radio.empty()) toast("O rádio deste grupo ainda não foi configurado.", true);
				else toast("Frequência do rádio do grupo: " + m_home.radio, false);
				break;
			case H_GPS: { Cmd c; c.kind = Cmd::RpcGps; m_cmds.push_back(c); } break;
			case H_DELIVER: { Cmd c; c.kind = Cmd::RpcFarm; c.a = 0; m_cmds.push_back(c); } break;
			case H_SEEALL: m_tab = TAB_FARM; m_tabFade = 0.0f; loadTab(m_tab, true); break;
			case H_NOTICE: if (has(P_NOTICE)) { focus(F_NOTICE); } else toast("Só quem tem permissão edita o mural.", true); break;
			case H_NOTICE_SAVE: post(T_NOTICE, "/o/notice", m_f[F_NOTICE].s); m_focus = F_NONE; break;
			case H_FIELD: focus(h.arg); break;
			case H_RECRUIT:
			{
				const int id = m_f[F_RG].s.empty() ? -1 : std::atoi(m_f[F_RG].s.c_str());
				if (id < 0 || id > 999) { toast("Digite o ID do jogador (0 a 999) que está no servidor.", true); break; }
				Cmd c; c.kind = Cmd::RpcMember; c.a = 0; c.b = 0; c.c = id; m_cmds.push_back(c); m_f[F_RG].s.clear(); m_focus = F_NONE;
				break;
			}
			case H_ROLES: m_modal = M_ROLES; m_roleSel = -1; if (!m_ranks.empty()) selectRole(std::min<int>(m_home.myRank, (int)m_ranks.size() - 1)); refreshRanks(); break;
			case H_CREATE: toast("Criar cargos novos ainda não existe: o grupo tem 7 cargos fixos (use Gerenciar cargos para editar).", true); break;
			case H_MEM_ROLE: if (h.arg >= 0 && h.arg < (int)m_members.size()) { m_modal = M_ROLE; m_modalArg = h.arg; } break;
			case H_MEM_KICK: if (h.arg >= 0 && h.arg < (int)m_members.size()) { m_modal = M_KICK; m_modalArg = h.arg; } break;
			case H_DEPOSIT: case H_WITHDRAW:
			{
				const long long v = m_f[F_AMOUNT].s.empty() ? 0 : std::atoll(m_f[F_AMOUNT].s.c_str());
				if (v < 1) { toast("Digite um valor.", true); break; }
				Cmd c; c.kind = h.id == H_DEPOSIT ? Cmd::RpcDeposit : Cmd::RpcWithdraw; c.a = (int)std::min<long long>(v, 100000000); m_cmds.push_back(c);
				m_f[F_AMOUNT].s.clear(); m_focus = F_NONE;
				break;
			}
			case H_RETRY: refresh(false); break;
			case H_M_CANCEL: case H_MODAL_BG: case H_R_CLOSE: closeModal(); break;
			case H_M_PROMOTE: case H_M_DEMOTE: case H_M_KICK_YES:
				if (m_modalArg >= 0 && m_modalArg < (int)m_members.size())
				{
					Cmd c; c.kind = Cmd::RpcMember; c.a = h.id == H_M_PROMOTE ? 1 : h.id == H_M_DEMOTE ? 2 : 3; c.b = m_members[m_modalArg].acct; m_cmds.push_back(c);
				}
				closeModal(); break;
			case H_R_SEL: selectRole(h.arg); m_focus = F_NONE; break;
			case H_R_PERM:
				if (m_roleSel >= 0 && m_roleSel < (int)m_ranks.size() && m_ranks[m_roleSel].rank != 1)
				{
					m_rolePerms ^= h.arg;
					post(T_RANKPERM, "/o/rankperm?r=" + std::to_string(m_ranks[m_roleSel].rank) + "&p=" + std::to_string(m_rolePerms), "");
				}
				break;
			case H_R_SAVENAME:
				if (m_roleSel >= 0 && m_roleSel < (int)m_ranks.size() && !m_f[F_ROLE].s.empty())
					post(T_RANKNAME, "/o/rankname?r=" + std::to_string(m_ranks[m_roleSel].rank) + "&n=" + UrlEnc(m_f[F_ROLE].s), "");
				m_focus = F_NONE; break;
			default: break;
			}
		}
		void denied(int id)
		{
			switch (id)
			{
			case H_RECRUIT: toast("Seu cargo não tem permissão para recrutar.", true); break;
			case H_ROLES: toast("Seu cargo não tem permissão para gerenciar cargos.", true); break;
			case H_MEM_ROLE: toast("Você só promove/rebaixa quem tem cargo abaixo do seu (e precisa da permissão).", true); break;
			case H_MEM_KICK: toast("Você só expulsa quem tem cargo abaixo do seu (e precisa da permissão).", true); break;
			case H_WITHDRAW: toast("Seu cargo não tem permissão para sacar do cofre.", true); break;
			case H_DELIVER: toast("O farm deste grupo ainda não foi configurado.", true); break;
			case H_GPS: toast("A sede deste grupo ainda não tem GPS definido.", true); break;
			default: break;
			}
		}

	public:
		// ================= desenho =================
		// (nomes em unidades de projeto: X/Y/W/H "lu" -> pixels)
	private:
		Rect R(float x, float y, float w, float h) const { return { m_ox + x * m_u, m_oy + y * m_u, w * m_u, h * m_u }; }
		float S(float lu) const { return lu * m_u; }
		float FS(float lu) const { return std::max(11.0f, lu * m_u); }
		void T(Canvas& cv, float x, float y, float w, float h, const std::string& s, float size, Color c, Align al = Align::Left, int font = FontText) const { DrawTextAs(cv, font, R(x, y, w, h), s, c, FS(size), al); }
		void addHit(const Rect& r, int id, int arg = 0, bool enabled = true, const Rect* clip = nullptr)
		{
			Hit h; h.r = r; if (clip) { const float x0 = std::max(r.x, clip->x), y0 = std::max(r.y, clip->y), x1 = std::min(r.right(), clip->right()), y1 = std::min(r.bottom(), clip->bottom()); if (x1 <= x0 || y1 <= y0) return; h.r = { x0, y0, x1 - x0, y1 - y0 }; }
			h.id = id; h.arg = arg; h.enabled = enabled; m_hits.push_back(h);
		}
		bool pressed(int id, int arg) const { return m_down >= 0 && m_down < (int)m_hits.size() && m_hits[m_down].id == id && m_hits[m_down].arg == arg; }

		// ---- icones (primitivas) ----
		void IcoHome(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.5f, h * 0.16f);
			cv.fillTriangle({ m.x, m.y - h }, { m.x - h, m.y - h * 0.05f }, { m.x + h, m.y - h * 0.05f }, c);
			cv.fillRect({ m.x - h * 0.72f, m.y - h * 0.1f, h * 1.44f, h * 1.05f }, c, t);
			cv.fillRect({ m.x - h * 0.2f, m.y + h * 0.3f, h * 0.4f, h * 0.65f }, Pal::Dark.alpha(c.a / 255.0f), 0);
		}
		void IcoPerson(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f;
			cv.fillCircle({ m.x, m.y - h * 0.38f }, h * 0.36f, c);
			cv.fillRect({ m.x - h * 0.62f, m.y + h * 0.1f, h * 1.24f, h * 0.8f }, c, h * 0.5f);
		}
		void IcoPeople(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f;
			IcoPerson(cv, { m.x - h * 0.15f, m.y - h * 0.9f, h * 1.5f, h * 1.8f }, c);
			IcoPerson(cv, { m.x - h * 1.05f, m.y - h * 0.7f, h * 1.2f, h * 1.5f }, c.alpha(0.6f));
		}
		void IcoTools(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.6f, h * 0.2f);
			cv.line({ m.x - h * 0.8f, m.y - h * 0.8f }, { m.x + h * 0.8f, m.y + h * 0.8f }, c, t);
			cv.line({ m.x + h * 0.8f, m.y - h * 0.8f }, { m.x - h * 0.8f, m.y + h * 0.8f }, c, t);
			cv.fillCircle({ m.x - h * 0.8f, m.y - h * 0.8f }, t * 0.9f, c); cv.fillCircle({ m.x + h * 0.8f, m.y - h * 0.8f }, t * 0.9f, c);
		}
		void IcoVault(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.5f, h * 0.16f);
			cv.strokeRect({ m.x - h, m.y - h, h * 2, h * 2 }, c, h * 0.4f, t);
			cv.strokeCircle(m, h * 0.5f, c, t); cv.fillCircle(m, h * 0.16f, c);
		}
		void IcoSearch(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.6f, h * 0.18f);
			cv.strokeCircle({ m.x - h * 0.15f, m.y - h * 0.15f }, h * 0.62f, c, t);
			cv.line({ m.x + h * 0.3f, m.y + h * 0.3f }, { m.x + h * 0.9f, m.y + h * 0.9f }, c, t);
		}
		void IcoCrown(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f;
			cv.fillTriangle({ m.x - h, m.y + h * 0.6f }, { m.x - h * 0.9f, m.y - h * 0.7f }, { m.x - h * 0.2f, m.y + h * 0.2f }, c);
			cv.fillTriangle({ m.x - h * 0.4f, m.y + h * 0.3f }, { m.x, m.y - h * 0.9f }, { m.x + h * 0.4f, m.y + h * 0.3f }, c);
			cv.fillTriangle({ m.x + h, m.y + h * 0.6f }, { m.x + h * 0.9f, m.y - h * 0.7f }, { m.x + h * 0.2f, m.y + h * 0.2f }, c);
			cv.fillRect({ m.x - h, m.y + h * 0.3f, h * 2, h * 0.45f }, c, 1);
		}
		void IcoTrophy(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.3f, h * 0.14f);
			cv.fillRect({ m.x - h * 0.55f, m.y - h * 0.9f, h * 1.1f, h * 1.1f }, c, h * 0.45f);
			cv.fillRect({ m.x - h * 0.55f, m.y - h * 0.9f, h * 1.1f, h * 0.5f }, c, 0);
			cv.strokeCircle({ m.x - h * 0.7f, m.y - h * 0.4f }, h * 0.3f, c, t); cv.strokeCircle({ m.x + h * 0.7f, m.y - h * 0.4f }, h * 0.3f, c, t);
			cv.fillRect({ m.x - h * 0.12f, m.y + h * 0.15f, h * 0.24f, h * 0.45f }, c, 0);
			cv.fillRect({ m.x - h * 0.5f, m.y + h * 0.6f, h, h * 0.3f }, c, 1);
		}
		void IcoX(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f * 0.55f, t = std::max(1.6f, h * 0.35f);
			cv.line({ m.x - h, m.y - h }, { m.x + h, m.y + h }, c, t); cv.line({ m.x + h, m.y - h }, { m.x - h, m.y + h }, c, t);
		}
		void IcoCheck(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f * 0.6f, t = std::max(1.6f, h * 0.35f);
			cv.line({ m.x - h, m.y }, { m.x - h * 0.25f, m.y + h * 0.7f }, c, t); cv.line({ m.x - h * 0.25f, m.y + h * 0.7f }, { m.x + h, m.y - h * 0.7f }, c, t);
		}
		void IcoClock(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f * 0.62f, t = std::max(1.3f, h * 0.22f);
			cv.strokeCircle(m, h, c, t); cv.line(m, { m.x, m.y - h * 0.6f }, c, t); cv.line(m, { m.x + h * 0.45f, m.y + h * 0.2f }, c, t);
		}
		void IcoDash(Canvas& cv, const Rect& r, Color c) const { const Vec2 m = r.center(); cv.line({ m.x - r.w * 0.18f, m.y }, { m.x + r.w * 0.18f, m.y }, c, std::max(1.4f, r.h * 0.1f)); }
		void IcoMinusCircle(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.4f, h * 0.12f);
			cv.strokeCircle(m, h * 0.92f, c, t); cv.line({ m.x - h * 0.45f, m.y }, { m.x + h * 0.45f, m.y }, c, t);
		}
		void IcoToggle(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float w = r.w * 0.5f, h = r.h * 0.2f;
			cv.fillRect({ m.x - w, m.y - h, w * 2, h * 2 }, c.alpha(0.5f), h);
			cv.fillCircle({ m.x + w - h, m.y }, h * 1.3f, c); cv.fillCircle({ m.x + w - h, m.y }, h * 0.5f, Pal::Dark);
		}
		void IcoUserPlus(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.4f, h * 0.18f);
			IcoPerson(cv, { m.x - h * 0.3f - h * 0.8f, m.y - h * 0.8f, h * 1.6f, h * 1.6f }, c);
			cv.line({ m.x + h * 0.55f, m.y - h * 0.1f }, { m.x + h * 1.15f, m.y - h * 0.1f }, c, t); cv.line({ m.x + h * 0.85f, m.y - h * 0.4f }, { m.x + h * 0.85f, m.y + h * 0.2f }, c, t);
		}
		void IcoExit(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.5f, h * 0.17f);
			cv.strokeRect({ m.x - h, m.y - h * 0.9f, h * 1.5f, h * 1.8f }, c, h * 0.3f, t);
			cv.line({ m.x - h * 0.1f, m.y }, { m.x + h, m.y }, c, t); cv.fillTriangle({ m.x + h * 1.25f, m.y }, { m.x + h * 0.6f, m.y - h * 0.5f }, { m.x + h * 0.6f, m.y + h * 0.5f }, c);
		}
		void IcoNotes(Canvas& cv, const Rect& r, Color c) const
		{
			const Vec2 m = r.center(); const float h = std::min(r.w, r.h) * 0.5f, t = std::max(1.4f, h * 0.16f);
			for (int i = 0; i < 3; ++i) cv.line({ m.x - h * 0.9f, m.y - h * 0.6f + i * h * 0.6f }, { m.x + h * (i == 1 ? 0.5f : 0.9f), m.y - h * 0.6f + i * h * 0.6f }, c, t);
		}

		// ---- blocos ----
		void Card(Canvas& cv, float x, float y, float w, float h, const char* title)
		{
			cv.fillRect(R(x, y, w, h), Pal::Card, S(22)); cv.strokeRect(R(x, y, w, h), Pal::CardLine, S(22), std::max(1.0f, S(1.4f)));
			if (title && title[0])
			{
				T(cv, x + 14, y + 12, w - 28, 28, title, 19, Pal::Text, Align::Left, FontDisplayBold);
				cv.fillRect(R(x + 1, y + 48, w - 2, 1), Color(200, 225, 255, 50), 0);
			}
		}
		// botao em formato de pilula. retorna o retangulo (para o toque)
		void Pill(Canvas& cv, float x, float y, float w, float h, const std::string& label, int id, int arg, bool enabled, float size = 12.5f, bool primary = false, Color tint = Pal::Text)
		{
			Rect r = R(x, y, w, h);
			const bool pr = pressed(id, arg);
			const float k = pr ? 0.95f : 1.0f; const Vec2 c = r.center(); r = { c.x - r.w * 0.5f * k, c.y - r.h * 0.5f * k, r.w * k, r.h * k };
			Color fill = primary ? Color(255, 255, 255, 235) : Color(255, 255, 255, pr ? 70 : 34);
			cv.fillRect(r, enabled ? fill : fill.alpha(0.5f), r.h * 0.5f);
			cv.strokeRect(r, Color(220, 235, 255, enabled ? 140 : 70), r.h * 0.5f, std::max(1.0f, S(1.2f)));
			DrawTextAs(cv, FontTextSemibold, r, label, primary ? Pal::Dark : (enabled ? tint : Pal::Faint), FS(size), Align::Center);
			addHit(R(x, y, w, h), id, arg, true);
			m_hits.back().enabled = enabled;
		}
		void FieldBox(Canvas& cv, float x, float y, float w, float h, int f, const char* hint, bool search)
		{
			const Rect r = R(x, y, w, h); const bool foc = m_focus == f;
			cv.fillRect(r, foc ? Color(255, 255, 255, 46) : Color(255, 255, 255, 26), r.h * 0.5f);
			cv.strokeRect(r, foc ? Color(160, 205, 255, 220) : Color(220, 235, 255, 120), r.h * 0.5f, std::max(1.0f, S(foc ? 1.8f : 1.2f)));
			std::string show = m_f[f].s;
			Color col = Pal::Text;
			if (show.empty()) { show = hint; col = Pal::Dim; }
			else if (foc && ((m_now / 450) & 1)) show += "|";
			const float pad = S(16); Rect tr = { r.x + pad, r.y, r.w - pad * 2 - (search ? S(30) : 0), r.h };
			DrawTextAs(cv, FontText, tr, show, col, FS(13.5f), Align::Left);
			if (search) IcoSearch(cv, { r.right() - S(34), r.y + r.h * 0.5f - S(10), S(20), S(20) }, Pal::Text);
			addHit(r, H_FIELD, f, true);
			if (foc) { m_focusRect = r; }
		}
		Rect m_focusRect;

		void drawBackground(Canvas& cv)
		{
			cv.fillGradientV({ 0, 0, m_sw, m_sh }, Pal::BgTop.alpha(0.97f), Pal::BgBot.alpha(0.98f));
			// luzes suaves (efeito de desfoque do original, sem shader)
			cv.fillCircle({ m_ox + S(250), m_oy + S(120) }, S(360), Color(90, 150, 230, 16));
			cv.fillCircle({ m_ox + S(1150), m_oy + S(640) }, S(420), Color(70, 120, 210, 14));
		}

		void draw(Canvas& cv)
		{
			m_hits.clear(); m_svs.clear();
			m_kbShow = m_focus != F_NONE;
			// teclado e deslocamento do conteudo
			if (m_kbShow)
			{
				m_km = Metrics(); m_km.s = m_sh / 600.0f; m_km.valid = true;
				const float kh = m_kb.height(m_km); m_kbRect = { 0, m_sh - kh, m_sw, kh };
				const float bottom = m_focusRect.bottom() + m_kbShift + S(14);       // m_focusRect veio do quadro anterior, ja deslocado
				m_kbShiftTarget = std::max(0.0f, bottom - m_kbRect.y);
			}
			else { m_kbShiftTarget = 0; m_kbRect = {}; }
			drawBackground(cv);
			const float shift = m_kbShift;
			const float baseOy = m_oy; m_oy -= shift;
			drawHeader(cv);
			drawTabs(cv);
			cv.setAlpha(m_tabFade);
			const float slide = (1.0f - m_tabFade) * S(14); const float oy2 = m_oy; m_oy += slide;
			if (!m_homeLoad.loaded) drawLoading(cv, m_homeLoad.failed);
			else if (m_tab == TAB_HOME) drawHome(cv);
			else if (m_tab == TAB_MEMBERS) drawMembers(cv);
			else if (m_tab == TAB_FARM) drawFarm(cv);
			else drawBank(cv);
			m_oy = oy2;
			cv.setAlpha(1.0f);
			if (m_modalA > 0.01f) drawModal(cv);
			m_oy = baseOy;
			if (m_kbShow) m_kb.draw(cv, m_km, m_kbRect);
			drawToast(cv);
		}

		void drawLoading(Canvas& cv, bool failed)
		{
			const Rect r = R(32, 175, 1302, 490);
			if (!failed)
			{
				T(cv, 32, 380, 1302, 30, "Carregando o gerenciador...", 17, Pal::Dim, Align::Center);
				const float a = (float)(m_now % 1200) / 1200.0f * 6.2831853f; const Vec2 c = { r.x + r.w * 0.5f, m_oy + S(350) };
				for (int i = 0; i < 8; ++i) { const float an = a + i * 0.785f; cv.fillCircle({ c.x + std::cos(an) * S(20), c.y + std::sin(an) * S(20) }, S(3.5f), Pal::Text.alpha(0.15f + 0.1f * i)); }
			}
			else
			{
				T(cv, 32, 360, 1302, 30, "Não foi possível carregar o gerenciador.", 18, Pal::Danger, Align::Center);
				Pill(cv, 603, 405, 160, 36, "Tentar de novo", H_RETRY, 0, true, 14);
			}
		}

		void drawHeader(Canvas& cv)
		{
			// titulo + nome do grupo: a largura do texto manda (a fonte do jogo e a do PC tem larguras diferentes)
			const float fs = FS(30);
			cv.setFont(FontDisplayBold);
			const float w1 = cv.measure("GERENCIADOR DE GRUPO", fs).x / m_u;
			cv.setFont(FontText);
			T(cv, 32, 30, w1 + 8, 54, "GERENCIADOR DE GRUPO", 30, Pal::Text, Align::Left, FontDisplayBold);
			const float sepX = 32 + w1 + 22;
			cv.fillRect(R(sepX, 36, 2, 44), Color(220, 235, 255, 110), 0);
			T(cv, sepX + 14, 30, std::max(100.0f, 950.0f - sepX - 14), 54, m_homeLoad.loaded ? upper(m_home.org) : "", 30, Pal::Text, Align::Left, FontDisplayBold);
			cv.fillRect(R(32, 100, 1302, 1.4f), Color(220, 235, 255, 70), 0);
			// pilula de resumo: [pessoas] 16 / 60  [cofre] R$ ...  [Fechar]  - largura pelo texto (de trás para frente a partir de 1334)
			const std::string cnt = m_homeLoad.loaded ? std::to_string(m_home.members) + " / " + std::to_string(m_home.maxMembers) : "-";
			const std::string mon = m_homeLoad.loaded ? Money(m_home.bank) : "-";
			cv.setFont(FontTextSemibold);
			const float cw = cv.measure(cnt, FS(14)).x / m_u + 4, mw = cv.measure(mon, FS(14)).x / m_u + 4, bw = cv.measure("Fechar", FS(14)).x / m_u + 4;
			cv.setFont(FontText);
			const float btnW = 52 + bw, total = 16 + 22 + 8 + cw + 18 + 20 + 8 + mw + 14 + btnW + 4;
			const float px = 1334 - total;
			const Rect p = R(px, 36, total, 48);
			cv.fillRect(p, Color(255, 255, 255, 28), p.h * 0.5f); cv.strokeRect(p, Color(220, 235, 255, 110), p.h * 0.5f, std::max(1.0f, S(1.2f)));
			float x = px + 16;
			IcoPeople(cv, R(x, 49, 22, 22), Pal::Text); x += 30;
			T(cv, x, 40, cw, 40, cnt, 14, Pal::Text, Align::Left, FontTextSemibold); x += cw + 18;
			IcoVault(cv, R(x, 50, 20, 20), Pal::Cyan); x += 28;
			T(cv, x, 40, mw, 40, mon, 14, Pal::Cyan, Align::Left, FontTextSemibold); x += mw + 14;
			// "Sair do grupo" do original = sair da tela; aqui: Fechar (deixar o grupo nao existe no servidor)
			const bool pr = pressed(H_CLOSE, 0);
			const Rect b = R(x, 40, btnW, 40);
			cv.fillRect(b, Color(255, 255, 255, pr ? 80 : 40), b.h * 0.5f); cv.strokeRect(b, Color(220, 235, 255, 150), b.h * 0.5f, std::max(1.0f, S(1.2f)));
			IcoExit(cv, R(x + 14, 49, 22, 22), Pal::Text);
			T(cv, x + 44, 40, bw + 4, 40, "Fechar", 14, Pal::Text, Align::Left, FontTextSemibold);
			addHit(b, H_CLOSE, 0);
		}
		static std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 32; return s; }

		void drawTabs(Canvas& cv)
		{
			struct TabDef { const char* label; int icon; } defs[4] = { { "Início", 0 }, { "Membros", 1 }, { "Farm", 2 }, { "Cofre", 3 } };
			float xs[4], ws[4]; float x = 25;
			cv.setFont(FontTextSemibold);
			for (int i = 0; i < 4; ++i) { ws[i] = 46 + cv.measure(defs[i].label, FS(14)).x / std::max(0.01f, m_u); xs[i] = x; x += ws[i] + 14; }
			cv.setFont(FontText);
			// pilula animada
			const int a = std::clamp((int)std::floor(m_tabPos), 0, 3), b = std::min(3, a + 1); const float f = std::clamp(m_tabPos - a, 0.0f, 1.0f);
			const float px = xs[a] + (xs[b] - xs[a]) * f, pw = ws[a] + (ws[b] - ws[a]) * f;
			cv.fillRect(R(px, 125, pw, 32), Color(255, 255, 255, 240), S(16));
			for (int i = 0; i < 4; ++i)
			{
				const float act = std::clamp(1.0f - std::fabs(m_tabPos - i), 0.0f, 1.0f);
				const Color col = Color::mix(Pal::Text, Pal::Dark, act);
				if (act < 0.98f) { cv.strokeRect(R(xs[i], 125, ws[i], 32), Color(220, 235, 255, (int)(130 * (1.0f - act))), S(16), std::max(1.0f, S(1.2f))); }
				const Rect ic = R(xs[i] + 13, 133, 17, 17);
				if (i == 0) IcoHome(cv, ic, col); else if (i == 1) IcoPeople(cv, ic, col); else if (i == 2) IcoTools(cv, ic, col); else IcoVault(cv, ic, col);
				T(cv, xs[i] + 36, 125, ws[i] - 40, 32, defs[i].label, 14, col, Align::Left, FontTextSemibold);
				addHit(R(xs[i], 125, ws[i], 32), H_TAB, i, true);
			}
		}

		// ---------------- INICIO ----------------
		void drawHome(Canvas& cv)
		{
			const Home& h = m_home;
			// --- opcoes + mural ---
			Card(cv, 32, 175, 389, 499, "OPÇÕES DO GRUPO");
			T(cv, 20, 237, 120, 18, "Discord do grupo", 11.5f, Pal::Dim, Align::Center);
			T(cv, 140, 237, 120, 18, "Rádio do grupo", 11.5f, Pal::Dim, Align::Center);
			T(cv, 296, 237, 100, 18, "Localização", 11.5f, Pal::Dim, Align::Center);
			Pill(cv, 45, 259, 70, 25, "Copiar", H_COPY, 0, true);
			Pill(cv, 157, 259, 86, 25, "Conectar", H_RADIO, 0, true);
			Pill(cv, 284, 259, 123, 25, "Marcar no GPS", H_GPS, 0, h.hasGps);
			cv.fillRect(R(33, 359, 387, 1), Color(200, 225, 255, 50), 0);
			IcoNotes(cv, R(48, 376, 18, 18), Pal::Text);
			T(cv, 74, 372, 250, 26, "Mural de avisos", 15, Pal::Text, Align::Left, FontTextSemibold);
			const Rect box = R(46, 404, 361, 250);
			cv.fillRect(box, Color(10, 22, 50, 70), S(16)); cv.strokeRect(box, m_focus == F_NOTICE ? Color(160, 205, 255, 220) : Color(220, 235, 255, 100), S(16), std::max(1.0f, S(1.2f)));
			{
				const std::string txt = m_f[F_NOTICE].s; const Rect inner = box.inset(S(14), S(12));
				cv.pushClip(inner);
				float y = inner.y; const float lh = FS(12.5f) * 1.35f;
				std::vector<std::string> lines; wrap(cv, txt.empty() && !m_noticeDirty ? std::string(has(P_NOTICE) ? "Toque aqui para escrever o aviso do grupo." : "Nenhum aviso por enquanto.") : txt + (m_focus == F_NOTICE && ((m_now / 450) & 1) ? "|" : ""), FS(12.5f), inner.w, lines);
				const Color col = txt.empty() && !m_noticeDirty ? Pal::Faint : Pal::Text;
				for (const std::string& l : lines) { cv.text({ inner.x, y }, col, l, FS(12.5f)); y += lh; }
				cv.popClip();
				m_focusRect = m_focus == F_NOTICE ? box : m_focusRect;
			}
			addHit(box, H_NOTICE, 0, true); m_hits.back().enabled = true;
			if (m_noticeDirty && has(P_NOTICE)) Pill(cv, 322, 606, 78, 26, "Salvar", H_NOTICE_SAVE, 0, true, 12.5f, true);
			// --- entregar + lideres ---
			Card(cv, 437, 175, 389, 183, "ENTREGAR");
			cv.fillRect(R(450, 231, 362, 113), Pal::Inner, S(18)); cv.strokeRect(R(450, 231, 362, 113), Color(220, 235, 255, 80), S(18), std::max(1.0f, S(1.1f)));
			cv.setFont(FontTextSemibold);
			const std::string unit = Money(h.farmPrice) + " / Unidade"; const float uw = cv.measure(unit, FS(16.5f)).x / m_u + 6;
			cv.setFont(FontText);
			T(cv, 464, 244, uw, 30, unit, 16.5f, Pal::Text, Align::Left, FontTextSemibold);
			Pill(cv, 464 + uw + 12, 247, 70, 22, "Entregar", H_DELIVER, 0, h.farmOn, 11.5f);
			cv.fillRect(R(464, 288, 334, 1), Color(200, 225, 255, 50), 0);
			T(cv, 464, 296, 120, 20, "Meta diária", 11.5f, Pal::Dim);
			T(cv, 584, 296, 214, 20, std::to_string(h.farmToday) + "/" + std::to_string(h.farmGoal), 11.5f, Pal::Text, Align::Right);
			{
				const Rect bar = R(464, 323, 334, 8); cv.fillRect(bar, Color(255, 255, 255, 36), bar.h * 0.5f);
				const float pct = h.farmGoal > 0 ? std::clamp((float)h.farmToday / (float)h.farmGoal, 0.0f, 1.0f) : 0.0f;
				if (pct > 0.0f) cv.fillRect({ bar.x, bar.y, std::max(bar.h, bar.w * pct), bar.h }, pct >= 1.0f ? Pal::Green : Pal::Cyan, bar.h * 0.5f);
			}
			Card(cv, 437, 373, 389, 301, "LÍDERES DA ORGANIZAÇÃO");
			for (int i = 0; i < 6; ++i)
			{
				const float y = 437 + i * 37.5f; const Rect r = R(450, y, 362, 29);
				cv.fillRect(r, i == 0 ? Color(15, 28, 58, 140) : Pal::Row, r.h * 0.5f);
				if (i < (int)h.leaders.size())
				{
					const Leader& l = h.leaders[i];
					if (i == 0) IcoCrown(cv, R(468, y + 7, 15, 15), Pal::Text); else IcoPerson(cv, R(468, y + 7, 15, 15), Pal::Text);
					T(cv, 494, y, 300, 29, std::to_string(l.acct) + " | " + l.name, 13, Pal::Text);
					if (l.online) cv.fillCircle({ r.right() - S(18), r.y + r.h * 0.5f }, S(4), Pal::Green);
				}
			}
			// --- ranking ---
			Card(cv, 842, 175, 389, 499, "RANKING");
			cv.fillRect(R(855, 231, 362, 163), Pal::Inner, S(18)); cv.strokeRect(R(855, 231, 362, 163), Color(220, 235, 255, 80), S(18), std::max(1.0f, S(1.1f)));
			T(cv, 869, 244, 200, 24, "Entrega de farm", 14.5f, Pal::Text, Align::Left, FontTextSemibold);
			Pill(cv, 1135, 244, 68, 24, "Ver todos", H_SEEALL, 0, true, 11.5f);
			{
				const Rect r = R(869, 282, 334, 27); cv.fillRect(r, Pal::Row, r.h * 0.5f);
				IcoTrophy(cv, R(884, 287, 16, 16), Pal::Text);
				T(cv, 910, 282, 280, 27, (h.farmPos > 0 ? std::to_string(h.farmPos) + "\xC2\xB0 | " : std::string("-")) + h.me.name, 12.5f, Pal::Text);
			}
			cv.fillRect(R(869, 322, 334, 1), Color(200, 225, 255, 50), 0);
			T(cv, 869, 328, 250, 18, "Seu desempenho semanal", 11.5f, Pal::Dim);
			static const char* dn[7] = { "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom" };
			for (int i = 0; i < 7; ++i)
			{
				const float x = 869 + i * 49.0f; const Rect r = R(x, 361, 44, 21); const int st = h.week[i];
				Color fill = st == 1 ? Color(76, 226, 150, 200) : st == 2 ? Color(255, 96, 172, 215) : st == 3 ? Color(214, 186, 172, 230) : Color(255, 255, 255, 22);
				cv.fillRect(r, fill, r.h * 0.5f); cv.strokeRect(r, Color(220, 235, 255, st ? 40 : 120), r.h * 0.5f, std::max(1.0f, S(1.0f)));
				DrawTextAs(cv, FontTextSemibold, r, dn[i], st ? Pal::Dark : Pal::Text, FS(11), Align::Center);
			}
			cv.fillRect(R(855, 410, 362, 251), Pal::Inner, S(18)); cv.strokeRect(R(855, 410, 362, 251), Color(220, 235, 255, 80), S(18), std::max(1.0f, S(1.1f)));
			T(cv, 869, 421, 250, 24, "Tempo jogado", 14.5f, Pal::Text, Align::Left, FontTextSemibold);
			for (int i = 0; i < 5; ++i)
			{
				const float y = 456 + i * 30.0f; const Rect r = R(869, y, 334, 25); cv.fillRect(r, Pal::Row, r.h * 0.5f);
				if (i < (int)h.top.size())
				{
					const TopTime& t = h.top[i];
					T(cv, 881, y, 40, 25, std::to_string(t.pos) + "\xC2\xBA", 11.5f, Pal::Dim);
					T(cv, 915, y, 190, 25, t.name + " #" + std::to_string(t.acct), 11.5f, Pal::Text);
					T(cv, 1050, y, 142, 25, DaysHours(t.secs), 11.5f, Pal::Text, Align::Right);
				}
			}
			{
				const float y = 620; const Rect r = R(869, y, 334, 25); cv.fillRect(r, Color(15, 28, 58, 150), r.h * 0.5f);
				T(cv, 881, y, 40, 25, std::to_string(h.me.pos) + "\xC2\xBA", 11.5f, Pal::Dim);
				T(cv, 915, y, 190, 25, h.me.name + " #" + std::to_string(h.me.acct), 11.5f, Pal::Text);
				T(cv, 1050, y, 142, 25, DaysHours(h.me.secs), 11.5f, Pal::Text, Align::Right);
			}
		}

		// quebra de linha por largura (UTF-8; respeita \n)
		static void wrap(Canvas& cv, const std::string& text, float size, float maxW, std::vector<std::string>& out)
		{
			size_t i = 0;
			while (i <= text.size())
			{
				size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size();
				std::string para = text.substr(i, e - i), line;
				size_t p = 0;
				while (p < para.size())
				{
					size_t q = para.find(' ', p); if (q == std::string::npos) q = para.size();
					const std::string word = para.substr(p, q - p);
					const std::string cand = line.empty() ? word : line + " " + word;
					if (cv.measure(cand, size).x <= maxW || line.empty()) line = cand; else { out.push_back(line); line = word; }
					p = q + 1;
				}
				out.push_back(line);
				i = e + 1;
			}
		}

		// ---------------- MEMBROS ----------------
		bool canAct(const Member& m, int perm) const { return has(perm) && m.rank > m_home.myRank && m.acct != m_home.acct; }
		void drawMembers(Canvas& cv)
		{
			FieldBox(cv, 32, 172, 873, 46, F_SEARCH_M, "Pesquisar nome", true);
			// adicionar membro
			const Rect p = R(921, 172, 414, 46); cv.fillRect(p, Color(255, 255, 255, 26), p.h * 0.5f); cv.strokeRect(p, Color(220, 235, 255, 120), p.h * 0.5f, std::max(1.0f, S(1.2f)));
			IcoUserPlus(cv, R(937, 184, 22, 22), Pal::Text);
			T(cv, 966, 172, 156, 46, "Adicionar novo membro", 11.5f, Pal::Text, Align::Left, FontTextSemibold);
			FieldBox(cv, 1128, 182, 84, 26, F_RG, "ID", false);
			Pill(cv, 1220, 182, 106, 26, "Recrutar", H_RECRUIT, 0, has(P_INVITE), 12.5f);
			Pill(cv, 920, 236, 156, 32, "Gerenciar cargos", H_ROLES, 0, has(P_ROLES), 13);
			Pill(cv, 1083, 236, 121, 32, "Criar cargo", H_CREATE, 0, true, 13);
			// tabela
			Card(cv, 32, 236, 874, 429, nullptr);
			const char* heads[6] = { "Nome", "Status", "Tempo jogado", "Entrada", "Último Login", "Cargo" };
			const float hx[6] = { 47, 214, 306, 423, 548, 738 };
			const float hw[6] = { 150, 70, 100, 90, 160, 110 };
			for (int i = 0; i < 6; ++i) T(cv, hx[i], 246, hw[i], 24, heads[i], 11, Pal::Dim, i == 0 ? Align::Left : Align::Center);
			cv.fillRect(R(33, 278, 872, 1), Color(200, 225, 255, 40), 0);
			std::vector<int> idx;
			for (int i = 0; i < (int)m_members.size(); ++i) if (Match(m_members[i].name, m_members[i].acct, m_f[F_SEARCH_M].s)) idx.push_back(i);
			const Rect vp = R(33, 280, 872, 383); const float pitch = 48.0f * m_u;
			m_svMem.setLayout(vp, pitch * idx.size()); m_svs.push_back(&m_svMem);
			cv.pushClip(vp);
			if (!m_memLoad.loaded) T(cv, 33, 440, 872, 30, m_memLoad.failed ? "Não foi possível carregar os membros." : "Carregando...", 14, Pal::Dim, Align::Center);
			else if (idx.empty()) T(cv, 33, 440, 872, 30, "Nenhum membro encontrado.", 14, Pal::Dim, Align::Center);
			for (size_t k = 0; k < idx.size(); ++k)
			{
				const Member& m = m_members[idx[k]]; const Rect row = m_svMem.itemRect(pitch * k, pitch);
				if (!m_svMem.visible(row)) continue;
				const float ry = (row.y - m_oy) / m_u;                       // y em lu
				if (k % 2 == 0) cv.fillRect({ row.x, row.y, row.w, row.h }, Color(160, 190, 235, 22), 0);
				T(cv, hx[0], ry, 170, 48, std::to_string(m.acct) + " | " + m.name, 11.5f, Pal::Text, Align::Left, FontTextSemibold);
				T(cv, hx[1], ry, hw[1], 48, m.online ? "ONLINE" : "OFFLINE", 11, m.online ? Pal::Green : Pal::Pink, Align::Center, FontTextSemibold);
				T(cv, hx[2], ry, hw[2], 48, HoursMin(m.secs), 11, Pal::Text, Align::Center);
				T(cv, hx[3], ry, hw[3], 48, m.joined, 11, Pal::Text, Align::Center);
				T(cv, hx[4], ry, hw[4], 48, m.last.empty() ? "-" : m.last, 11, Pal::Text, Align::Center);
				T(cv, hx[5], ry, hw[5], 48, m.rankName, 11, Pal::Text, Align::Center);
				const bool a1 = canAct(m, P_RANK), a2 = canAct(m, P_KICK);
				const Rect b1 = { m_ox + S(836), row.y + (row.h - S(24)) * 0.5f, S(30), S(24) }, b2 = { m_ox + S(870), row.y + (row.h - S(24)) * 0.5f, S(24), S(24) };
				IcoToggle(cv, b1, a1 ? Pal::Blue : Pal::Faint); IcoMinusCircle(cv, b2, a2 ? Pal::Text : Pal::Faint);
				addHit(b1.inset(-S(4), -S(10)), H_MEM_ROLE, idx[k], a1, &vp); addHit(b2.inset(-S(4), -S(10)), H_MEM_KICK, idx[k], a2, &vp);
			}
			cv.popClip();
			drawScrollbar(cv, m_svMem);
		}

		void drawScrollbar(Canvas& cv, const ScrollView& sv)
		{
			if (sv.maxOffset() <= 1.0f || m_barA < 0.02f) return;
			const Rect vp = sv.viewport(); const float total = vp.h + sv.maxOffset();
			const float th = std::max(S(30), vp.h * vp.h / total); const float ty = vp.y + (vp.h - th) * (sv.offset() / sv.maxOffset());
			cv.fillRect({ vp.right() - S(7), ty, S(3.5f), th }, Color(235, 245, 255, (int)(190 * m_barA)), S(2));
		}

		// ---------------- FARM ----------------
		void drawDayCell(Canvas& cv, const Rect& r, int st)
		{
			Color fill = st == 1 ? Color(76, 226, 150, 190) : st == 2 ? Color(255, 96, 172, 215) : st == 3 ? Color(214, 186, 172, 235) : Color(255, 255, 255, 24);
			cv.fillRect(r, fill, r.h * 0.5f); cv.strokeRect(r, Color(220, 235, 255, st ? 60 : 120), r.h * 0.5f, std::max(1.0f, S(1.0f)));
			if (st == 1) IcoCheck(cv, r, Pal::Dark); else if (st == 2) IcoX(cv, r, Color(255, 255, 255, 235)); else if (st == 3) IcoClock(cv, r, Pal::Dark); else IcoDash(cv, r, Pal::Text.alpha(0.8f));
		}
		void drawFarm(Canvas& cv)
		{
			FieldBox(cv, 32, 172, 873, 46, F_SEARCH_F, "Pesquisar nome", true);
			Card(cv, 32, 236, 874, 429, nullptr);
			T(cv, 45, 248, 70, 26, "Posição", 11, Pal::Dim); T(cv, 138, 248, 100, 26, "Nome", 11, Pal::Dim);
			T(cv, 280, 245, 70, 14, "Quantidade", 9.5f, Pal::Dim, Align::Center); T(cv, 280, 258, 70, 14, "Diária", 9.5f, Pal::Dim, Align::Center); T(cv, 349, 245, 70, 14, "Quantidade", 9.5f, Pal::Dim, Align::Center); T(cv, 349, 258, 70, 14, "Semanal", 9.5f, Pal::Dim, Align::Center);
			static const char* dn[7] = { "SEG", "TER", "QUA", "QUI", "SEX", "SÁB", "DOM" };
			for (int d = 0; d < 7; ++d) T(cv, 425 + d * 64 - 0, 248, 56, 26, dn[d], 10.5f, Pal::Dim, Align::Center, FontTextMedium);
			cv.fillRect(R(33, 278, 872, 1), Color(200, 225, 255, 40), 0);
			std::vector<int> idx;
			for (int i = 0; i < (int)m_farm.size(); ++i) if (Match(m_farm[i].name, m_farm[i].acct, m_f[F_SEARCH_F].s)) idx.push_back(i);
			const Rect vp = R(33, 280, 872, 383); const float pitch = 55.5f * m_u;
			m_svFarm.setLayout(vp, pitch * idx.size()); m_svs.push_back(&m_svFarm);
			cv.pushClip(vp);
			if (!m_farmLoad.loaded) T(cv, 33, 440, 872, 30, m_farmLoad.failed ? "Não foi possível carregar o farm." : "Carregando...", 14, Pal::Dim, Align::Center);
			else if (idx.empty()) T(cv, 33, 440, 872, 30, "Nenhum membro encontrado.", 14, Pal::Dim, Align::Center);
			for (size_t k = 0; k < idx.size(); ++k)
			{
				const FarmRow& f = m_farm[idx[k]]; const Rect row = m_svFarm.itemRect(pitch * k, pitch);
				if (!m_svFarm.visible(row)) continue;
				const float ry = (row.y - m_oy) / m_u;
				if (k % 2 == 0) cv.fillRect(row, Color(160, 190, 235, 22), 0);
				if (f.pos <= 3) IcoTrophy(cv, R(46, ry + 19, 17, 17), f.pos == 1 ? Color(255, 205, 70) : f.pos == 2 ? Color(205, 215, 230) : Color(255, 130, 200));
				T(cv, 70, ry, 50, pitch / m_u, std::to_string(f.pos) + "\xC2\xB0", 11, Pal::Text, Align::Left, FontTextSemibold);
				T(cv, 138, ry, 146, pitch / m_u, std::to_string(f.acct) + " | " + f.name, 10.5f, Pal::Text, Align::Left, FontTextSemibold);
				for (int c = 0; c < 2; ++c)
				{
					const Rect pr = R(287 + c * 69, ry + 19, 58, 18); cv.fillRect(pr, Color(255, 255, 255, 28), pr.h * 0.5f); cv.strokeRect(pr, Color(220, 235, 255, 120), pr.h * 0.5f, std::max(1.0f, S(1.0f)));
					DrawTextAs(cv, FontTextSemibold, pr, Dots(c == 0 ? f.day : f.week), Pal::Text, FS(10.5f), Align::Center);
				}
				for (int d = 0; d < 7; ++d) drawDayCell(cv, R(425 + d * 64 + 0, ry + 18, 58, 20), f.st[d]);
			}
			cv.popClip();
			drawScrollbar(cv, m_svFarm);
			// historico
			Card(cv, 923, 172, 391, 493, "HISTÓRICO FARM");
			T(cv, 953, 232, 90, 20, "Data", 11, Pal::Dim); T(cv, 1065, 232, 110, 20, "Player", 11, Pal::Dim); T(cv, 1190, 232, 100, 20, "Qnt Itens", 11, Pal::Dim, Align::Right);
			const Rect vp2 = R(924, 256, 389, 407); const float p2 = 34.0f * m_u;
			m_svHist.setLayout(vp2, p2 * m_hist.size()); m_svs.push_back(&m_svHist);
			cv.pushClip(vp2);
			if (m_hist.empty()) T(cv, 924, 420, 389, 26, m_farmLoad.loaded ? "Nenhuma entrega registrada." : "", 12, Pal::Faint, Align::Center);
			for (size_t k = 0; k < m_hist.size(); ++k)
			{
				const FarmHist& y = m_hist[k]; const Rect row = m_svHist.itemRect(p2 * k, p2);
				if (!m_svHist.visible(row)) continue;
				const float ry = (row.y - m_oy) / m_u;
				T(cv, 940, ry, 120, 34, y.time.substr(0, 16), 10, Pal::Text);
				T(cv, 1065, ry, 130, 34, y.name, 10, Pal::Text);
				T(cv, 1190, ry, 100, 34, Dots(y.qty), 10.5f, Pal::Text, Align::Right, FontTextSemibold);
			}
			cv.popClip();
			drawScrollbar(cv, m_svHist);
		}

		// ---------------- COFRE ----------------
		void drawBank(Canvas& cv)
		{
			Card(cv, 32, 172, 711, 493, "COFRE DO GRUPO");
			// faixa "COFRE" (a arte do original nao existe: gradiente + formas)
			{
				const Rect b = R(46, 235, 682, 201);
				cv.fillGradientV(b, Color(60, 98, 150, 235), Color(24, 44, 86, 235));
				cv.pushClip(b);
				for (int i = 0; i < 9; ++i) cv.fillRect(R(60 + i * 78.0f, 255 + (i % 3) * 14.0f, 46, 20), Color(150, 190, 235, 24), S(4));
				cv.fillCircle({ b.x + b.w * 0.18f, b.y + b.h * 0.8f }, S(70), Color(255, 255, 255, 10));
				cv.fillCircle({ b.x + b.w * 0.82f, b.y + b.h * 0.2f }, S(60), Color(255, 255, 255, 10));
				cv.popClip();
				cv.strokeRect(b, Color(220, 235, 255, 90), S(14), std::max(1.0f, S(1.2f)));
				std::string t; for (char c : std::string("COFRE")) { t += c; t += ' '; t += ' '; }
				T(cv, 46, 255, 682, 160, t, 56, Pal::Text, Align::Center, FontDisplayBold);
			}
			cv.fillRect(R(46, 445, 682, 206), Pal::Inner, S(18)); cv.strokeRect(R(46, 445, 682, 206), Color(220, 235, 255, 80), S(18), std::max(1.0f, S(1.1f)));
			T(cv, 60, 455, 200, 22, "Saldo atual", 12.5f, Pal::Dim);
			T(cv, 60, 480, 640, 56, Money(m_balance), 42, Pal::Text, Align::Left, FontDisplayBold);
			cv.fillRect(R(60, 545, 654, 1), Color(200, 225, 255, 50), 0);
			FieldBox(cv, 60, 556, 213, 34, F_AMOUNT, "Valor", false);
			Pill(cv, 60, 602, 78, 34, "Depositar", H_DEPOSIT, 0, true, 12.5f);
			Pill(cv, 146, 602, 58, 34, "Sacar", H_WITHDRAW, 0, m_canWithdraw, 12.5f);
			// transacoes
			Card(cv, 759, 172, 555, 493, "TRANSAÇÕES");
			FieldBox(cv, 773, 228, 526, 32, F_SEARCH_B, "Pesquisar nome", true);
			T(cv, 787, 270, 150, 22, "Player", 11, Pal::Dim); T(cv, 940, 270, 80, 22, "Ação", 11, Pal::Dim, Align::Center); T(cv, 1030, 270, 80, 22, "Tipo", 11, Pal::Dim, Align::Center);
			T(cv, 1108, 270, 80, 22, "Valor", 11, Pal::Dim, Align::Center); T(cv, 1190, 270, 100, 22, "Data", 11, Pal::Dim, Align::Right);
			std::vector<int> idx;
			for (int i = 0; i < (int)m_tx.size(); ++i) if (Match(m_tx[i].name, m_tx[i].acct, m_f[F_SEARCH_B].s)) idx.push_back(i);
			const Rect vp = R(760, 293, 553, 370); const float pitch = 35.0f * m_u;
			m_svBank.setLayout(vp, pitch * idx.size()); m_svs.push_back(&m_svBank);
			cv.pushClip(vp);
			if (idx.empty()) T(cv, 760, 440, 553, 26, m_bankLoad.loaded ? "Nenhuma transação registrada." : "Carregando...", 12, Pal::Faint, Align::Center);
			for (size_t k = 0; k < idx.size(); ++k)
			{
				const Tx& x = m_tx[idx[k]]; const Rect row = m_svBank.itemRect(pitch * k, pitch);
				if (!m_svBank.visible(row)) continue;
				const float ry = (row.y - m_oy) / m_u;
				const Rect pr = R(773, ry + 3, 526, 29); cv.fillRect(pr, Pal::Row, pr.h * 0.5f);
				T(cv, 787, ry, 160, 35, std::to_string(x.acct) + " | " + x.name, 10.5f, Pal::Text, Align::Left, FontTextSemibold);
				const bool in = x.action == "ENTRADA";
				T(cv, 940, ry, 80, 35, x.action, 10, in ? Pal::Cyan : Pal::Pink, Align::Center, FontTextSemibold);
				T(cv, 1030, ry, 80, 35, x.kind, 10, Pal::Text, Align::Center, FontTextSemibold);
				T(cv, 1108, ry, 80, 35, Dots(x.amount), 10.5f, Pal::Text, Align::Center, FontTextSemibold);
				T(cv, 1180, ry, 110, 35, x.time, 9.5f, Pal::Text, Align::Right);
			}
			cv.popClip();
			drawScrollbar(cv, m_svBank);
		}

		// ---------------- MODAIS ----------------
		void drawModal(Canvas& cv)
		{
			// um modal ocupa as areas de toque: o fundo escurecido captura o toque (fora do cartao fecha)
			cv.setAlpha(m_modalA);
			cv.fillRect({ 0, 0, m_sw, m_sh }, Color(4, 10, 26, 150), 0);
			const float k = 0.94f + 0.06f * m_modalA; auto Rk = [&](float x, float y, float w, float h) { Rect r = R(x, y, w, h); const Vec2 c = r.center(); return Rect{ c.x - r.w * 0.5f * k, c.y - r.h * 0.5f * k, r.w * k, r.h * k }; };
			addHit({ 0, 0, m_sw, m_sh }, H_MODAL_BG, 0, true);
			if (m_modal == M_ROLE || m_modal == M_KICK)
			{
				if (m_modalArg < 0 || m_modalArg >= (int)m_members.size()) { cv.setAlpha(1.0f); return; }
				const Member& m = m_members[m_modalArg];
				const Rect c = Rk(483, 250, 400, 268); cv.fillRect(c, Color(24, 44, 84, 250), S(22)); cv.strokeRect(c, Pal::CardLine, S(22), std::max(1.0f, S(1.4f)));
				addHit(c, H_NONE, 0, true);
				T(cv, 483, 266, 400, 30, m_modal == M_KICK ? "Expulsar do grupo" : "Alterar cargo", 19, Pal::Text, Align::Center, FontDisplayBold);
				T(cv, 483, 304, 400, 24, std::to_string(m.acct) + " | " + m.name, 14, Pal::Text, Align::Center, FontTextSemibold);
				T(cv, 483, 328, 400, 22, "Cargo atual: " + m.rankName, 12, Pal::Dim, Align::Center);
				if (m_modal == M_KICK)
				{
					T(cv, 503, 362, 360, 40, "Ele perde o cargo e sai da organização. Confirma?", 12.5f, Pal::Text, Align::Center);
					Pill(cv, 513, 438, 150, 38, "Cancelar", H_M_CANCEL, 0, true, 14);
					Pill(cv, 703, 438, 150, 38, "Expulsar", H_M_KICK_YES, 0, true, 14, true);
				}
				else
				{
					Pill(cv, 513, 372, 150, 38, "Promover", H_M_PROMOTE, 0, true, 14, true);
					Pill(cv, 703, 372, 150, 38, "Rebaixar", H_M_DEMOTE, 0, true, 14);
					Pill(cv, 608, 440, 150, 36, "Cancelar", H_M_CANCEL, 0, true, 13);
				}
			}
			else if (m_modal == M_ROLES)
			{
				const Rect c = Rk(233, 140, 900, 490); cv.fillRect(c, Color(24, 44, 84, 250), S(22)); cv.strokeRect(c, Pal::CardLine, S(22), std::max(1.0f, S(1.4f)));
				addHit(c, H_NONE, 0, true);
				T(cv, 253, 154, 500, 30, "GERENCIAR CARGOS", 20, Pal::Text, Align::Left, FontDisplayBold);
				Pill(cv, 1010, 154, 100, 30, "Fechar", H_R_CLOSE, 0, true, 13);
				cv.fillRect(R(234, 194, 898, 1), Color(200, 225, 255, 50), 0);
				// lista
				const Rect vp = R(248, 208, 300, 408); const float pitch = 52.0f * m_u;
				m_svRoles.setLayout(vp, pitch * m_ranks.size()); m_svs.push_back(&m_svRoles);
				cv.pushClip(vp);
				if (m_ranks.empty()) T(cv, 248, 380, 300, 26, "Carregando...", 13, Pal::Dim, Align::Center);
				for (size_t i = 0; i < m_ranks.size(); ++i)
				{
					const Rank& rk = m_ranks[i]; const Rect row = m_svRoles.itemRect(pitch * i, pitch); if (!m_svRoles.visible(row)) continue;
					const float ry = (row.y - m_oy) / m_u; const bool sel = (int)i == m_roleSel;
					const Rect pr = R(248, ry + 4, 292, 44); cv.fillRect(pr, sel ? Color(255, 255, 255, 70) : Pal::Row, S(14)); if (sel) cv.strokeRect(pr, Color(180, 215, 255, 220), S(14), std::max(1.0f, S(1.6f)));
					T(cv, 262, ry + 4, 30, 44, std::to_string(rk.rank), 15, Pal::Cyan, Align::Left, FontDisplayBold);
					T(cv, 292, ry + 4, 190, 44, rk.name, 13, Pal::Text, Align::Left, FontTextSemibold);
					T(cv, 480, ry + 4, 56, 44, std::to_string(rk.count) + (rk.count == 1 ? " membro" : " membros"), 8.5f, Pal::Dim, Align::Right);
					addHit(pr, H_R_SEL, (int)i, true, &vp);
				}
				cv.popClip();
				drawScrollbar(cv, m_svRoles);
				// detalhe
				cv.fillRect(R(570, 208, 1, 408), Color(200, 225, 255, 40), 0);
				if (m_roleSel >= 0 && m_roleSel < (int)m_ranks.size())
				{
					const Rank& rk = m_ranks[m_roleSel]; const bool mine = rk.rank > m_home.myRank && has(P_ROLES);
					T(cv, 590, 208, 500, 24, "Cargo " + std::to_string(rk.rank), 12, Pal::Dim);
					FieldBox(cv, 590, 236, 340, 36, F_ROLE, "Nome do cargo", false);
					Pill(cv, 942, 238, 150, 32, "Salvar nome", H_R_SAVENAME, 0, mine || (has(P_ROLES) && m_home.myRank == 1), 13);
					T(cv, 590, 292, 500, 24, "Permissões deste cargo", 14, Pal::Text, Align::Left, FontTextSemibold);
					static const struct { int bit; const char* label; } defs[6] = { { P_INVITE, "Recrutar membros" }, { P_KICK, "Expulsar membros" }, { P_RANK, "Promover / rebaixar" }, { P_ROLES, "Gerenciar cargos" }, { P_BANK, "Sacar do cofre" }, { P_NOTICE, "Editar o mural" } };
					const bool editable = rk.rank != 1 && mine;
					for (int i = 0; i < 6; ++i)
					{
						const float x = 590 + (i % 2) * 262.0f, y = 326 + (i / 2) * 52.0f; const Rect r = R(x, y, 250, 42); const bool on = (m_rolePerms & defs[i].bit) != 0 || rk.rank == 1;
						cv.fillRect(r, Pal::Row, S(14)); cv.strokeRect(r, Color(220, 235, 255, 70), S(14), std::max(1.0f, S(1.0f)));
						T(cv, x + 14, y, 180, 42, defs[i].label, 12.5f, Pal::Text);
						const Rect tg = R(x + 196, y + 9, 40, 24); cv.fillRect(tg, on ? Pal::Cyan.alpha(0.85f) : Color(255, 255, 255, 40), tg.h * 0.5f);
						cv.fillCircle({ on ? tg.right() - tg.h * 0.5f : tg.x + tg.h * 0.5f, tg.y + tg.h * 0.5f }, tg.h * 0.38f, Pal::Text);
						addHit(r, H_R_PERM, defs[i].bit, editable);
					}
					T(cv, 590, 540, 500, 60, rk.rank == 1 ? "O Líder (cargo 1) sempre tem todas as permissões." : mine ? "Cada toque salva na hora. Você só concede permissões que você mesmo tem." : "Você só edita cargos abaixo do seu (e precisa da permissão Gerenciar cargos).", 11.5f, Pal::Dim);
				}
			}
			cv.setAlpha(1.0f);
		}

		void drawToast(Canvas& cv)
		{
			if (m_toast.empty() || m_toastA < 0.02f) return;
			cv.setFont(FontTextSemibold);
			const float w = std::min(1100.0f * m_u, std::max(S(280), cv.measure(m_toast, FS(14)).x + S(64)));
			cv.setFont(FontText);
			const float y = m_oy + S(104) + (1.0f - m_toastA) * -S(14);
			const Rect r = { (m_sw - w) * 0.5f, y, w, S(44) };
			cv.setAlpha(m_toastA);
			cv.fillRect(r.moved(0, S(3)), Color(0, 0, 0, 80), r.h * 0.5f);
			cv.fillRect(r, m_toastBad ? Color(120, 28, 52, 245) : Color(18, 96, 88, 245), r.h * 0.5f);
			cv.strokeRect(r, m_toastBad ? Color(255, 140, 160, 200) : Color(120, 240, 220, 200), r.h * 0.5f, std::max(1.0f, S(1.3f)));
			DrawTextAs(cv, FontTextSemibold, r.inset(S(18), 0), m_toast, Pal::Text, FS(14), Align::Center);
			cv.setAlpha(1.0f);
		}
	};
}
