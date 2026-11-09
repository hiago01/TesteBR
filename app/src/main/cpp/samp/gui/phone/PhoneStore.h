#pragma once
// Celular - LOJA DE APPS, APARELHO (IMEI) e PAPEL DE PAREDE: logica do servico. Fala com as rotas /s/* do servico do celular (mesmo servidor,
// porta e token do Gram; ver celular-service/src/store.js). Quem manda e o SERVIDOR: o que esta instalado, o IMEI e o papel de parede vem de la.
// Nada aqui conhece ImGui nem o jogo. Sem servidor/token nada e simulado: o estado fica "desconhecido" e a interface avisa.
#include "PhoneApp.h"
#include "PhoneGram.h"
#include <set>

namespace Phone
{
	namespace Store
	{
		struct AppEntry { std::string id, title, descr; int color = 0x808080; bool core = false, installed = false; int sort = 100; };
		struct WpEntry { std::string id, title; };

		inline std::string ErrText(const std::string& c)
		{
			if (c == "nodevice")    return "Você ainda não tem um celular.";
			if (c == "notreleased") return "Esse app ainda não foi liberado.";
			if (c == "noapp")       return "Esse app não existe.";
			if (c == "core")        return "App de sistema não pode ser removido.";
			if (c == "blocked")     return "Este aparelho foi bloqueado pela administração.";
			if (c == "full")        return "Limite de apps instalados atingido.";
			if (c == "badwp")       return "Papel de parede inválido.";
			if (c == "notinstalled") return "Esse app não está instalado.";
			if (c == "already")     return "Esse app já está instalado.";
			if (c == "rate")        return "Aguarde um pouco para enviar outra foto.";
			if (c == "dims")        return "Foto grande demais (máx. 1280 px).";
			return Gram::ErrText(c);
		}
		inline bool ValidAppId(const std::string& s)
		{
			if (s.size() < 2 || s.size() > 24) return false;
			for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
			return true;
		}
		// "w3" (predefinido) ou "m:<32 hex>" (foto enviada)
		inline bool IsPreset(const std::string& w)
		{
			if (w.size() < 2 || w.size() > 4 || w[0] != 'w') return false;
			for (size_t i = 1; i < w.size(); ++i) if (w[i] < '0' || w[i] > '9') return false;
			return true;
		}
		inline bool IsPhotoWp(const std::string& w) { return w.size() == 34 && w[0] == 'm' && w[1] == ':' && Gram::ValidMedia(w.substr(2)); }
		inline bool ValidWallpaper(const std::string& w) { return IsPreset(w) || IsPhotoWp(w); }
		// 351935042598070 -> 35-193504-259807-0
		inline std::string FormatImei(const std::string& s)
		{
			if (s.size() != 15) return s;
			return s.substr(0, 2) + "-" + s.substr(2, 6) + "-" + s.substr(8, 6) + "-" + s.substr(14, 1);
		}
		inline bool ParseApp(const std::string& line, AppEntry& a)
		{
			const auto f = Gram::Split(line, '\t'); long long col = 0, core = 0, inst = 0, sort = 0;
			if (f.size() < 7 || !ValidAppId(f[0]) || f[1].empty()) return false;
			if (!Gram::ToInt64(f[3], col) || col > 0xFFFFFF || !Gram::ToInt64(f[4], core) || !Gram::ToInt64(f[5], inst) || !Gram::ToInt64(f[6], sort)) return false;
			a.id = f[0]; a.title = f[1]; a.descr = f[2]; a.color = (int)col; a.core = core != 0; a.installed = inst != 0; a.sort = (int)std::min<long long>(sort, 100000);
			return true;
		}
		// 3 cores (topo / meio / base) de cada papel de parede predefinido; id desconhecido = false (o chamador usa o w1)
		inline bool WallpaperColors(const std::string& id, Color& a, Color& b, Color& c)
		{
			struct W { const char* id; Color a, b, c; };
			static const W t[] = {
				{ "w1",  Color(14, 60, 170),  Color(8, 10, 40),    Color(150, 130, 190) },   // Noite azul (o original)
				{ "w2",  Color(255, 140, 66), Color(180, 50, 90),  Color(40, 20, 80) },      // Por do sol
				{ "w3",  Color(20, 200, 160), Color(30, 90, 170),  Color(60, 20, 110) },     // Aurora
				{ "w4",  Color(30, 140, 220), Color(10, 70, 140),  Color(5, 20, 60) },       // Oceano
				{ "w5",  Color(60, 160, 90),  Color(20, 90, 60),   Color(8, 36, 30) },       // Floresta
				{ "w6",  Color(255, 150, 190), Color(220, 80, 140), Color(110, 30, 90) },    // Rosa
				{ "w7",  Color(90, 94, 104),  Color(40, 42, 50),   Color(14, 14, 18) },      // Grafite
				{ "w8",  Color(255, 90, 40),  Color(190, 30, 20),  Color(50, 8, 10) },       // Lava
				{ "w9",  Color(135, 200, 255), Color(90, 150, 230), Color(200, 225, 250) },  // Ceu
				{ "w10", Color(255, 40, 200), Color(70, 30, 190),  Color(10, 10, 50) },      // Neon
				{ "w11", Color(255, 215, 110), Color(200, 140, 40), Color(90, 55, 15) },     // Dourado
				{ "w12", Color(150, 90, 240), Color(80, 40, 170),  Color(25, 12, 70) },      // Roxo
			};
			for (const W& w : t) if (id == w.id) { a = w.a; b = w.b; c = w.c; return true; }
			return false;
		}
		// predefinidos desconhecidos (criados depois pelo painel) caem num degrade derivado do numero: nunca fica em branco
		inline void WallpaperColorsOrDerived(const std::string& id, Color& a, Color& b, Color& c)
		{
			if (WallpaperColors(id, a, b, c)) return;
			unsigned h = 2166136261u; for (char ch : id) { h ^= (unsigned char)ch; h *= 16777619u; }
			a = Color((h & 0xFF) / 2 + 80, ((h >> 8) & 0xFF) / 2 + 80, ((h >> 16) & 0xFF) / 2 + 100);
			b = Color::mix(a, Color(0, 0, 0), 0.55f); c = Color::mix(a, Color(0, 0, 40), 0.8f);
		}
		// desenha o papel de parede na tela (r). Foto: textura pronta (cobre o retangulo) ou, enquanto baixa, o degrade padrao.
		inline void DrawWallpaper(Canvas& cv, PhoneContext* c, const Rect& r)
		{
			const std::string& w = c ? c->wallpaper : std::string("w1");
			if (IsPhotoWp(w) && c && c->photos)
			{
				PhotoTex t;
				if (c->photos->texture("R:" + w.substr(2), t) && t.id && t.w > 0 && t.h > 0)
				{
					const float ra = r.w / r.h, ia = (float)t.w / (float)t.h;
					Vec2 a{ 0, 0 }, b{ 1, 1 };
					if (ia > ra) { const float k = ra / ia; a.x = (1 - k) * 0.5f; b.x = 1 - a.x; } else { const float k = ia / ra; a.y = (1 - k) * 0.5f; b.y = 1 - a.y; }
					cv.image(r, t.id, a, b);
					cv.fillRect(r, Color(0, 0, 0, 38), 0);          // leve escurecida: os rotulos dos icones continuam legiveis
					return;
				}
			}
			Color c0, c1, c2; WallpaperColorsOrDerived(IsPreset(w) ? w : std::string("w1"), c0, c1, c2);
			cv.fillGradientV({ r.x, r.y, r.w, r.h * 0.45f }, c0, c1);
			cv.fillGradientV({ r.x, r.y + r.h * 0.45f, r.w, r.h * 0.55f + 1 }, c1, c2);
		}

