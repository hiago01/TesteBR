#pragma once
// Logica pura (sem Android/ImGui) da Central da Cidade: modelos de fundo, lista e rolagem. Testada no host.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace CenLogic
{
	constexpr int kStyles = 9;               // igual ao servidor: CEN_STYLES
	constexpr int kMaxRows = 1100;
	enum Effect { E_None = 0, E_Shimmer, E_Pulse, E_Wave, E_Rainbow, E_Lights };

	struct StyleDef { const char* name; uint8_t c1[3], c2[3], edge[3]; int effect; };

	inline const StyleDef& Style(int idx)
	{
		static const StyleDef k[kStyles] = {
			{ "Padrao",     {  34,  34,  40 }, {  48,  48,  58 }, {  86,  86,  98 }, E_None },
			{ "Rosa",       { 120,  16,  52 }, { 236,  40,  96 }, { 255, 120, 168 }, E_None },
			{ "Oceano",     {  10,  40,  96 }, {  30, 130, 220 }, {  96, 184, 255 }, E_None },
			{ "Floresta",   {  10,  64,  40 }, {  40, 170,  90 }, { 110, 232, 156 }, E_None },
			{ "Dourado",    {  84,  58,   8 }, { 204, 152,  30 }, { 255, 218,  96 }, E_Shimmer },
			{ "Neon roxo",  {  36,  10,  76 }, { 112,  42, 204 }, { 196, 128, 255 }, E_Pulse },
			{ "Fogo",       {  96,  16,   8 }, { 240, 110,  20 }, { 255, 176,  64 }, E_Wave },
			{ "Arco-iris",  {  24,  24,  32 }, {  44,  44,  58 }, { 255, 255, 255 }, E_Rainbow },
			{ "Natal",      { 104,  10,  20 }, {  20,  92,  42 }, { 255, 232, 130 }, E_Lights },
		};
		return k[(idx >= 0 && idx < kStyles) ? idx : 0];
	}

	struct Row { uint16_t pid = 0; uint32_t acc = 0; uint8_t style = 0, border = 0, flags = 0, org = 0; uint16_t ping = 0; std::string name; };

	struct List
	{
		uint16_t total = 0, slots = 0, myMin = 0, admins = 0, pm = 0, mec = 0, samu = 0; uint8_t myStyle = 0;
		bool have = false;
		std::vector<Row> rows;

		void Header(uint16_t tot, uint16_t sl, uint16_t mins, uint8_t st, uint16_t adm, uint16_t p = 0, uint16_t m = 0, uint16_t sa = 0)
		{
			rows.clear(); total = tot; slots = sl; myMin = mins; myStyle = (st < kStyles) ? st : 0; admins = adm; pm = p; mec = m; samu = sa; have = true;
		}
		bool Add(const Row& r)
		{
			if ((int)rows.size() >= kMaxRows) return false;
			Row c = r; if (c.style >= kStyles) c.style = 0;
			rows.push_back(c);
			return true;
		}
	};

	inline float ClampScroll(float s, float content, float view)
	{
		const float mx = std::max(0.0f, content - view);
		return std::min(std::max(s, 0.0f), mx);
	}
	// fracao 0..1 da barra de populacao
	inline float PopFill(int total, int slots) { return slots > 0 ? std::min(1.0f, std::max(0.0f, (float)total / (float)slots)) : 0.0f; }

	// ---- pesquisa: trecho do nome (sem diferenciar maiusculas) ou do ID da conta ----
	inline std::string Lower(const std::string& a) { std::string o = a; for (char& c : o) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return o; }
	inline std::string Trim(const std::string& a)
	{
		size_t b = 0, e = a.size();
		while (b < e && (unsigned char)a[b] <= ' ') b++;
		while (e > b && (unsigned char)a[e - 1] <= ' ') e--;
		return a.substr(b, e - b);
	}
	inline bool Match(const Row& r, const std::string& query)
	{
		const std::string q = Lower(Trim(query));
		if (q.empty()) return true;
		if (Lower(r.name).find(q) != std::string::npos) return true;
		return std::to_string(r.acc).find(q) != std::string::npos;
	}
	// org: 0 = todos | 1 PM | 2 MEC | 3 SAMU (so os jogadores online que sao membros da organizacao daquela TAG)
	inline std::vector<int> Filter(const List& l, const std::string& query, int org = 0)
	{
		std::vector<int> out; out.reserve(l.rows.size());
		for (size_t i = 0; i < l.rows.size(); i++) if ((org == 0 || l.rows[i].org == org) && Match(l.rows[i], query)) out.push_back((int)i);
		return out;
	}
}
