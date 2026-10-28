// Auxiliar do teste ponta a ponta (tools_ipl_delta.py): monta um IPL binario a partir de um IPL decodificado em texto,
// aplica um arquivo de ajuste e imprime o resultado como linhas 'inst'.   uso: ipl_apply_host original.ipl ajuste.ipl
#include "CustomIplParse.h"
#include <fstream>
#include <sstream>
#include <iostream>
using namespace CustomIplParse;
int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    std::ifstream fo(argv[1]); std::string line; std::vector<Inst> v; bool in = false;
    while (std::getline(fo, line))
    {
        size_t h = line.find('#'); if (h != std::string::npos) line.erase(h);
        std::vector<std::string> t; detail::Split(line, t); if (t.empty()) continue;
        std::string k = t[0]; for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        if (t.size() == 1 && k == "inst") { in = true; continue; }
        if (t.size() == 1 && k == "end") { in = false; continue; }
        if (!in) continue;
        InstText x; std::string why; if (t.size() == 10) t.push_back("-1");
        if (!detail::ParseInstTokens(t, 0, x, why)) { std::cerr << "orig: " << why << "\n"; return 3; }
        Inst i{}; std::memcpy(i.pos, x.pos, 12); std::memcpy(i.rot, x.rot, 16); i.id = x.id; i.type = x.type; i.lod = x.lod; v.push_back(i);
    }
    std::vector<uint8_t> bin(kHdrSize, 0); std::memcpy(bin.data(), "bnry", 4);
    WrU32(bin.data() + 4, (uint32_t)v.size()); WrU32(bin.data() + 0x1C, (uint32_t)kHdrSize);
    bin.resize(kHdrSize + v.size() * kInstSize + 8, 0); if (!v.empty()) std::memcpy(bin.data() + kHdrSize, v.data(), v.size() * kInstSize);
    std::ifstream fa(argv[2]); std::stringstream ss; ss << fa.rdbuf(); std::string txt = ss.str();
    OverrideFile f; ParseOverride(txt.data(), txt.size(), f);
    for (auto& e : f.errors) std::cerr << "ajuste: " << e << "\n";
    std::vector<uint8_t> out; MergeReport rep; std::string why;
    if (!Merge(bin.data(), bin.size(), { &f }, [](const char*, int id) { return id; }, out, rep, &why)) { std::cerr << "sem mistura: " << why << "\n"; return 4; }
    for (auto& n : rep.notes) std::cerr << "nota: " << n << "\n";
    BinView bv; ReadBin(out.data(), out.size(), bv);
    for (uint32_t i = 0; i < bv.count; i++)
    {
        Inst x; std::memcpy(&x, out.data() + bv.instOff + i * kInstSize, kInstSize);
        std::printf("%d, m%d, %u, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %d\n", x.id, x.id, x.type, x.pos[0], x.pos[1], x.pos[2], x.rot[0], x.rot[1], x.rot[2], x.rot[3], x.lod);
    }
    return 0;
}
