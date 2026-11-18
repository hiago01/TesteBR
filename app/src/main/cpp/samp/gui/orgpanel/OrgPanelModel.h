#pragma once
// Gerenciador de Grupo - MODELO de dados + leitura das respostas do servico (celular-service /o/*). Logica pura (sem ImGui/jogo): testada no PC.
// Formato das respostas: "ok\n" + linhas separadas por TAB (ver servidor celular-service/src/orgpanel.js) ou "err\n<codigo>\n".
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace OrgPanel
{
	enum Perm { P_INVITE = 1, P_KICK = 2, P_RANK = 4, P_ROLES = 8, P_BANK = 16, P_NOTICE = 32 };

	struct Leader { int acct = 0, rank = 0, online = 0; std::string name, rankName; };
	struct TopTime { int pos = 0, acct = 0; std::string name; long secs = 0; };
	struct Home
	{
		bool ok = false;
		std::string org; int members = 0, maxMembers = 0; long long bank = 0; int myRank = 7; std::string rankName; int perms = 0, online = 0, acct = 0;
		std::string discord, radio; bool hasGps = false;
		std::string notice;
		std::string farmName; long farmPrice = 0, farmGoal = 0, farmToday = 0, farmWeek = 0; int farmPos = 0; bool farmOn = false;
		int week[7] = { 0, 0, 0, 0, 0, 0, 0 }; int todayIdx = 0;          // 0 futuro | 1 meta batida | 2 perdido | 3 hoje
		std::vector<Leader> leaders; std::vector<TopTime> top; TopTime me;
	};
	struct Member { int acct = 0, online = 0, rank = 7; long secs = 0; std::string name, joined, last, rankName; };
	struct FarmRow { int pos = 0, acct = 0; long day = 0, week = 0; int st[7] = { 0, 0, 0, 0, 0, 0, 0 }; std::string name; };
	struct FarmHist { std::string time, name; int acct = 0; long qty = 0; };
	struct Tx { int acct = 0; std::string name, action, kind, time; long long amount = 0; };
	struct Rank { int rank = 0, perms = 0, count = 0; std::string name; };

	// ---------------- utilidades de texto ----------------
	inline std::vector<std::string> Lines(const std::string& body)
	{
		std::vector<std::string> v; size_t i = 0;
		while (i <= body.size())
		{
			size_t e = body.find('\n', i); if (e == std::string::npos) e = body.size();
			std::string l = body.substr(i, e - i); if (!l.empty() && l.back() == '\r') l.pop_back();
			if (!l.empty()) v.push_back(l);
			i = e + 1;
		}
		return v;
	}
	inline std::vector<std::string> Fields(const std::string& line)
	{
		std::vector<std::string> v; size_t i = 0;
		for (;;) { size_t e = line.find('\t', i); if (e == std::string::npos) { v.push_back(line.substr(i)); break; } v.push_back(line.substr(i, e - i)); i = e + 1; }
		return v;
	}
	inline int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }
	inline std::string Unhex(const std::string& h) { std::string o; for (size_t i = 0; i + 1 < h.size(); i += 2) o.push_back((char)((HexVal(h[i]) << 4) | HexVal(h[i + 1]))); return o; }
	inline std::string Hex(const std::string& s) { static const char* H = "0123456789abcdef"; std::string o; for (unsigned char c : s) { o.push_back(H[c >> 4]); o.push_back(H[c & 15]); } return o; }
	inline long long ToLL(const std::vector<std::string>& f, size_t i) { return i < f.size() ? std::atoll(f[i].c_str()) : 0; }
	inline int ToInt(const std::vector<std::string>& f, size_t i) { return (int)ToLL(f, i); }
	inline std::string At(const std::vector<std::string>& f, size_t i) { return i < f.size() ? f[i] : std::string(); }
	// nomes do SA-MP vem com "_": a tela mostra com espaco
	inline std::string Pretty(std::string s) { for (auto& c : s) if (c == '_') c = ' '; return s; }

	inline std::string UrlEnc(const std::string& s)
	{
		static const char* H = "0123456789ABCDEF"; std::string o;
		for (unsigned char c : s)
		{
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') o.push_back((char)c);
			else { o.push_back('%'); o.push_back(H[c >> 4]); o.push_back(H[c & 15]); }
		}
		return o;
	}
	// "R$ 1.234.567,00"
	inline std::string Money(long long v, bool cents = true)
	{
		const bool neg = v < 0; if (neg) v = -v;
		std::string s = std::to_string(v), o;
		for (size_t i = 0; i < s.size(); ++i) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; }
		return std::string(neg ? "-" : "") + "R$ " + o + (cents ? ",00" : "");
	}
	inline std::string Dots(long long v) { std::string s = std::to_string(v < 0 ? -v : v), o; for (size_t i = 0; i < s.size(); ++i) { if (i && (s.size() - i) % 3 == 0) o += '.'; o += s[i]; } return (v < 0 ? "-" : "") + o; }
	// 127 Dias, 20 Horas
	inline std::string DaysHours(long secs) { if (secs < 0) secs = 0; const long h = secs / 3600; char b[48]; std::snprintf(b, sizeof(b), "%ld Dias, %02ld Horas", h / 24, h % 24); return b; }
	// 420H, 20Min
	inline std::string HoursMin(long secs) { if (secs < 0) secs = 0; char b[48]; std::snprintf(b, sizeof(b), "%ldH, %ldMin", secs / 3600, (secs % 3600) / 60); return b; }

	// ---------------- leitura das respostas ----------------
	// true = "ok" (lines recebe o resto); false = erro (code recebe o codigo do servidor, ex.: "noperm")
	inline bool Split(const std::string& body, std::vector<std::string>& lines, std::string& code)
	{
		std::vector<std::string> L = Lines(body);
		lines.clear(); code.clear();
		if (L.empty()) { code = "net"; return false; }
		if (L[0] == "ok") { lines.assign(L.begin() + 1, L.end()); return true; }
		code = (L[0] == "err" && L.size() > 1) ? L[1] : "net";
		return false;
	}

	inline bool ParseHome(const std::vector<std::string>& lines, Home& h)
	{
		Home n;
		for (const std::string& ln : lines)
		{
			const std::vector<std::string> f = Fields(ln);
			if (f.empty() || f[0].size() != 1) continue;
			switch (f[0][0])
			{
			case 'H': n.org = Pretty(At(f, 1)); n.members = ToInt(f, 2); n.maxMembers = ToInt(f, 3); n.bank = ToLL(f, 4); n.myRank = ToInt(f, 5); n.rankName = At(f, 6); n.perms = ToInt(f, 7); n.online = ToInt(f, 8); n.acct = ToInt(f, 9); n.ok = true; break;
			case 'O': n.discord = At(f, 1); n.radio = At(f, 2); n.hasGps = ToInt(f, 3) != 0; break;
			case 'N': n.notice = Unhex(At(f, 1)); break;
			case 'F': n.farmName = At(f, 1); n.farmPrice = (long)ToLL(f, 2); n.farmGoal = (long)ToLL(f, 3); n.farmToday = (long)ToLL(f, 4); n.farmWeek = (long)ToLL(f, 5); n.farmPos = ToInt(f, 6); n.farmOn = ToInt(f, 7) != 0; break;
			case 'W': for (int i = 0; i < 7; ++i) n.week[i] = ToInt(f, 1 + i); n.todayIdx = ToInt(f, 8); break;
			case 'L': { Leader l; l.acct = ToInt(f, 1); l.name = Pretty(At(f, 2)); l.rank = ToInt(f, 3); l.rankName = At(f, 4); l.online = ToInt(f, 5); n.leaders.push_back(l); break; }
			case 'T': { TopTime t; t.pos = ToInt(f, 1); t.acct = ToInt(f, 2); t.name = Pretty(At(f, 3)); t.secs = (long)ToLL(f, 4); n.top.push_back(t); break; }
			case 'U': n.me.pos = ToInt(f, 1); n.me.acct = ToInt(f, 2); n.me.name = Pretty(At(f, 3)); n.me.secs = (long)ToLL(f, 4); break;
			default: break;
			}
		}
		if (!n.ok) return false;
		h = n; return true;
	}
	inline void ParseMembers(const std::vector<std::string>& lines, std::vector<Member>& out)
	{
		out.clear();
		for (const std::string& ln : lines)
		{
			const std::vector<std::string> f = Fields(ln);
			if (f.size() < 9 || f[0] != "M") continue;
			Member m; m.acct = ToInt(f, 1); m.name = Pretty(f[2]); m.online = ToInt(f, 3); m.secs = (long)ToLL(f, 4); m.joined = f[5]; m.last = f[6]; m.rank = ToInt(f, 7); m.rankName = f[8];
			out.push_back(m);
		}
	}
	inline void ParseFarm(const std::vector<std::string>& lines, std::vector<FarmRow>& rows, std::vector<FarmHist>& hist)
	{
		rows.clear(); hist.clear();
		for (const std::string& ln : lines)
		{
			const std::vector<std::string> f = Fields(ln);
			if (f.empty()) continue;
			if (f[0] == "P" && f.size() >= 13)
			{
				FarmRow r; r.pos = ToInt(f, 1); r.acct = ToInt(f, 2); r.name = Pretty(f[3]); r.day = (long)ToLL(f, 4); r.week = (long)ToLL(f, 5);
				for (int i = 0; i < 7; ++i) r.st[i] = ToInt(f, 6 + i);
				rows.push_back(r);
			}
			else if (f[0] == "Y" && f.size() >= 5) { FarmHist y; y.time = f[1]; y.acct = ToInt(f, 2); y.name = Pretty(f[3]); y.qty = (long)ToLL(f, 4); hist.push_back(y); }
		}
	}
	inline void ParseBank(const std::vector<std::string>& lines, long long& balance, bool& canWithdraw, std::vector<Tx>& tx)
	{
		tx.clear();
		for (const std::string& ln : lines)
		{
			const std::vector<std::string> f = Fields(ln);
			if (f.empty()) continue;
			if (f[0] == "B") { balance = ToLL(f, 1); canWithdraw = ToInt(f, 2) != 0; }
			else if (f[0] == "X" && f.size() >= 7) { Tx t; t.acct = ToInt(f, 1); t.name = Pretty(f[2]); t.action = f[3]; t.kind = f[4]; t.amount = ToLL(f, 5); t.time = f[6]; tx.push_back(t); }
		}
	}
	inline void ParseRanks(const std::vector<std::string>& lines, std::vector<Rank>& out)
	{
		out.clear();
		for (const std::string& ln : lines)
		{
			const std::vector<std::string> f = Fields(ln);
			if (f.size() < 5 || f[0] != "K") continue;
			Rank r; r.rank = ToInt(f, 1); r.name = f[2]; r.perms = ToInt(f, 3); r.count = ToInt(f, 4);
			out.push_back(r);
		}
	}
	// "ok|<saldo>" etc. nao passam por aqui: o cliente so le /o/*.
}
