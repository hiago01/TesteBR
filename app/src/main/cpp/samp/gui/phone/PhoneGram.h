#pragma once
// Celular - GRAM (rede social de fotos): logica do servico. Fala direto com o servico de fotos do servidor por HTTP (PhoneHttp.h);
// o gamemode so entrega um token curto (RPC op 6 / 30). Nada aqui conhece ImGui nem o jogo. Logica pura (testada no PC contra o servico Node).
// Honestidade: sem token/servidor nada e simulado; o app mostra o erro real. Toda acao tem tempo limite e no maximo 1 nova tentativa.
#include "PhoneHttp.h"
#include <cstdio>
#include <functional>
#include <map>

namespace Phone
{
	namespace Gram
	{
		// number = AUTOR do post/comentario: "g<id>" (conta do Gram) ou "n<numero>" (post antigo, de antes das contas)
		struct Post { long long id = 0; std::string number, name, media, caption; int likes = 0, comments = 0; bool liked = false; int ageS = 0; };
		struct Comment { long long id = 0; std::string number, name, body; int ageS = 0; };

		inline std::string AgeText(int s)
		{
			if (s < 60) return "agora";
			if (s < 3600) return std::to_string(s / 60) + " min";
			if (s < 86400) return std::to_string(s / 3600) + " h";
			return std::to_string(s / 86400) + " d";
		}
		inline bool ValidMedia(const std::string& m)
		{
			if (m.size() != 32) return false;
			for (char c : m) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
			return true;
		}
		inline std::string ErrText(const std::string& c)
		{
			if (c == "net") return "Sem conexão com o servidor de fotos.";
			if (c == "rate") return "Muitas ações seguidas. Aguarde um pouco.";
			if (c == "auth") return "Sessão expirada. Tente de novo.";
			if (c == "jpeg" || c == "dims") return "Foto inválida.";
			if (c == "size") return "Foto grande demais.";
			if (c == "nopost") return "Esse post não existe mais.";
			if (c == "empty") return "Escreva algo primeiro.";
			if (c == "long") return "Texto muito longo (máx. 140).";
			if (c == "text") return "Texto com caracteres inválidos.";
			if (c == "full") return "Esse post atingiu o limite de comentários.";
			if (c == "busy") return "Servidor ocupado. Tente de novo.";
			if (c == "token") return "O servidor não liberou o Gram.";
			if (c == "wait") return "Sem resposta do servidor.";
			if (c == "nologin") return "Entre numa conta do Gram.";
			if (c == "badlogin") return "Usuário ou senha incorretos.";
			if (c == "locked") return "Muitas tentativas. Aguarde alguns minutos.";
			if (c == "badname") return "Usuário: 3 a 20 letras minúsculas, números, _ ou .";
			if (c == "badpass") return "Senha: de 6 a 48 caracteres.";
			if (c == "taken") return "Esse usuário já existe.";
			if (c == "nodevice") return "Celular não reconhecido pelo servidor.";
			return "Erro do Gram.";
		}
		inline std::vector<std::string> Split(const std::string& s, char sep)
		{
			std::vector<std::string> v; size_t i = 0;
			for (;;) { const size_t j = s.find(sep, i); if (j == std::string::npos) { v.push_back(s.substr(i)); break; } v.push_back(s.substr(i, j - i)); i = j + 1; }
			return v;
		}
		// "ok\nL1\nL2\n" -> true + linhas; "err\ncodigo\n" -> false + code
		inline bool ParseReply(const std::string& body, std::vector<std::string>& lines, std::string& code)
		{
			lines.clear(); code.clear();
			size_t i = 0; bool first = true; bool ok = false;
			while (i <= body.size())
			{
				size_t j = body.find('\n', i); if (j == std::string::npos) j = body.size();
				std::string l = body.substr(i, j - i); if (!l.empty() && l.back() == '\r') l.pop_back();
				i = j + 1;
				if (first) { first = false; if (l == "ok") ok = true; else if (l == "err") ok = false; else { code = "resp"; return false; } continue; }
				if (l.empty() && j >= body.size()) break;
				if (ok) lines.push_back(l); else { code = l; break; }
			}
			if (!ok && code.empty()) code = "resp";
			return ok;
		}
		inline bool ToInt64(const std::string& s, long long& v)
		{
			if (s.empty() || s.size() > 18) return false;
			long long x = 0; for (char c : s) { if (c < '0' || c > '9') return false; x = x * 10 + (c - '0'); }
			v = x; return true;
		}
		inline bool ParsePost(const std::string& line, Post& p)
		{
			const auto f = Split(line, '\t');
			if (f.size() < 9) return false;
			long long a = 0, b = 0, c = 0, d = 0;
			if (!ToInt64(f[0], p.id) || !ToInt64(f[4], a) || !ToInt64(f[5], b) || !ToInt64(f[6], c) || !ToInt64(f[7], d) || !ValidMedia(f[3])) return false;
			p.number = f[1]; p.name = f[2]; p.media = f[3]; p.likes = (int)a; p.comments = (int)b; p.liked = c != 0; p.ageS = (int)std::min<long long>(d, 2000000000);
			p.caption = f[8];
			for (size_t i = 9; i < f.size(); ++i) p.caption += "\t" + f[i];
			return true;
		}
		inline bool ParseComment(const std::string& line, Comment& k)
		{
			const auto f = Split(line, '\t'); long long a = 0;
			if (f.size() < 5 || !ToInt64(f[0], k.id) || !ToInt64(f[3], a)) return false;
			k.number = f[1]; k.name = f[2]; k.ageS = (int)std::min<long long>(a, 2000000000); k.body = f[4];
			return true;
		}

