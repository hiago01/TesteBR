#pragma once
// ---------------------------------------------------------------------------
// SafeTexDb - carga de bancos de texturas (texdb/<nome>) A PROVA DE CRASH, para o jogador sempre conseguir abrir o jogo.
//
// Problema: TextureDatabaseRuntime::Load do libGTASA faz SIGSEGV em SortEntries quando o indice do banco esta
// ausente/truncado/corrompido (ex.: relatorio Xiaomi 23129RAA4G, falha em Initialise3D, 5 vezes igual).
//
// Estrategia (tenta TODAS as saidas ate uma funcionar):
//   1. Cada tentativa de Load roda "guardada": um handler temporario de SIGSEGV/SIGBUS (so para a thread que chamou)
//      volta com siglongjmp para ca em vez de derrubar o processo. Falhou -> tenta o proximo formato.
//   2. Ordem de formatos: o pedido; depois Default, ETC, DXT, PVR (sem repetir).
//   3. Memoria entre execucoes (SAMP/texdb_bad.txt): antes de cada tentativa grava SAMP/texdb_try.txt. Se o processo
//      morreu mesmo assim (crash fora do alcance do handler), na proxima abertura aquela combinacao nome:formato vira
//      "ruim" e e PULADA - cada abertura avanca para a proxima alternativa ate uma funcionar.
//      A lista guarda uma assinatura dos arquivos do banco (nome+tamanho+data): se o jogador consertar os arquivos
//      (VERIFICAR ARQUIVOS), a assinatura muda e a combinacao e testada de novo.
//   4. Nenhum formato serviu: loga, avisa o Health (aviso de arquivos incompletos) e segue SEM esse banco (nullptr).
// Tudo e registrado em SAMP/texdb_status.txt e no samp_log.txt ([TEXDB]) para a correcao com o jogador.
//
//   5. Banco MEIO CARREGADO: o Load do jogo ja poe o banco nas listas estaticas TextureDatabaseRuntime::loaded/registered
//      ANTES de ordenar o indice. Se ele falhar no meio, o banco quebrado ficava na lista e o 1o GetTexture (ex.:
//      CFont::Initialise -> CSprite2d::SetTexture -> RwTextureRead -> TxdStoreFindCB -> GetTexture) caia nele.
//      Por isso, apos qualquer falha as duas listas sao devolvidas ao tamanho de antes da tentativa, e todo banco que
//      "carregou" passa por uma varredura guardada do proprio indice (Verify) antes de ser aceito.
// LIMITE: um SIGSEGV dentro do Load pode deixar estado parcial no libGTASA; o objetivo e evitar o crash de abertura
// e dar pista do banco/formato que falha - nao substitui reparar os arquivos.
// ---------------------------------------------------------------------------

#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "../main.h"
#include "../log.h"
#include "../crashreport.h"
#include <dlfcn.h>
#include "../vendor/armhook/patch.h"
#include "game/Textures/TextureDatabaseRuntime.h"

namespace SafeTexDb
{
    namespace detail
    {
        inline sigjmp_buf*     g_jb = nullptr;
        inline pthread_t       g_tid{};
        inline struct sigaction g_oldSegv{}, g_oldBus{};

        inline void Handler(int sig, siginfo_t* si, void* ctx)
        {
            if (g_jb && pthread_equal(pthread_self(), g_tid)) siglongjmp(*g_jb, sig);   // so a thread que esta no Load
            const struct sigaction& old = (sig == SIGBUS) ? g_oldBus : g_oldSegv;       // qualquer outra: segue o fluxo normal
            if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction) { old.sa_sigaction(sig, si, ctx); return; }
            if (old.sa_handler && old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) { old.sa_handler(sig); return; }
            signal(sig, SIG_DFL);
            raise(sig);
        }

