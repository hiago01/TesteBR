// ReAnimTest.cpp - ver ReAnimTest.h
#include "../main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "game/Entity/CPedGTA.h"

#include "ReAnimTest.h"
#include "CustomAnim.h"
#include "IfpLoader.h"

#include <sstream>
#include <vector>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <strings.h>
#include <algorithm>

extern CNetGame* pNetGame;
extern char* g_pszStorage;

namespace ReAnimTest {
namespace {

const char* kBlock = "reanim";
const char* kExtras[] = { "baseball", "chainsaw", "fight_e", "goggles", "grenade", "knife", "parachute", "playidles", "shop", "vending" };

// animacoes que NAO existem no ped.ifp oficial do projeto (so no Re Anims)
const char* kNovas[] = {
    "CAR_sitp2", "FIGHT_PUSH", "IDLE_SWAT", "IDLE_homeless", "IDLE_latino", "IDLE_vendor", "IDLE_vendorfemale", "LIMP", "M7DECENT",
    "WALK_ballas1", "WALK_civi2", "WALK_civi3", "WALK_civi4", "WALK_civi5", "WALK_fam1", "WALK_fam2", "WALK_homeless", "WALK_latino1",
    "WALK_latino2", "WALK_start_gang1", "fatwoman_idlestance", "oldsprint_panic", "oldwoman_idlestance", "roadcross_gang1",
    "run_csaw_right", "run_homeless", "run_panic_fat", "sexywoman_idlestance", "sexywomanfat_idlestance", "sprint_gang1",
    "sprint_panic2", "walk_csaw_right", "walk_csaw_right2", "walk_start_wuzi", "walkst_csaw_right" };

std::vector<std::string> g_view;   // lista atual (filtrada) para proxima/anterior/numero
int g_cur = -1;
std::string g_filter = "novas";

CPlayerPed* LocalPlayerPed() {
    CPlayerPool* pool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
    if (!pool || !pool->GetLocalPlayer()) return nullptr;
    return pool->GetLocalPlayer()->GetPlayerPed();
}
CPedGTA* LocalPed() { CPlayerPed* p = LocalPlayerPed(); return p ? p->m_pPed : nullptr; }

std::string AnimDir() {
    if (!g_pszStorage || !g_pszStorage[0]) return {};
    std::string p(g_pszStorage); if (p.back() != '/') p.push_back('/');
    return p + "SAMP/anims/";
}
std::vector<std::string> Split(const std::string& s) { std::vector<std::string> v; std::istringstream is(s); std::string t; while (is >> t) v.push_back(t); return v; }
bool IsNova(const std::string& n) { for (const char* k : kNovas) if (!strcasecmp(k, n.c_str())) return true; return false; }
bool Has(const char* block) { return CustomIfp::GetAnimNames(block) != nullptr; }

void Rebuild(const std::string& filter) {
    g_filter = filter; g_view.clear(); g_cur = -1;
    const auto* names = CustomIfp::GetAnimNames(kBlock);
    if (!names) return;
    for (const auto& n : *names) {
        if (filter == "novas") { if (!IsNova(n)) continue; }
        else if (filter != "todas" && filter != "todos" && !filter.empty()) {
            std::string a = n, f = filter; std::transform(a.begin(), a.end(), a.begin(), ::tolower); std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            if (a.find(f) == std::string::npos) continue;
        }
        g_view.push_back(n);
    }
}

std::string PlayName(const std::string& block, const std::string& anim, bool hold) {
    CPedGTA* ped = LocalPed();
    if (!ped) return "[REANIM] ped local indisponivel (entre no servidor e nasca primeiro)";
    CustomAnim::StopAll(ped, 4.0f);
    uint16_t fl = hold ? CustomAnim::CA_HOLD_LAST : CustomAnim::CA_LOOP;     // padrao: repete, para voce ver o ciclo
    auto* a = CustomAnim::Play(ped, block.c_str(), anim.c_str(), fl, 4.0f);
    return a ? "[REANIM] " + block + ":" + anim + (hold ? " (hold)" : " (loop)") : "[REANIM] nao tocou " + block + ":" + anim + " (veja samp_log, [CustomAnim])";
}

} // namespace

std::string Command(const std::string& args) {
    std::vector<std::string> t = Split(args);
    if (t.empty() || t[0] == "ajuda")
        return "[REANIM] carregar | extras | lista [novas|todas|texto] [pag] | tocar <nome|n|bloco:nome> [hold] | proxima | anterior | oficial <nome> | parar   (testa o Re Anims sem trocar o ped.ifp oficial)";
    const std::string c = t[0];

    if (c == "carregar") {
        if (Has(kBlock)) return "[REANIM] ja carregado. /reanim lista novas";
        std::string f = AnimDir() + "reanim_ped.ifp";
        if (!CustomAnim::Init(f.c_str(), kBlock)) return "[REANIM] FALHOU: coloque o arquivo em " + f + " (veja samp_log [CustomAnim]/[IFP])";
        Rebuild("novas");
        const auto* n = CustomIfp::GetAnimNames(kBlock);
        return "[REANIM] OK: " + std::to_string(n ? n->size() : 0) + " anims no bloco 'reanim'; " + std::to_string(g_view.size()) + " novas. Use /reanim proxima, /reanim lista, /reanim tocar <nome>";
    }

    if (c == "extras") {
        int ok = 0, fail = 0;
        for (const char* e : kExtras) {
            std::string blk = std::string("re_") + e;
            if (Has(blk.c_str())) { ++ok; continue; }
            std::string f = AnimDir() + "reanim/" + e + ".ifp";
            if (CustomAnim::Init(f.c_str(), blk.c_str())) ++ok; else ++fail;
        }
        return "[REANIM] extras: " + std::to_string(ok) + " blocos ok, " + std::to_string(fail) + " falharam. Use /reanim lista re_knife (e tocar re_knife:<anim>)";
    }

    if (c == "parar") { CPedGTA* p = LocalPed(); if (!p) return "[REANIM] ped local indisponivel"; CustomAnim::StopAll(p); return "[REANIM] parado"; }

    if (c == "oficial") {
        if (t.size() < 2) return "[REANIM] uso: /reanim oficial <nome>   (toca a MESMA anim do ped.ifp oficial, para comparar com /reanim tocar)";
        CPlayerPed* pp = LocalPlayerPed(); if (!pp) return "[REANIM] ped local indisponivel";
        pp->ApplyAnimation(t[1].c_str(), "PED", 4.0f, 1, 0, 0, 0, -1);
        return "[REANIM] oficial PED:" + t[1] + " (loop)";
    }

    if (c == "lista") {
        std::string blk = kBlock;
        if (t.size() > 1 && (t[1].rfind("re_", 0) == 0 || t[1] == kBlock)) {      // lista de um bloco extra
            blk = t[1]; const auto* n = CustomIfp::GetAnimNames(blk.c_str());
            if (!n) return "[REANIM] bloco '" + blk + "' nao carregado (/reanim extras)";
            std::string r = "[REANIM] " + blk + ": "; for (size_t i = 0; i < n->size(); ++i) { if (i) r += ", "; r += (*n)[i]; } return r;
        }
        if (!Has(kBlock)) return "[REANIM] use /reanim carregar primeiro";
        int page = 1; std::string filt = g_filter;
        for (size_t i = 1; i < t.size(); ++i) { if (isdigit((unsigned char)t[i][0])) page = atoi(t[i].c_str()); else filt = t[i]; }
        if (filt != g_filter || g_view.empty()) Rebuild(filt);
        const int per = 14, total = (int)g_view.size(), pages = std::max(1, (total + per - 1) / per);
        page = std::min(std::max(page, 1), pages);
        std::string r = "[REANIM] " + g_filter + " " + std::to_string(total) + " anims, pag " + std::to_string(page) + "/" + std::to_string(pages) + ": ";
        for (int i = (page - 1) * per; i < std::min(total, page * per); ++i) { r += std::to_string(i + 1) + "=" + g_view[i] + " "; }
        return r;
    }

    if (c == "tocar") {
        if (t.size() < 2) return "[REANIM] uso: /reanim tocar <nome|numero|bloco:nome> [hold]";
        bool hold = t.size() > 2 && t[2] == "hold";
        std::string a = t[1], blk = kBlock; size_t colon = a.find(':');
        if (colon != std::string::npos) { blk = a.substr(0, colon); a = a.substr(colon + 1); }
        else if (isdigit((unsigned char)a[0])) {
            int n = atoi(a.c_str()); if (n < 1 || n > (int)g_view.size()) return "[REANIM] numero fora da lista atual (/reanim lista)";
            g_cur = n - 1; a = g_view[g_cur];
        }
        if (!Has(blk.c_str())) return "[REANIM] bloco '" + blk + "' nao carregado (/reanim carregar | extras)";
        return PlayName(blk, a, hold);
    }

    if (c == "proxima" || c == "anterior" || c == "next" || c == "prev") {
        if (!Has(kBlock)) return "[REANIM] use /reanim carregar primeiro";
        if (g_view.empty()) Rebuild(g_filter);
        if (g_view.empty()) return "[REANIM] lista vazia";
        int n = (int)g_view.size();
        g_cur = (c == "proxima" || c == "next") ? (g_cur + 1) % n : (g_cur <= 0 ? n - 1 : g_cur - 1);
        return std::to_string(g_cur + 1) + "/" + std::to_string(n) + "  " + PlayName(kBlock, g_view[g_cur], false);
    }
    return "[REANIM] comando desconhecido. /reanim ajuda";
}

} // namespace ReAnimTest
