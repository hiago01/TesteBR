#pragma once
#include "../main.h"
#include <string.h>
#include <dlfcn.h>
#include <asm/unistd.h>
#include <unistd.h>
#include <ctype.h>
#include <link.h>
#include <sys/cachectl.h>

#ifdef __arm__
#define __32BIT
#define DETHUMB(_a) (((uintptr_t)_a) & ~0x1)
#define RETHUMB(_a) (((uintptr_t)_a) | 0x1)
#define THUMBMODE(_a) ((((uintptr_t)_a) & 0x1)||(((uintptr_t)_a) & 0x2)||(CHook::GetAddrBaseXDL((uintptr_t)_a) & 0x1))
extern "C" bool MSHookFunction(void* symbol, void* replace, void** result);
#elif defined __aarch64__
#define __64BIT
    #define DETHUMB(_a)
    #define RETHUMB(_a)
    #define THUMBMODE(_a) (false)
    #define cacheflush(c, n, zeroarg) __builtin___clear_cache((char*)(c), (char*)(n))
#else
    #error This lib is supposed to work on ARM only!
#endif


#define GET_LR(dwRetAddr) \
    do { \
        uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0)); \
        dwRetAddr = lr - g_libGTASA; \
    } while (0)



#define SET_TO(__a1, __a2) *(void**)&(__a1) = (void*)(__a2)

#include "shadowhook.h"

class CHook {
public:

    static uintptr_t mmap_start, mmap_end, memlib_start, memlib_end;

    // Libera escrita na memoria [ptr, ptr+len). Devolve false (e registra no log) se o endereco for invalido (0/pagina nula)
    // ou se o mprotect falhar; quem escreve DEVE conferir o retorno (escrever mesmo assim derruba o app dentro do memcpy).
    static bool UnFuck(uintptr_t ptr, size_t len = PAGE_SIZE);

    static uintptr_t GetAddrBaseXDL(uintptr_t addr)
    {
#ifdef __XDL
        xdl_info_t info;
        void* cache = NULL;
        if(!xdl_addr((void*)addr, &info, &cache)) return 0;
        xdl_addr_clean(&cache);
        return (uintptr_t)info.dli_saddr;
#endif
        return 0;
    }



    template<typename Addr>
    static void NOP(Addr adr, size_t count)
    {
        // fully check
        auto addr = (uintptr)(adr);

        if(count > 1)
            count /= 2;

        int bytesCount = 4 * count;
        uintptr_t endAddr = addr + bytesCount;
        if (!UnFuck(addr, bytesCount))
        {
            FLog("[CHook] NOP ignorado em 0x%lx", (unsigned long)addr);
            return;
        }
        for (uintptr_t p = addr; p != endAddr; p += 4)
        {
            (*(char*)(p + 0)) = 0x1F;
            (*(char*)(p + 1)) = 0x20;
            (*(char*)(p + 2)) = 0x03;
            (*(char*)(p + 3)) = 0xD5;
        }
        cacheflush(addr, endAddr, 0);
    }

    static void RET(const char* sym)
    {
        auto addr = getSymSoft(sym);
        if (addr == 0) return;   // simbolo ausente: nao aplica o patch (ja registrado no log) em vez de escrever no endereco 0
        RET(addr);
    }

    static void RET(uintptr addr)
    {
        if (addr == 0)
        {
            FLog("[CHook] RET recusado: endereco 0");
            return;
        }

        WriteMemory(addr, (uintptr_t)"\xC0\x03\x5F\xD6", 4);

    }

    template <typename Src>
    static void WriteMemory(uintptr_t dest, Src src, size_t size)
    {
        if (dest == 0 || (uintptr_t)src == 0 || size == 0)
        {
            FLog("[CHook] WriteMemory recusado: dest=0x%lx src=0x%lx size=%lu", (unsigned long)dest, (unsigned long)(uintptr_t)src, (unsigned long)size);
            return;
        }
        if (!UnFuck(dest, size))
        {
            FLog("[CHook] WriteMemory ignorado (sem permissao de escrita) em 0x%lx", (unsigned long)dest);
            return;   // o log acima de UnFuck traz o errno
        }
        memcpy((void*)dest, (void*)src, size);

        cacheflush(dest, dest + size, 0);
    }

    template <typename Src>
    static Src Write(uintptr_t dest, Src src, size_t size = 0)
    {   
	    size = sizeof(Src);
        CHook::WriteMemory(dest, &src, size);
	    return src;
    }

