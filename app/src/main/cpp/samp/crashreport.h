#pragma once
// ---------------------------------------------------------------------------------------------------------------------------
//  Relatorio de crash POR JOGADOR (nativo).
//    - Mark(tag,a,b)      : "migalhas" (ultimas 32 etapas: fase de carga, modelo pedido ao streaming, conexao...). Barato e sem alocacao.
//    - SetIdentity(...)   : nick / servidor do jogador (vai no relatorio).
//    - Write(sig,...)     : chamado pelos handlers de SIGSEGV/SIGABRT/SIGFPE/SIGBUS ANTES de encadear o handler anterior (que
//                           encerra o processo). So usa open/write/close, sem malloc nem stdio, para ser seguro dentro do sinal.
//  Arquivo: <storage>SAMP/last_crash.txt (texto, ~1-2 KB). O launcher le no proximo abrir, junta aparelho/Android/GPU/versao e envia
//  ao Crashlytics (nao-fatal) com o nick do jogador (ver MainActivity.reportLastCrash).
// ---------------------------------------------------------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

extern char* g_pszStorage;
extern uintptr_t g_libGTASA;
extern uintptr_t g_libSAMP;
extern int g_iLastRenderedObject;

namespace CrashReport
{
	struct Crumb { char tag[20]; int a; int b; };
	inline Crumb                 g_crumbs[32];
	inline std::atomic<uint32_t> g_next{ 0 };
	inline char                  g_nick[32]  = "";
	inline char                  g_host[48]  = "";
	inline int                   g_port      = 0;
	inline std::atomic<bool>     g_writing{ false };

	inline void Mark(const char* tag, int a = 0, int b = 0)
	{
		Crumb& c = g_crumbs[g_next.fetch_add(1, std::memory_order_relaxed) & 31];
		strncpy(c.tag, tag, sizeof(c.tag) - 1); c.tag[sizeof(c.tag) - 1] = 0;
		c.a = a; c.b = b;
	}

	inline void SetIdentity(const char* nick, const char* host, int port)
	{
		if (nick) { strncpy(g_nick, nick, sizeof(g_nick) - 1); g_nick[sizeof(g_nick) - 1] = 0; }
		if (host) { strncpy(g_host, host, sizeof(g_host) - 1); g_host[sizeof(g_host) - 1] = 0; }
		g_port = port;
	}

	namespace detail
	{
		struct Out { char buf[2048]; int n = 0; };
		inline void S(Out& o, const char* s) { while (*s && o.n < (int)sizeof(o.buf) - 1) o.buf[o.n++] = *s++; }
		inline void D(Out& o, long long v)
		{
			char t[24]; int k = 0; bool neg = v < 0; unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
			do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u && k < 22);
			if (neg) S(o, "-");
			while (k) { char c[2] = { t[--k], 0 }; S(o, c); }
		}
		inline void X(Out& o, unsigned long long v)
		{
			S(o, "0x"); char t[20]; int k = 0;
			do { t[k++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && k < 18);
			while (k) { char c[2] = { t[--k], 0 }; S(o, c); }
		}
	}

