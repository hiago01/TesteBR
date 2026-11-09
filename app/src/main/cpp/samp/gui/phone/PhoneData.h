#pragma once
// Celular - DADOS LOCAIS dos apps (contatos, notas, eventos, mensagens, chamadas), gravados via PhoneContext::getString/setString
// (o adaptador grava em SAMP/settings.ini, secao [celular_dados], no maximo a cada 3 s). Formato: registros separados por ',' e campos por '|',
// cada campo escapado com %XX (PhoneText.h) -> nunca quebra o .ini, aceita qualquer texto/UTF-8. Dados corrompidos sao descartados registro a registro.
// Fase 3: o servidor passa a ser a fonte (PostgreSQL); estas funcoes viram o CACHE local sem mudar os apps.
#include "PhoneApp.h"
#include "PhoneText.h"
#include <algorithm>

namespace Phone
{
	namespace Data
	{
		using Text::Esc; using Text::Unesc; using Text::Split; using Text::Join;

		constexpr size_t kMaxContacts = 150, kMaxNotes = 50, kMaxEvents = 200, kMaxMsgs = 300, kMaxCalls = 60;
		constexpr size_t kNameMax = 32, kNumberMax = 16, kNoteMax = 2000, kEventTitleMax = 60;
		// kMsgMax = limite do que o jogador digita (bytes; o servidor aceita 240 caracteres). kMsgStore = limite do que se guarda (mensagem recebida: 240 caracteres x 4 bytes).
		constexpr size_t kMsgMax = 240, kMsgStore = 960;

		struct Contact { std::string name, number; bool emergency = false; };
		struct Note    { int id = 0; std::string text; };
		struct Event   { int date = 0; std::string time, title; };            // date = AAAAMMDD; time = "HH:MM" ou ""
		struct Msg     { int seq = 0; std::string number, text; bool out = true, read = true; int state = 0; };   // state (so enviadas): 0 enviada, 1 enviando, 2 nao enviada
		struct Call    { int seq = 0; std::string number; int dir = 0; };       // dir: 0 = feita, 1 = recebida, 2 = perdida

		// ---- numero: so digitos e + * # (no maximo kNumberMax) ----
		inline std::string CleanNumber(const std::string& s)
		{
			std::string o;
			for (char c : s) if ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#') { if (o.size() < kNumberMax) o += c; }
			return o;
		}
		// 7 digitos -> "123-4567" (outros formatos ficam como estao)
		inline std::string FormatNumber(const std::string& n)
		{
			if (n.size() != 7) return n;
			for (char c : n) if (c < '0' || c > '9') return n;
			return n.substr(0, 3) + "-" + n.substr(3);
		}
		inline bool HasDigit(const std::string& s) { for (char c : s) if (c >= '0' && c <= '9') return true; return false; }
		inline std::string Clip(const std::string& s, size_t max)
		{
			if (s.size() <= max) return s;
			size_t n = max; while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;     // nunca corta no meio de um caractere
			return s.substr(0, n);
		}

		// ---- chaves ----
		inline const char* kContacts() { return "contatos"; }
		inline const char* kNotes()    { return "notas"; }
		inline const char* kEvents()   { return "eventos"; }
		inline const char* kMsgs()     { return "mensagens"; }
		inline const char* kCalls()    { return "chamadas"; }

		inline std::vector<std::string> Records(const PhoneContext& c, const char* key)
		{
			const std::string raw = c.get(key, "");
			std::vector<std::string> out;
			if (raw.empty()) return out;
			for (auto& r : Split(raw, ',')) if (!r.empty()) out.push_back(r);
			return out;
		}
		inline void Put(PhoneContext& c, const char* key, const std::vector<std::string>& recs) { c.set(key, Join(recs, ',')); }