		class Service
		{
		public:
			// ---- estado lido pela interface / PhoneManager ----
			bool meLoaded = false;           // o servidor ja respondeu sobre o aparelho
			bool noPhone = false;            // o servidor do jogo disse: nenhum celular no inventario (nao consulta a Loja)
			bool hasDevice = false, blocked = false;
			std::string imei, wallpaper = "w1";
			std::vector<AppEntry> apps; bool catalogLoaded = false;
			std::vector<WpEntry> wallpapers; bool wpLoaded = false;
			bool uploading = false;
			int version = 0;                 // sobe a cada mudanca (o PhoneManager reaplica)
			std::string takeToast() { std::string t; t.swap(m_toast); return t; }

			explicit Service(Gram::Service* net) : m_net(net) {}

			void reset()      // reconexao: esquece tudo (o servidor decide de novo)
			{
				meLoaded = hasDevice = blocked = catalogLoaded = wpLoaded = uploading = noPhone = false;
				imei.clear(); wallpaper = "w1"; apps.clear(); wallpapers.clear(); m_busy.clear(); m_loading = false; m_at = 0; m_toast.clear(); ++version;
			}
			void markNoPhone()   // sem celular no inventario: o aparelho mostra o aviso e nada abre
			{
				noPhone = true; meLoaded = true; hasDevice = false; blocked = false; imei.clear(); apps.clear(); catalogLoaded = false; ++version;
			}
			bool busy(const std::string& id) const { return m_busy.count(id) != 0; }
			std::set<std::string> installedIds() const { std::set<std::string> s; for (auto& a : apps) if (a.installed || a.core) s.insert(a.id); return s; }
			const AppEntry* find(const std::string& id) const { for (auto& a : apps) if (a.id == id) return &a; return nullptr; }

