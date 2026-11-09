#pragma once
// Celular - cliente HTTP minimo (HTTP/1.1, sem TLS) para falar com o servico de fotos do servidor (Gram).
// Leve e seguro para o jogo: ate 2 threads de trabalho; pedidos com tempo limite; resposta limitada em tamanho; nada roda na thread de desenho.
// A thread de desenho chama poll() e recebe as respostas prontas. Logica pura (testada no PC contra o servico Node real).
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace Phone
{
	struct HttpReq
	{
		std::string method = "GET", host, path, body, contentType = "application/octet-stream";
		int port = 80, timeoutMs = 8000;
		size_t maxResp = 256 * 1024;
		uint32_t tag = 0;
	};
	struct HttpRes
	{
		uint32_t tag = 0; int status = 0;      // 0 = falha de rede (error explica)
		std::string body, error;
	};

	class HttpClient
	{
	public:
		static constexpr size_t kMaxQueue = 24;
		explicit HttpClient(int workers = 2) { for (int i = 0; i < workers; ++i) m_th.emplace_back([this] { run(); }); }
		~HttpClient()
		{
			{ std::lock_guard<std::mutex> l(m_mx); m_stop = true; }
			m_cv.notify_all();
			for (auto& t : m_th) if (t.joinable()) t.join();
		}
		// false = fila cheia
		bool submit(const HttpReq& r)
		{
			{ std::lock_guard<std::mutex> l(m_mx); if (m_in.size() >= kMaxQueue) return false; m_in.push_back(r); }
			m_cv.notify_one();
			return true;
		}
		// thread de desenho: pega as respostas prontas
		void poll(std::vector<HttpRes>& out)
		{
			std::lock_guard<std::mutex> l(m_mx);
			while (!m_out.empty()) { out.push_back(std::move(m_out.front())); m_out.pop_front(); }
		}
		size_t pending() { std::lock_guard<std::mutex> l(m_mx); return m_in.size() + m_busy; }

		// ---- util (publico para testes) ----
		static std::string UrlEncode(const std::string& s)
		{
			static const char* H = "0123456789ABCDEF"; std::string o;
			for (unsigned char c : s)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') o.push_back((char)c);
				else { o.push_back('%'); o.push_back(H[c >> 4]); o.push_back(H[c & 15]); }
			}
			return o;
		}

	private:
		std::vector<std::thread> m_th; std::mutex m_mx; std::condition_variable m_cv;
		std::deque<HttpReq> m_in; std::deque<HttpRes> m_out; bool m_stop = false; size_t m_busy = 0;

		void run()
		{
			for (;;)
			{
				HttpReq r;
				{
					std::unique_lock<std::mutex> l(m_mx);
					m_cv.wait(l, [this] { return m_stop || !m_in.empty(); });
					if (m_stop) return;
					r = std::move(m_in.front()); m_in.pop_front(); ++m_busy;
				}
				HttpRes res = exec(r);
				{
					std::lock_guard<std::mutex> l(m_mx);
					--m_busy;
					if (m_out.size() < 64) m_out.push_back(std::move(res));
				}
			}
		}

		static int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		// espera o descritor ficar pronto ate 'deadline'; true = pronto
		static bool Wait(int fd, short ev, int64_t deadline)
		{
			for (;;)
			{
				const int64_t left = deadline - NowMs(); if (left <= 0) return false;
				struct pollfd p; p.fd = fd; p.events = ev; p.revents = 0;
				const int n = ::poll(&p, 1, (int)std::min<int64_t>(left, 1000));
				if (n > 0) return true;
				if (n < 0 && errno != EINTR) return false;
			}
		}

		static HttpRes Fail(uint32_t tag, const std::string& why) { HttpRes r; r.tag = tag; r.status = 0; r.error = why; return r; }

		HttpRes exec(const HttpReq& r)
		{
			const int64_t deadline = NowMs() + std::max(500, r.timeoutMs);
			struct addrinfo hints; std::memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
			struct addrinfo* ai = nullptr; const std::string ps = std::to_string(r.port);
			if (r.host.empty() || ::getaddrinfo(r.host.c_str(), ps.c_str(), &hints, &ai) != 0 || !ai) return Fail(r.tag, "dns");
			int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (fd < 0) { ::freeaddrinfo(ai); return Fail(r.tag, "socket"); }
			::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
			int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			int cr = ::connect(fd, ai->ai_addr, ai->ai_addrlen); ::freeaddrinfo(ai);
			if (cr < 0 && errno != EINPROGRESS) { ::close(fd); return Fail(r.tag, "connect"); }
			if (cr < 0)
			{
				if (!Wait(fd, POLLOUT, deadline)) { ::close(fd); return Fail(r.tag, "timeout"); }
				int err = 0; socklen_t el = sizeof(err); ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
				if (err) { ::close(fd); return Fail(r.tag, "connect"); }
			}
			// pedido
			std::string req = r.method + " " + r.path + " HTTP/1.1\r\nHost: " + r.host + ":" + ps + "\r\nConnection: close\r\nUser-Agent: maximo-celular\r\nAccept: */*\r\n";
			if (!r.body.empty() || r.method == "POST") req += "Content-Type: " + r.contentType + "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\n";
			req += "\r\n"; req += r.body;
			size_t sent = 0;
			while (sent < req.size())
			{
				if (!Wait(fd, POLLOUT, deadline)) { ::close(fd); return Fail(r.tag, "timeout"); }
				const ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
				if (n > 0) sent += (size_t)n;
				else if (n < 0 && errno != EAGAIN && errno != EINTR) { ::close(fd); return Fail(r.tag, "send"); }
			}
			// resposta ate fechar (Connection: close)
			std::string raw; char buf[8192]; bool eof = false;
			while (!eof)
			{
				if (!Wait(fd, POLLIN, deadline)) { ::close(fd); return Fail(r.tag, "timeout"); }
				const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
				if (n > 0) { raw.append(buf, (size_t)n); if (raw.size() > r.maxResp + 4096) { ::close(fd); return Fail(r.tag, "grande"); } }
				else if (n == 0) eof = true;
				else if (errno != EAGAIN && errno != EINTR) { ::close(fd); return Fail(r.tag, "recv"); }
			}
			::close(fd);
			return Parse(r.tag, raw, r.maxResp);
		}

		static HttpRes Parse(uint32_t tag, const std::string& raw, size_t maxResp)
		{
			const size_t he = raw.find("\r\n\r\n");
			if (he == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) return Fail(tag, "resposta");
			const size_t sp = raw.find(' '); if (sp == std::string::npos || sp > 12) return Fail(tag, "resposta");
			const int code = std::atoi(raw.c_str() + sp + 1);
			if (code < 100 || code > 599) return Fail(tag, "resposta");
			HttpRes res; res.tag = tag; res.status = code;
			std::string head = raw.substr(0, he); for (auto& c : head) c = (char)std::tolower((unsigned char)c);
			std::string body = raw.substr(he + 4);
			if (head.find("transfer-encoding: chunked") != std::string::npos)
			{
				std::string dec; size_t i = 0;
				for (;;)
				{
					const size_t e = body.find("\r\n", i); if (e == std::string::npos) return Fail(tag, "chunk");
					const size_t n = std::strtoul(body.c_str() + i, nullptr, 16); i = e + 2;
					if (n == 0) break;
					if (i + n > body.size() || dec.size() + n > maxResp) return Fail(tag, "chunk");
					dec.append(body, i, n); i += n + 2;
				}
				body.swap(dec);
			}
			else
			{
				const size_t cl = head.find("content-length:");
				if (cl != std::string::npos) { const size_t n = std::strtoul(head.c_str() + cl + 15, nullptr, 10); if (n > body.size()) return Fail(tag, "cortada"); body.resize(n); }
			}
			if (body.size() > maxResp) return Fail(tag, "grande");
			res.body.swap(body);
			return res;
		}
	};
}
