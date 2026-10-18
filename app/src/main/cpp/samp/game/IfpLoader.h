// IfpLoader.h - loader de IFP (ANP3 / ANP2 / ANPK) para blocos de animacao customizados.
// Baseado no fluxo do CClientIFP (MTA): le o arquivo para um formato intermediario e depois
// constroi CAnimBlendHierarchy / CAnimBlendSequence NAO comprimidas no CAnimManager do seu projeto.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace CustomIfp {

struct LoadResult {
    bool        ok = false;
    char        block[24] = {};   // nome final do bloco registrado
    int         numAnims = 0;
    std::string error;
};

// blockNameOverride: troca o nome do bloco gravado no arquivo (max 15 chars, limite de CAnimBlock::szName).
LoadResult LoadFile(const char* path, const char* blockNameOverride = nullptr);
LoadResult LoadMemory(const uint8_t* data, size_t size, const char* blockNameOverride = nullptr);

// Nomes das animacoes de um bloco customizado carregado (para /ifp lista). nullptr se o bloco nao veio daqui.
const std::vector<std::string>* GetAnimNames(const char* block);

// Nome de osso (ANPK sem bone id) -> ID de osso do ped. -1 se desconhecido.
int BoneIdFromName(const char* name);

} // namespace CustomIfp