			// consulta aparelho + catalogo + papeis. Sem force, so repete depois de 2 minutos.
			void refresh(int64_t now, bool force = false)
			{
				if (!m_net || m_loading || noPhone) return;
				if (!force && meLoaded && now - m_at < 120000) return;
				m_loading = true; m_at = now;
				m_net->request("GET", "/s/me?t={T}", "", "", 8000, [this, now](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					if (!ok && code != "nodevice") { m_loading = false; m_at = now - 105000; if (!meLoaded) m_toast = ErrText(code); return; }   // tenta de novo em 15 s   // sem resposta: nao conclui nada (tenta de novo)
					meLoaded = true;
					if (!ok) { hasDevice = false; imei.clear(); apps.clear(); catalogLoaded = false; m_loading = false; ++version; return; }
					const auto f = L.empty() ? std::vector<std::string>() : Gram::Split(L[0], '\t'); long long bl = 0;
					if (f.size() < 3 || f[0].size() != 15 || !Gram::ToInt64(f[2], bl) || !ValidWallpaper(f[1])) { m_loading = false; m_at = now - 105000; m_toast = "Resposta inválida da Loja."; return; }
					hasDevice = true; imei = f[0]; wallpaper = f[1]; blocked = bl != 0; ++version;
					loadCatalog(now);
				}, now);
			}
			void install(const std::string& id, int64_t now) { toggle(id, true, now); }
			void uninstall(const std::string& id, int64_t now) { toggle(id, false, now); }

			void setWallpaper(const std::string& w, int64_t now)
			{
				if (!IsPreset(w) || !m_net || !hasDevice) return;
				const std::string prev = wallpaper; wallpaper = w; ++version;
				m_net->request("POST", "/s/wallpaper?t={T}&w=" + w, "", "", 8000, [this, prev](bool ok, const std::string& code, const std::vector<std::string>&)
				{
					if (!ok) { wallpaper = prev; ++version; m_toast = ErrText(code); }
				}, now);
			}
			// "" = enviando; senao o texto do erro (validacao local)
			std::string uploadWallpaper(const std::string& jpeg, int64_t now)
			{
				if (!m_net || !hasDevice) return ErrText("nodevice");
				if (uploading) return "Aguarde: foto sendo enviada.";
				if (jpeg.size() < 200 || jpeg.size() > 90 * 1024) return "Foto fora do tamanho permitido.";
				uploading = true;
				m_net->request("POST", "/s/wpupload?t={T}", jpeg, "image/jpeg", 20000, [this](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					uploading = false;
					if (!ok) { m_toast = ErrText(code); return; }
					if (!L.empty() && IsPhotoWp(L[0])) { wallpaper = L[0]; ++version; m_toast = "Papel de parede atualizado."; }
				}, now);
				return std::string();
			}

		private:
			Gram::Service* m_net; std::string m_toast; std::set<std::string> m_busy; bool m_loading = false; int64_t m_at = 0;

			void loadCatalog(int64_t now)
			{
				m_net->request("GET", "/s/catalog?t={T}", "", "", 8000, [this, now](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					if (!ok) { m_loading = false; m_toast = ErrText(code); return; }
					std::vector<AppEntry> v; for (auto& l : L) { AppEntry a; if (ParseApp(l, a)) v.push_back(a); }
					apps.swap(v); catalogLoaded = true; ++version;
					m_net->request("GET", "/s/wallpapers?t={T}", "", "", 8000, [this](bool ok2, const std::string&, const std::vector<std::string>& W)
					{
						m_loading = false;
						if (!ok2) return;
						std::vector<WpEntry> w;
						for (auto& l : W) { const auto f = Gram::Split(l, '\t'); if (f.size() >= 2 && IsPreset(f[0])) w.push_back({ f[0], f[1] }); }
						wallpapers.swap(w); wpLoaded = true; ++version;
					}, now);
				}, now);
			}
			void toggle(const std::string& id, bool on, int64_t now)
			{
				if (!m_net || !hasDevice || !ValidAppId(id) || m_busy.count(id)) return;
				m_busy.insert(id);
				m_net->request("POST", std::string(on ? "/s/install" : "/s/uninstall") + "?t={T}&id=" + id, "", "", 8000, [this, id, on, now](bool ok, const std::string& code, const std::vector<std::string>&)
				{
					m_busy.erase(id);
					if (ok || (on && code == "already") || (!on && code == "notinstalled"))
					{
						for (auto& a : apps) if (a.id == id) a.installed = on || a.core;
						++version; return;
					}
					m_toast = ErrText(code);
					refresh(now, true);                          // o servidor e a verdade: recarrega (ex.: o app foi ocultado pelo admin)
				}, now);
			}
		};
	}
}
