// CustomAnimTest.cpp - ver CustomAnimTest.h
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "game/Entity/CPedGTA.h"

#include "CustomAnimTest.h"
#include "CustomAnim.h"
#include "CustomAnimSync.h"
#include "IfpLoader.h"

#include <sstream>
#include <vector>
#include <cstdlib>
#include <strings.h>

extern CNetGame* pNetGame;
extern char* g_pszStorage;

namespace CustomAnimTest {
namespace {

std::string s_block;     // ultimo bloco carregado por /ifp carregar

CPedGTA* LocalPed() {
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool || !pool->GetLocalPlayer()) return nullptr;
    CPlayerPed* pp = pool->GetLocalPlayer()->GetPlayerPed();
    return pp ? pp->m_pPed : nullptr;
}

std::string AnimDir() {
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage);
    if (p.back() != '/') p.push_back('/');
    return p + "SAMP/anims/";
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> v; std::istringstream is(s); std::string t;
    while (is >> t) v.push_back(t);
    return v;
}

uint16_t ParseFlags(const std::vector<std::string>& t, size_t from) {
    uint16_t f = 0; bool partial = false;
    for (size_t i = from; i < t.size(); ++i) {
        if (!strcasecmp(t[i].c_str(), "loop")) f |= CustomAnim::CA_LOOP;
        else if (!strcasecmp(t[i].c_str(), "hold")) f |= CustomAnim::CA_HOLD_LAST;
        else if (!strcasecmp(t[i].c_str(), "parcial") || !strcasecmp(t[i].c_str(), "partial")) partial = true;
    }
    f |= partial ? CustomAnim::CA_PARTIAL : CustomAnim::CA_AUTO_PARTIAL;
    return f;
}

} // namespace

bool AutoLoad() {
    if (CustomIfp::GetAnimNames("poses")) return true;
    std::string f = AnimDir() + "poses.ifp";
    return CustomAnim::Init(f.c_str(), "poses");
}

std::string Command(const std::string& args) {
    std::vector<std::string> t = Split(args);
    if (t.empty() || t[0] == "ajuda" || t[0] == "help")
        return "[IFP] /ifp cruzar | apontar [esq] | parar | carregar [arquivo] [bloco] | lista | tocar <anim> [loop] [hold] [parcial] | parar | remoto <id> <anim|limpar> [loop]";

    const std::string& c = t[0];

    if (c == "carregar" || c == "load") {
        std::string file = t.size() > 1 ? t[1] : "teste.ifp";
        std::string blk  = t.size() > 2 ? t[2] : "catest";
        if (file.find('/') == std::string::npos) file = AnimDir() + file;
        if (CustomIfp::GetAnimNames(blk.c_str())) { s_block = blk; return "[IFP] bloco '" + blk + "' ja esta carregado (use /ifp lista)"; }
        if (!CustomAnim::Init(file.c_str(), blk.c_str())) return "[IFP] FALHOU ao carregar " + file + " (veja samp.log, linhas [CustomAnim]/[IFP])";
        s_block = blk;
        const auto* names = CustomIfp::GetAnimNames(blk.c_str());
        return "[IFP] OK bloco '" + blk + "' com " + std::to_string(names ? names->size() : 0) + " anim(s). Use /ifp lista e /ifp tocar <anim>";
    }

    if (c == "parar" || c == "stop") {
        CPedGTA* ped = LocalPed();
        if (!ped) return "[IFP] ped local indisponivel";
        CustomAnim::StopAll(ped);
        return "[IFP] parado";
    }

    if (c == "cruzar" || c == "apontar") {      // atalhos locais (so voce ve; para todos verem use o servidor/plugin)
        if (!AutoLoad()) return "[IFP] poses.ifp nao encontrado/invalido em " + AnimDir() + " (veja samp.log)";
        CPedGTA* ped = LocalPed();
        if (!ped) return "[IFP] ped local indisponivel (entre no servidor e nasca primeiro)";
        bool esq = t.size() > 1 && (t[1] == "esq" || t[1] == "esquerdo");
        const char* an = c == "cruzar" ? "bracos_cruzados" : (esq ? "apontar_esq" : "apontar_dir");
        auto* a = CustomAnim::Play(ped, "poses", an, CustomAnim::CA_HOLD_LAST | CustomAnim::CA_PARTIAL, 4.0f);
        return a ? std::string("[IFP] pose: ") + an + " (/ifp parar para soltar)" : "[IFP] nao tocou (veja samp.log)";
    }

    if (s_block.empty()) return "[IFP] nenhum bloco carregado: use /ifp carregar";

    if (c == "lista" || c == "list") {
        const auto* names = CustomIfp::GetAnimNames(s_block.c_str());
        if (!names || names->empty()) return "[IFP] bloco vazio";
        std::string r = "[IFP] " + s_block + ": ";
        for (size_t i = 0; i < names->size(); ++i) { if (i) r += ", "; r += (*names)[i]; }
        return r;
    }

    if (c == "tocar" || c == "play") {
        if (t.size() < 2) return "[IFP] uso: /ifp tocar <anim> [loop] [hold] [parcial]";
        CPedGTA* ped = LocalPed();
        if (!ped) return "[IFP] ped local indisponivel (entre no servidor e nasca primeiro)";
        auto* a = CustomAnim::Play(ped, s_block.c_str(), t[1].c_str(), ParseFlags(t, 2), 4.0f);
        return a ? "[IFP] tocando " + s_block + ":" + t[1] : "[IFP] nao tocou (anim nao existe? veja /ifp lista e samp.log)";
    }

    if (c == "remoto" || c == "remote") {
        if (t.size() < 3) return "[IFP] uso: /ifp remoto <id> <anim|limpar> [loop]";
        PLAYERID id = (PLAYERID)atoi(t[1].c_str());
        bool ok = (t[2] == "limpar")
            ? CustomAnimSync::DebugSet(id, "", "", 0, 0.f)
            : CustomAnimSync::DebugSet(id, s_block.c_str(), t[2].c_str(), ParseFlags(t, 3), 4.0f);
        return ok ? "[IFP] estado aplicado ao jogador " + t[1] : "[IFP] recusado (id invalido/bloco nao customizado)";
    }

    return "[IFP] comando desconhecido. /ifp ajuda";
}

} // namespace CustomAnimTest
