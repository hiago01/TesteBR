// Teste no host do CustomDff.h (indice texdb/DFF + validacao estrutural + leitura de tamanho real).
// Uso:  bash host_test_customdff.sh /caminho/laehospital1.dff
#include <cassert>
#include <fstream>
#include <filesystem>
#include <cstdlib>
namespace fs = std::filesystem;
char* g_pszStorage = nullptr;
#include "samp/game/CustomDff.h"

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ ++fails; std::printf("FALHOU linha %d: %s\n", __LINE__, #c);} }while(0)
static void Put(const std::string& p, const std::vector<uint8_t>& d){ fs::create_directories(fs::path(p).parent_path()); std::ofstream(p, std::ios::binary).write((const char*)d.data(), d.size()); }
static std::vector<uint8_t> Get(const std::string& p){ std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Reset(){ fs::remove_all("/tmp/dffroot"); fs::create_directories("/tmp/dffroot/SAMP"); }

// o Index() e' estatico (1 vez por processo, como no jogo): cada cenario roda em processo proprio via argv
int main(int argc, char** argv)
{
    const std::string real = argc > 1 ? argv[1] : "";
    const int sc = argc > 2 ? atoi(argv[2]) : 0;
    static std::string root = "/tmp/dffroot/";
    g_pszStorage = (char*)root.c_str();
    std::vector<uint8_t> dff = Get(real);
    std::vector<uint8_t> out; std::string path, why;
    const uint32_t key = CKeyGen::GetUppercaseKey("laehospital1");

    if (sc == 1) {            // arquivo real em texdb/DFF
        Reset(); Put(root + "texdb/DFF/laehospital1.dff", dff);
        CHECK(CustomDff::Find(key, out, path, why)); CHECK(out == dff);
        std::printf("  real: %zu bytes, setores=%zu, lido=%zu\n", dff.size(), (dff.size()+2047)/2048, out.size());
        CHECK(!CustomDff::Find(CKeyGen::GetUppercaseKey("outro"), out, path, why)); CHECK(why.empty());
        CustomDff::Info i; std::string e; CHECK(CustomDff::Validate(dff.data(), dff.size(), i, e)); CHECK(i.atomics==1 && i.geoms==1);
        std::printf("  stamp=0x%x atomics=%d geoms=%d\n", i.stamp, i.atomics, i.geoms);
    } else if (sc == 2) {     // truncado -> reprovado (sem crash)
        Reset(); auto t = dff; t.resize(t.size()/2); Put(root + "texdb/DFF/laehospital1.dff", t);
        CHECK(!CustomDff::Find(key, out, path, why)); CHECK(!why.empty()); std::printf("  motivo: %s\n", why.c_str());
    } else if (sc == 3) {     // nome em maiuscula + variante texdb/dff + SAMP/dff: o ultimo vence
        Reset(); Put(root + "texdb/DFF/LAEHOSPITAL1.DFF", dff); auto b = dff; Put(root + "SAMP/dff/laehospital1.dff", b);
        CHECK(CustomDff::Find(key, out, path, why)); CHECK(path.find("SAMP/dff/") != std::string::npos);
    } else if (sc == 4) {     // arquivo vazio e lixo
        Reset(); Put(root + "texdb/DFF/laehospital1.dff", {}); CHECK(!CustomDff::Find(key, out, path, why)); std::printf("  motivo: %s\n", why.c_str());
    } else if (sc == 5) {     // dff_off.on
        Reset(); Put(root + "texdb/DFF/laehospital1.dff", dff); Put(root + "SAMP/dff_off.on", {1});
        CHECK(!CustomDff::HasAny());
    } else if (sc == 6) {     // sem pasta alguma
        Reset(); CHECK(!CustomDff::HasAny());
    } else if (sc == 7) {     // tamanho de chunk estourado no meio (corrupcao)
        Reset(); auto t = dff; t[12+4] = 0xFF; t[12+5] = 0xFF; t[12+6] = 0xFF; t[12+7] = 0x7F; Put(root + "texdb/DFF/laehospital1.dff", t);
        CHECK(!CustomDff::Find(key, out, path, why)); std::printf("  motivo: %s\n", why.c_str());
    } else if (sc == 8) {     // arquivo GRANDE (> 2 MB, > buffer tipico): precisa ler inteiro
        Reset(); auto big = dff; std::vector<uint8_t> pad(3*1024*1024, 0); // enche a extensao do clump com plugin desconhecido
        // anexa um chunk de plugin dentro da Extension final do clump e corrige os tamanhos
        size_t extPos = big.size() - 12;           // Extension final (tamanho 0) do clump
        uint32_t t = 0x3F00001, sz = (uint32_t)pad.size(), ver = 0x1803FFFF;
        std::vector<uint8_t> chunk(12); memcpy(&chunk[0], &t, 4); memcpy(&chunk[4], &sz, 4); memcpy(&chunk[8], &ver, 4);
        chunk.insert(chunk.end(), pad.begin(), pad.end());
        big.insert(big.end(), chunk.begin(), chunk.end());
        uint32_t extSize = (uint32_t)chunk.size(); memcpy(&big[extPos+4], &extSize, 4);
        uint32_t clump = (uint32_t)big.size() - 12; memcpy(&big[4], &clump, 4);
        Put(root + "texdb/DFF/laehospital1.dff", big);
        CHECK(CustomDff::Find(key, out, path, why)); CHECK(out.size() == big.size());
        std::printf("  grande: %zu bytes (%zu setores) lido inteiro=%d\n", big.size(), (big.size()+2047)/2048, (int)(out.size()==big.size()));
    }
    return fails ? 1 : 0;
}