		class Service
		{
		public:
			// ---- ligacoes (adaptador / testes) ----
			std::function<bool(int64_t now)> requestToken;           // pede token ao servidor do jogo (PhoneNet::requestGramToken)
			std::function<std::string()> host;                       // endereco do servidor ao qual o jogador esta conectado

			// ---- estado lido pela interface ----
			std::vector<Post> posts; bool loadingFeed = false, hasMore = false, feedLoaded = false;
			std::vector<Comment> comments; long long commentsPost = 0; bool loadingComments = false;
			int postState = 0;                                       // 0 livre, 1 enviando, 2 concluido, 3 falhou (postError)
			std::string postError;
			std::string takeToast() { std::string t; t.swap(m_toast); return t; }

			// ---- conta do Gram (por APARELHO: o servidor lembra qual conta esta conectada neste celular) ----
			int accountState = 0;                                    // 0 desconhecido, 1 consultando, 2 nenhuma conta conectada, 3 conectada
			std::string username, authError; long long gid = 0; bool authBusy = false;
			std::string ownAuthor() const { return accountState == 3 ? "g" + std::to_string(gid) : std::string(); }

			explicit Service(HttpClient* http) : m_http(http) {}

			bool hasToken() const { return !m_token.empty(); }
			int port() const { return m_port; }
			void reset()      // conexao com o servidor reiniciada: esquece sessao e tudo que dependia dela
			{
				m_token.clear(); m_port = 0; m_wait.clear(); m_flight.clear(); m_asking = false;
				accountState = 0; username.clear(); authError.clear(); gid = 0; authBusy = false; m_meRetry = 0;
				posts.clear(); comments.clear(); loadingFeed = loadingComments = hasMore = feedLoaded = false; postState = 0; postError.clear(); m_toast.clear();
			}
			void onToken(int status, const std::string& token, int port, int64_t now)
			{
				m_asking = false;
				if (status != 0 || token.empty()) { failWaiting("token", now); return; }
				m_token = token; m_port = port; m_tokenAt = now;
				std::vector<Job> w; w.swap(m_wait);
				for (auto& j : w) dispatch(std::move(j), now);
			}

