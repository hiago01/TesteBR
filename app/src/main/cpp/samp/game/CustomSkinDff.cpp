#include "main.h"
#include "CustomSkinDff.h"
#include "game.h"
#include "CFileMgr.h"
#include "RW/RenderWare.h"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <limits>

namespace
{
constexpr uintptr_t GTA_CLUMP_STREAM_READ = 0x6F4650;
constexpr size_t MAX_DFF_SIZE = 32 * 1024 * 1024;

RpClump* ReadGtaClump(RwStream* stream)
{
    return CHook::CallFunction<RpClump*>(
        g_libGTASA + GTA_CLUMP_STREAM_READ,
        stream
    );
}

RpAtomic* CountAtomic(RpAtomic* atomic, void* data)
{
    if (atomic && data)
        ++(*static_cast<int*>(data));

    return atomic;
}

}

void CustomSkinDff::TestLoad()
{
static bool started = false;

if (started)
    return;

started = true;

CGame::PostToMainThread([]()
{
    const char* path =
        "/storage/emulated/0/Android/data/com.samp.mobile/files/SAMP/skins/skin.dff";

    FLog("[CustomDFF] Iniciando teste: %s", path);

    FILE* file = fopen(path, "rb");

    if (!file)
    {
        FLog("[CustomDFF] ERRO: nao foi possivel abrir o arquivo");
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        FLog("[CustomDFF] ERRO: falha ao medir arquivo");
        fclose(file);
        return;
    }

    long fileSize = ftell(file);

    if (fileSize <= 0 ||
        static_cast<unsigned long long>(fileSize) > MAX_DFF_SIZE)
    {
        FLog("[CustomDFF] ERRO: tamanho invalido: %ld", fileSize);
        fclose(file);
        return;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        FLog("[CustomDFF] ERRO: falha ao voltar ao inicio");
        fclose(file);
        return;
    }

    std::vector<RwUInt8> bytes(
        static_cast<size_t>(fileSize)
    );

    size_t bytesRead = fread(
        bytes.data(),
        1,
        bytes.size(),
        file
    );

    fclose(file);

    if (bytesRead != bytes.size())
    {
        FLog("[CustomDFF] ERRO: leitura incompleta: %zu/%zu",
             bytesRead, bytes.size());
        return;
    }

    FLog("[CustomDFF] Arquivo lido: %zu bytes", bytes.size());

    RwMemory memory{};
    memory.start = bytes.data();
    memory.length = static_cast<RwUInt32>(bytes.size());

    RwStream* stream = RwStreamOpen(
        rwSTREAMMEMORY,
        rwSTREAMREAD,
        &memory
    );

    if (!stream)
    {
        FLog("[CustomDFF] ERRO: RwStreamOpen falhou");
        return;
    }

    FLog("[CustomDFF] Fluxo de memoria aberto");

    RpClump* clump = ReadGtaClump(stream);

    RwStreamClose(stream, nullptr);

    if (!clump)
    {
        FLog("[CustomDFF] FALHA: GTA nao conseguiu ler o clump");
        FLog("[CustomDFF] Possivel incompatibilidade do DFF exportado");
        return;
    }

    int atomicCount = 0;

    RpClumpForAllAtomics(
        clump,
        CountAtomic,
        &atomicCount
    );

    FLog("[CustomDFF] SUCESSO: clump carregado");
    FLog("[CustomDFF] Atomics encontrados: %d", atomicCount);

    RpClumpDestroy(clump);

    FLog("[CustomDFF] Teste finalizado; modelo original preservado");
});

}