	// Seguro dentro de handler de sinal (sem malloc/stdio). Escreve so uma vez por processo.
	inline void Write(int sig, siginfo_t* info, void* ctxPtr)
	{
		if (g_writing.exchange(true)) return;
		if (!g_pszStorage || !g_pszStorage[0]) return;

		char path[300]; int pn = 0;
		for (const char* s = g_pszStorage; *s && pn < 260; ) path[pn++] = *s++;
		for (const char* s = "SAMP/last_crash.txt"; *s; ) path[pn++] = *s++;
		path[pn] = 0;

		detail::Out o;
		detail::S(o, "CRASH v1\nsignal="); detail::D(o, sig);
		detail::S(o, "\nfault="); detail::X(o, info ? (unsigned long long)(uintptr_t)info->si_addr : 0);
		detail::S(o, "\ncode="); detail::D(o, info ? info->si_code : 0);
		detail::S(o, "\ntime="); detail::D(o, (long long)time(nullptr));
		if (ctxPtr)
		{
			ucontext_t* c = (ucontext_t*)ctxPtr;
#if defined(__aarch64__)
			const uintptr_t pc = (uintptr_t)c->uc_mcontext.pc, lr = (uintptr_t)c->uc_mcontext.regs[30];
#else
			const uintptr_t pc = 0, lr = 0; (void)c;   // so ARM64 tem pc/lr; outros so para teste no host
#endif
			detail::S(o, "\npc_gta="); detail::X(o, g_libGTASA ? pc - g_libGTASA : 0);
			detail::S(o, "\nlr_gta="); detail::X(o, g_libGTASA ? lr - g_libGTASA : 0);
			detail::S(o, "\npc_samp="); detail::X(o, g_libSAMP ? pc - g_libSAMP : 0);
			detail::S(o, "\nlr_samp="); detail::X(o, g_libSAMP ? lr - g_libSAMP : 0);
		}
		detail::S(o, "\nlast_render="); detail::D(o, g_iLastRenderedObject);
		detail::S(o, "\nnick="); detail::S(o, g_nick);
		detail::S(o, "\nserver="); detail::S(o, g_host); detail::S(o, ":"); detail::D(o, g_port);
		detail::S(o, "\ncrumbs(antigo->novo):\n");
		const uint32_t end = g_next.load(std::memory_order_relaxed);
		const uint32_t cnt = end < 32 ? end : 32;
		for (uint32_t i = 0; i < cnt; i++)
		{
			const Crumb& c = g_crumbs[(end - cnt + i) & 31];
			detail::S(o, c.tag); detail::S(o, " "); detail::D(o, c.a); detail::S(o, " "); detail::D(o, c.b); detail::S(o, "\n");
		}

		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)!write(fd, o.buf, (size_t)o.n); close(fd); }
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
//  Saude dos arquivos do jogo (fora de sinal: pode usar stdio). O cliente conta o que precisou ser contornado
//  (modelo/textura sem entrada no IMG, arquivo de dados que nao abre) e grava <storage>SAMP/health.txt; o launcher le no
//  proximo abrir e avisa o jogador (MainActivity.showHealthNotice). Uma execucao saudavel apaga o arquivo (Reset).
// ---------------------------------------------------------------------------------------------------------------------------
namespace Health
{
	inline int  g_fbModels = 0, g_fbTxd = 0, g_removed = 0, g_missing = 0;
	inline char g_missingList[200] = "";
	inline int  g_writes = 0;

	inline bool Path(char* out, size_t n)
	{
		if (!g_pszStorage || !g_pszStorage[0]) return false;
		snprintf(out, n, "%sSAMP/health.txt", g_pszStorage);
		return true;
	}
	// Arquivos que o jogo original tambem nao encontra (opcionais): nao contam como problema.
	inline bool IsOptionalFile(const char* name)
	{
		if (!name) return true;
		const char* base = name; for (const char* p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
		static const char* opt[] = { "Adjustable.cfg", "GTASAsf10.b", "gta3.ini", "CUTSCENE.IMG",
		                            "gta_sa.set", "CINFO.BIN", "MINFO.BIN" };   // gta_sa.set = opcoes (criado ao salvar); CINFO/MINFO = caches que o jogo recria sozinho
		for (const char* o : opt) if (strcasecmp(base, o) == 0) return true;
		return strncasecmp(base, "scache", 6) == 0;
	}
	inline void Flush()
	{
		char path[320]; if (!Path(path, sizeof(path)) || g_writes++ > 60) return;
		if (FILE* f = fopen(path, "w")) {
			fprintf(f, "HEALTH v1\nfallback_models=%d\nfallback_txd=%d\nremoved=%d\nmissing=%d\nmissing_list=%s\n", g_fbModels, g_fbTxd, g_removed, g_missing, g_missingList);
			fclose(f);
		}
	}
	inline void Reset() { char path[320]; if (Path(path, sizeof(path))) remove(path); g_fbModels = g_fbTxd = g_removed = g_missing = 0; g_missingList[0] = 0; g_writes = 0; }
	inline void NoteFallbackModel() { g_fbModels++; Flush(); }
	inline void NoteFallbackTxd()   { g_fbTxd++;    Flush(); }
	inline void NoteRemoved()       { g_removed++;  Flush(); }
	inline void NoteMissingFile(const char* name)
	{
		if (IsOptionalFile(name)) return;
		g_missing++;
		const size_t l = strlen(g_missingList), room = sizeof(g_missingList) - l - 1;
		if (room > 4) { snprintf(g_missingList + l, room + 1, "%s%s", l ? ";" : "", name ? name : "?"); }
		Flush();
	}
}