			// ---------------- conta ----------------
			void loadMe(int64_t now)
			{
				if (accountState != 0 || now < m_meRetry) return;
				accountState = 1;
				Job j; j.method = "GET"; j.path = "/g/me?t={T}"; j.done = [this, now](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					const auto f = (ok && !L.empty()) ? Split(L[0], '\t') : std::vector<std::string>();
					long long id = 0;
					if (ok && f.size() == 2 && ToInt64(f[0], id) && !f[1].empty()) { gid = id; username = f[1]; accountState = 3; return; }
					if (!ok && code == "nologin") { accountState = 2; return; }
					accountState = 0; m_meRetry = m_lastNow + 15000; m_toast = ErrText(ok ? "resp" : code);      // tenta de novo em 15 s
				};
				submit(std::move(j), now);
			}
			// "" = enviado; senao o erro (validacao local). reg = criar conta nova.
			std::string login(std::string user, const std::string& pass, bool reg, int64_t now)
			{
				if (authBusy) return "Aguarde...";
				while (!user.empty() && user[0] == '@') user.erase(0, 1);
				for (auto& c : user) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
				bool okName = user.size() >= 3 && user.size() <= 20;
				for (char c : user) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) okName = false;
				if (!okName) return ErrText("badname");
				if (pass.size() < 6 || pass.size() > 48) return ErrText("badpass");
				authBusy = true; authError.clear();
				Job j; j.method = "POST"; j.path = std::string(reg ? "/g/register" : "/g/login") + "?t={T}"; j.body = user + "\n" + pass; j.contentType = "text/plain; charset=utf-8";
				j.done = [this](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					authBusy = false;
					const auto f = (ok && !L.empty()) ? Split(L[0], '\t') : std::vector<std::string>(); long long id = 0;
					if (!(ok && f.size() == 2 && ToInt64(f[0], id))) { authError = ErrText(ok ? "resp" : code); return; }
					gid = id; username = f[1]; accountState = 3; authError.clear();
					posts.clear(); comments.clear(); feedLoaded = false; loadingFeed = false; refresh(m_lastNow);
				};
				submit(std::move(j), now);
				return std::string();
			}
			void logout(int64_t now)
			{
				Job j; j.method = "POST"; j.path = "/g/logout?t={T}"; j.done = [](bool, const std::string&, const std::vector<std::string>&) {};
				submit(std::move(j), now);
				accountState = 2; username.clear(); gid = 0; posts.clear(); comments.clear(); feedLoaded = false; loadingFeed = false; hasMore = false;
			}