		// ------------------------------------------------------------------ contatos
		inline std::vector<Contact> LoadContacts(const PhoneContext& c)
		{
			std::vector<Contact> v;
			for (auto& r : Records(c, kContacts()))
			{
				auto f = Split(r, '|'); if (f.size() != 3) continue;
				Contact k; k.name = Unesc(f[0]); k.number = CleanNumber(Unesc(f[1])); k.emergency = f[2] == "1";
				if (k.name.empty() || !Text::ValidUtf8(k.name) || k.number.empty()) continue;
				v.push_back(k);
			}
			return v;
		}
		inline void SaveContacts(PhoneContext& c, std::vector<Contact> v)
		{
			if (v.size() > kMaxContacts) v.resize(kMaxContacts);
			std::vector<std::string> r;
			for (auto& k : v) r.push_back(Esc(k.name) + "|" + Esc(k.number) + "|" + (k.emergency ? "1" : "0"));
			Put(c, kContacts(), r);
		}
		inline void SortContacts(std::vector<Contact>& v)
		{
			auto low = [](const std::string& s) { std::string o = s; for (auto& ch : o) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32); return o; };
			std::stable_sort(v.begin(), v.end(), [&](const Contact& a, const Contact& b) { return low(a.name) < low(b.name); });
		}
		// erro em portugues ou "" se ok. 'editing' = indice do contato em edicao (-1 = novo)
		inline std::string ValidateContact(const std::vector<Contact>& v, const std::string& name, const std::string& number, int editing)
		{
			if (Text::Trim(name).empty()) return "Digite o nome.";
			if (!HasDigit(number)) return "Digite um número válido.";
			for (size_t i = 0; i < v.size(); ++i) if ((int)i != editing && v[i].number == CleanNumber(number)) return "Este número já está nos contatos.";
			if (editing < 0 && v.size() >= kMaxContacts) return "Limite de contatos atingido.";
			return std::string();
		}
		inline std::string NameFor(const std::vector<Contact>& v, const std::string& number)
		{ for (auto& k : v) if (k.number == number) return k.name; return std::string(); }

		// ------------------------------------------------------------------ notas
		inline std::vector<Note> LoadNotes(const PhoneContext& c)    // mais recente primeiro (ordem gravada)
		{
			std::vector<Note> v;
			for (auto& r : Records(c, kNotes()))
			{
				auto f = Split(r, '|'); if (f.size() != 2) continue;
				Note n; n.id = Text::ToInt(f[0], -1); n.text = Unesc(f[1]);
				if (n.id < 0 || !Text::ValidUtf8(n.text)) continue;
				v.push_back(n);
			}
			return v;
		}
		inline void SaveNotes(PhoneContext& c, std::vector<Note> v)
		{
			if (v.size() > kMaxNotes) v.resize(kMaxNotes);
			std::vector<std::string> r;
			for (auto& n : v) r.push_back(std::to_string(n.id) + "|" + Esc(Clip(n.text, kNoteMax)));
			Put(c, kNotes(), r);
		}
		inline int NextNoteId(const std::vector<Note>& v) { int m = 0; for (auto& n : v) m = std::max(m, n.id); return m + 1; }
		inline std::string FirstLine(const std::string& t) { const size_t e = t.find('\n'); return Text::Trim(t.substr(0, e)); }
		inline std::string SecondLine(const std::string& t)
		{
			const size_t e = t.find('\n'); if (e == std::string::npos) return std::string();
			const std::string rest = Text::Trim(t.substr(e + 1)); const size_t e2 = rest.find('\n');
			return rest.substr(0, e2);
		}

