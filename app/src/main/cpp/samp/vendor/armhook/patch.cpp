#include "../main.h"
#include "patch.h"

#include <sys/mman.h>
#include <errno.h>

#include <unistd.h> // system api
#include <sys/mman.h>
#include <assert.h> // assert()
#include <dlfcn.h> // dlopen
#include <link.h>  // dl_iterate_phdr

//uintptr_t CHook::mmap_start 	= 0;
//uintptr_t CHook::mmap_end		= 0;
//uintptr_t CHook::memlib_start	= 0;
//uintptr_t CHook::memlib_end	= 0;


// Um slot de PLT/GOT so e valido em segmento PT_LOAD gravavel e NAO executavel (.got/.data.rel.ro/.data).
// Offsets herdados do build 32-bit caem em .text/.rodata do libGTASA ARM64: escrever ali corrompe codigo/dados
// (e era a causa dos avisos "mprotect RWX falhou"). Fora do libGTASA nao ha como julgar: permite.
struct SlotCheck { uintptr_t base; uintptr_t addr; bool inLib; bool ok; };

static int SlotCheckCb(struct dl_phdr_info* info, size_t, void* data)
{
    SlotCheck* c = (SlotCheck*)data;
    if ((uintptr_t)info->dlpi_addr != c->base) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        uintptr_t s = info->dlpi_addr + ph.p_vaddr;
        uintptr_t e = s + ph.p_memsz;
        if (c->addr >= s && c->addr + sizeof(uintptr_t) <= e)
        {
            c->inLib = true;
            c->ok = (ph.p_flags & PF_W) && !(ph.p_flags & PF_X);
            return 1;
        }
    }
    return 1;
}

bool CHook::IsValidSlot(uintptr_t addr)
{
    if (!g_libGTASA) return true;
    SlotCheck c = { g_libGTASA, addr, false, false };
    // dlpi_addr do libGTASA e o load bias == g_libGTASA quando o ELF tem vaddr base 0 (caso do libGTASA)
    dl_iterate_phdr(SlotCheckCb, &c);
    if (!c.inLib)
    {
        // PT_LOAD nao encontrado (bias diferente de g_libGTASA): no libGTASA ARM64 o segmento RW comeca em +0x821958;
        // tudo abaixo disso e codigo/rodata e nunca e slot.
        return !(addr >= g_libGTASA && addr < g_libGTASA + 0x821000);
    }
    return c.ok;
}

bool CHook::UnFuck(uintptr_t addr, size_t len)
{
    // Android 15+ pode rodar com paginas de 16 KB: mprotect exige endereco alinhado ao tamanho REAL da
    // pagina (a mascara fixa de 4 KB falhava com EINVAL) e o intervalo precisa cobrir [addr, addr+len),
    // inclusive quando a escrita atravessa o limite de uma pagina.
    static const uintptr_t pageSize = []() {
        long p = sysconf(_SC_PAGESIZE);
        return (uintptr_t)(p > 0 ? p : 4096);
    }();

    // Endereco 0 / primeiras paginas nunca sao validos: escrever ali derruba o app dentro do memcpy.
    if (addr < 0x10000)
    {
        FLog("[CHook] UnFuck recusado: endereco invalido 0x%lx", (unsigned long)addr);
        return false;
    }

    if (len == 0) len = 1;
    const uintptr_t start = addr & ~(pageSize - 1);
    const uintptr_t end   = (addr + len + pageSize - 1) & ~(pageSize - 1);

    if(mprotect((void*)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0)
        return true;

    const int errRwx = errno;
    if(mprotect((void*)start, end - start, PROT_READ | PROT_WRITE) == 0)
    {
        // escrita liberada, mas a pagina ficou SEM execucao (comportamento anterior): vale para dados; em codigo, o jogo falharia ao executar
        FLog("[CHook] aviso: mprotect RWX falhou (errno %d) em 0x%lx; usando RW", errRwx, (unsigned long)addr);
        return true;
    }

    FLog("[CHook] mprotect falhou em 0x%lx len %lu (RWX errno %d, RW errno %d)", (unsigned long)addr, (unsigned long)len, errRwx, errno);
    return false;
}

void CHook::InitHookStuff()
{
//    memlib_start = g_libGTASA + 0x180044;
//    memlib_end = memlib_start + 0x450;
//
//    mmap_start = (uintptr_t)mmap(0, PAGE_SIZE, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//    mprotect((void*)(mmap_start & 0xFFFFF000), PAGE_SIZE, PROT_READ | PROT_EXEC | PROT_WRITE);
//    mmap_end = (mmap_start + PAGE_SIZE);

    lib = dlopen("libGTASA.so", RTLD_LAZY);
}