			// ---------------- acoes ----------------
			void refresh(int64_t now)
			{
				if (loadingFeed) return;
				loadingFeed = true;
				Job j; j.method = "GET"; j.path = "/g/feed?t={T}&b=0"; j.done = [this](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					loadingFeed = false;
					if (!ok) { m_toast = ErrText(code); return; }
					posts.clear(); appendPosts(L); feedLoaded = true;
				};
				submit(std::move(j), now);
			}
			void more(int64_t now)
			{
				if (loadingFeed || !hasMore || posts.empty()) return;
				loadingFeed = true;
				Job j; j.method = "GET"; j.path = "/g/feed?t={T}&b=" + std::to_string(posts.back().id); j.done = [this](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					loadingFeed = false;
					if (!ok) { m_toast = ErrText(code); return; }
					appendPosts(L);
				};
				submit(std::move(j), now);
			}
			void like(long long id, int64_t now)
			{
				Post* p = find(id); if (!p) return;
				const bool prevL = p->liked; const int prevN = p->likes;
				p->liked = !prevL; p->likes = std::max(0, prevN + (p->liked ? 1 : -1));
				Job j; j.method = "POST"; j.path = "/g/like?t={T}&p=" + std::to_string(id);
				j.done = [this, id, prevL, prevN](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					Post* q = find(id); if (!q) return;
					if (!ok) { q->liked = prevL; q->likes = prevN; m_toast = ErrText(code); return; }
					const auto f = Split(L.empty() ? std::string() : L[0], '\t'); long long a = 0, b = 0;
					if (f.size() == 2 && ToInt64(f[0], a) && ToInt64(f[1], b)) { q->liked = a != 0; q->likes = (int)b; }
				};
				submit(std::move(j), now);
			}
			void loadComments(long long id, int64_t now)
			{
				commentsPost = id; comments.clear(); loadingComments = true;
				Job j; j.method = "GET"; j.path = "/g/comments?t={T}&p=" + std::to_string(id);
				j.done = [this, id](bool ok, const std::string& code, const std::vector<std::string>& L)
				{
					if (commentsPost != id) return;               // o usuario ja abriu outro post
					loadingComments = false;
					if (!ok) { m_toast = ErrText(code); return; }
					comments.clear(); for (auto& l : L) { Comment k; if (ParseComment(l, k)) comments.push_back(k); }
				};
				submit(std::move(j), now);
			}
			// "" = enviado; senao o texto do erro (validacao local)
			std::string comment(long long id, const std::string& text, int64_t now)
			{
				if (text.empty()) return ErrText("empty");
				if (text.size() > 600) return ErrText("long");
				Job j; j.method = "POST"; j.path = "/g/comment?t={T}&p=" + std::to_string(id); j.body = text; j.contentType = "text/plain; charset=utf-8";
				j.done = [this, id, now](bool ok, const std::string& code, const std::vector<std::string>&)
				{
					if (!ok) { m_toast = ErrText(code); return; }
					if (Post* p = find(id)) ++p->comments;
					loadComments(id, now);
				};
				submit(std::move(j), now);
				return std::string();
			}
			std::string post(const std::string& jpeg, const std::string& caption, int64_t now)
			{
				if (postState == 1) return "Aguarde: foto sendo enviada.";
				if (jpeg.size() < 200 || jpeg.size() > 90 * 1024) return "Foto fora do tamanho permitido.";
				postState = 1; postError.clear();
				Job j; j.method = "POST"; j.path = "/g/post?t={T}&c=" + HttpClient::UrlEncode(caption.substr(0, 400)); j.body = jpeg; j.contentType = "image/jpeg"; j.timeoutMs = 20000;
				j.done = [this, now](bool ok, const std::string& code, const std::vector<std::string>&)
				{
					if (!ok) { postState = 3; postError = ErrText(code); return; }
					postState = 2; feedLoaded = false; loadingFeed = false; refresh(m_lastNow);
				};
				submit(std::move(j), now);
				return std::string();
			}
			void remove(long long id, int64_t now)
			{
				Job j; j.method = "POST"; j.path = "/g/del?t={T}&p=" + std::to_string(id);
				j.done = [this, id](bool ok, const std::string& code, const std::vector<std::string>&)
				{
					if (!ok) { m_toast = ErrText(code); return; }
					for (size_t i = 0; i < posts.size(); ++i) if (posts[i].id == id) { posts.erase(posts.begin() + i); break; }
				};
				submit(std::move(j), now);
			}
			// pedido generico (Loja / aparelho / papel de parede: rotas /s/*). path pode ter {T} (vira o token). done roda na thread de desenho.
			using Done = std::function<void(bool, const std::string&, const std::vector<std::string>&)>;
			void request(const std::string& method, const std::string& path, const std::string& body, const std::string& contentType, int timeoutMs, Done done, int64_t now)
			{
				Job j; j.method = method; j.path = path; j.body = body; if (!contentType.empty()) j.contentType = contentType; j.timeoutMs = timeoutMs; j.done = std::move(done);
				submit(std::move(j), now);
			}
			// baixa uma imagem publica. cb(ok, bytes) roda na thread de desenho (dentro de pump). false = nao deu para iniciar.
			bool fetchImage(const std::string& media, std::function<void(bool, const std::string&)> cb)
			{
				if (!ValidMedia(media) || !m_http || m_port <= 0) return false;
				const std::string h = host ? host() : std::string(); if (h.empty()) return false;
				HttpReq r; r.method = "GET"; r.host = h; r.port = m_port; r.path = "/g/img/" + media + ".jpg"; r.maxResp = 160 * 1024; r.timeoutMs = 10000; r.tag = ++m_tag;
				if (!m_http->submit(r)) return false;
				Flight f; f.img = std::move(cb); m_flight[r.tag] = std::move(f);
				return true;
			}

			// thread de desenho, todo quadro
			void pump(int64_t now)
			{
				m_lastNow = now;
				if (m_http)
				{
					std::vector<HttpRes> rs; m_http->poll(rs);
					for (auto& r : rs) onResponse(r, now);
				}
				// sessao: espera maxima de 10 s por token (repete o pedido a cada quadro ate o PhoneNet deixar; ele limita a 1 a cada 6 s)
				if (!m_wait.empty() && m_token.empty())
				{
					if (!m_asking) ask(now);
					if (now - m_waitSince > 10000) { m_asking = false; failWaiting("wait", now); }
				}
				// token perto de vencer (6 h): renova sozinho na proxima acao
				if (!m_token.empty() && now - m_tokenAt > 5 * 3600 * 1000LL) m_token.clear();
			}

