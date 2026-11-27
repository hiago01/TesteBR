// Divide um DFF com geometrias > 65535 vertices (mesmo codigo que o cliente usa em texdb/DFF/*.dff).
//   g++ -std=c++17 -O2 -Iapp/src/main/cpp/samp/game tools/dffsplit_cli.cpp -o dffsplit_cli
//   ./dffsplit_cli entrada.dff saida.dff        (saida so e' gravada quando houve divisao)
// Codigos de saida: 0 = dividido ou ja valido (inalterado), 2 = nao foi possivel dividir (motivo em stderr), 1 = uso/arquivo.
#include "DffSplit.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "uso: %s entrada.dff saida.dff\n", argv[0]); return 1; }
    std::vector<uint8_t> in;
    if (FILE* f = std::fopen(argv[1], "rb")) { std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET); in.resize((size_t)n); if (n > 0 && std::fread(in.data(), 1, (size_t)n, f) != (size_t)n) { std::fprintf(stderr, "leitura parcial\n"); std::fclose(f); return 1; } std::fclose(f); }
    else { std::fprintf(stderr, "nao abriu %s\n", argv[1]); return 1; }
    DffSplit::Report rep; std::string err; std::vector<uint8_t> out;
    const DffSplit::Result r = DffSplit::Process(in.data(), in.size(), out, rep, err);
    if (r == DffSplit::Result::Failed) { std::fprintf(stderr, "NAO DIVIDIDO: %s\n", err.c_str()); return 2; }
    std::printf("%s\n", DffSplit::Summary(rep).c_str());
    for (const std::string& n : rep.notes) std::printf("aviso: %s\n", n.c_str());
    if (r == DffSplit::Result::Unchanged) { std::printf("nenhuma geometria passa de %u vertices: arquivo inalterado, nada gravado\n", DffSplit::kMaxVerts); return 0; }
    if (FILE* f = std::fopen(argv[2], "wb")) { std::fwrite(out.data(), 1, out.size(), f); std::fclose(f); std::printf("gravado %s (%zu bytes); %d geometria(s) dividida(s), %d parte(s) nova(s), %llu vertice(s) sem uso descartado(s)\n", argv[2], out.size(), rep.splitGeoms, rep.partsCreated, (unsigned long long)rep.droppedUnused); return 0; }
    std::fprintf(stderr, "nao gravou %s\n", argv[2]); return 1;
}