    template <typename Src>
    static Src Write(const char* sym, Src src, size_t size = 0)
    {
        auto addr = getSymSoft(sym);
        size = sizeof(Src);
        if (addr == 0) return src;   // simbolo ausente: nao escreve
        CHook::WriteMemory(addr, &src, size);
        return src;
    }

    static void InitHookStuff();
    static inline void* lib;

    static uintptr_t getSym(const char* sym)
    {

        auto res = (uintptr_t)dlsym(lib, sym);
        if(res == 0){
            FLog("Error find %s", sym);
            exit(0);
            return 0;
        }
       // Log("getsym = %x", sym - g_libGTASA);
        return res;
    }

    // Igual ao getSym, mas NAO encerra o app: devolve 0 e registra. Usado por RET/Redirect/Write, que apenas pulam o patch.
    static uintptr_t getSymSoft(const char* sym)
    {
        auto res = (uintptr_t)dlsym(lib, sym);
        if (res == 0) FLog("[CHook] simbolo nao encontrado: %s - patch ignorado", sym);
        return res;
    }

    template <typename Ret, typename... Args>
    static inline Ret CallFunction(uintptr address, Args... args)
    {
        return (( Ret(*)(Args...))(address) )(args...);
    }

    template <typename Ret, typename... Args>
    static Ret CallFunction(const char* sym, Args... args)
    {
        auto addr = (uintptr_t)dlsym(lib, sym);

        if(addr == 0){
            FLog("Error find %s", sym);
            exit(0);
        }

        using FuncType = Ret(__cdecl *)(Args...);
        FuncType func = reinterpret_cast<FuncType>(addr);
        return func(args...);
    }

    template <typename Addr, typename Func, typename Orig>
    static void InstallPLT(Addr addr, Func hook_func, Orig* orig_func)
    {
        if ((uintptr_t)addr == 0 || !UnFuck(addr))
        {
            FLog("[CHook] InstallPLT ignorado em 0x%lx", (unsigned long)(uintptr_t)addr);
            return;   // hook nao instalado: o jogo usa a funcao original e o ponteiro orig nunca e chamado
        }

        *orig_func = reinterpret_cast<Orig>(*(uintptr_t*)addr);

        *(uintptr_t*)addr = reinterpret_cast<uintptr_t>(hook_func);
    }

    template <typename Addr, typename Func>
    static void InstallPLT(Addr addr, Func hook_func)
    {
        if ((uintptr_t)addr == 0 || !UnFuck(addr))
        {
            FLog("[CHook] InstallPLT ignorado em 0x%lx", (unsigned long)(uintptr_t)addr);
            return;
        }
        *(uintptr_t*)addr = reinterpret_cast<uintptr_t>(hook_func);
    }

    template <typename Addr, typename Func, typename Orig>
    static void InlineHook(uintptr_t lib, Addr addr, Func func, Orig orig)
    {
        shadowhook_hook_func_addr(
                (void*)(lib + addr + 1),
                (void *)func,
                (void **)orig);
    }

    template <typename Ptr, typename Func, typename Orig>
    static void InlineHook(Ptr ptr, Func func, Orig orig)
    {
        shadowhook_hook_func_addr(
                (void*)ptr,
                (void *)func,
                (void **)orig);
    }

    template <typename Func, typename Orig>
    static void InlineHook(const char* sym, Func func, Orig orig)
    {
        auto addr = getSym(sym);

        if(addr == 0){
            FLog("Error find %s", sym);
            exit(0);
        }

        shadowhook_hook_func_addr(
                (void*)(addr),
                (void *)func,
                (void **)orig);
    }

    template <typename Func>
    static void Redirect(const char* sym, Func func)
    {
        auto addr = getSymSoft(sym);
        if (addr == 0) return;   // simbolo ausente: sem redirecionamento (ja registrado no log)

#ifdef __32BIT
        uint32_t hook[2] = {0xE51FF004, reinterpret_cast<uintptr_t>(func)};
        if (THUMBMODE(addr)) {
            addr &= ~0x1;
            if (addr & 0x2) {
                NOP(RETHUMB(addr), 1);
                addr += 2;
            }
            hook[0] = 0xF000F8DF;
        }
        WriteMemory(DETHUMB(addr), reinterpret_cast<uintptr_t>(hook), sizeof(hook));
#elif defined __64BIT
        if (!UnFuck(addr, 16))
        {
            FLog("[CHook] Redirect ignorado: %s", sym);
            return;
        }
        uint64_t hook[2] = {0xD61F022058000051, reinterpret_cast<uintptr_t>(func)};
        WriteMemory(addr, reinterpret_cast<uintptr_t>(hook), sizeof(hook));
#endif
    }
};