		// ------------------------------------------------------------------ eventos
		inline bool ValidTime(const std::string& t)
		{
			if (t.empty()) return true;
			if (t.size() != 5 || t[2] != ':') return false;
			for (int i : { 0, 1, 3, 4 }) if (t[i] < '0' || t[i] > '9') return false;
			return Text::ToInt(t.substr(0, 2)) <= 23 && Text::ToInt(t.substr(3, 2)) <= 59;
		}
		inline std::vector<Event> LoadEvents(const PhoneContext& c)
		{
			std::vector<Event> v;
			for (auto& r : Records(c, kEvents()))
			{
				auto f = Split(r, '|'); if (f.size() != 3) continue;
				Event e; e.date = Text::ToInt(f[0], 0); e.time = Unesc(f[1]); e.title = Unesc(f[2]);
				if (e.date < 19000101 || e.date > 99991231 || !ValidTime(e.time) || e.title.empty() || !Text::ValidUtf8(e.title)) continue;
				v.push_back(e);
			}
			return v;
		}
		inline void SaveEvents(PhoneContext& c, std::vector<Event> v)
		{
			if (v.size() > kMaxEvents) v.resize(kMaxEvents);
			std::vector<std::string> r;
			for (auto& e : v) r.push_back(std::to_string(e.date) + "|" + Esc(e.time) + "|" + Esc(e.title));
			Put(c, kEvents(), r);
		}
		inline void SortEvents(std::vector<Event>& v)
		{ std::stable_sort(v.begin(), v.end(), [](const Event& a, const Event& b) { return a.date != b.date ? a.date < b.date : a.time < b.time; }); }
		inline int  DateKey(int y, int m, int d) { return y * 10000 + m * 100 + d; }
		inline bool LeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
		inline int  DaysInMonth(int y, int m) { static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 }; return (m == 2 && LeapYear(y)) ? 29 : d[(m - 1 + 12) % 12]; }
		inline int  Weekday(int y, int m, int d)   // 0 = domingo (Sakamoto)
		{ static const int t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 }; if (m < 3) --y; return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7; }
		inline bool ValidDate(int y, int m, int d) { return y >= 1900 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= DaysInMonth(y, m); }

		// ------------------------------------------------------------------ mensagens
		inline std::vector<Msg> LoadMsgs(const PhoneContext& c)      // ordem cronologica
		{
			std::vector<Msg> v;
			for (auto& r : Records(c, kMsgs()))
			{
				auto f = Split(r, '|'); if (f.size() != 5 && f.size() != 6) continue;
				Msg m; m.seq = Text::ToInt(f[0], -1); m.number = CleanNumber(Unesc(f[1])); m.text = Unesc(f[2]); m.out = f[3] == "1"; m.read = f[4] == "1";
				if (f.size() == 6) m.state = std::min(2, std::max(0, Text::ToInt(f[5], 0)));
				if (m.seq < 0 || m.number.empty() || m.text.empty() || !Text::ValidUtf8(m.text)) continue;
				v.push_back(m);
			}
			return v;
		}
		inline void SaveMsgs(PhoneContext& c, std::vector<Msg> v)
		{
			if (v.size() > kMaxMsgs) v.erase(v.begin(), v.begin() + (v.size() - kMaxMsgs));
			std::vector<std::string> r;
			for (auto& m : v) r.push_back(std::to_string(m.seq) + "|" + Esc(m.number) + "|" + Esc(Clip(m.text, kMsgStore)) + "|" + (m.out ? "1" : "0") + "|" + (m.read ? "1" : "0") + "|" + std::to_string(m.state));
			Put(c, kMsgs(), r);
		}
		inline int NextSeq(const std::vector<Msg>& v) { int m = 0; for (auto& x : v) m = std::max(m, x.seq); return m + 1; }
		inline void AddMessage(PhoneContext& c, const std::string& number, const std::string& text, bool outgoing, int state = 0)
		{
			std::vector<Msg> v = LoadMsgs(c);
			Msg m; m.seq = NextSeq(v); m.number = CleanNumber(number); m.text = Clip(text, kMsgStore); m.out = outgoing; m.read = outgoing; m.state = outgoing ? state : 0;
			if (m.number.empty() || m.text.empty()) return;
			v.push_back(m); SaveMsgs(c, v);
		}
		// mensagem RECEBIDA (usada pela integracao com o servidor na Fase 3)
		inline void ReceiveMessage(PhoneContext& c, const std::string& from, const std::string& text) { AddMessage(c, from, text, false); }
		// resposta do servidor: acha a ULTIMA mensagem enviada "enviando" desse numero/texto e define o estado (0 enviada, 2 nao enviada)
		inline bool MarkOutgoing(PhoneContext& c, const std::string& number, const std::string& text, int newState)
		{
			std::vector<Msg> v = LoadMsgs(c);
			for (size_t i = v.size(); i-- > 0;)
				if (v[i].out && v[i].state == 1 && v[i].number == CleanNumber(number) && v[i].text == Clip(text, kMsgStore)) { v[i].state = newState; SaveMsgs(c, v); return true; }
			return false;
		}
		// ao iniciar: "enviando" que sobrou de uma sessao anterior nao tem como ser confirmado -> nao enviada
		inline void FailStalePending(PhoneContext& c)
		{
			std::vector<Msg> v = LoadMsgs(c); bool ch = false;
			for (auto& m : v) if (m.out && m.state == 1) { m.state = 2; ch = true; }
			if (ch) SaveMsgs(c, v);
		}
		inline int UnreadMessages(const PhoneContext& c) { int n = 0; for (auto& m : LoadMsgs(c)) if (!m.out && !m.read) ++n; return n; }
		inline void MarkThreadRead(PhoneContext& c, const std::string& number)
		{
			std::vector<Msg> v = LoadMsgs(c); bool ch = false;
			for (auto& m : v) if (m.number == number && !m.read) { m.read = true; ch = true; }
			if (ch) SaveMsgs(c, v);
		}
		inline void DeleteThread(PhoneContext& c, const std::string& number)
		{
			std::vector<Msg> v = LoadMsgs(c);
			v.erase(std::remove_if(v.begin(), v.end(), [&](const Msg& m) { return m.number == number; }), v.end());
			SaveMsgs(c, v);
		}
		struct Thread { std::string number; std::string last; int unread = 0; int lastSeq = 0; };
		inline std::vector<Thread> Threads(const PhoneContext& c)    // mais recente primeiro
		{
			std::vector<Thread> t;
			for (auto& m : LoadMsgs(c))
			{
				Thread* p = nullptr; for (auto& x : t) if (x.number == m.number) { p = &x; break; }
				if (!p) { t.push_back(Thread()); p = &t.back(); p->number = m.number; }
				p->last = m.text; p->lastSeq = m.seq; if (!m.out && !m.read) ++p->unread;
			}
			std::sort(t.begin(), t.end(), [](const Thread& a, const Thread& b) { return a.lastSeq > b.lastSeq; });
			return t;
		}

		// ------------------------------------------------------------------ chamadas
		inline std::vector<Call> LoadCalls(const PhoneContext& c)    // mais recente primeiro
		{
			std::vector<Call> v;
			for (auto& r : Records(c, kCalls()))
			{
				auto f = Split(r, '|'); if (f.size() != 3) continue;
				Call k; k.seq = Text::ToInt(f[0], -1); k.number = CleanNumber(Unesc(f[1])); k.dir = Text::ToInt(f[2], 0);
				if (k.seq < 0 || k.number.empty() || k.dir > 2) continue;
				v.push_back(k);
			}
			return v;
		}
		inline void AddCall(PhoneContext& c, const std::string& number, int dir)
		{
			std::vector<Call> v = LoadCalls(c);
			Call k; k.seq = v.empty() ? 1 : v.front().seq + 1; k.number = CleanNumber(number); k.dir = dir;
			if (k.number.empty()) return;
			v.insert(v.begin(), k);
			if (v.size() > kMaxCalls) v.resize(kMaxCalls);
			std::vector<std::string> r;
			for (auto& x : v) r.push_back(std::to_string(x.seq) + "|" + Esc(x.number) + "|" + std::to_string(x.dir));
			Put(c, kCalls(), r);
		}
	}
}
