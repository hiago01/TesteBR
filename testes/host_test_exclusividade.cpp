#include "SkinSlots.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <set>
#include <algorithm>
using namespace SkinSlots;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ ++fails; std::printf("FALHOU: %s (linha %d)\n",#c,__LINE__);} }while(0)

// "DFF" simulado: 1 material por nome de textura listado nos slots (como o inventário real), na ordem de kSlots
static std::vector<std::string> g_mats;
static std::vector<int> MatsOf(const Slot& s){
    std::vector<int> r;
    for(size_t i=0;i<g_mats.size();++i) if(TextureListHas(s.texture,g_mats[i].c_str())) r.push_back((int)i);
    return r;
}
static std::vector<uint8_t> hid;
static void Off(const char* n){ for(int i: MatsOf(*Find(n))) hid[i]=1; }
// espelha DoSlot: aplica variante OK -> visível; depois oculta vítimas. ok=false => variante inválida (nada muda)
static void Activate(const char* n,bool ok=true){
    if(!ok) return;
    const Slot& s=*Find(n);
    for(int i: MatsOf(s)) hid[i]=0;
    for(int i: ExclusiveVictims(s,MatsOf)) hid[i]=1;
}
static bool Vis(const char* n){ auto m=MatsOf(*Find(n)); if(m.empty()) return false; for(int i:m) if(hid[i]) return false; return true; }
static void Reset(){ hid.assign(g_mats.size(),0); }
static int VisibleIn(const char* list){ int c=0; std::string l=list; size_t p=0; while(p<l.size()){ size_t e=l.find(',',p); if(e==std::string::npos)e=l.size(); if(Vis(l.substr(p,e-p).c_str()))++c; p=e+1;} return c; }

int main(){
    // monta DFF: todas as texturas dos slots, sem duplicar
    std::set<std::string> seen;
    for(int k=0;k<kCount;++k){ std::string t=kSlots[k].texture; size_t p=0; while(p<=t.size()){ size_t e=t.find(',',p); if(e==std::string::npos)e=t.size(); std::string x=t.substr(p,e-p); if(!x.empty()&&seen.insert(x).second) g_mats.push_back(x); p=e+1; } }
    Reset();

    // 0) todos os nomes dos grupos existem em kSlots; nenhum slot em 2 grupos
    for(int g=0;g<kExclusiveGroupCount;++g){ std::string l=kExclusiveGroups[g]; size_t p=0; while(p<l.size()){ size_t e=l.find(',',p); if(e==std::string::npos)e=l.size(); std::string n=l.substr(p,e-p); CHECK(Find(n.c_str())!=nullptr); CHECK(GroupOf(n.c_str())==g); p=e+1; } }
    CHECK(GroupOf("hat1")==-1 && GroupOf("underwear")==-1 && GroupOf("skin_leg")==-1 && GroupOf("brow1")==-1);

    // 1) hair1 -> hair2
    Activate("hair1"); CHECK(Vis("hair1")); Activate("hair2"); CHECK(Vis("hair2")&&!Vis("hair1")&&VisibleIn("hair1,hair2,hair3,hair4")==1);
    // 2) hair2 -> hair4
    Activate("hair4"); CHECK(Vis("hair4")&&VisibleIn("hair1,hair2,hair3,hair4")==1);
    // 3) backpack1 -> backpack3
    Activate("backpack1"); Activate("backpack3"); CHECK(Vis("backpack3")&&VisibleIn("backpack1,backpack2,backpack3")==1);
    // 4) botas
    Activate("boot1"); Activate("boot3"); Activate("boot2"); CHECK(Vis("boot2")&&VisibleIn("boot1,boot2,boot3,boot4")==1);
    // 5) óculos, brincos, relógios, máscaras, torso, slipper, head
    Activate("glasse1"); Activate("glasse2"); CHECK(VisibleIn("glasse1,glasse2")==1&&Vis("glasse2"));
    Activate("ear1"); Activate("ear4"); Activate("ear2"); CHECK(VisibleIn("ear1,ear2,ear3,ear4")==1&&Vis("ear2"));
    Activate("clock1"); Activate("clock2"); CHECK(VisibleIn("clock1,clock2")==1&&Vis("clock2"));
    Activate("mask1"); Activate("mask2"); CHECK(VisibleIn("mask1,mask2")==1&&Vis("mask2"));
    Activate("torso1"); Activate("torso4"); CHECK(VisibleIn("torso1,torso2,torso3,torso4")==1&&Vis("torso4"));
    Activate("slipper1"); Activate("slipper3"); CHECK(VisibleIn("slipper1,slipper2,slipper3")==1&&Vis("slipper3"));
    Activate("head1"); Activate("head3"); CHECK(VisibleIn("head1,head2,head3,head4,head5")==1&&Vis("head3"));
    // 6) calça <-> bermuda
    Activate("pants2"); Activate("short3"); CHECK(VisibleIn("pants1,pants2,pants3,pants4,short1,short2,short3")==1&&Vis("short3"));
    Activate("pants4"); CHECK(VisibleIn("pants1,pants2,pants3,pants4,short1,short2,short3")==1&&Vis("pants4"));
    // 9) categorias diferentes continuam todas visíveis
    CHECK(Vis("hair4")&&Vis("backpack3")&&Vis("glasse2")&&Vis("ear2")&&Vis("clock2")&&Vis("mask2")&&Vis("torso4")&&Vis("slipper3")&&Vis("head3")&&Vis("pants4"));
    // tenis x chinelo: grupo unico (slipper3 ativo => boot2 foi desligado; ativar um tenis desliga o chinelo)
    CHECK(!Vis("boot2")); Activate("boot4"); CHECK(Vis("boot4")&&!Vis("slipper3")&&VisibleIn("boot1,boot2,boot3,boot4,slipper1,slipper2,slipper3")==1);
    Activate("slipper2"); CHECK(Vis("slipper2")&&!Vis("boot4")&&VisibleIn("boot1,boot2,boot3,boot4,slipper1,slipper2,slipper3")==1);
    // 7) off manual e depois ativar outro
    Reset(); Activate("hair1"); Off("hair1"); CHECK(!Vis("hair1")); Activate("hair2"); CHECK(Vis("hair2")&&!Vis("hair1")&&VisibleIn("hair1,hair2,hair3,hair4")==1);
    // 8) variante inválida: nada muda
    Reset(); Activate("hair3"); Activate("hair2",false); CHECK(Vis("hair3")); CHECK(!Vis("hair2"));
    // independência: ativar categoria não mexe em materiais fora do grupo
    Reset(); std::vector<uint8_t> before=hid; Activate("backpack2"); for(int i=0;i<(int)hid.size();++i){ bool inBp=false; for(int k=0;k<3;++k){ std::string n="backpack"+std::to_string(k+1); for(int m:MatsOf(*Find(n.c_str()))) if(m==i) inBp=true; } if(!inBp) CHECK(hid[i]==before[i]); }
    // slots sem grupo: nenhuma vítima
    CHECK(ExclusiveVictims(*Find("hat1"),MatsOf).empty()); CHECK(ExclusiveVictims(*Find("skin_leg"),MatsOf).empty()); CHECK(ExclusiveVictims(*Find("underwear"),MatsOf).empty());
    // DFF sem os materiais de um slot do grupo: ignora sem erro
    std::vector<std::string> saved=g_mats; g_mats.erase(std::remove(g_mats.begin(),g_mats.end(),std::string("fantasy_m_hair3")),g_mats.end()); hid.assign(g_mats.size(),0);
    CHECK(ExclusiveVictims(*Find("hair1"),MatsOf).size()==2); g_mats=saved; Reset();
    // material compartilhado com slot de OUTRA categoria é preservado
    g_mats.push_back("shared_x"); hid.assign(g_mats.size(),0);
    // (simula compartilhamento mudando a textura do hair2 para incluir o material de um boot)
    // verificado via matsOf sintético:
    auto fake=[&](const Slot& s){ std::vector<int> r=MatsOf(s); if(std::string(s.name)=="hair2"||std::string(s.name)=="boot1") r.push_back((int)g_mats.size()-1); return r; };
    auto v=ExclusiveVictims(*Find("hair1"),fake); CHECK(std::find(v.begin(),v.end(),(int)g_mats.size()-1)==v.end());
    if(fails) std::printf("RESULTADO: %d FALHA(S)\n",fails); else std::printf("RESULTADO: todos os testes de logica passaram (materiais simulados: %zu, slots: %d, grupos: %d)\n",g_mats.size(),kCount,kExclusiveGroupCount);
    return fails?1:0;
}