        inline std::string Root()
        {
            if (!g_pszStorage || !g_pszStorage[0]) return {};
            std::string p(g_pszStorage);
            if (p.back() != '/') p.push_back('/');
            return p;
        }
        inline std::string ReadAll(const std::string& path)
        {
            std::string out;
            if (FILE* f = std::fopen(path.c_str(), "r")) { char b[256]; size_t n; while ((n = std::fread(b, 1, sizeof(b), f)) > 0) out.append(b, n); std::fclose(f); }
            return out;
        }
        inline void WriteAll(const std::string& path, const std::string& s, const char* mode = "w")
        {
            if (path.empty()) return;
            if (FILE* f = std::fopen(path.c_str(), mode)) { std::fwrite(s.data(), 1, s.size(), f); std::fclose(f); }
        }
        // assinatura (nome+tamanho+data) de todos os arquivos de texdb/<nome>/ ; "ausente" se a pasta nao existe
        inline std::string Signature(const std::string& name, std::string* listing = nullptr)
        {
            const std::string dir = Root() + "texdb/" + name;
            DIR* d = opendir(dir.c_str());
            if (!d) return "ausente";
            uint64_t h = 1469598103934665603ULL; int n = 0;
            while (dirent* e = readdir(d))
            {
                if (e->d_name[0] == '.') continue;
                struct stat st {};
                if (stat((dir + "/" + e->d_name).c_str(), &st) != 0) continue;
                const std::string item = std::string(e->d_name) + ":" + std::to_string((long long)st.st_size) + ":" + std::to_string((long long)st.st_mtime);
                for (unsigned char c : item) { h ^= c; h *= 1099511628211ULL; }
                if (listing) { *listing += std::string(e->d_name) + "=" + std::to_string((long long)st.st_size) + " "; }
                n++;
            }
            closedir(d);
            return std::to_string(n) + "-" + std::to_string((unsigned long long)h);
        }
        inline const char* FmtName(int f)
        {
            switch (f) { case 0: return "unc"; case 1: return "dxt"; case 4: return "pvr"; case 5: return "etc"; case 6: return "default"; default: return "?"; }
        }
    }

    // Roda f() protegida contra SIGSEGV/SIGBUS (so desta thread). Retorna 0 = ok, senao o numero do sinal contido.
    template <class F>
    inline int Run(F&& f)
    {
        struct sigaction sa {};
        sa.sa_sigaction = detail::Handler;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &detail::g_oldSegv);
        sigaction(SIGBUS,  &sa, &detail::g_oldBus);

        sigjmp_buf jb;
        detail::g_tid = pthread_self();
        detail::g_jb = &jb;
        const int j = sigsetjmp(jb, 1);          // 1 = restaura a mascara de sinais ao voltar do crash
        if (j == 0) f();
        detail::g_jb = nullptr;

        sigaction(SIGSEGV, &detail::g_oldSegv, nullptr);
        sigaction(SIGBUS,  &detail::g_oldBus,  nullptr);
        return j;
    }

    // listas estaticas REAIS do libGTASA (o espelho declarado na classe do projeto NAO e usado pelo jogo)
    struct RawArr { unsigned numAlloced, numEntries; void** dataPtr; };
    inline RawArr* Sym(const char* mangled)
    {
        return CHook::lib ? (RawArr*)dlsym(CHook::lib, mangled) : nullptr;     // nao usar getSym(): ele chama exit(0) se falhar
    }
    inline RawArr* LoadedList()     { static RawArr* a = Sym("_ZN22TextureDatabaseRuntime6loadedE");     return a; }
    inline RawArr* RegisteredList() { static RawArr* a = Sym("_ZN22TextureDatabaseRuntime10registeredE"); return a; }

    // devolve as listas ao tamanho de antes da tentativa (tira da lista o banco meio carregado/invalido)
    inline void Rollback(unsigned snapLoaded, unsigned snapRegistered)
    {
        if (RawArr* a = LoadedList())     if (a->numEntries > snapLoaded)     { FLog("[TEXDB] removendo banco quebrado da lista loaded (%u -> %u)", a->numEntries, snapLoaded); a->numEntries = snapLoaded; }
        if (RawArr* a = RegisteredList()) if (a->numEntries > snapRegistered) { FLog("[TEXDB] removendo banco quebrado da lista registered (%u -> %u)", a->numEntries, snapRegistered); a->numEntries = snapRegistered; }
    }

    // varredura guardada do indice do banco: ponteiro/contagem de entradas absurdos viram SIGSEGV contido aqui, e nao no GetTexture
    inline bool Verify(TextureDatabaseRuntime* db)
    {
        if (!db) return false;
        bool ok = true;
        const int s = Run([&]() {
            const unsigned n = db->entries.numEntries;
            if (n > 2000000u) { ok = false; return; }                         // contagem corrompida
            if (n > 0 && db->entries.dataPtr == nullptr) { ok = false; return; }
            volatile uint8_t sink = 0;
            for (unsigned i = 0; i < n; i++)                                  // le o 1o e o ultimo byte de cada entrada
            {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&db->entries.dataPtr[i]);
                sink ^= p[0]; sink ^= p[sizeof(TextureDatabaseEntry) - 1];
            }
            (void)sink;
        });
        return ok && s == 0;
    }

    // Substitui TextureDatabaseRuntime::Load nas cargas de abertura do jogo.
    inline TextureDatabaseRuntime* Load(const char* name, bool fullyLoad, TextureDatabaseFormat fmt)
    {
        using namespace detail;
        static bool inited = false;
        static std::string badPath, tryPath, statusPath, bad;
        if (!inited)
        {
            inited = true;
            const std::string r = Root();
            if (!r.empty())
            {
                badPath = r + "SAMP/texdb_bad.txt"; tryPath = r + "SAMP/texdb_try.txt"; statusPath = r + "SAMP/texdb_status.txt";
                bad = ReadAll(badPath);
                const std::string left = ReadAll(tryPath);     // sobrou da abertura anterior = o processo morreu nessa tentativa
                if (!left.empty())
                {
                    bad += left; if (bad.back() != '\n') bad += '\n';
                    WriteAll(badPath, bad);
                    FLog("[TEXDB] abertura anterior morreu em '%s': combinacao marcada como ruim e sera pulada", left.c_str());
                    unlink(tryPath.c_str());
                }
                WriteAll(statusPath, "");   // zera o relatorio desta abertura
            }
        }

        std::vector<TextureDatabaseFormat> order { fmt };
        for (TextureDatabaseFormat f : { DF_Default, DF_ETC, DF_DXT, DF_PVR })
            if (f != fmt) order.push_back(f);

        const std::string sig = Signature(name);
        std::string listing; Signature(name, &listing);
        FLog("[TEXDB] banco '%s' (formato pedido=%s) arquivos: %s", name, FmtName((int)fmt), listing.empty() ? "(pasta ausente/vazia)" : listing.c_str());

        int tested = 0;
        for (TextureDatabaseFormat f : order)
        {
            const std::string key = std::string(name) + ":" + FmtName((int)f) + ":" + sig;
            if (bad.find(key + "\n") != std::string::npos)
            {
                FLog("[TEXDB] '%s' formato %s ja falhou antes com estes mesmos arquivos: pulado", name, FmtName((int)f));
                continue;
            }
            tested++;
            WriteAll(tryPath, key + "\n");
            const unsigned snapL = LoadedList() ? LoadedList()->numEntries : 0u;
            const unsigned snapR = RegisteredList() ? RegisteredList()->numEntries : 0u;
            TextureDatabaseRuntime* volatile dbv = nullptr;
            int s = Run([&]() { dbv = TextureDatabaseRuntime::Load(name, fullyLoad, f); });
            TextureDatabaseRuntime* db = dbv;
            if (db && !Verify(db)) { FLog("[TEXDB] '%s' formato %s carregou mas o indice e invalido", name, FmtName((int)f)); db = nullptr; s = -1; }
            if (!db) Rollback(snapL, snapR);        // falhou no meio ou indice ruim: o jogo nao pode achar esse banco depois
            unlink(tryPath.c_str());
            if (db)
            {
                if (f != fmt) FLog("[TEXDB][CONTORNO] '%s': formato pedido %s falhou; funcionou com %s", name, FmtName((int)fmt), FmtName((int)f));
                WriteAll(statusPath, std::string(name) + ": ok formato=" + FmtName((int)f) + (f != fmt ? " (CONTORNO)" : "") + "\n", "a");
                return db;
            }
            FLog("[TEXDB] '%s' formato %s FALHOU (%s)", name, FmtName((int)f), s == -1 ? "indice invalido" : s ? (s == SIGBUS ? "SIGBUS contido" : "SIGSEGV contido") : "retornou nulo");
            bad += key + "\n";
            WriteAll(badPath, bad);
        }

        FLog("[TEXDB] '%s': nenhuma alternativa funcionou (%d testada(s)); o jogo segue SEM este banco", name, tested);
        WriteAll(statusPath, std::string(name) + ": FALHOU (sem banco) - rode VERIFICAR ARQUIVOS\n", "a");
        const std::string miss = std::string("texdb/") + name;
        Health::NoteMissingFile(miss.c_str());
        return nullptr;
    }
}