		private:
			struct Job
			{
				std::string method, path, body, contentType = "application/x-www-form-urlencoded"; int timeoutMs = 8000; int tries = 0;
				std::function<void(bool, const std::string&, const std::vector<std::string>&)> done;
			};
			struct Flight { Job job; std::function<void(bool, const std::string&)> img; };
			HttpClient* m_http; std::string m_token, m_toast; int m_port = 0; int64_t m_tokenAt = 0, m_waitSince = 0, m_lastNow = 0, m_meRetry = 0;
			bool m_asking = false; std::vector<Job> m_wait; std::map<uint32_t, Flight> m_flight; uint32_t m_tag = 0;

			Post* find(long long id) { for (auto& p : posts) if (p.id == id) return &p; return nullptr; }
			void appendPosts(const std::vector<std::string>& L)
			{
				int got = 0;
				for (auto& l : L)
				{
					Post p; if (!ParsePost(l, p)) continue; ++got;
					bool dup = false; for (auto& q : posts) if (q.id == p.id) { dup = true; break; }
					if (!dup) posts.push_back(p);
				}
				hasMore = got >= 8;
			}
			void ask(int64_t now)
			{
				if (requestToken && requestToken(now)) m_asking = true;
				// se nao deu para pedir agora (muito cedo / sem rede) tenta de novo no proximo quadro; o limite de 10 s encerra
			}
			void submit(Job&& j, int64_t now)
			{
				m_lastNow = now;
				if (m_token.empty())
				{
					if (m_wait.size() >= 8) { if (j.done) j.done(false, "busy", {}); return; }
					if (m_wait.empty()) m_waitSince = now;
					m_wait.push_back(std::move(j));
					if (!m_asking) ask(now);
					return;
				}
				dispatch(std::move(j), now);
			}
			void dispatch(Job&& j, int64_t)
			{
				const std::string h = host ? host() : std::string();
				if (h.empty() || !m_http) { if (j.done) j.done(false, "net", {}); return; }
				HttpReq r; r.method = j.method; r.host = h; r.port = m_port; r.body = j.body; r.contentType = j.contentType; r.timeoutMs = j.timeoutMs; r.tag = ++m_tag;
				r.path = j.path; const size_t k = r.path.find("{T}"); if (k != std::string::npos) r.path.replace(k, 3, m_token);
				if (!m_http->submit(r)) { if (j.done) j.done(false, "busy", {}); return; }
				Flight f; f.job = std::move(j); m_flight[r.tag] = std::move(f);
			}
			void failWaiting(const char* code, int64_t)
			{
				std::vector<Job> w; w.swap(m_wait);
				for (auto& j : w) if (j.done) j.done(false, code, {});
			}
			void onResponse(const HttpRes& r, int64_t now)
			{
				auto it = m_flight.find(r.tag); if (it == m_flight.end()) return;
				Flight f = std::move(it->second); m_flight.erase(it);
				if (f.img) { f.img(r.status == 200, r.status == 200 ? r.body : std::string()); return; }
				std::vector<std::string> lines; std::string code; bool ok = false;
				if (r.status == 0) code = "net";
				else if (r.status == 401) code = "auth";
				else if (r.status == 429) code = "rate";
				else if (r.status == 413) code = "size";
				else if (r.status == 503) code = "busy";
				else ok = ParseReply(r.body, lines, code);
				if (!ok && code == "auth" && f.job.tries == 0)       // token recusado: pede outro e repete UMA vez
				{
					f.job.tries = 1; m_token.clear();
					if (m_wait.empty()) m_waitSince = now;
					m_wait.push_back(std::move(f.job));
					m_asking = false; ask(now);
					return;
				}
				if (f.job.done) f.job.done(ok, code, lines);
			}
		};
	}
}
