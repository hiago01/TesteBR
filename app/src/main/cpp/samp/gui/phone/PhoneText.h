#pragma once
// Celular - utilitarios de TEXTO (UTF-8, escape para armazenamento em .ini, quebra de linha). Logica pura, testada no PC.
#include "../maximoos/Canvas.h"
#include <string>
#include <vector>

namespace Phone
{
	namespace Text
	{
		using MaximoOS::Canvas;

		// inicio do ultimo caractere UTF-8 de s (s nao vazio)
		inline size_t LastCpStart(const std::string& s)
		{
			size_t n = s.size();
			if (n == 0) return 0;
			--n;
			while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
			return n;
		}
		inline void PopBack(std::string& s) { if (!s.empty()) s.resize(LastCpStart(s)); }
		inline size_t CpCount(const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n; return n; }

		inline bool ValidUtf8(const std::string& s)
		{
			size_t i = 0;
			while (i < s.size())
			{
				const unsigned char c = (unsigned char)s[i];
				size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
				if (!len || i + len > s.size()) return false;
				for (size_t k = 1; k < len; ++k) if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
				i += len;
			}
			return true;
		}
		// largura em bytes do primeiro caractere de s a partir de pos
		inline size_t CpLen(const std::string& s, size_t pos)
		{
			const unsigned char c = (unsigned char)s[pos];
			size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
			return std::min(len, s.size() - pos);
		}

		inline std::string Trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
			while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
			return s.substr(a, b - a);
		}

		// maiuscula de ASCII e Latin-1 (acentos portugueses); o resto passa igual
		inline std::string Upper(const std::string& s)
		{
			std::string o = s;
			for (size_t i = 0; i < o.size(); ++i)
			{
				unsigned char c = (unsigned char)o[i];
				if (c >= 'a' && c <= 'z') o[i] = (char)(c - 32);
				else if (c == 0xC3 && i + 1 < o.size()) { unsigned char d = (unsigned char)o[i + 1]; if (d >= 0xA0 && d <= 0xBE && d != 0xB7) o[i + 1] = (char)(d - 0x20); ++i; }
			}
			return o;
		}

		// ---- escape para armazenamento: tudo que nao for [A-Za-z0-9_.-] vira %XX (separadores ficam livres; ';' '#' e espaco nunca aparecem crus no .ini) ----
		// primeira letra (maiuscula) para o avatar
		inline std::string FirstChar(const std::string& s)
		{
			if (s.empty()) return std::string();
			return Upper(s.substr(0, CpLen(s, 0)));
		}
		inline std::string Esc(const std::string& s)
		{
			static const char* hx = "0123456789ABCDEF";
			std::string o;
			for (unsigned char c : s)
			{
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') o += (char)c;
				else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
			}
			return o;
		}
		inline int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
		inline std::string Unesc(const std::string& s)
		{
			std::string o;
			for (size_t i = 0; i < s.size(); ++i)
			{
				if (s[i] == '%' && i + 2 < s.size() + 0 && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0) { o += (char)(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2])); i += 2; }
				else o += s[i];
			}
			return o;
		}
		inline std::vector<std::string> Split(const std::string& s, char sep)
		{
			std::vector<std::string> v; std::string cur;
			for (char c : s) { if (c == sep) { v.push_back(cur); cur.clear(); } else cur += c; }
			v.push_back(cur);
			return v;
		}
		inline std::string Join(const std::vector<std::string>& v, char sep)
		{
			std::string o;
			for (size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; }
			return o;
		}
		inline int ToInt(const std::string& s, int def = 0)
		{
			if (s.empty() || s.size() > 9) return def;
			int v = 0; for (char c : s) { if (c < '0' || c > '9') return def; v = v * 10 + (c - '0'); }
			return v;
		}

		// ---- quebra de linha por largura (\n respeitado; palavra maior que a largura e quebrada por caractere) ----
		inline std::vector<std::string> Wrap(Canvas& cv, const std::string& s, float size, float maxW)
		{
			std::vector<std::string> lines;
			size_t p = 0;
			while (true)
			{
				const size_t e = s.find('\n', p);
				const std::string para = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
				std::string cur;
				size_t i = 0;
				while (i < para.size())
				{
					const size_t j = para.find(' ', i);
					const std::string word = para.substr(i, j == std::string::npos ? std::string::npos : j - i + 1);   // inclui o espaco final
					if (cv.measure(cur + word, size).x <= maxW) { cur += word; i += word.size(); continue; }
					if (!cur.empty()) { lines.push_back(cur); cur.clear(); continue; }        // reprocessa a mesma palavra em linha nova
					for (size_t k = 0; k < word.size();)                                    // palavra sozinha maior que a linha
					{
						const size_t l = CpLen(word, k);
						const std::string ch = word.substr(k, l);
						if (!cur.empty() && cv.measure(cur + ch, size).x > maxW) { lines.push_back(cur); cur.clear(); }
						cur += ch; k += l;
					}
					i += word.size();
				}
				lines.push_back(cur);
				if (e == std::string::npos) break;
				p = e + 1;
			}
			return lines;
		}
	}
}
